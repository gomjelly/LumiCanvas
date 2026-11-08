// LumiCanvas surface warp shader.
// Each surface is drawn as its destination quad. The pixel shader maps the
// canvas position back into content UV space with the inverse homography,
// which gives perspective-correct texturing for any convex quad.

cbuffer SurfaceConstants : register(b0)
{
    float4 cornersA;   // TL.xy, TR.xy  (normalized canvas space, y down)
    float4 cornersB;   // BR.xy, BL.xy
    float4 hRow0;      // inverse homography rows: canvas (x, y, 1) -> content (u, v, w)
    float4 hRow1;
    float4 hRow2;
    float4 contentRect; // image placement in surface UV: xy offset, zw size
    float4 previousRect; // same for the previous step image
    float4 tint;
    float4 params;     // x: opacity, y: 1 = texture, 0 = test pattern
    float4 guide;      // x: 0 normal, 1 highlight changes, 2 onion skin
                       // y: dim unchanged, z: previous opacity, w: tint amount
    float4 highlightColor;
    float4 adjust;     // x: invert, y: recolour, z: brightness, w: grid strength
    float4 lineColor;
    float4 gridCells;  // xy: columns, rows; z: corner being aligned or -1
};

Texture2D contentTexture : register(t0);
Texture2D previousTexture : register(t1); // unbound = nothing drawn before
SamplerState contentSampler : register(s0);

struct VSOutput
{
    float4 position : SV_Position;
    float2 canvas   : TEXCOORD0;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    // Two triangles: TL-TR-BR, TL-BR-BL.
    static const uint indices[6] = { 0, 1, 2, 0, 2, 3 };
    const float2 corners[4] = { cornersA.xy, cornersA.zw, cornersB.xy, cornersB.zw };
    float2 c = corners[indices[vertexId]];

    VSOutput o;
    o.canvas = c;
    o.position = float4(c.x * 2.0 - 1.0, 1.0 - c.y * 2.0, 0.0, 1.0);
    return o;
}

// Anti-aliased lines at integer coordinates, roughly one pixel wide.
float lineMask(float2 coord)
{
    float2 d = abs(frac(coord - 0.5) - 0.5) / max(fwidth(coord), 1e-5);
    return 1.0 - saturate(min(d.x, d.y));
}

float3 testPattern(float2 uv)
{
    float2 cells = uv * 8.0;
    float checker = fmod(floor(cells.x) + floor(cells.y), 2.0);
    float3 color = tint.rgb * lerp(0.18, 0.32, checker);

    color = lerp(color, float3(0.8, 0.8, 0.8), lineMask(cells));

    // Centre crosshair and circle.
    float2 d = uv - 0.5;
    float2 crossDist = abs(d) / max(fwidth(uv), 1e-5);
    float crosshair = 1.0 - saturate(min(crossDist.x, crossDist.y) - 0.5);
    float r = length(d);
    float ring = 1.0 - saturate(abs(r - 0.3) / max(fwidth(r), 1e-5) - 0.75);
    color = lerp(color, float3(1.0, 1.0, 1.0), max(crosshair, ring));

    // Thick border in the surface colour.
    float2 edgeDist = min(uv, 1.0 - uv) / max(fwidth(uv), 1e-5);
    float border = 1.0 - saturate(min(edgeDist.x, edgeDist.y) - 4.0);
    color = lerp(color, tint.rgb, border);

    // White brackets show where each corner has to land on the real canvas.
    float thick = 1.0 - saturate(min(edgeDist.x, edgeDist.y) - 8.0);
    float2 fromCorner = min(uv, 1.0 - uv);
    float bracket = step(max(fromCorner.x, fromCorner.y), 0.08) * thick;
    color = lerp(color, float3(1.0, 1.0, 1.0), bracket);

    // The corner being moved gets longer yellow arms.
    float active = gridCells.z;
    float2 activeCorner = float2((abs(active - 1.0) < 0.5 || abs(active - 2.0) < 0.5) ? 1.0 : 0.0,
                                 active > 1.5 ? 1.0 : 0.0);
    float2 fromActive = abs(uv - activeCorner);
    float activeArm = step(max(fromActive.x, fromActive.y), 0.25) * thick * step(-0.5, active);
    return lerp(color, float3(1.0, 0.9, 0.1), activeArm);
}

// Fit leaves bars outside the image transparent; Fill crops via the clamp.
float4 sampleContent(Texture2D image, float2 uv, float4 rect)
{
    float2 contentUv = (uv - rect.xy) / rect.zw;
    bool inside = all(contentUv >= 0.0) && all(contentUv <= 1.0);
    float4 c = image.Sample(contentSampler, saturate(contentUv));
    return inside ? c : float4(0.0, 0.0, 0.0, 0.0);
}

// Invert and recolour happen before comparing steps, so both steps stay comparable.
float4 adjustImage(float4 c)
{
    float3 rgb = lerp(c.rgb, 1.0 - c.rgb, adjust.x);
    float value = dot(rgb, float3(0.299, 0.587, 0.114));
    return float4(lerp(rgb, lineColor.rgb * value, adjust.y), c.a);
}

float4 applyGuide(float4 current, float4 previous)
{
    // Compare in premultiplied space so transparent and black count as "empty".
    float3 cur = current.rgb * current.a;
    float3 prev = previous.rgb * previous.a;

    // Highlight: what changed since the previous step stays bright, the rest is dimmed.
    float3 d = abs(cur - prev);
    float changed = smoothstep(0.06, 0.2, max(d.r, max(d.g, d.b)));
    // Tint keeps the current brightness, so parts erased since the last step stay dark.
    float3 tinted = highlightColor.rgb * max(cur.r, max(cur.g, cur.b));
    float3 fresh = lerp(cur, tinted, guide.w);
    float3 highlighted = lerp(cur * guide.y, fresh, changed);

    // Onion skin: the previous step shows through faintly wherever it is brighter.
    float3 onion = max(cur, prev * guide.z);

    float3 rgb = guide.x < 0.5 ? cur : (guide.x < 1.5 ? highlighted : onion);
    float alpha = guide.x < 0.5 ? current.a : max(current.a, previous.a);
    return float4(rgb / max(alpha, 1e-4), alpha);
}

// Grid over the whole canvas, including bars around a fitted image; then overall brightness.
float4 finishGuide(float4 color, float2 uv)
{
    float grid = lineMask(uv * gridCells.xy) * adjust.w;
    float3 premultiplied = lerp(color.rgb * color.a, float3(1.0, 1.0, 1.0), grid);
    float alpha = lerp(color.a, 1.0, grid);
    return float4(premultiplied / max(alpha, 1e-4) * adjust.z, alpha);
}

float4 PSMain(VSOutput input) : SV_Target
{
    float3 p = float3(input.canvas, 1.0);
    float w = dot(hRow2.xyz, p);
    float2 uv = saturate(float2(dot(hRow0.xyz, p), dot(hRow1.xyz, p)) / w);

    // Both steps are always sampled so no gradient op sits inside flow control.
    float4 current = sampleContent(contentTexture, uv, contentRect);
    float4 previous = sampleContent(previousTexture, uv, previousRect);

    float4 guided = finishGuide(applyGuide(adjustImage(current), adjustImage(previous)), uv);
    float4 color = params.y > 0.5 ? guided : float4(testPattern(uv), 1.0);
    color.a *= params.x;
    return color;
}
