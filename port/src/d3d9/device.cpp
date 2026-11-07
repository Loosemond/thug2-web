// The Direct3D 9 device: its state as the game sets it (with D3D9's defaults), the resources it makes, and its draws
// and presents handed to the backend.

#include <SDL2/SDL.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "d3d9/device.h"
#include "hook/hook.h"

extern "C" {
unsigned thug2_frames_presented = 0; // (the headless build's scripted keys go by it)
}

namespace D3D9 {

namespace {

// Draws nothing (headless), but says once a second how many frames were presented and how many draws they had.
class NullBackend final : public Backend {
public:
    void Draw(Device&, const DrawCall&) override { ++m_draws; }
    void Present(Device&) override
    {
        ++m_frames;
        uint64_t now = SDL_GetTicks64();
        if (!m_since) {
            m_since = now;
        } else if (now - m_since >= 1000) {
            std::fprintf(stderr, "headless: %u frames presented in %.2f s, %.0f draws a frame\n", m_frames,
                         (now - m_since) / 1000.0, m_frames ? static_cast<double>(m_draws) / m_frames : 0.0);
            m_since = now;
            m_frames = m_draws = 0;
        }
    }

private:
    uint64_t m_since = 0;
    uint32_t m_frames = 0, m_draws = 0;
};

NullBackend g_nullBackend;
Backend* g_backend = &g_nullBackend;

const D3DMATRIX kIdentity = {{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}}};

uint32_t VerticesFor(D3DPRIMITIVETYPE type, uint32_t primitives)
{
    switch (type) {
    case D3DPT_POINTLIST: return primitives;
    case D3DPT_LINELIST: return primitives * 2;
    case D3DPT_LINESTRIP: return primitives + 1;
    case D3DPT_TRIANGLELIST: return primitives * 3;
    case D3DPT_TRIANGLESTRIP:
    case D3DPT_TRIANGLEFAN: return primitives + 2;
    default: return 0;
    }
}

template <typename T, typename I>
void Set(T*& slot, I* object)
{
    T* next = static_cast<T*>(object);
    if (next)
        next->AddRef();
    if (slot)
        slot->Release();
    slot = next;
}

template <typename T, typename I>
HRESULT Give(T* object, I** out)
{
    *out = object;
    if (object)
        object->AddRef();
    return D3D_OK;
}

} // namespace

void Device::SetBackend(Backend* backend) { g_backend = backend ? backend : &g_nullBackend; }

void NotifyReleased(const void* resource) { g_backend->Released(resource); }

Device::Device(Direct3D* d3d, HWND window, DWORD behavior, const D3DPRESENT_PARAMETERS& parameters)
    : m_d3d(d3d), m_window(window), m_behavior(behavior), m_parameters(parameters)
{
    m_d3d->AddRef();
    MakeBackBuffers();
    SetDefaultState();
}

Device::~Device()
{
    for (auto*& t : m_textures)
        Set(t, static_cast<IDirect3DBaseTexture9*>(nullptr));
    for (Stream& s : m_streams)
        Set(s.buffer, static_cast<IDirect3DVertexBuffer9*>(nullptr));
    Set(m_indices, static_cast<IDirect3DIndexBuffer9*>(nullptr));
    Set(m_declaration, static_cast<IDirect3DVertexDeclaration9*>(nullptr));
    Set(m_vertexShader, static_cast<IDirect3DVertexShader9*>(nullptr));
    Set(m_pixelShader, static_cast<IDirect3DPixelShader9*>(nullptr));
    for (auto*& r : m_renderTargets)
        Set(r, static_cast<IDirect3DSurface9*>(nullptr));
    Set(m_depthStencil, static_cast<IDirect3DSurface9*>(nullptr));
    Set(m_backBuffer, static_cast<IDirect3DSurface9*>(nullptr));
    Set(m_autoDepth, static_cast<IDirect3DSurface9*>(nullptr));
    m_d3d->Release();
}

void Device::MakeBackBuffers()
{
    D3DPRESENT_PARAMETERS& pp = m_parameters;
    if (!pp.BackBufferWidth || !pp.BackBufferHeight) {
        int width = 640, height = 480;
        if (m_window)
            SDL_GetWindowSize(reinterpret_cast<SDL_Window*>(m_window), &width, &height);
        pp.BackBufferWidth = static_cast<UINT>(width);
        pp.BackBufferHeight = static_cast<UINT>(height);
    }
    if (pp.BackBufferFormat == D3DFMT_UNKNOWN)
        pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    auto* back = new Surface(this, nullptr, pp.BackBufferWidth, pp.BackBufferHeight, pp.BackBufferFormat,
                             D3DUSAGE_RENDERTARGET, D3DPOOL_DEFAULT);
    Set(m_backBuffer, back);
    back->Release();
    Set(m_renderTargets[0], m_backBuffer);
    if (pp.EnableAutoDepthStencil) {
        auto* depth = new Surface(this, nullptr, pp.BackBufferWidth, pp.BackBufferHeight, pp.AutoDepthStencilFormat,
                                  D3DUSAGE_DEPTHSTENCIL, D3DPOOL_DEFAULT);
        Set(m_autoDepth, depth);
        depth->Release();
        Set(m_depthStencil, m_autoDepth);
    }
    m_viewport = {0, 0, pp.BackBufferWidth, pp.BackBufferHeight, 0.0f, 1.0f};
    m_scissor = {0, 0, static_cast<LONG>(pp.BackBufferWidth), static_cast<LONG>(pp.BackBufferHeight)};
}

// D3D9's documented defaults.
void Device::SetDefaultState()
{
    auto f = [](float value) {
        DWORD bits;
        std::memcpy(&bits, &value, 4);
        return bits;
    };
    DWORD* rs = m_renderStates;
    rs[D3DRS_ZENABLE] = m_parameters.EnableAutoDepthStencil ? D3DZB_TRUE : D3DZB_FALSE;
    rs[D3DRS_FILLMODE] = D3DFILL_SOLID;
    rs[D3DRS_SHADEMODE] = D3DSHADE_GOURAUD;
    rs[D3DRS_ZWRITEENABLE] = TRUE;
    rs[D3DRS_ALPHATESTENABLE] = FALSE;
    rs[D3DRS_LASTPIXEL] = TRUE;
    rs[D3DRS_SRCBLEND] = D3DBLEND_ONE;
    rs[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
    rs[D3DRS_CULLMODE] = D3DCULL_CCW;
    rs[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
    rs[D3DRS_ALPHAREF] = 0;
    rs[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS;
    rs[D3DRS_DITHERENABLE] = FALSE;
    rs[D3DRS_ALPHABLENDENABLE] = FALSE;
    rs[D3DRS_FOGENABLE] = FALSE;
    rs[D3DRS_SPECULARENABLE] = FALSE;
    rs[D3DRS_FOGCOLOR] = 0;
    rs[D3DRS_FOGTABLEMODE] = D3DFOG_NONE;
    rs[D3DRS_FOGSTART] = f(0.0f);
    rs[D3DRS_FOGEND] = f(1.0f);
    rs[D3DRS_FOGDENSITY] = f(1.0f);
    rs[D3DRS_RANGEFOGENABLE] = FALSE;
    rs[D3DRS_STENCILENABLE] = FALSE;
    rs[D3DRS_STENCILFAIL] = D3DSTENCILOP_KEEP;
    rs[D3DRS_STENCILZFAIL] = D3DSTENCILOP_KEEP;
    rs[D3DRS_STENCILPASS] = D3DSTENCILOP_KEEP;
    rs[D3DRS_STENCILFUNC] = D3DCMP_ALWAYS;
    rs[D3DRS_STENCILREF] = 0;
    rs[D3DRS_STENCILMASK] = 0xFFFFFFFF;
    rs[D3DRS_STENCILWRITEMASK] = 0xFFFFFFFF;
    rs[D3DRS_TEXTUREFACTOR] = 0xFFFFFFFF;
    rs[D3DRS_CLIPPING] = TRUE;
    rs[D3DRS_LIGHTING] = TRUE;
    rs[D3DRS_AMBIENT] = 0;
    rs[D3DRS_FOGVERTEXMODE] = D3DFOG_NONE;
    rs[D3DRS_COLORVERTEX] = TRUE;
    rs[D3DRS_LOCALVIEWER] = TRUE;
    rs[D3DRS_NORMALIZENORMALS] = FALSE;
    rs[D3DRS_DIFFUSEMATERIALSOURCE] = D3DMCS_COLOR1;
    rs[D3DRS_SPECULARMATERIALSOURCE] = D3DMCS_COLOR2;
    rs[D3DRS_AMBIENTMATERIALSOURCE] = D3DMCS_MATERIAL;
    rs[D3DRS_EMISSIVEMATERIALSOURCE] = D3DMCS_MATERIAL;
    rs[D3DRS_VERTEXBLEND] = D3DVBF_DISABLE;
    rs[D3DRS_CLIPPLANEENABLE] = 0;
    rs[D3DRS_POINTSIZE] = f(1.0f);
    rs[D3DRS_POINTSIZE_MIN] = f(1.0f);
    rs[D3DRS_POINTSPRITEENABLE] = FALSE;
    rs[D3DRS_POINTSCALEENABLE] = FALSE;
    rs[D3DRS_POINTSCALE_A] = f(1.0f);
    rs[D3DRS_MULTISAMPLEANTIALIAS] = TRUE;
    rs[D3DRS_MULTISAMPLEMASK] = 0xFFFFFFFF;
    rs[D3DRS_POINTSIZE_MAX] = f(64.0f);
    rs[D3DRS_COLORWRITEENABLE] = 0xF;
    rs[D3DRS_BLENDOP] = D3DBLENDOP_ADD;
    rs[D3DRS_SCISSORTESTENABLE] = FALSE;
    rs[D3DRS_TWOSIDEDSTENCILMODE] = FALSE;
    rs[D3DRS_CCW_STENCILFAIL] = D3DSTENCILOP_KEEP;
    rs[D3DRS_CCW_STENCILZFAIL] = D3DSTENCILOP_KEEP;
    rs[D3DRS_CCW_STENCILPASS] = D3DSTENCILOP_KEEP;
    rs[D3DRS_CCW_STENCILFUNC] = D3DCMP_ALWAYS;
    rs[D3DRS_COLORWRITEENABLE1] = rs[D3DRS_COLORWRITEENABLE2] = rs[D3DRS_COLORWRITEENABLE3] = 0xF;
    rs[D3DRS_BLENDFACTOR] = 0xFFFFFFFF;
    rs[D3DRS_SRCBLENDALPHA] = D3DBLEND_ONE;
    rs[D3DRS_DESTBLENDALPHA] = D3DBLEND_ZERO;
    rs[D3DRS_BLENDOPALPHA] = D3DBLENDOP_ADD;

    for (DWORD stage = 0; stage < 8; ++stage) {
        DWORD* ts = m_stageStates[stage];
        ts[D3DTSS_COLOROP] = stage == 0 ? D3DTOP_MODULATE : D3DTOP_DISABLE;
        ts[D3DTSS_COLORARG1] = D3DTA_TEXTURE;
        ts[D3DTSS_COLORARG2] = D3DTA_CURRENT;
        ts[D3DTSS_ALPHAOP] = stage == 0 ? D3DTOP_SELECTARG1 : D3DTOP_DISABLE;
        ts[D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
        ts[D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
        ts[D3DTSS_TEXCOORDINDEX] = stage;
        ts[D3DTSS_TEXTURETRANSFORMFLAGS] = D3DTTFF_DISABLE;
        ts[D3DTSS_COLORARG0] = ts[D3DTSS_ALPHAARG0] = ts[D3DTSS_RESULTARG] = D3DTA_CURRENT;
    }
    for (DWORD sampler = 0; sampler < 16; ++sampler) {
        DWORD* ss = m_samplerStates[sampler];
        ss[D3DSAMP_ADDRESSU] = ss[D3DSAMP_ADDRESSV] = ss[D3DSAMP_ADDRESSW] = D3DTADDRESS_WRAP;
        ss[D3DSAMP_MAGFILTER] = ss[D3DSAMP_MINFILTER] = D3DTEXF_POINT;
        ss[D3DSAMP_MIPFILTER] = D3DTEXF_NONE;
        ss[D3DSAMP_MAXANISOTROPY] = 1;
    }
}

// --- Device information ---

HRESULT Device::GetDirect3D(IDirect3D9** d3d) { return Give(m_d3d, d3d); }
HRESULT Device::GetDeviceCaps(D3DCAPS9* caps) { return m_d3d->GetDeviceCaps(0, D3DDEVTYPE_HAL, caps); }
HRESULT Device::GetDisplayMode(UINT, D3DDISPLAYMODE* mode) { return m_d3d->GetAdapterDisplayMode(0, mode); }

HRESULT Device::GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS* parameters)
{
    *parameters = {D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, m_window, m_behavior};
    return D3D_OK;
}

HRESULT Device::Reset(D3DPRESENT_PARAMETERS* parameters)
{
    m_parameters = *parameters;
    MakeBackBuffers();
    SetDefaultState();
    *parameters = m_parameters;
    return D3D_OK;
}

HRESULT Device::Present(const RECT*, const RECT*, HWND, const RGNDATA*)
{
    ++thug2_frames_presented;
    g_backend->Present(*this);
    ++m_frame;
    return D3D_OK;
}

HRESULT Device::GetBackBuffer(UINT, UINT, D3DBACKBUFFER_TYPE, IDirect3DSurface9** surface)
{
    return Give(m_backBuffer, surface);
}

HRESULT Device::GetRasterStatus(UINT, D3DRASTER_STATUS* status)
{
    *status = {TRUE, 0};
    return D3D_OK;
}

// --- Resources ---

HRESULT Device::CreateTexture(UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
                              IDirect3DTexture9** texture, HANDLE*)
{
    *texture = new Texture(this, width, height, levels, usage, format, pool);
    return D3D_OK;
}

HRESULT Device::CreateCubeTexture(UINT edge, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
                                  IDirect3DCubeTexture9** texture, HANDLE*)
{
    *texture = new CubeTexture(this, edge, levels, usage, format, pool);
    return D3D_OK;
}

HRESULT Device::CreateVertexBuffer(UINT length, DWORD usage, DWORD fvf, D3DPOOL pool, IDirect3DVertexBuffer9** buffer,
                                   HANDLE*)
{
    *buffer = new VertexBuffer(this, length, usage, fvf, pool);
    return D3D_OK;
}

HRESULT Device::CreateIndexBuffer(UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool,
                                  IDirect3DIndexBuffer9** buffer, HANDLE*)
{
    *buffer = new IndexBuffer(this, length, usage, format, pool);
    return D3D_OK;
}

HRESULT Device::CreateRenderTarget(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE, DWORD, WINBOOL,
                                   IDirect3DSurface9** surface, HANDLE*)
{
    *surface = new Surface(this, nullptr, width, height, format, D3DUSAGE_RENDERTARGET, D3DPOOL_DEFAULT);
    return D3D_OK;
}

HRESULT Device::CreateDepthStencilSurface(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE, DWORD,
                                          WINBOOL, IDirect3DSurface9** surface, HANDLE*)
{
    *surface = new Surface(this, nullptr, width, height, format, D3DUSAGE_DEPTHSTENCIL, D3DPOOL_DEFAULT);
    return D3D_OK;
}

HRESULT Device::CreateOffscreenPlainSurface(UINT width, UINT height, D3DFORMAT format, D3DPOOL pool,
                                            IDirect3DSurface9** surface, HANDLE*)
{
    *surface = new Surface(this, nullptr, width, height, format, 0, pool);
    return D3D_OK;
}

HRESULT Device::UpdateSurface(IDirect3DSurface9* source, const RECT* sourceRect, IDirect3DSurface9* destination,
                              const POINT* point)
{
    auto* from = static_cast<Surface*>(source);
    auto* to = static_cast<Surface*>(destination);
    if (from->Format() != to->Format())
        return D3DERR_INVALIDCALL;
    RECT r = sourceRect ? *sourceRect : RECT{0, 0, static_cast<LONG>(from->Width()), static_cast<LONG>(from->Height())};
    POINT at = point ? *point : POINT{0, 0};
    D3DLOCKED_RECT in, out;
    from->LockRect(&in, &r, 0);
    RECT target = {at.x, at.y, at.x + (r.right - r.left), at.y + (r.bottom - r.top)};
    to->LockRect(&out, &target, 0);
    uint32_t bytes = RowBytes(from->Format(), static_cast<uint32_t>(r.right - r.left));
    uint32_t rows = RowCount(from->Format(), static_cast<uint32_t>(r.bottom - r.top));
    for (uint32_t y = 0; y < rows; ++y)
        std::memcpy(static_cast<uint8_t*>(out.pBits) + y * out.Pitch, static_cast<uint8_t*>(in.pBits) + y * in.Pitch,
                    bytes);
    return D3D_OK;
}

HRESULT Device::StretchRect(IDirect3DSurface9* source, const RECT* sourceRect, IDirect3DSurface9* destination,
                            const RECT* destinationRect, D3DTEXTUREFILTERTYPE filter)
{
    if (!source || !destination)
        return D3DERR_INVALIDCALL;
    // (on the GPU: the backend's)
    g_backend->Stretch(*this, static_cast<Surface*>(source), sourceRect, static_cast<Surface*>(destination),
                       destinationRect, filter);
    return D3D_OK;
}

HRESULT Device::SetRenderTarget(DWORD index, IDirect3DSurface9* surface)
{
    if (index >= 4 || (index == 0 && !surface))
        return D3DERR_INVALIDCALL;
    Set(m_renderTargets[index], surface);
    if (index == 0 && surface) { // (the viewport and scissor follow the new target)
        auto* s = static_cast<Surface*>(surface);
        m_viewport = {0, 0, s->Width(), s->Height(), 0.0f, 1.0f};
        m_scissor = {0, 0, static_cast<LONG>(s->Width()), static_cast<LONG>(s->Height())};
    }
    return D3D_OK;
}

HRESULT Device::GetRenderTarget(DWORD index, IDirect3DSurface9** surface)
{
    if (index >= 4 || !m_renderTargets[index])
        return D3DERR_NOTFOUND;
    return Give(m_renderTargets[index], surface);
}

HRESULT Device::SetDepthStencilSurface(IDirect3DSurface9* surface)
{
    Set(m_depthStencil, surface);
    return D3D_OK;
}

HRESULT Device::GetDepthStencilSurface(IDirect3DSurface9** surface)
{
    if (!m_depthStencil)
        return D3DERR_NOTFOUND;
    return Give(m_depthStencil, surface);
}

// --- State ---

HRESULT Device::Clear(DWORD count, const D3DRECT* rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil)
{
    g_backend->Clear(*this, count, rects, flags, color, z, stencil);
    return D3D_OK;
}

const D3DMATRIX& Device::Transform(D3DTRANSFORMSTATETYPE state) const
{
    auto it = m_transforms.find(static_cast<uint32_t>(state));
    return it != m_transforms.end() ? it->second : kIdentity;
}

HRESULT Device::SetTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX* matrix)
{
    m_transforms[static_cast<uint32_t>(state)] = *matrix;
    return D3D_OK;
}

HRESULT Device::GetTransform(D3DTRANSFORMSTATETYPE state, D3DMATRIX* matrix)
{
    *matrix = Transform(state);
    return D3D_OK;
}

HRESULT Device::MultiplyTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX* matrix)
{
    const D3DMATRIX a = Transform(state);
    D3DMATRIX r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            r.m[i][j] = a.m[i][0] * matrix->m[0][j] + a.m[i][1] * matrix->m[1][j] + a.m[i][2] * matrix->m[2][j] +
                        a.m[i][3] * matrix->m[3][j];
    m_transforms[static_cast<uint32_t>(state)] = r;
    return D3D_OK;
}

HRESULT Device::SetViewport(const D3DVIEWPORT9* viewport)
{
    m_viewport = *viewport;
    return D3D_OK;
}

HRESULT Device::GetViewport(D3DVIEWPORT9* viewport)
{
    *viewport = m_viewport;
    return D3D_OK;
}

HRESULT Device::SetMaterial(const D3DMATERIAL9* material)
{
    m_material = *material;
    return D3D_OK;
}

HRESULT Device::GetMaterial(D3DMATERIAL9* material)
{
    *material = m_material;
    return D3D_OK;
}

HRESULT Device::SetLight(DWORD index, const D3DLIGHT9* light)
{
    m_lights[index] = *light;
    return D3D_OK;
}

HRESULT Device::GetLight(DWORD index, D3DLIGHT9* light)
{
    auto it = m_lights.find(index);
    if (it == m_lights.end())
        return D3DERR_INVALIDCALL;
    *light = it->second;
    return D3D_OK;
}

HRESULT Device::LightEnable(DWORD index, WINBOOL enable)
{
    if (!m_lights.count(index)) { // (a light enabled before it's set: D3D9's default light)
        D3DLIGHT9 light = {};
        light.Type = D3DLIGHT_DIRECTIONAL;
        light.Diffuse = {1, 1, 1, 0};
        light.Direction = {0, 0, 1};
        m_lights[index] = light;
    }
    m_lightEnabled[index] = enable != FALSE;
    return D3D_OK;
}

HRESULT Device::GetLightEnable(DWORD index, WINBOOL* enable)
{
    auto it = m_lightEnabled.find(index);
    *enable = it != m_lightEnabled.end() && it->second ? 128 : FALSE; // (D3D9 says 128 for on)
    return D3D_OK;
}

HRESULT Device::SetClipPlane(DWORD index, const float* plane)
{
    if (index >= 6)
        return D3DERR_INVALIDCALL;
    std::memcpy(m_clipPlanes[index], plane, sizeof(m_clipPlanes[index]));
    return D3D_OK;
}

HRESULT Device::GetClipPlane(DWORD index, float* plane)
{
    if (index >= 6)
        return D3DERR_INVALIDCALL;
    std::memcpy(plane, m_clipPlanes[index], sizeof(m_clipPlanes[index]));
    return D3D_OK;
}

HRESULT Device::SetRenderState(D3DRENDERSTATETYPE state, DWORD value)
{
    if (static_cast<DWORD>(state) >= 256)
        return D3DERR_INVALIDCALL;
    m_renderStates[state] = value;
    return D3D_OK;
}

HRESULT Device::GetRenderState(D3DRENDERSTATETYPE state, DWORD* value)
{
    if (static_cast<DWORD>(state) >= 256)
        return D3DERR_INVALIDCALL;
    *value = m_renderStates[state];
    return D3D_OK;
}

HRESULT Device::GetClipStatus(D3DCLIPSTATUS9* status)
{
    *status = {};
    return D3D_OK;
}

HRESULT Device::GetTexture(DWORD stage, IDirect3DBaseTexture9** texture)
{
    if (stage >= 16)
        return D3DERR_INVALIDCALL;
    return Give(m_textures[stage], texture);
}

HRESULT Device::SetTexture(DWORD stage, IDirect3DBaseTexture9* texture)
{
    if (stage >= 16)
        return D3D_OK; // (the displacement map and vertex samplers: not used)
    Set(m_textures[stage], texture);
    return D3D_OK;
}

HRESULT Device::GetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD* value)
{
    if (stage >= 8 || static_cast<DWORD>(type) >= 33)
        return D3DERR_INVALIDCALL;
    *value = m_stageStates[stage][type];
    return D3D_OK;
}

HRESULT Device::SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value)
{
    if (stage >= 8 || static_cast<DWORD>(type) >= 33)
        return D3DERR_INVALIDCALL;
    m_stageStates[stage][type] = value;
    return D3D_OK;
}

HRESULT Device::GetSamplerState(DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD* value)
{
    if (sampler >= 16 || static_cast<DWORD>(type) >= 14)
        return D3DERR_INVALIDCALL;
    *value = m_samplerStates[sampler][type];
    return D3D_OK;
}

HRESULT Device::SetSamplerState(DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD value)
{
    if (sampler >= 16 || static_cast<DWORD>(type) >= 14)
        return D3D_OK;
    m_samplerStates[sampler][type] = value;
    return D3D_OK;
}

HRESULT Device::ValidateDevice(DWORD* passes)
{
    *passes = 1;
    return D3D_OK;
}

HRESULT Device::SetScissorRect(const RECT* rect)
{
    m_scissor = *rect;
    return D3D_OK;
}

HRESULT Device::GetScissorRect(RECT* rect)
{
    *rect = m_scissor;
    return D3D_OK;
}

// --- Drawing ---

HRESULT Device::Draw(const DrawCall& call)
{
    if (call.primitiveCount == 0)
        return D3D_OK;
    g_backend->Draw(*this, call);
    return D3D_OK;
}

HRESULT Device::DrawPrimitive(D3DPRIMITIVETYPE type, UINT start, UINT count)
{
    DrawCall call{type, count, VerticesFor(type, count)};
    call.firstVertex = start;
    return Draw(call);
}

HRESULT Device::DrawIndexedPrimitive(D3DPRIMITIVETYPE type, INT baseVertex, UINT minIndex, UINT vertexCount,
                                     UINT startIndex, UINT count)
{
    DrawCall call{type, count, vertexCount};
    call.indexed = true;
    call.baseVertex = baseVertex;
    call.firstVertex = minIndex;
    call.firstIndex = startIndex;
    return Draw(call);
}

HRESULT Device::DrawPrimitiveUP(D3DPRIMITIVETYPE type, UINT count, const void* vertices, UINT stride)
{
    DrawCall call{type, count, VerticesFor(type, count)};
    call.userVertices = vertices;
    call.userStride = stride;
    HRESULT result = Draw(call);
    // (D3D9 unbinds stream 0 and the indices after a *UP draw)
    Set(m_streams[0].buffer, static_cast<IDirect3DVertexBuffer9*>(nullptr));
    m_streams[0].offset = m_streams[0].stride = 0;
    return result;
}

HRESULT Device::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE type, UINT minIndex, UINT vertexCount, UINT count,
                                       const void* indices, D3DFORMAT indexFormat, const void* vertices, UINT stride)
{
    DrawCall call{type, count, vertexCount};
    call.indexed = true;
    call.firstVertex = minIndex;
    call.userVertices = vertices;
    call.userStride = stride;
    call.userIndices = indices;
    call.userIndexFormat = indexFormat;
    HRESULT result = Draw(call);
    Set(m_streams[0].buffer, static_cast<IDirect3DVertexBuffer9*>(nullptr));
    m_streams[0].offset = m_streams[0].stride = 0;
    Set(m_indices, static_cast<IDirect3DIndexBuffer9*>(nullptr));
    return result;
}

HRESULT Device::CreateVertexDeclaration(const D3DVERTEXELEMENT9* elements, IDirect3DVertexDeclaration9** declaration)
{
    *declaration = new VertexDeclaration(this, elements);
    return D3D_OK;
}

HRESULT Device::SetVertexDeclaration(IDirect3DVertexDeclaration9* declaration)
{
    Set(m_declaration, declaration);
    if (declaration)
        m_fvf = 0;
    return D3D_OK;
}

HRESULT Device::GetVertexDeclaration(IDirect3DVertexDeclaration9** declaration)
{
    return Give(m_declaration, declaration);
}

HRESULT Device::SetFVF(DWORD fvf)
{
    m_fvf = fvf;
    if (fvf)
        Set(m_declaration, static_cast<IDirect3DVertexDeclaration9*>(nullptr));
    return D3D_OK;
}

HRESULT Device::GetFVF(DWORD* fvf)
{
    *fvf = m_fvf;
    return D3D_OK;
}

HRESULT Device::CreateVertexShader(const DWORD* tokens, IDirect3DVertexShader9** shader)
{
    *shader = new VertexShader(this, tokens);
    return D3D_OK;
}

HRESULT Device::SetVertexShader(IDirect3DVertexShader9* shader)
{
    Set(m_vertexShader, shader);
    return D3D_OK;
}

HRESULT Device::GetVertexShader(IDirect3DVertexShader9** shader) { return Give(m_vertexShader, shader); }

HRESULT Device::SetVertexShaderConstantF(UINT start, const float* data, UINT count)
{
    if (start + count > 256)
        return D3DERR_INVALIDCALL;
    std::memcpy(m_vsConstants[start], data, count * 16);
    return D3D_OK;
}

HRESULT Device::GetVertexShaderConstantF(UINT start, float* data, UINT count)
{
    if (start + count > 256)
        return D3DERR_INVALIDCALL;
    std::memcpy(data, m_vsConstants[start], count * 16);
    return D3D_OK;
}

HRESULT Device::SetStreamSource(UINT stream, IDirect3DVertexBuffer9* buffer, UINT offset, UINT stride)
{
    if (stream >= 16)
        return D3DERR_INVALIDCALL;
    Set(m_streams[stream].buffer, buffer);
    m_streams[stream].offset = offset;
    m_streams[stream].stride = stride;
    return D3D_OK;
}

HRESULT Device::GetStreamSource(UINT stream, IDirect3DVertexBuffer9** buffer, UINT* offset, UINT* stride)
{
    if (stream >= 16)
        return D3DERR_INVALIDCALL;
    *offset = m_streams[stream].offset;
    *stride = m_streams[stream].stride;
    return Give(m_streams[stream].buffer, buffer);
}

HRESULT Device::SetIndices(IDirect3DIndexBuffer9* indices)
{
    Set(m_indices, indices);
    return D3D_OK;
}

HRESULT Device::GetIndices(IDirect3DIndexBuffer9** indices) { return Give(m_indices, indices); }

HRESULT Device::CreatePixelShader(const DWORD* tokens, IDirect3DPixelShader9** shader)
{
    *shader = new PixelShader(this, tokens);
    return D3D_OK;
}

HRESULT Device::SetPixelShader(IDirect3DPixelShader9* shader)
{
    Set(m_pixelShader, shader);
    return D3D_OK;
}

HRESULT Device::GetPixelShader(IDirect3DPixelShader9** shader) { return Give(m_pixelShader, shader); }

HRESULT Device::SetPixelShaderConstantF(UINT start, const float* data, UINT count)
{
    if (start + count > 32)
        return D3DERR_INVALIDCALL;
    std::memcpy(m_psConstants[start], data, count * 16);
    return D3D_OK;
}

HRESULT Device::GetPixelShaderConstantF(UINT start, float* data, UINT count)
{
    if (start + count > 32)
        return D3DERR_INVALIDCALL;
    std::memcpy(data, m_psConstants[start], count * 16);
    return D3D_OK;
}

HRESULT Device::CreateQuery(D3DQUERYTYPE type, IDirect3DQuery9** query)
{
    if (type != D3DQUERYTYPE_OCCLUSION && type != D3DQUERYTYPE_EVENT)
        return D3DERR_NOTAVAILABLE;
    if (query)
        *query = new Query(this, type);
    return D3D_OK;
}

// --- IDirect3D9 ---

namespace {

// The modes offered: the usual 4:3 and wide ones, up to the desktop's.
const struct {
    UINT width, height;
} kModes[] = {{640, 480}, {800, 600}, {1024, 768}, {1280, 720}, {1280, 1024}, {1366, 768}, {1600, 900},
              {1920, 1080}, {2560, 1440}};

D3DDISPLAYMODE Desktop()
{
    SDL_DisplayMode mode;
    if (SDL_GetDesktopDisplayMode(0, &mode) != 0)
        return {1920, 1080, 60, D3DFMT_X8R8G8B8};
    return {static_cast<UINT>(mode.w), static_cast<UINT>(mode.h), mode.refresh_rate ? static_cast<UINT>(mode.refresh_rate) : 60,
            D3DFMT_X8R8G8B8};
}

} // namespace

HRESULT Direct3D::GetAdapterIdentifier(UINT adapter, DWORD, D3DADAPTER_IDENTIFIER9* identifier)
{
    if (adapter != 0)
        return D3DERR_INVALIDCALL;
    *identifier = {};
    std::snprintf(identifier->Driver, sizeof(identifier->Driver), "thug2-web");
    std::snprintf(identifier->Description, sizeof(identifier->Description), "Direct3D 9 on WebGPU");
    std::snprintf(identifier->DeviceName, sizeof(identifier->DeviceName), "\\\\.\\DISPLAY1");
    identifier->VendorId = 0x10DE; // (an NVIDIA id: the game has no vendor workarounds that would hurt)
    identifier->DeviceId = 0x0001;
    return D3D_OK;
}

UINT Direct3D::GetAdapterModeCount(UINT, D3DFORMAT format)
{
    if (format != D3DFMT_X8R8G8B8)
        return 0;
    D3DDISPLAYMODE desktop = Desktop();
    UINT count = 0;
    for (const auto& m : kModes)
        if (m.width <= desktop.Width && m.height <= desktop.Height)
            ++count;
    return count;
}

HRESULT Direct3D::EnumAdapterModes(UINT adapter, D3DFORMAT format, UINT index, D3DDISPLAYMODE* mode)
{
    if (index >= GetAdapterModeCount(adapter, format))
        return D3DERR_INVALIDCALL;
    *mode = {kModes[index].width, kModes[index].height, 60, D3DFMT_X8R8G8B8};
    return D3D_OK;
}

HRESULT Direct3D::GetAdapterDisplayMode(UINT, D3DDISPLAYMODE* mode)
{
    *mode = Desktop();
    return D3D_OK;
}

HRESULT Direct3D::CheckDeviceFormat(UINT, D3DDEVTYPE, D3DFORMAT, DWORD usage, D3DRESOURCETYPE, D3DFORMAT format)
{
    switch (format) {
    case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: case D3DFMT_R5G6B5: case D3DFMT_X1R5G5B5: case D3DFMT_A1R5G5B5:
    case D3DFMT_A4R4G4B4: case D3DFMT_A8: case D3DFMT_L8: case D3DFMT_A8L8: case D3DFMT_DXT1: case D3DFMT_DXT2:
    case D3DFMT_DXT3: case D3DFMT_DXT4: case D3DFMT_DXT5: case D3DFMT_V8U8: case D3DFMT_D16: case D3DFMT_D24S8:
    case D3DFMT_D24X8: case D3DFMT_D32:
        return D3D_OK;
    default:
        (void)usage;
        return D3DERR_NOTAVAILABLE;
    }
}

HRESULT Direct3D::CheckDeviceMultiSampleType(UINT, D3DDEVTYPE, D3DFORMAT, WINBOOL, D3DMULTISAMPLE_TYPE type,
                                             DWORD* qualities)
{
    if (qualities)
        *qualities = 1;
    return type == D3DMULTISAMPLE_NONE || type == D3DMULTISAMPLE_4_SAMPLES ? D3D_OK : D3DERR_NOTAVAILABLE;
}

// A DirectX 9 card of the game's time with WebGPU's limits: pixel shaders 1.4, vertex shaders 1.1, 8 texture stages.
HRESULT Direct3D::GetDeviceCaps(UINT, D3DDEVTYPE, D3DCAPS9* caps)
{
    *caps = {};
    caps->DeviceType = D3DDEVTYPE_HAL;
    caps->Caps2 = D3DCAPS2_DYNAMICTEXTURES | D3DCAPS2_FULLSCREENGAMMA;
    caps->PresentationIntervals = D3DPRESENT_INTERVAL_IMMEDIATE | D3DPRESENT_INTERVAL_ONE;
    caps->DevCaps = D3DDEVCAPS_HWTRANSFORMANDLIGHT | D3DDEVCAPS_HWRASTERIZATION | D3DDEVCAPS_PUREDEVICE |
                    D3DDEVCAPS_DRAWPRIMITIVES2EX;
    caps->PrimitiveMiscCaps = D3DPMISCCAPS_CULLNONE | D3DPMISCCAPS_CULLCW | D3DPMISCCAPS_CULLCCW |
                              D3DPMISCCAPS_COLORWRITEENABLE | D3DPMISCCAPS_BLENDOP | D3DPMISCCAPS_TSSARGTEMP |
                              D3DPMISCCAPS_SEPARATEALPHABLEND | D3DPMISCCAPS_MASKZ;
    caps->RasterCaps = D3DPRASTERCAPS_DEPTHBIAS | D3DPRASTERCAPS_SLOPESCALEDEPTHBIAS | D3DPRASTERCAPS_FOGTABLE |
                       D3DPRASTERCAPS_FOGVERTEX | D3DPRASTERCAPS_ZFOG | D3DPRASTERCAPS_SCISSORTEST |
                       D3DPRASTERCAPS_ANISOTROPY | D3DPRASTERCAPS_MIPMAPLODBIAS;
    caps->ZCmpCaps = caps->AlphaCmpCaps = 0xFF;
    caps->SrcBlendCaps = caps->DestBlendCaps = 0x3FFF; // (every blend, the blend factor too)
    caps->ShadeCaps = D3DPSHADECAPS_COLORGOURAUDRGB | D3DPSHADECAPS_ALPHAGOURAUDBLEND | D3DPSHADECAPS_FOGGOURAUD;
    caps->TextureCaps = D3DPTEXTURECAPS_ALPHA | D3DPTEXTURECAPS_MIPMAP | D3DPTEXTURECAPS_CUBEMAP |
                        D3DPTEXTURECAPS_PROJECTED | D3DPTEXTURECAPS_MIPCUBEMAP;
    caps->TextureFilterCaps = caps->CubeTextureFilterCaps =
        D3DPTFILTERCAPS_MINFPOINT | D3DPTFILTERCAPS_MINFLINEAR | 0x00000400 /* D3DPTFILTERCAPS_MINFANISOTROPY */ |
        D3DPTFILTERCAPS_MIPFPOINT | D3DPTFILTERCAPS_MIPFLINEAR | D3DPTFILTERCAPS_MAGFPOINT | D3DPTFILTERCAPS_MAGFLINEAR;
    caps->TextureAddressCaps = D3DPTADDRESSCAPS_WRAP | D3DPTADDRESSCAPS_MIRROR | D3DPTADDRESSCAPS_CLAMP |
                               D3DPTADDRESSCAPS_BORDER | D3DPTADDRESSCAPS_INDEPENDENTUV |
                               D3DPTADDRESSCAPS_MIRRORONCE;
    caps->LineCaps = D3DLINECAPS_TEXTURE | D3DLINECAPS_ZTEST | D3DLINECAPS_BLEND | D3DLINECAPS_ALPHACMP;
    caps->MaxTextureWidth = caps->MaxTextureHeight = 4096;
    caps->MaxTextureRepeat = 8192;
    caps->MaxTextureAspectRatio = 4096;
    caps->MaxAnisotropy = 16;
    caps->MaxVertexW = 1e10f;
    caps->GuardBandLeft = caps->GuardBandTop = -1e8f;
    caps->GuardBandRight = caps->GuardBandBottom = 1e8f;
    caps->StencilCaps = 0x1FF;
    caps->FVFCaps = 8 | D3DFVFCAPS_PSIZE;
    caps->TextureOpCaps = 0x03FFFFFF;
    caps->MaxTextureBlendStages = 8;
    caps->MaxSimultaneousTextures = 8;
    caps->VertexProcessingCaps = D3DVTXPCAPS_TEXGEN | D3DVTXPCAPS_MATERIALSOURCE7 | D3DVTXPCAPS_DIRECTIONALLIGHTS |
                                 D3DVTXPCAPS_POSITIONALLIGHTS | D3DVTXPCAPS_LOCALVIEWER;
    caps->MaxActiveLights = 8;
    caps->MaxUserClipPlanes = 6;
    caps->MaxVertexBlendMatrices = 4;
    caps->MaxVertexBlendMatrixIndex = 0;
    caps->MaxPointSize = 64.0f;
    caps->MaxPrimitiveCount = 0xFFFFF;
    caps->MaxVertexIndex = 0xFFFFFF;
    caps->MaxStreams = 16;
    caps->MaxStreamStride = 255;
    caps->VertexShaderVersion = D3DVS_VERSION(1, 1);
    caps->MaxVertexShaderConst = 256;
    caps->PixelShaderVersion = D3DPS_VERSION(1, 4);
    caps->PixelShader1xMaxValue = 8.0f;
    caps->DevCaps2 = D3DDEVCAPS2_STREAMOFFSET;
    caps->NumSimultaneousRTs = 1;
    caps->StretchRectFilterCaps = D3DPTFILTERCAPS_MINFPOINT | D3DPTFILTERCAPS_MINFLINEAR |
                                  D3DPTFILTERCAPS_MAGFPOINT | D3DPTFILTERCAPS_MAGFLINEAR;
    caps->DeclTypes = D3DDTCAPS_UBYTE4 | D3DDTCAPS_UBYTE4N | D3DDTCAPS_SHORT2N | D3DDTCAPS_SHORT4N;
    return D3D_OK;
}

HRESULT Direct3D::CreateDevice(UINT, D3DDEVTYPE, HWND window, DWORD behavior, D3DPRESENT_PARAMETERS* parameters,
                               IDirect3DDevice9** device)
{
    auto* made = new Device(this, window ? window : parameters->hDeviceWindow, behavior, *parameters);
    *parameters = made->PresentParameters();
    *device = made;
    return D3D_OK;
}

} // namespace D3D9

namespace {

IDirect3D9* __stdcall CreateDirect3D9(UINT) { return new D3D9::Direct3D; }

} // namespace

extern "C" IDirect3D9* WINAPI Direct3DCreate9(UINT version) { return CreateDirect3D9(version); }

REPLACE(0x005BEB14, CreateDirect3D9); // (the exe's import thunk)
