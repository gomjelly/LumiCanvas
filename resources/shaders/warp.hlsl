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
    float4 tint;
    float4 params;     // x: opacity, y: 1 = texture, 0 = test pattern
};

Texture2D contentTexture : register(t0);
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
    return lerp(color, tint.rgb, border);
}

float4 PSMain(VSOutput input) : SV_Target
{
    float3 p = float3(input.canvas, 1.0);
    float w = dot(hRow2.xyz, p);
    float2 uv = saturate(float2(dot(hRow0.xyz, p), dot(hRow1.xyz, p)) / w);

    float4 texColor = contentTexture.Sample(contentSampler, uv);
    float4 color = params.y > 0.5 ? texColor : float4(testPattern(uv), 1.0);
    color.a *= params.x;
    return color;
}
