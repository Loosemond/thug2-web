#pragma once

#include <array>
#include <cstdint>
#include <map>

#include "d3d9/d3d9.h"

namespace D3D9 {

class Direct3D;

// One draw, as the backend gets it: the primitives, where the vertices and indices are (the bound streams and
// index buffer, or the game's own memory for the *UP calls), the rest from the device's state.
struct DrawCall {
    D3DPRIMITIVETYPE type;
    uint32_t primitiveCount;
    uint32_t vertexCount;       // (from the primitive count for unindexed draws; NumVertices for indexed)
    int32_t baseVertex = 0;     // added to each index
    uint32_t firstVertex = 0;   // unindexed: the first vertex; indexed: MinVertexIndex
    uint32_t firstIndex = 0;
    bool indexed = false;
    const void* userVertices = nullptr; // DrawPrimitiveUP / DrawIndexedPrimitiveUP
    uint32_t userStride = 0;
    const void* userIndices = nullptr;
    D3DFORMAT userIndexFormat = D3DFMT_INDEX16;
};

// What draws: WebGPU, WebGL2 - or nothing (headless). Reads the device's state for each call.
class Backend {
public:
    virtual ~Backend() = default;
    virtual void Clear(class Device&, DWORD count, const D3DRECT* rects, DWORD flags, D3DCOLOR color, float z,
                       DWORD stencil) {}
    virtual void Draw(class Device&, const DrawCall&) {}
    virtual void Present(class Device&) {}
    // A resource (texture, surface, buffer) was deleted: anything kept for it can go.
    virtual void Released(const void* resource) {}
    // StretchRect between surfaces the backend draws to (render targets, the back buffer).
    virtual void Stretch(class Device&, Surface* source, const RECT* sourceRect, Surface* destination,
                         const RECT* destinationRect, D3DTEXTUREFILTERTYPE filter) {}
};

class Device final : public Object<StubDirect3DDevice9> {
public:
    Device(Direct3D* d3d, HWND window, DWORD behavior, const D3DPRESENT_PARAMETERS& parameters);
    ~Device() override;

    // --- IDirect3DDevice9 ---
    HRESULT STDMETHODCALLTYPE TestCooperativeLevel() override { return D3D_OK; }
    UINT STDMETHODCALLTYPE GetAvailableTextureMem() override { return 512u << 20; }
    HRESULT STDMETHODCALLTYPE EvictManagedResources() override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetDirect3D(IDirect3D9** d3d) override;
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9* caps) override;
    HRESULT STDMETHODCALLTYPE GetDisplayMode(UINT, D3DDISPLAYMODE* mode) override;
    HRESULT STDMETHODCALLTYPE GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS* parameters) override;
    HRESULT STDMETHODCALLTYPE SetCursorProperties(UINT, UINT, IDirect3DSurface9*) override { return D3D_OK; }
    void STDMETHODCALLTYPE SetCursorPosition(int, int, DWORD) override {}
    WINBOOL STDMETHODCALLTYPE ShowCursor(WINBOOL) override { return FALSE; }
    UINT STDMETHODCALLTYPE GetNumberOfSwapChains() override { return 1; }
    HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS* parameters) override;
    HRESULT STDMETHODCALLTYPE Present(const RECT*, const RECT*, HWND, const RGNDATA*) override;
    HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT, UINT, D3DBACKBUFFER_TYPE, IDirect3DSurface9** surface) override;
    HRESULT STDMETHODCALLTYPE GetRasterStatus(UINT, D3DRASTER_STATUS* status) override;
    HRESULT STDMETHODCALLTYPE SetDialogBoxMode(WINBOOL) override { return D3D_OK; }
    void STDMETHODCALLTYPE SetGammaRamp(UINT, DWORD, const D3DGAMMARAMP* ramp) override { m_gamma = *ramp; }
    void STDMETHODCALLTYPE GetGammaRamp(UINT, D3DGAMMARAMP* ramp) override { *ramp = m_gamma; }
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format,
                                            D3DPOOL pool, IDirect3DTexture9** texture, HANDLE*) override;
    HRESULT STDMETHODCALLTYPE CreateCubeTexture(UINT edge, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
                                                IDirect3DCubeTexture9** texture, HANDLE*) override;
    HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT length, DWORD usage, DWORD fvf, D3DPOOL pool,
                                                 IDirect3DVertexBuffer9** buffer, HANDLE*) override;
    HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool,
                                                IDirect3DIndexBuffer9** buffer, HANDLE*) override;
    HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE,
                                                 DWORD, WINBOOL, IDirect3DSurface9** surface, HANDLE*) override;
    HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT width, UINT height, D3DFORMAT format,
                                                        D3DMULTISAMPLE_TYPE, DWORD, WINBOOL,
                                                        IDirect3DSurface9** surface, HANDLE*) override;
    HRESULT STDMETHODCALLTYPE UpdateSurface(IDirect3DSurface9* source, const RECT* sourceRect,
                                            IDirect3DSurface9* destination, const POINT* point) override;
    HRESULT STDMETHODCALLTYPE UpdateTexture(IDirect3DBaseTexture9*, IDirect3DBaseTexture9*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetRenderTargetData(IDirect3DSurface9*, IDirect3DSurface9*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetFrontBufferData(UINT, IDirect3DSurface9*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE StretchRect(IDirect3DSurface9* source, const RECT* sourceRect,
                                          IDirect3DSurface9* destination, const RECT* destinationRect,
                                          D3DTEXTUREFILTERTYPE filter) override;
    HRESULT STDMETHODCALLTYPE ColorFill(IDirect3DSurface9*, const RECT*, D3DCOLOR) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE CreateOffscreenPlainSurface(UINT width, UINT height, D3DFORMAT format, D3DPOOL pool,
                                                          IDirect3DSurface9** surface, HANDLE*) override;
    HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD index, IDirect3DSurface9* surface) override;
    HRESULT STDMETHODCALLTYPE GetRenderTarget(DWORD index, IDirect3DSurface9** surface) override;
    HRESULT STDMETHODCALLTYPE SetDepthStencilSurface(IDirect3DSurface9* surface) override;
    HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface9** surface) override;
    HRESULT STDMETHODCALLTYPE BeginScene() override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE EndScene() override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE Clear(DWORD count, const D3DRECT* rects, DWORD flags, D3DCOLOR color, float z,
                                    DWORD stencil) override;
    HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX* matrix) override;
    HRESULT STDMETHODCALLTYPE GetTransform(D3DTRANSFORMSTATETYPE state, D3DMATRIX* matrix) override;
    HRESULT STDMETHODCALLTYPE MultiplyTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX* matrix) override;
    HRESULT STDMETHODCALLTYPE SetViewport(const D3DVIEWPORT9* viewport) override;
    HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT9* viewport) override;
    HRESULT STDMETHODCALLTYPE SetMaterial(const D3DMATERIAL9* material) override;
    HRESULT STDMETHODCALLTYPE GetMaterial(D3DMATERIAL9* material) override;
    HRESULT STDMETHODCALLTYPE SetLight(DWORD index, const D3DLIGHT9* light) override;
    HRESULT STDMETHODCALLTYPE GetLight(DWORD index, D3DLIGHT9* light) override;
    HRESULT STDMETHODCALLTYPE LightEnable(DWORD index, WINBOOL enable) override;
    HRESULT STDMETHODCALLTYPE GetLightEnable(DWORD index, WINBOOL* enable) override;
    HRESULT STDMETHODCALLTYPE SetClipPlane(DWORD index, const float* plane) override;
    HRESULT STDMETHODCALLTYPE GetClipPlane(DWORD index, float* plane) override;
    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE state, DWORD value) override;
    HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE state, DWORD* value) override;
    HRESULT STDMETHODCALLTYPE SetClipStatus(const D3DCLIPSTATUS9*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetClipStatus(D3DCLIPSTATUS9* status) override;
    HRESULT STDMETHODCALLTYPE GetTexture(DWORD stage, IDirect3DBaseTexture9** texture) override;
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD stage, IDirect3DBaseTexture9* texture) override;
    HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD* value) override;
    HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value) override;
    HRESULT STDMETHODCALLTYPE GetSamplerState(DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD* value) override;
    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD value) override;
    HRESULT STDMETHODCALLTYPE ValidateDevice(DWORD* passes) override;
    HRESULT STDMETHODCALLTYPE SetScissorRect(const RECT* rect) override;
    HRESULT STDMETHODCALLTYPE GetScissorRect(RECT* rect) override;
    HRESULT STDMETHODCALLTYPE SetSoftwareVertexProcessing(WINBOOL) override { return D3D_OK; }
    WINBOOL STDMETHODCALLTYPE GetSoftwareVertexProcessing() override { return FALSE; }
    HRESULT STDMETHODCALLTYPE SetNPatchMode(float) override { return D3D_OK; }
    float STDMETHODCALLTYPE GetNPatchMode() override { return 0.0f; }
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE type, UINT start, UINT count) override;
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE type, INT baseVertex, UINT minIndex,
                                                   UINT vertexCount, UINT startIndex, UINT count) override;
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE type, UINT count, const void* vertices,
                                              UINT stride) override;
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE type, UINT minIndex, UINT vertexCount,
                                                     UINT count, const void* indices, D3DFORMAT indexFormat,
                                                     const void* vertices, UINT stride) override;
    HRESULT STDMETHODCALLTYPE CreateVertexDeclaration(const D3DVERTEXELEMENT9* elements,
                                                      IDirect3DVertexDeclaration9** declaration) override;
    HRESULT STDMETHODCALLTYPE SetVertexDeclaration(IDirect3DVertexDeclaration9* declaration) override;
    HRESULT STDMETHODCALLTYPE GetVertexDeclaration(IDirect3DVertexDeclaration9** declaration) override;
    HRESULT STDMETHODCALLTYPE SetFVF(DWORD fvf) override;
    HRESULT STDMETHODCALLTYPE GetFVF(DWORD* fvf) override;
    HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD* tokens, IDirect3DVertexShader9** shader) override;
    HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9* shader) override;
    HRESULT STDMETHODCALLTYPE GetVertexShader(IDirect3DVertexShader9** shader) override;
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT start, const float* data, UINT count) override;
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT start, float* data, UINT count) override;
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantI(UINT, const int*, UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantB(UINT, const WINBOOL*, UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE SetStreamSource(UINT stream, IDirect3DVertexBuffer9* buffer, UINT offset,
                                              UINT stride) override;
    HRESULT STDMETHODCALLTYPE GetStreamSource(UINT stream, IDirect3DVertexBuffer9** buffer, UINT* offset,
                                              UINT* stride) override;
    HRESULT STDMETHODCALLTYPE SetStreamSourceFreq(UINT, UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer9* indices) override;
    HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer9** indices) override;
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* tokens, IDirect3DPixelShader9** shader) override;
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* shader) override;
    HRESULT STDMETHODCALLTYPE GetPixelShader(IDirect3DPixelShader9** shader) override;
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT start, const float* data, UINT count) override;
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantF(UINT start, float* data, UINT count) override;
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantI(UINT, const int*, UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantB(UINT, const WINBOOL*, UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE CreateQuery(D3DQUERYTYPE type, IDirect3DQuery9** query) override;

    // --- For the backend ---
    DWORD RenderState(D3DRENDERSTATETYPE state) const { return m_renderStates[state & 0xFF]; }
    DWORD TextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type) const { return m_stageStates[stage][type]; }
    DWORD SamplerState(DWORD sampler, D3DSAMPLERSTATETYPE type) const { return m_samplerStates[sampler][type]; }
    const D3DMATRIX& Transform(D3DTRANSFORMSTATETYPE state) const;
    IDirect3DBaseTexture9* BoundTexture(DWORD stage) const { return m_textures[stage]; }
    struct Stream {
        VertexBuffer* buffer = nullptr;
        UINT offset = 0, stride = 0;
    };
    const Stream& StreamSource(UINT stream) const { return m_streams[stream]; }
    IndexBuffer* Indices() const { return m_indices; }
    DWORD Fvf() const { return m_fvf; }
    VertexDeclaration* Declaration() const { return m_declaration; }
    VertexShader* CurrentVertexShader() const { return m_vertexShader; }
    PixelShader* CurrentPixelShader() const { return m_pixelShader; }
    const float* VertexConstants() const { return &m_vsConstants[0][0]; }
    const float* PixelConstants() const { return &m_psConstants[0][0]; }
    const D3DVIEWPORT9& Viewport() const { return m_viewport; }
    const D3DMATERIAL9& Material() const { return m_material; }
    const std::map<DWORD, D3DLIGHT9>& Lights() const { return m_lights; }
    const std::map<DWORD, bool>& EnabledLights() const { return m_lightEnabled; }
    const RECT& Scissor() const { return m_scissor; }
    Surface* RenderTarget() const { return m_renderTargets[0]; }
    Surface* BackBuffer() const { return m_backBuffer; }
    const float* ClipPlane(DWORD index) const { return m_clipPlanes[index]; }
    Surface* DepthStencil() const { return m_depthStencil; }
    const D3DPRESENT_PARAMETERS& PresentParameters() const { return m_parameters; }
    uint64_t FrameNumber() const { return m_frame; }

    static void SetBackend(Backend* backend);

private:
    void MakeBackBuffers();
    void SetDefaultState();
    HRESULT Draw(const DrawCall& call);

    Direct3D* m_d3d;
    HWND m_window;
    DWORD m_behavior;
    D3DPRESENT_PARAMETERS m_parameters;
    D3DGAMMARAMP m_gamma = {};

    DWORD m_renderStates[256] = {};
    DWORD m_stageStates[8][33] = {};
    DWORD m_samplerStates[16][14] = {};
    std::map<uint32_t, D3DMATRIX> m_transforms;
    IDirect3DBaseTexture9* m_textures[16] = {};
    Stream m_streams[16];
    IndexBuffer* m_indices = nullptr;
    DWORD m_fvf = 0;
    VertexDeclaration* m_declaration = nullptr;
    VertexShader* m_vertexShader = nullptr;
    PixelShader* m_pixelShader = nullptr;
    float m_vsConstants[256][4] = {};
    float m_psConstants[32][4] = {};
    D3DVIEWPORT9 m_viewport = {};
    D3DMATERIAL9 m_material = {};
    std::map<DWORD, D3DLIGHT9> m_lights;
    std::map<DWORD, bool> m_lightEnabled;
    float m_clipPlanes[6][4] = {};
    RECT m_scissor = {};
    Surface* m_renderTargets[4] = {};
    Surface* m_depthStencil = nullptr;
    Surface* m_backBuffer = nullptr;
    Surface* m_autoDepth = nullptr;
    uint64_t m_frame = 0;
};

class Direct3D final : public Object<StubDirect3D9> {
public:
    HRESULT STDMETHODCALLTYPE RegisterSoftwareDevice(void*) override { return D3D_OK; }
    UINT STDMETHODCALLTYPE GetAdapterCount() override { return 1; }
    HRESULT STDMETHODCALLTYPE GetAdapterIdentifier(UINT adapter, DWORD, D3DADAPTER_IDENTIFIER9* identifier) override;
    UINT STDMETHODCALLTYPE GetAdapterModeCount(UINT adapter, D3DFORMAT format) override;
    HRESULT STDMETHODCALLTYPE EnumAdapterModes(UINT adapter, D3DFORMAT format, UINT index,
                                               D3DDISPLAYMODE* mode) override;
    HRESULT STDMETHODCALLTYPE GetAdapterDisplayMode(UINT adapter, D3DDISPLAYMODE* mode) override;
    HRESULT STDMETHODCALLTYPE CheckDeviceType(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT, WINBOOL) override
    {
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE CheckDeviceFormat(UINT, D3DDEVTYPE, D3DFORMAT, DWORD usage, D3DRESOURCETYPE,
                                                D3DFORMAT format) override;
    HRESULT STDMETHODCALLTYPE CheckDeviceMultiSampleType(UINT, D3DDEVTYPE, D3DFORMAT, WINBOOL,
                                                         D3DMULTISAMPLE_TYPE type, DWORD* qualities) override;
    HRESULT STDMETHODCALLTYPE CheckDepthStencilMatch(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT, D3DFORMAT) override
    {
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE CheckDeviceFormatConversion(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT) override
    {
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(UINT, D3DDEVTYPE, D3DCAPS9* caps) override;
    HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT) override { return nullptr; }
    HRESULT STDMETHODCALLTYPE CreateDevice(UINT adapter, D3DDEVTYPE type, HWND window, DWORD behavior,
                                           D3DPRESENT_PARAMETERS* parameters, IDirect3DDevice9** device) override;
};

} // namespace D3D9
