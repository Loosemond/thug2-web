// The Direct3D 9 resources: memory the game locks and fills; the backend uploads what changed (their versions).

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>
#include <string>

#include "d3d9/device.h"

namespace D3D9 {

void NotImplemented(const char* method)
{
    static std::mutex lock;
    static std::set<std::string> logged;
    std::lock_guard guard(lock);
    if (logged.insert(method).second)
        std::fprintf(stderr, "D3D9: %s not implemented\n", method);
}

namespace {

bool IsCompressed(D3DFORMAT format)
{
    return format == D3DFMT_DXT1 || format == D3DFMT_DXT2 || format == D3DFMT_DXT3 || format == D3DFMT_DXT4 ||
           format == D3DFMT_DXT5;
}

uint32_t PixelBytes(D3DFORMAT format)
{
    switch (format) {
    case D3DFMT_A8: case D3DFMT_L8: case D3DFMT_P8: case D3DFMT_A4L4:
        return 1;
    case D3DFMT_R5G6B5: case D3DFMT_X1R5G5B5: case D3DFMT_A1R5G5B5: case D3DFMT_A4R4G4B4: case D3DFMT_X4R4G4B4:
    case D3DFMT_A8L8: case D3DFMT_V8U8: case D3DFMT_L6V5U5: case D3DFMT_D16: case D3DFMT_D16_LOCKABLE:
    case D3DFMT_D15S1: case D3DFMT_L16: case D3DFMT_R16F: case D3DFMT_A8R3G3B2: case D3DFMT_A8P8: case D3DFMT_INDEX16:
        return 2;
    case D3DFMT_R8G8B8:
        return 3;
    case D3DFMT_A16B16G16R16: case D3DFMT_A16B16G16R16F: case D3DFMT_G32R32F: case D3DFMT_Q16W16V16U16:
        return 8;
    case D3DFMT_A32B32G32R32F:
        return 16;
    default:
        return 4;
    }
}

} // namespace

uint32_t RowBytes(D3DFORMAT format, uint32_t width)
{
    if (IsCompressed(format))
        return ((width + 3) / 4) * (format == D3DFMT_DXT1 ? 8 : 16);
    return width * PixelBytes(format);
}

uint32_t RowCount(D3DFORMAT format, uint32_t height) { return IsCompressed(format) ? (height + 3) / 4 : height; }

bool IsDepthFormat(D3DFORMAT format)
{
    switch (format) {
    case D3DFMT_D16: case D3DFMT_D16_LOCKABLE: case D3DFMT_D15S1: case D3DFMT_D24S8: case D3DFMT_D24X8:
    case D3DFMT_D24X4S4: case D3DFMT_D24FS8: case D3DFMT_D32: case D3DFMT_D32F_LOCKABLE:
        return true;
    default:
        return false;
    }
}

namespace {

HRESULT GiveDevice(Device* device, IDirect3DDevice9** out)
{
    *out = device;
    device->AddRef();
    return D3D_OK;
}

// The byte offset of a rectangle's top left in a surface's memory.
uint32_t OffsetOf(D3DFORMAT format, uint32_t pitch, const RECT* rect)
{
    if (!rect)
        return 0;
    bool compressed = RowCount(format, 4) == 1;
    uint32_t row = compressed ? rect->top / 4 : rect->top;
    uint32_t column = compressed ? RowBytes(format, rect->left) : RowBytes(format, rect->left);
    return row * pitch + column;
}

} // namespace

// --- Surface ---

Surface::Surface(Device* device, IUnknown* container, uint32_t width, uint32_t height, D3DFORMAT format, DWORD usage,
                 D3DPOOL pool, uint32_t* containerVersion)
    : m_device(device), m_container(container), m_width(width ? width : 1), m_height(height ? height : 1),
      m_format(format), m_usage(usage), m_pool(pool),
      m_bits(static_cast<size_t>(RowBytes(format, m_width)) * RowCount(format, m_height)),
      m_version(containerVersion ? containerVersion : &m_ownVersion)
{
}

HRESULT Surface::GetDevice(IDirect3DDevice9** device) { return GiveDevice(m_device, device); }

HRESULT Surface::GetContainer(REFIID, void** container)
{
    IUnknown* owner = m_container ? m_container : static_cast<IUnknown*>(m_device);
    owner->AddRef();
    *container = owner;
    return D3D_OK;
}

HRESULT Surface::GetDesc(D3DSURFACE_DESC* desc)
{
    *desc = {};
    desc->Format = m_format;
    desc->Type = D3DRTYPE_SURFACE;
    desc->Usage = m_usage;
    desc->Pool = m_pool;
    desc->MultiSampleType = D3DMULTISAMPLE_NONE;
    desc->Width = m_width;
    desc->Height = m_height;
    return D3D_OK;
}

HRESULT Surface::LockRect(D3DLOCKED_RECT* locked, const RECT* rect, DWORD)
{
    locked->Pitch = static_cast<INT>(Pitch());
    locked->pBits = m_bits.data() + OffsetOf(m_format, Pitch(), rect);
    return D3D_OK;
}

// --- Texture ---

namespace {
uint32_t LevelsFor(uint32_t width, uint32_t height, uint32_t levels)
{
    uint32_t full = 1;
    while ((width >> full) || (height >> full))
        ++full;
    return levels == 0 || levels > full ? full : levels;
}
} // namespace

Texture::Texture(Device* device, uint32_t width, uint32_t height, uint32_t levels, DWORD usage, D3DFORMAT format,
                 D3DPOOL pool)
    : m_device(device), m_usage(usage)
{
    uint32_t count = LevelsFor(width, height, levels);
    for (uint32_t i = 0; i < count; ++i)
        m_levels.push_back(new Surface(device, this, std::max(width >> i, 1u), std::max(height >> i, 1u), format,
                                       usage, pool, &m_version));
}

Texture::~Texture()
{
    NotifyReleased(this);
    for (Surface* level : m_levels)
        level->Release();
}

HRESULT Texture::GetDevice(IDirect3DDevice9** device) { return GiveDevice(m_device, device); }

HRESULT Texture::GetLevelDesc(UINT level, D3DSURFACE_DESC* desc)
{
    if (level >= m_levels.size())
        return D3DERR_INVALIDCALL;
    return m_levels[level]->GetDesc(desc);
}

HRESULT Texture::GetSurfaceLevel(UINT level, IDirect3DSurface9** surface)
{
    if (level >= m_levels.size())
        return D3DERR_INVALIDCALL;
    m_levels[level]->AddRef();
    *surface = m_levels[level];
    return D3D_OK;
}

HRESULT Texture::LockRect(UINT level, D3DLOCKED_RECT* locked, const RECT* rect, DWORD flags)
{
    if (level >= m_levels.size())
        return D3DERR_INVALIDCALL;
    return m_levels[level]->LockRect(locked, rect, flags);
}

HRESULT Texture::UnlockRect(UINT level)
{
    if (level >= m_levels.size())
        return D3DERR_INVALIDCALL;
    ++m_version;
    return D3D_OK;
}

// --- CubeTexture ---

CubeTexture::CubeTexture(Device* device, uint32_t edge, uint32_t levels, DWORD usage, D3DFORMAT format, D3DPOOL pool)
    : m_device(device), m_levelCount(LevelsFor(edge, edge, levels))
{
    for (uint32_t face = 0; face < 6; ++face)
        for (uint32_t i = 0; i < m_levelCount; ++i)
            m_faces.push_back(new Surface(device, this, std::max(edge >> i, 1u), std::max(edge >> i, 1u), format,
                                          usage, pool, &m_version));
}

CubeTexture::~CubeTexture()
{
    NotifyReleased(this);
    for (Surface* face : m_faces)
        face->Release();
}

HRESULT CubeTexture::GetDevice(IDirect3DDevice9** device) { return GiveDevice(m_device, device); }

HRESULT CubeTexture::GetLevelDesc(UINT level, D3DSURFACE_DESC* desc)
{
    if (level >= m_levelCount)
        return D3DERR_INVALIDCALL;
    return Face(0, level)->GetDesc(desc);
}

HRESULT CubeTexture::GetCubeMapSurface(D3DCUBEMAP_FACES face, UINT level, IDirect3DSurface9** surface)
{
    if (level >= m_levelCount || face > 5)
        return D3DERR_INVALIDCALL;
    Surface* s = Face(face, level);
    s->AddRef();
    *surface = s;
    return D3D_OK;
}

HRESULT CubeTexture::LockRect(D3DCUBEMAP_FACES face, UINT level, D3DLOCKED_RECT* locked, const RECT* rect,
                              DWORD flags)
{
    if (level >= m_levelCount || face > 5)
        return D3DERR_INVALIDCALL;
    return Face(face, level)->LockRect(locked, rect, flags);
}

// --- Buffers ---

VertexBuffer::VertexBuffer(Device* device, uint32_t length, DWORD usage, DWORD fvf, D3DPOOL pool)
    : m_device(device), m_usage(usage), m_fvf(fvf), m_pool(pool), m_data(length)
{
}

HRESULT VertexBuffer::GetDevice(IDirect3DDevice9** device) { return GiveDevice(m_device, device); }

HRESULT VertexBuffer::Lock(UINT offset, UINT size, void** data, DWORD)
{
    if (offset > m_data.size())
        return D3DERR_INVALIDCALL;
    m_dirty.Add(offset, size, Length());
    *data = m_data.data() + offset;
    return D3D_OK;
}

HRESULT VertexBuffer::GetDesc(D3DVERTEXBUFFER_DESC* desc)
{
    *desc = {};
    desc->Format = D3DFMT_VERTEXDATA;
    desc->Type = D3DRTYPE_VERTEXBUFFER;
    desc->Usage = m_usage;
    desc->Pool = m_pool;
    desc->Size = static_cast<UINT>(m_data.size());
    desc->FVF = m_fvf;
    return D3D_OK;
}

IndexBuffer::IndexBuffer(Device* device, uint32_t length, DWORD usage, D3DFORMAT format, D3DPOOL pool)
    : m_device(device), m_usage(usage), m_format(format), m_pool(pool), m_data(length)
{
}

HRESULT IndexBuffer::GetDevice(IDirect3DDevice9** device) { return GiveDevice(m_device, device); }

HRESULT IndexBuffer::Lock(UINT offset, UINT size, void** data, DWORD)
{
    if (offset > m_data.size())
        return D3DERR_INVALIDCALL;
    m_dirty.Add(offset, size, Length());
    *data = m_data.data() + offset;
    return D3D_OK;
}

HRESULT IndexBuffer::GetDesc(D3DINDEXBUFFER_DESC* desc)
{
    *desc = {};
    desc->Format = m_format;
    desc->Type = D3DRTYPE_INDEXBUFFER;
    desc->Usage = m_usage;
    desc->Pool = m_pool;
    desc->Size = static_cast<UINT>(m_data.size());
    return D3D_OK;
}

// --- Declarations, shaders, queries ---

VertexDeclaration::VertexDeclaration(Device* device, const D3DVERTEXELEMENT9* elements) : m_device(device)
{
    for (;; ++elements) {
        m_elements.push_back(*elements);
        if (elements->Stream == 0xFF)
            break;
    }
}

HRESULT VertexDeclaration::GetDevice(IDirect3DDevice9** device) { return GiveDevice(m_device, device); }

HRESULT VertexDeclaration::GetDeclaration(D3DVERTEXELEMENT9* elements, UINT* count)
{
    if (elements)
        std::memcpy(elements, m_elements.data(), m_elements.size() * sizeof(D3DVERTEXELEMENT9));
    *count = static_cast<UINT>(m_elements.size());
    return D3D_OK;
}

template <typename Stub>
Shader<Stub>::Shader(Device* device, const DWORD* tokens) : m_device(device)
{
    // (up to the end token, 0x0000FFFF, inclusive; comments skipped by their length)
    for (const DWORD* t = tokens;; ++t) {
        m_tokens.push_back(*t);
        if (*t == 0x0000FFFF)
            break;
        if ((*t & 0xFFFF) == 0xFFFE) { // a comment: its length in the high word
            DWORD length = *t >> 16;
            m_tokens.insert(m_tokens.end(), t + 1, t + 1 + length);
            t += length;
        }
    }
}

template <typename Stub>
HRESULT Shader<Stub>::GetDevice(IDirect3DDevice9** device)
{
    return GiveDevice(m_device, device);
}

template <typename Stub>
HRESULT Shader<Stub>::GetFunction(void* data, UINT* size)
{
    *size = static_cast<UINT>(m_tokens.size() * sizeof(DWORD));
    if (data)
        std::memcpy(data, m_tokens.data(), *size);
    return D3D_OK;
}

template class Shader<StubDirect3DVertexShader9>;
template class Shader<StubDirect3DPixelShader9>;

HRESULT Query::GetDevice(IDirect3DDevice9** device) { return GiveDevice(m_device, device); }

DWORD Query::GetDataSize()
{
    switch (m_type) {
    case D3DQUERYTYPE_OCCLUSION: return sizeof(DWORD);
    case D3DQUERYTYPE_EVENT: return sizeof(BOOL);
    default: return 0;
    }
}

// Every query has its answer at once: occlusion queries say everything is visible (the conservative answer) until a
// backend counts samples.
HRESULT Query::GetData(void* data, DWORD size, DWORD)
{
    if (data && size >= sizeof(DWORD))
        *static_cast<DWORD*>(data) = m_type == D3DQUERYTYPE_OCCLUSION ? 0x7FFFFFFF : TRUE;
    return S_OK;
}

} // namespace D3D9
