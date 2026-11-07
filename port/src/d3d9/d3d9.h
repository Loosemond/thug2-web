#pragma once

// Direct3D 9 for the web build: the objects the game makes and the state it sets, kept here; what's drawn goes to a
// backend (WebGPU, WebGL2 - or none, for running headless). Every interface method exists (stubs.h, generated), in
// the SDK's vtable order, since the game calls many of them by offset.

#include <cstdint>
#include <vector>

#include "d3d9/stubs.h"

namespace D3D9 {

// IUnknown for a class implementing interface I: the count of references, deleted at 0.
template <typename Stub>
class Object : public Stub {
public:
    virtual ~Object() = default;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** object) override
    {
        *object = this;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_references; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG left = --m_references;
        if (left == 0)
            delete this;
        return left;
    }

private:
    ULONG m_references = 1;
};

class Device;

// Tells the backend a resource is gone (its copy can go too). (device.cpp)
void NotifyReleased(const void* resource);

// The bytes of a buffer written since the backend last took them: the union of the locked ranges.
struct DirtyRange {
    uint32_t begin = 0, end = 0;
    void Add(uint32_t offset, uint32_t size, uint32_t length)
    {
        uint32_t last = size == 0 || offset + size > length ? length : offset + size; // (0: to the end)
        if (begin == end) {
            begin = offset;
            end = last;
        } else {
            begin = begin < offset ? begin : offset;
            end = end > last ? end : last;
        }
    }
    bool Empty() const { return begin >= end; }
    DirtyRange Take()
    {
        DirtyRange taken = *this;
        begin = end = 0;
        return taken;
    }
};

// The bytes of one row of `width` pixels in `format` (of 4x4 blocks for DXT), and how many such rows `height` has.
uint32_t RowBytes(D3DFORMAT format, uint32_t width);
uint32_t RowCount(D3DFORMAT format, uint32_t height);
bool IsDepthFormat(D3DFORMAT format);

// A level of a texture, or a render target / depth surface of its own: its pixels in memory.
class Surface final : public Object<StubDirect3DSurface9> {
public:
    Surface(Device* device, IUnknown* container, uint32_t width, uint32_t height, D3DFORMAT format, DWORD usage,
            D3DPOOL pool, uint32_t* containerVersion = nullptr);
    ~Surface() override { NotifyReleased(this); }
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** device) override;
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_SURFACE; }
    HRESULT STDMETHODCALLTYPE GetContainer(REFIID, void** container) override;
    HRESULT STDMETHODCALLTYPE GetDesc(D3DSURFACE_DESC* desc) override;
    HRESULT STDMETHODCALLTYPE LockRect(D3DLOCKED_RECT* locked, const RECT* rect, DWORD flags) override;
    HRESULT STDMETHODCALLTYPE UnlockRect() override
    {
        ++*m_version; // (its texture's, for a level)
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDC(HDC*) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE ReleaseDC(HDC) override { return D3DERR_INVALIDCALL; }
    DWORD STDMETHODCALLTYPE SetPriority(DWORD) override { return 0; }
    DWORD STDMETHODCALLTYPE GetPriority() override { return 0; }
    void STDMETHODCALLTYPE PreLoad() override {}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void*, DWORD, DWORD) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void*, DWORD*) override { return D3DERR_NOTFOUND; }
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) override { return D3D_OK; }

    uint32_t Width() const { return m_width; }
    uint32_t Height() const { return m_height; }
    D3DFORMAT Format() const { return m_format; }
    uint32_t Pitch() const { return RowBytes(m_format, m_width); }
    uint8_t* Bits() { return m_bits.data(); }
    const uint8_t* Bits() const { return m_bits.data(); }
    DWORD Usage() const { return m_usage; }
    uint32_t Version() const { return *m_version; }
    IUnknown* Container() const { return m_container; } // (the texture it's a level of, or null)

private:
    Device* m_device;
    IUnknown* m_container; // (the texture it's a level of, or null)
    uint32_t m_ownVersion = 0;
    uint32_t* m_version; // (bumped on each unlock: the container's for a level, else m_ownVersion)
    uint32_t m_width, m_height;
    D3DFORMAT m_format;
    DWORD m_usage;
    D3DPOOL m_pool;
    std::vector<uint8_t> m_bits;
};

class Texture final : public Object<StubDirect3DTexture9> {
public:
    Texture(Device* device, uint32_t width, uint32_t height, uint32_t levels, DWORD usage, D3DFORMAT format,
            D3DPOOL pool);
    ~Texture() override;
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** device) override;
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_TEXTURE; }
    DWORD STDMETHODCALLTYPE GetLevelCount() override { return static_cast<DWORD>(m_levels.size()); }
    DWORD STDMETHODCALLTYPE SetLOD(DWORD) override { return 0; }
    DWORD STDMETHODCALLTYPE GetLOD() override { return 0; }
    DWORD STDMETHODCALLTYPE SetPriority(DWORD) override { return 0; }
    DWORD STDMETHODCALLTYPE GetPriority() override { return 0; }
    void STDMETHODCALLTYPE PreLoad() override {}
    HRESULT STDMETHODCALLTYPE SetAutoGenFilterType(D3DTEXTUREFILTERTYPE) override { return D3D_OK; }
    D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE GetAutoGenFilterType() override { return D3DTEXF_LINEAR; }
    void STDMETHODCALLTYPE GenerateMipSubLevels() override {}
    HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT level, D3DSURFACE_DESC* desc) override;
    HRESULT STDMETHODCALLTYPE GetSurfaceLevel(UINT level, IDirect3DSurface9** surface) override;
    HRESULT STDMETHODCALLTYPE LockRect(UINT level, D3DLOCKED_RECT* locked, const RECT* rect, DWORD flags) override;
    HRESULT STDMETHODCALLTYPE UnlockRect(UINT level) override;
    HRESULT STDMETHODCALLTYPE AddDirtyRect(const RECT*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void*, DWORD, DWORD) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void*, DWORD*) override { return D3DERR_NOTFOUND; }
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) override { return D3D_OK; }

    Surface* Level(uint32_t level) const { return m_levels[level]; }
    uint32_t LevelCount() const { return static_cast<uint32_t>(m_levels.size()); }
    uint32_t Version() const { return m_version; } // (bumped on each unlock: the backend's copy is stale)
    DWORD Usage() const { return m_usage; }
    D3DFORMAT Format() const { return m_levels[0]->Format(); }

private:
    Device* m_device;
    DWORD m_usage;
    std::vector<Surface*> m_levels;
    uint32_t m_version = 0;
};

class CubeTexture final : public Object<StubDirect3DCubeTexture9> {
public:
    CubeTexture(Device* device, uint32_t edge, uint32_t levels, DWORD usage, D3DFORMAT format, D3DPOOL pool);
    ~CubeTexture() override;
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** device) override;
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_CUBETEXTURE; }
    DWORD STDMETHODCALLTYPE GetLevelCount() override { return m_levelCount; }
    HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT level, D3DSURFACE_DESC* desc) override;
    HRESULT STDMETHODCALLTYPE GetCubeMapSurface(D3DCUBEMAP_FACES face, UINT level, IDirect3DSurface9** surface) override;
    HRESULT STDMETHODCALLTYPE LockRect(D3DCUBEMAP_FACES face, UINT level, D3DLOCKED_RECT* locked, const RECT* rect,
                                       DWORD flags) override;
    HRESULT STDMETHODCALLTYPE UnlockRect(D3DCUBEMAP_FACES, UINT) override
    {
        ++m_version;
        return D3D_OK;
    }

    Surface* Face(uint32_t face, uint32_t level) const { return m_faces[face * m_levelCount + level]; }
    uint32_t Version() const { return m_version; }
    uint32_t LevelCount() const { return m_levelCount; }
    D3DFORMAT Format() const { return m_faces[0]->Format(); }

private:
    Device* m_device;
    uint32_t m_levelCount;
    std::vector<Surface*> m_faces; // [face][level]
    uint32_t m_version = 0;
};

class VertexBuffer final : public Object<StubDirect3DVertexBuffer9> {
public:
    VertexBuffer(Device* device, uint32_t length, DWORD usage, DWORD fvf, D3DPOOL pool);
    ~VertexBuffer() override { NotifyReleased(this); }
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** device) override;
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_VERTEXBUFFER; }
    void STDMETHODCALLTYPE PreLoad() override {}
    HRESULT STDMETHODCALLTYPE Lock(UINT offset, UINT size, void** data, DWORD flags) override;
    HRESULT STDMETHODCALLTYPE Unlock() override
    {
        ++m_version;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDesc(D3DVERTEXBUFFER_DESC* desc) override;

    const uint8_t* Data() const { return m_data.data(); }
    uint32_t Length() const { return static_cast<uint32_t>(m_data.size()); }
    uint32_t Version() const { return m_version; }
    DWORD Usage() const { return m_usage; }
    DirtyRange& Dirty() { return m_dirty; }

private:
    Device* m_device;
    DWORD m_usage, m_fvf;
    D3DPOOL m_pool;
    std::vector<uint8_t> m_data;
    uint32_t m_version = 0;
    DirtyRange m_dirty;
};

class IndexBuffer final : public Object<StubDirect3DIndexBuffer9> {
public:
    IndexBuffer(Device* device, uint32_t length, DWORD usage, D3DFORMAT format, D3DPOOL pool);
    ~IndexBuffer() override { NotifyReleased(this); }
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** device) override;
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_INDEXBUFFER; }
    void STDMETHODCALLTYPE PreLoad() override {}
    HRESULT STDMETHODCALLTYPE Lock(UINT offset, UINT size, void** data, DWORD flags) override;
    HRESULT STDMETHODCALLTYPE Unlock() override
    {
        ++m_version;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDesc(D3DINDEXBUFFER_DESC* desc) override;

    const uint8_t* Data() const { return m_data.data(); }
    uint32_t Length() const { return static_cast<uint32_t>(m_data.size()); }
    D3DFORMAT Format() const { return m_format; }
    uint32_t Version() const { return m_version; }
    DWORD Usage() const { return m_usage; }
    DirtyRange& Dirty() { return m_dirty; }

private:
    Device* m_device;
    DWORD m_usage;
    D3DFORMAT m_format;
    D3DPOOL m_pool;
    std::vector<uint8_t> m_data;
    uint32_t m_version = 0;
    DirtyRange m_dirty;
};

class VertexDeclaration final : public Object<StubDirect3DVertexDeclaration9> {
public:
    VertexDeclaration(Device* device, const D3DVERTEXELEMENT9* elements);
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** device) override;
    HRESULT STDMETHODCALLTYPE GetDeclaration(D3DVERTEXELEMENT9* elements, UINT* count) override;
    const std::vector<D3DVERTEXELEMENT9>& Elements() const { return m_elements; } // (with the end marker)

private:
    Device* m_device;
    std::vector<D3DVERTEXELEMENT9> m_elements;
};

// A shader's tokens, as the game gave them (or, from our D3DXAssembleShader, its source text: port/src/library).
template <typename Stub>
class Shader final : public Object<Stub> {
public:
    Shader(Device* device, const DWORD* tokens);
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** device) override;
    HRESULT STDMETHODCALLTYPE GetFunction(void* data, UINT* size) override;
    const std::vector<DWORD>& Tokens() const { return m_tokens; }

private:
    Device* m_device;
    std::vector<DWORD> m_tokens;
};
using VertexShader = Shader<StubDirect3DVertexShader9>;
using PixelShader = Shader<StubDirect3DPixelShader9>;

class Query final : public Object<StubDirect3DQuery9> {
public:
    Query(Device* device, D3DQUERYTYPE type) : m_device(device), m_type(type) {}
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** device) override;
    D3DQUERYTYPE STDMETHODCALLTYPE GetType() override { return m_type; }
    DWORD STDMETHODCALLTYPE GetDataSize() override;
    HRESULT STDMETHODCALLTYPE Issue(DWORD) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetData(void* data, DWORD size, DWORD flags) override;

private:
    Device* m_device;
    D3DQUERYTYPE m_type;
};

} // namespace D3D9
