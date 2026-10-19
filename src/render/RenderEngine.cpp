#include "render/RenderEngine.h"

#include <QFile>
#include <QPolygonF>
#include <QTransform>
#include <QtDebug>

#include <d3dcompiler.h>

#include <algorithm>
#include <chrono>
#include <iterator>

namespace lumi {

using Microsoft::WRL::ComPtr;

namespace {

// Must match the SurfaceConstants cbuffer in warp.hlsl.
struct SurfaceConstants {
    float corners[8];
    float hRow0[4];
    float hRow1[4];
    float hRow2[4];
    float contentRect[4];
    float previousRect[4];
    float tint[4];
    float params[4]; // x: opacity, y: 1 = texture, 0 = test pattern
    float guide[4];  // x: mode, y: dim unchanged, z: previous opacity, w: tint amount
    float highlightColor[4];
};
static_assert(sizeof(SurfaceConstants) % 16 == 0);

constexpr uint64_t kTextureIdleFrames = 120;

QString hresultString(HRESULT hr)
{
    return QStringLiteral("0x%1").arg(quint32(hr), 8, 16, QLatin1Char('0'));
}

bool check(HRESULT hr, const char* what, QString* error)
{
    if (SUCCEEDED(hr))
        return true;
    if (error)
        *error = QStringLiteral("%1 failed (%2)").arg(QLatin1String(what), hresultString(hr));
    return false;
}

bool compileShader(const QByteArray& source, const char* entry, const char* target,
                   ComPtr<ID3DBlob>& blob, QString* error)
{
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifndef NDEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
    ComPtr<ID3DBlob> errors;
    const HRESULT hr = D3DCompile(source.constData(), size_t(source.size()), "warp.hlsl", nullptr,
                                  nullptr, entry, target, flags, 0, &blob, &errors);
    if (FAILED(hr)) {
        if (error) {
            const QString detail =
                errors ? QString::fromLocal8Bit(static_cast<const char*>(errors->GetBufferPointer()),
                                                qsizetype(errors->GetBufferSize()))
                       : hresultString(hr);
            *error = QStringLiteral("Shader %1 failed to compile:\n%2")
                         .arg(QLatin1String(entry), detail);
        }
        return false;
    }
    return true;
}

} // namespace

RenderEngine::~RenderEngine()
{
    stop();
}

bool RenderEngine::start(QString* error)
{
    if (m_thread.joinable())
        return true;
    if (!createDevice(error) || !createPipeline(error))
        return false;
    {
        std::lock_guard lock(m_mutex);
        m_running = true;
    }
    m_thread = std::thread(&RenderEngine::threadMain, this);
    return true;
}

void RenderEngine::stop()
{
    {
        std::lock_guard lock(m_mutex);
        if (!m_running)
            return;
        m_running = false;
    }
    m_cv.notify_all();
    if (m_thread.joinable())
        m_thread.join();
}

void RenderEngine::setScene(std::shared_ptr<const Scene> scene)
{
    std::lock_guard lock(m_mutex);
    m_scene = std::move(scene);
}

int RenderEngine::addOutput(HWND hwnd)
{
    const int id = m_nextOutputId++;
    {
        std::lock_guard lock(m_mutex);
        m_commands.push_back({Command::Add, id, hwnd, nullptr});
    }
    m_cv.notify_all();
    return id;
}

void RenderEngine::removeOutput(int id)
{
    std::promise<void> done;
    std::future<void> finished = done.get_future();
    {
        std::lock_guard lock(m_mutex);
        if (!m_running)
            return; // the render thread has already released every output
        m_commands.push_back({Command::Remove, id, nullptr, &done});
    }
    m_cv.notify_all();
    finished.wait();
}

bool RenderEngine::createDevice(QString* error)
{
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    auto create = [&](UINT flags) {
        return D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
                                 UINT(std::size(levels)), D3D11_SDK_VERSION, &m_device, nullptr,
                                 &m_context);
    };

    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = E_FAIL;
#ifndef NDEBUG
    // The debug layer is only present when the Graphics Tools feature is installed.
    hr = create(flags | D3D11_CREATE_DEVICE_DEBUG);
#endif
    if (FAILED(hr))
        hr = create(flags);
    if (!check(hr, "D3D11CreateDevice", error))
        return false;

    ComPtr<IDXGIDevice1> dxgiDevice;
    if (!check(m_device.As(&dxgiDevice), "Query IDXGIDevice1", error))
        return false;
    // Keep projector latency low: never queue more than one frame ahead.
    dxgiDevice->SetMaximumFrameLatency(1);

    ComPtr<IDXGIAdapter> adapter;
    if (!check(dxgiDevice->GetAdapter(&adapter), "GetAdapter", error))
        return false;
    if (!check(adapter->GetParent(IID_PPV_ARGS(&m_factory)), "Get IDXGIFactory2", error))
        return false;

    DXGI_ADAPTER_DESC desc{};
    if (SUCCEEDED(adapter->GetDesc(&desc)))
        m_adapterName = QString::fromWCharArray(desc.Description);
    return true;
}

bool RenderEngine::createPipeline(QString* error)
{
    QFile file(QStringLiteral(":/shaders/warp.hlsl"));
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Shader resource warp.hlsl is missing.");
        return false;
    }
    const QByteArray source = file.readAll();

    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> psBlob;
    if (!compileShader(source, "VSMain", "vs_5_0", vsBlob, error)
        || !compileShader(source, "PSMain", "ps_5_0", psBlob, error))
        return false;

    if (!check(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                            nullptr, &m_vertexShader),
               "CreateVertexShader", error))
        return false;
    if (!check(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                                           nullptr, &m_pixelShader),
               "CreatePixelShader", error))
        return false;

    D3D11_BUFFER_DESC cbDesc{};
    cbDesc.ByteWidth = sizeof(SurfaceConstants);
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (!check(m_device->CreateBuffer(&cbDesc, nullptr, &m_constants), "CreateBuffer", error))
        return false;

    D3D11_SAMPLER_DESC samplerDesc{};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxAnisotropy = 1;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    if (!check(m_device->CreateSamplerState(&samplerDesc, &m_sampler), "CreateSamplerState", error))
        return false;

    D3D11_BLEND_DESC blendDesc{};
    auto& rt = blendDesc.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
    rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    rt.BlendOp = D3D11_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D11_BLEND_ONE;
    rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (!check(m_device->CreateBlendState(&blendDesc, &m_blendState), "CreateBlendState", error))
        return false;

    D3D11_RASTERIZER_DESC rasterDesc{};
    rasterDesc.FillMode = D3D11_FILL_SOLID;
    rasterDesc.CullMode = D3D11_CULL_NONE;
    rasterDesc.DepthClipEnable = TRUE;
    return check(m_device->CreateRasterizerState(&rasterDesc, &m_rasterizerState),
                 "CreateRasterizerState", error);
}

void RenderEngine::threadMain()
{
    SetThreadDescription(GetCurrentThread(), L"LumiCanvas Render");

    std::vector<Command> commands;
    for (;;) {
        std::shared_ptr<const Scene> scene;
        {
            std::unique_lock lock(m_mutex);
            if (m_outputs.empty())
                m_cv.wait(lock, [this] { return !m_running || !m_commands.empty(); });
            if (!m_running)
                break;
            commands.swap(m_commands);
            scene = m_scene;
        }
        processCommands(commands);
        commands.clear();
        if (m_outputs.empty())
            continue;

        ++m_frame;
        static const Scene emptyScene;
        const Scene& current = scene ? *scene : emptyScene;
        bool presented = false;
        // Only the first output waits for vblank so several outputs don't divide the frame rate.
        for (size_t i = 0; i < m_outputs.size(); ++i)
            presented |= renderOutput(m_outputs[i], current, i == 0);
        pruneTextures();

        if (!presented) // every output is minimized or occluded; don't spin
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    {
        std::lock_guard lock(m_mutex);
        commands.swap(m_commands);
    }
    for (Command& c : commands) {
        if (c.done)
            c.done->set_value();
    }
    m_context->ClearState();
    m_outputs.clear();
    m_textures.clear();
    m_context->Flush();
}

void RenderEngine::processCommands(std::vector<Command>& commands)
{
    for (Command& c : commands) {
        if (c.type == Command::Add) {
            Output out;
            out.id = c.id;
            out.hwnd = c.hwnd;
            if (createSwapChain(out))
                m_outputs.push_back(std::move(out));
        } else {
            auto it = std::find_if(m_outputs.begin(), m_outputs.end(),
                                   [&](const Output& o) { return o.id == c.id; });
            if (it != m_outputs.end()) {
                m_context->ClearState();
                m_outputs.erase(it);
                // Make sure the swap chain is really gone before the window is destroyed.
                m_context->Flush();
            }
        }
        if (c.done)
            c.done->set_value();
    }
}

bool RenderEngine::createSwapChain(Output& out)
{
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    const HRESULT hr = m_factory->CreateSwapChainForHwnd(m_device.Get(), out.hwnd, &desc, nullptr,
                                                         nullptr, &out.swapChain);
    if (FAILED(hr)) {
        qWarning("CreateSwapChainForHwnd failed (%s)", qPrintable(hresultString(hr)));
        return false;
    }
    m_factory->MakeWindowAssociation(out.hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    return createRenderTarget(out);
}

bool RenderEngine::createRenderTarget(Output& out)
{
    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = out.swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (SUCCEEDED(hr))
        hr = m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &out.rtv);
    if (FAILED(hr)) {
        qWarning("Creating the output render target failed (%s)", qPrintable(hresultString(hr)));
        return false;
    }
    D3D11_TEXTURE2D_DESC desc{};
    backBuffer->GetDesc(&desc);
    out.width = desc.Width;
    out.height = desc.Height;
    return true;
}

bool RenderEngine::resizeOutput(Output& out, UINT width, UINT height)
{
    out.rtv.Reset();
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    const HRESULT hr = out.swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        qWarning("ResizeBuffers failed (%s)", qPrintable(hresultString(hr)));
        return false;
    }
    return createRenderTarget(out);
}

bool RenderEngine::renderOutput(Output& out, const Scene& scene, bool vsync)
{
    RECT rc{};
    if (!GetClientRect(out.hwnd, &rc))
        return false;
    const UINT width = UINT(rc.right - rc.left);
    const UINT height = UINT(rc.bottom - rc.top);
    if (width == 0 || height == 0)
        return false;
    if ((width != out.width || height != out.height || !out.rtv) && !resizeOutput(out, width, height))
        return false;

    ID3D11RenderTargetView* rtv = out.rtv.Get();
    m_context->OMSetRenderTargets(1, &rtv, nullptr);
    const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    m_context->ClearRenderTargetView(rtv, black);

    const D3D11_VIEWPORT viewport{0.0f, 0.0f, float(width), float(height), 0.0f, 1.0f};
    m_context->RSSetViewports(1, &viewport);
    m_context->RSSetState(m_rasterizerState.Get());
    m_context->OMSetBlendState(m_blendState.Get(), nullptr, 0xffffffff);
    m_context->IASetInputLayout(nullptr);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    ID3D11Buffer* constants = m_constants.Get();
    m_context->VSSetConstantBuffers(0, 1, &constants);
    m_context->PSSetConstantBuffers(0, 1, &constants);
    ID3D11SamplerState* sampler = m_sampler.Get();
    m_context->PSSetSamplers(0, 1, &sampler);

    for (const SceneSurface& s : scene.surfaces) {
        const QPolygonF quad{s.corners[0], s.corners[1], s.corners[2], s.corners[3]};
        QTransform inverse;
        if (!QTransform::quadToSquare(quad, inverse))
            continue; // degenerate quad

        ID3D11ShaderResourceView* srv = s.image.isNull() ? nullptr : textureFor(s.image);
        // An unbound previous texture samples as transparent black: "nothing drawn yet".
        ID3D11ShaderResourceView* previousSrv = s.previous.isNull() ? nullptr : textureFor(s.previous);

        SurfaceConstants c{};
        for (int i = 0; i < 4; ++i) {
            c.corners[i * 2] = float(s.corners[i].x());
            c.corners[i * 2 + 1] = float(s.corners[i].y());
        }
        // QTransform maps row vectors: (x, y, 1) * M.
        c.hRow0[0] = float(inverse.m11()); c.hRow0[1] = float(inverse.m21()); c.hRow0[2] = float(inverse.m31());
        c.hRow1[0] = float(inverse.m12()); c.hRow1[1] = float(inverse.m22()); c.hRow1[2] = float(inverse.m32());
        c.hRow2[0] = float(inverse.m13()); c.hRow2[1] = float(inverse.m23()); c.hRow2[2] = float(inverse.m33());
        std::copy(s.contentRect.begin(), s.contentRect.end(), c.contentRect);
        std::copy(s.tint.begin(), s.tint.end(), c.tint);
        c.params[0] = s.opacity;
        c.params[1] = srv ? 1.0f : 0.0f;
        std::copy(s.previousRect.begin(), s.previousRect.end(), c.previousRect);
        c.guide[0] = float(int(s.guide));
        c.guide[1] = s.dimUnchanged;
        c.guide[2] = s.previousOpacity;
        c.guide[3] = s.tintAmount;
        std::copy(s.highlightColor.begin(), s.highlightColor.end(), c.highlightColor);

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(m_context->Map(m_constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            continue;
        memcpy(mapped.pData, &c, sizeof(c));
        m_context->Unmap(m_constants.Get(), 0);

        ID3D11ShaderResourceView* views[2] = {srv, previousSrv};
        m_context->PSSetShaderResources(0, 2, views);
        m_context->Draw(6, 0);
    }

    const HRESULT hr = out.swapChain->Present(vsync ? 1 : 0, 0);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        qCritical("GPU device lost (%s)", qPrintable(hresultString(m_device->GetDeviceRemovedReason())));
        return false;
    }
    return hr == S_OK;
}

ID3D11ShaderResourceView* RenderEngine::textureFor(const QImage& image)
{
    const qint64 key = image.cacheKey();
    if (auto it = m_textures.find(key); it != m_textures.end()) {
        it->second.lastUsedFrame = m_frame;
        return it->second.srv.Get();
    }

    const QImage rgba = image.format() == QImage::Format_RGBA8888
                            ? image
                            : image.convertToFormat(QImage::Format_RGBA8888);

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = UINT(rgba.width());
    desc.Height = UINT(rgba.height());
    desc.MipLevels = 0; // full mip chain, generated below
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

    ComPtr<ID3D11Texture2D> texture;
    CachedTexture entry;
    HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, &texture);
    if (SUCCEEDED(hr))
        hr = m_device->CreateShaderResourceView(texture.Get(), nullptr, &entry.srv);
    if (FAILED(hr)) {
        qWarning("Texture upload failed (%s)", qPrintable(hresultString(hr)));
        return nullptr;
    }
    m_context->UpdateSubresource(texture.Get(), 0, nullptr, rgba.constBits(),
                                 UINT(rgba.bytesPerLine()), 0);
    m_context->GenerateMips(entry.srv.Get());

    entry.lastUsedFrame = m_frame;
    return m_textures.emplace(key, std::move(entry)).first->second.srv.Get();
}

void RenderEngine::pruneTextures()
{
    if (m_frame % kTextureIdleFrames != 0)
        return;
    for (auto it = m_textures.begin(); it != m_textures.end();) {
        if (m_frame - it->second.lastUsedFrame > kTextureIdleFrames)
            it = m_textures.erase(it);
        else
            ++it;
    }
}

} // namespace lumi
