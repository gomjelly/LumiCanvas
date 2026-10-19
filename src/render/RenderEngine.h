#pragma once

#include <QImage>
#include <QPointF>
#include <QString>

#include <array>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

namespace lumi {

// Immutable snapshot of what the outputs should draw, built on the GUI thread.
struct SceneSurface {
    std::array<QPointF, 4> corners; // normalized output space, TL, TR, BR, BL
    QImage image;                   // RGBA8888; null draws the test pattern
    std::array<float, 4> contentRect{0.0f, 0.0f, 1.0f, 1.0f}; // image placement in surface UV
    std::array<float, 4> tint{1.0f, 1.0f, 1.0f, 1.0f};
    float opacity = 1.0f;

    // Step guide: how the image relates to the previous step. A null previous
    // image with a guide mode set means "no previous step" (everything is new).
    enum class Guide { Normal = 0, Highlight = 1, Onion = 2 } guide = Guide::Normal;
    QImage previous;
    std::array<float, 4> previousRect{0.0f, 0.0f, 1.0f, 1.0f};
    float dimUnchanged = 0.25f;
    float previousOpacity = 0.35f;
    float tintAmount = 0.0f;
    std::array<float, 4> highlightColor{0.0f, 1.0f, 0.55f, 1.0f};
};

struct Scene {
    std::vector<SceneSurface> surfaces;
};

// Owns the D3D11 device and a render thread that draws the current scene into
// every registered output window. Public methods are called from the GUI thread.
class RenderEngine {
public:
    RenderEngine() = default;
    ~RenderEngine();
    RenderEngine(const RenderEngine&) = delete;
    RenderEngine& operator=(const RenderEngine&) = delete;

    bool start(QString* error);
    void stop();

    QString adapterName() const { return m_adapterName; }

    void setScene(std::shared_ptr<const Scene> scene);

    // The window must stay alive until removeOutput() returns.
    int addOutput(HWND hwnd);
    // Blocks until the render thread has released the output's swap chain.
    void removeOutput(int id);

private:
    template <class T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    struct Output {
        int id = 0;
        HWND hwnd = nullptr;
        ComPtr<IDXGISwapChain1> swapChain;
        ComPtr<ID3D11RenderTargetView> rtv;
        UINT width = 0;
        UINT height = 0;
    };

    struct Command {
        enum Type { Add, Remove } type;
        int id;
        HWND hwnd;
        std::promise<void>* done;
    };

    struct CachedTexture {
        ComPtr<ID3D11ShaderResourceView> srv;
        uint64_t lastUsedFrame = 0;
    };

    bool createDevice(QString* error);
    bool createPipeline(QString* error);

    void threadMain();
    void processCommands(std::vector<Command>& commands);
    bool createSwapChain(Output& out);
    bool createRenderTarget(Output& out);
    bool resizeOutput(Output& out, UINT width, UINT height);
    bool renderOutput(Output& out, const Scene& scene, bool vsync);
    ID3D11ShaderResourceView* textureFor(const QImage& image);
    void pruneTextures();

    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IDXGIFactory2> m_factory;
    ComPtr<ID3D11VertexShader> m_vertexShader;
    ComPtr<ID3D11PixelShader> m_pixelShader;
    ComPtr<ID3D11Buffer> m_constants;
    ComPtr<ID3D11SamplerState> m_sampler;
    ComPtr<ID3D11BlendState> m_blendState;
    ComPtr<ID3D11RasterizerState> m_rasterizerState;
    QString m_adapterName;

    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_running = false;                 // guarded by m_mutex
    std::vector<Command> m_commands;        // guarded by m_mutex
    std::shared_ptr<const Scene> m_scene;   // guarded by m_mutex
    int m_nextOutputId = 1;                 // GUI thread only

    // Render thread only.
    std::vector<Output> m_outputs;
    std::unordered_map<qint64, CachedTexture> m_textures;
    uint64_t m_frame = 0;
};

} // namespace lumi
