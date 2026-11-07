// The Direct3D 9 device's backend in the page: each frame recorded, on the game's thread, as commands for the page's
// thread to draw (gpu_frame.h). Like a driver it sends a resource's bytes when they change (a buffer's locked range,
// a texture's levels) and keeps the state that makes a pipeline as one id per kind; each draw carries its uniforms.
// A buffer changed after it was drawn from in the same frame has the draw's vertices sent with the draw instead (its
// GPU copy is only brought up to date before the frame's first use).

#include <windows.h>

#include <d3d9.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include "d3d9/device.h"
#include "web/gpu_frame.h"

namespace Gpu {

FrameQueue& Frames()
{
    static FrameQueue queue(2);
    return queue;
}

namespace {

using namespace D3D9;

uint32_t Id(const void* object) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(object)); }
float AsFloat(DWORD bits)
{
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

Format FormatOf(D3DFORMAT format)
{
    switch (format) {
    case D3DFMT_A8R8G8B8: return Format::BGRA8;
    case D3DFMT_X8R8G8B8: return Format::BGRX8;
    case D3DFMT_R5G6B5: return Format::R5G6B5;
    case D3DFMT_A1R5G5B5: return Format::A1R5G5B5;
    case D3DFMT_X1R5G5B5: return Format::X1R5G5B5;
    case D3DFMT_A4R4G4B4: return Format::A4R4G4B4;
    case D3DFMT_A8: return Format::A8;
    case D3DFMT_L8: return Format::L8;
    case D3DFMT_A8L8: return Format::A8L8;
    case D3DFMT_DXT1: return Format::DXT1;
    case D3DFMT_DXT2:
    case D3DFMT_DXT3: return Format::DXT3;
    case D3DFMT_DXT4:
    case D3DFMT_DXT5: return Format::DXT5;
    case D3DFMT_D16:
    case D3DFMT_D16_LOCKABLE: return Format::Depth16;
    case D3DFMT_D24S8:
    case D3DFMT_D24X8:
    case D3DFMT_D24X4S4:
    case D3DFMT_D32:
    case D3DFMT_D15S1: return Format::Depth24S8;
    default: return Format::None;
    }
}

// 4x4 matrices, Direct3D's row-vector layout.
using Matrix = float[16];
void Multiply(const float* a, const float* b, float* out)
{
    float r[16];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            r[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j] + a[i * 4 + 3] * b[12 + j];
    std::memcpy(out, r, sizeof(r));
}
// The inverse transpose's upper 3x3 (for normals), in a 4x4.
void NormalMatrix(const float* m, float* out)
{
    float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    float A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    float det = a * A + b * B + c * C;
    float s = std::fabs(det) > 1e-20f ? 1.0f / det : 0.0f;
    // inverse = adjugate / det; its transpose is the cofactor matrix / det
    float r[16] = {A * s, B * s, C * s, 0,
                   -(b * i - c * h) * s, (a * i - c * g) * s, -(a * h - b * g) * s, 0,
                   (b * f - c * e) * s, -(a * f - c * d) * s, (a * e - b * d) * s, 0,
                   0, 0, 0, 1};
    std::memcpy(out, r, sizeof(r));
}
void TransformPoint(const float* m, const float* p, float* out)
{
    for (int j = 0; j < 3; ++j)
        out[j] = p[0] * m[j] + p[1] * m[4 + j] + p[2] * m[8 + j] + m[12 + j];
}
void TransformVector(const float* m, const float* v, float* out)
{
    for (int j = 0; j < 3; ++j)
        out[j] = v[0] * m[j] + v[1] * m[4 + j] + v[2] * m[8 + j];
}
void ColorOf(DWORD color, float* out)
{
    out[0] = ((color >> 16) & 0xFF) / 255.0f;
    out[1] = ((color >> 8) & 0xFF) / 255.0f;
    out[2] = (color & 0xFF) / 255.0f;
    out[3] = ((color >> 24) & 0xFF) / 255.0f;
}
void ColorOf(const D3DCOLORVALUE& color, float* out)
{
    out[0] = color.r;
    out[1] = color.g;
    out[2] = color.b;
    out[3] = color.a;
}

// The vertex elements of an FVF.
uint8_t ElementsOfFvf(DWORD fvf, VertexElement* out)
{
    uint8_t n = 0;
    uint16_t offset = 0;
    auto add = [&](uint8_t type, uint8_t usage, uint8_t index, uint16_t size) {
        out[n++] = {0, type, usage, index, offset};
        offset = static_cast<uint16_t>(offset + size);
    };
    DWORD position = fvf & D3DFVF_POSITION_MASK;
    switch (position) {
    case D3DFVF_XYZ: add(D3DDECLTYPE_FLOAT3, D3DDECLUSAGE_POSITION, 0, 12); break;
    case D3DFVF_XYZRHW: add(D3DDECLTYPE_FLOAT4, D3DDECLUSAGE_POSITIONT, 0, 16); break;
    case D3DFVF_XYZW: add(D3DDECLTYPE_FLOAT4, D3DDECLUSAGE_POSITION, 0, 16); break;
    default:
        if (position >= D3DFVF_XYZB1 && position <= D3DFVF_XYZB5) {
            add(D3DDECLTYPE_FLOAT3, D3DDECLUSAGE_POSITION, 0, 12);
            int weights = static_cast<int>((position - D3DFVF_XYZB1) / 2 + 1);
            bool indices = fvf & D3DFVF_LASTBETA_UBYTE4;
            int floats = weights - (indices ? 1 : 0);
            if (floats > 0)
                add(static_cast<uint8_t>(D3DDECLTYPE_FLOAT1 + floats - 1), D3DDECLUSAGE_BLENDWEIGHT, 0,
                    static_cast<uint16_t>(floats * 4));
            if (indices)
                add(D3DDECLTYPE_UBYTE4, D3DDECLUSAGE_BLENDINDICES, 0, 4);
        }
        break;
    }
    if (fvf & D3DFVF_NORMAL)
        add(D3DDECLTYPE_FLOAT3, D3DDECLUSAGE_NORMAL, 0, 12);
    if (fvf & D3DFVF_PSIZE)
        add(D3DDECLTYPE_FLOAT1, D3DDECLUSAGE_PSIZE, 0, 4);
    if (fvf & D3DFVF_DIFFUSE)
        add(D3DDECLTYPE_D3DCOLOR, D3DDECLUSAGE_COLOR, 0, 4);
    if (fvf & D3DFVF_SPECULAR)
        add(D3DDECLTYPE_D3DCOLOR, D3DDECLUSAGE_COLOR, 1, 4);
    int sets = static_cast<int>((fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT);
    for (int i = 0; i < sets && n < 16; ++i) {
        DWORD format = (fvf >> (16 + i * 2)) & 3; // D3DFVF_TEXTUREFORMAT2 = 0, 1 = 3 floats, 2 = 4, 3 = 1
        static const uint8_t kTypes[4] = {D3DDECLTYPE_FLOAT2, D3DDECLTYPE_FLOAT3, D3DDECLTYPE_FLOAT4,
                                          D3DDECLTYPE_FLOAT1};
        static const uint16_t kSizes[4] = {8, 12, 16, 4};
        add(kTypes[format], D3DDECLUSAGE_TEXCOORD, static_cast<uint8_t>(i), kSizes[format]);
    }
    return n;
}

struct PipelineHash {
    size_t operator()(const PipelineKey& key) const
    {
        const auto* bytes = reinterpret_cast<const uint8_t*>(&key);
        uint64_t h = 1469598103934665603ull;
        for (size_t i = 0; i < sizeof(key); ++i)
            h = (h ^ bytes[i]) * 1099511628211ull;
        return static_cast<size_t>(h);
    }
};
struct PipelineEqual {
    bool operator()(const PipelineKey& a, const PipelineKey& b) const { return std::memcmp(&a, &b, sizeof(a)) == 0; }
};

uint32_t Fnv(const void* data, size_t size)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < size; ++i)
        h = (h ^ static_cast<const uint8_t*>(data)[i]) * 16777619u;
    return h ? h : 1;
}

class Recorder final : public Backend {
public:
    Recorder() : m_frame(Frames().Spare()), m_last(std::chrono::steady_clock::now()) {}

    void Clear(Device& device, DWORD count, const D3DRECT* rects, DWORD flags, D3DCOLOR color, float z,
               DWORD stencil) override
    {
        std::lock_guard lock(m_lock);
        Gpu::Clear clear = {};
        clear.target = TargetOf(device);
        clear.flags = flags;
        clear.color = color;
        clear.z = z;
        clear.stencil = stencil;
        const D3DVIEWPORT9& vp = device.Viewport();
        clear.viewport[0] = static_cast<int32_t>(vp.X);
        clear.viewport[1] = static_cast<int32_t>(vp.Y);
        clear.viewport[2] = static_cast<int32_t>(vp.Width);
        clear.viewport[3] = static_cast<int32_t>(vp.Height);
        if (count == 0 || !rects) {
            clear.rect[0] = clear.rect[1] = clear.rect[2] = clear.rect[3] = -1;
            m_frame->Add(Op::Clear, clear);
            return;
        }
        for (DWORD i = 0; i < count; ++i) {
            clear.rect[0] = rects[i].x1;
            clear.rect[1] = rects[i].y1;
            clear.rect[2] = rects[i].x2;
            clear.rect[3] = rects[i].y2;
            m_frame->Add(Op::Clear, clear);
        }
    }

    void Draw(Device& device, const DrawCall& call) override
    {
        std::lock_guard lock(m_lock);
        Gpu::Draw draw = {};
        draw.target = TargetOf(device);
        PipelineKey key;
        std::memset(&key, 0, sizeof(key));
        FillKey(device, call, key);

        // The primitives: fans become lists (WebGPU has none).
        uint32_t count = 0;
        switch (call.type) {
        case D3DPT_POINTLIST: count = call.primitiveCount; break;
        case D3DPT_LINELIST: count = call.primitiveCount * 2; break;
        case D3DPT_LINESTRIP: count = call.primitiveCount + 1; break;
        case D3DPT_TRIANGLELIST: count = call.primitiveCount * 3; break;
        case D3DPT_TRIANGLESTRIP: count = call.primitiveCount + 2; break;
        case D3DPT_TRIANGLEFAN: count = call.primitiveCount * 3; key.topology = D3DPT_TRIANGLELIST; break;
        default: return;
        }

        if (!Vertices(device, call, key, draw))
            return;
        if (!Indices(device, call, key, draw, count))
            return;
        draw.count = count;
        draw.pipeline = PipelineId(key);
        Textures(device, key, draw);
        Uniforms(device, key, draw);
        const D3DVIEWPORT9& vp = device.Viewport();
        draw.viewport[0] = static_cast<float>(vp.X);
        draw.viewport[1] = static_cast<float>(vp.Y);
        draw.viewport[2] = static_cast<float>(vp.Width);
        draw.viewport[3] = static_cast<float>(vp.Height);
        draw.viewport[4] = vp.MinZ;
        draw.viewport[5] = vp.MaxZ;
        if (device.RenderState(D3DRS_SCISSORTESTENABLE)) {
            const RECT& s = device.Scissor();
            draw.scissor[0] = s.left;
            draw.scissor[1] = s.top;
            draw.scissor[2] = s.right;
            draw.scissor[3] = s.bottom;
        }
        draw.stencilRef = device.RenderState(D3DRS_STENCILREF);
        draw.blendFactor = device.RenderState(D3DRS_BLENDFACTOR);
        m_frame->Add(Op::Draw, draw);
        ++m_draws;
    }

    void Stretch(Device& device, Surface* source, const RECT* sourceRect, Surface* destination,
                 const RECT* destinationRect, D3DTEXTUREFILTERTYPE filter) override
    {
        std::lock_guard lock(m_lock);
        Gpu::Stretch stretch = {};
        stretch.source = SurfaceId(device, source);
        stretch.destination = SurfaceId(device, destination);
        auto rect = [](const RECT* r, int32_t* out) {
            if (r) {
                out[0] = r->left;
                out[1] = r->top;
                out[2] = r->right;
                out[3] = r->bottom;
            } else {
                out[0] = out[1] = out[2] = out[3] = -1;
            }
        };
        rect(sourceRect, stretch.sourceRect);
        rect(destinationRect, stretch.destinationRect);
        stretch.linear = filter == D3DTEXF_LINEAR;
        m_frame->Add(Op::Stretch, stretch);
    }

    void Present(Device& device) override
    {
        std::unique_ptr<Frame> done;
        {
            std::lock_guard lock(m_lock);
            uint32_t backBuffer = SurfaceId(device, device.BackBuffer());
            m_frame->Add(Op::Present, Gpu::Present{backBuffer});
            auto now = std::chrono::steady_clock::now();
            m_frame->recordedMs = std::chrono::duration<double, std::milli>(now - m_last).count();
            m_frame->number = m_frameNumber++;
            done = std::move(m_frame);
            m_frame = Frames().Spare();
            m_draws = 0;
            m_lastDrawOffset = m_lastLightingOffset = ~0u; // (the new frame's data starts empty)
        }
        Frames().Push(std::move(done)); // (waits while the page is two frames behind)
        m_last = std::chrono::steady_clock::now();
    }

    void Released(const void* resource) override
    {
        std::lock_guard lock(m_lock);
        bool known = m_textures.erase(resource) + m_buffers.erase(resource) > 0;
        if (known)
            m_frame->Add(Op::Release, Gpu::Release{Id(resource)});
    }

private:
    struct Known {
        uint32_t version = ~0u; // what the GPU copy holds
        uint64_t usedFrame = ~0ull;
    };

    // --- Targets and textures ---

    // The id a surface draws to: a target texture's (its level 0), else its own (defined on first use).
    uint32_t SurfaceId(Device& device, Surface* surface)
    {
        if (!surface)
            return 0;
        if (IUnknown* container = surface->Container()) {
            auto* texture = static_cast<IDirect3DBaseTexture9*>(static_cast<IUnknown*>(container));
            DefineTextureOnce(texture);
            return Id(texture);
        }
        auto [it, added] = m_textures.try_emplace(surface);
        if (added) {
            DefineTexture define = {};
            define.id = Id(surface);
            define.format = FormatOf(surface->Format());
            define.kind = IsDepthFormat(surface->Format()) ? TextureKind::Depth : TextureKind::Target;
            define.levels = 1;
            define.width = surface->Width();
            define.height = surface->Height();
            if (surface == device.BackBuffer())
                define.kind = TextureKind::Target, define.format = Format::BGRA8;
            m_frame->Add(Op::DefineTexture, define);
        }
        return Id(surface);
    }

    Target TargetOf(Device& device)
    {
        Target target = {};
        Surface* color = device.RenderTarget();
        target.color = SurfaceId(device, color);
        target.depth = device.RenderState(D3DRS_ZENABLE) || device.RenderState(D3DRS_STENCILENABLE)
                           ? SurfaceId(device, device.DepthStencil())
                           : 0;
        target.width = color ? color->Width() : 0;
        target.height = color ? color->Height() : 0;
        if (Surface* depth = device.DepthStencil(); target.depth && depth &&
                                                    (depth->Width() != target.width || depth->Height() != target.height))
            target.depth = 0; // (WebGPU wants the attachments the same size: drawn without depth)
        return target;
    }

    void DefineTextureOnce(IDirect3DBaseTexture9* texture)
    {
        auto [it, added] = m_textures.try_emplace(texture);
        if (!added)
            return;
        DefineTexture define = {};
        define.id = Id(texture);
        if (texture->GetType() == D3DRTYPE_CUBETEXTURE) {
            auto* cube = static_cast<CubeTexture*>(texture);
            define.kind = TextureKind::Cube;
            define.format = FormatOf(cube->Format());
            define.levels = static_cast<uint16_t>(cube->LevelCount());
            define.width = define.height = cube->Face(0, 0)->Width();
        } else {
            auto* plain = static_cast<D3D9::Texture*>(texture);
            define.format = FormatOf(plain->Format());
            define.kind = plain->Usage() & D3DUSAGE_DEPTHSTENCIL ? TextureKind::Depth
                          : plain->Usage() & D3DUSAGE_RENDERTARGET ? TextureKind::Target
                                                                   : TextureKind::Plain;
            define.levels = static_cast<uint16_t>(define.kind == TextureKind::Plain ? plain->LevelCount() : 1);
            define.width = plain->Level(0)->Width();
            define.height = plain->Level(0)->Height();
        }
        m_frame->Add(Op::DefineTexture, define);
    }

    // A texture's pixels sent when they changed (all its levels: textures change seldom, mostly while loading).
    void UploadTextureIfChanged(IDirect3DBaseTexture9* texture)
    {
        DefineTextureOnce(texture);
        Known& known = m_textures[texture];
        auto upload = [&](Surface* surface, uint16_t face, uint16_t level) {
            UploadTexture up = {};
            up.id = Id(texture);
            up.face = face;
            up.level = level;
            up.pitch = surface->Pitch();
            up.size = static_cast<uint32_t>(up.pitch * RowCount(surface->Format(), surface->Height()));
            up.data = m_frame->Put(surface->Bits(), up.size);
            m_frame->Add(Op::UploadTexture, up);
        };
        if (texture->GetType() == D3DRTYPE_CUBETEXTURE) {
            auto* cube = static_cast<CubeTexture*>(texture);
            if (known.version == cube->Version())
                return;
            known.version = cube->Version();
            for (uint16_t face = 0; face < 6; ++face)
                for (uint16_t level = 0; level < cube->LevelCount(); ++level)
                    upload(cube->Face(face, level), face, level);
        } else {
            auto* plain = static_cast<D3D9::Texture*>(texture);
            if (plain->Usage() & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL) || known.version == plain->Version())
                return;
            known.version = plain->Version();
            for (uint16_t level = 0; level < plain->LevelCount(); ++level)
                upload(plain->Level(level), 0, level);
        }
    }

    void Textures(Device& device, const PipelineKey& key, Gpu::Draw& draw)
    {
        for (DWORD stage = 0; stage < 8; ++stage) {
            if (!key.shader.stages[stage].texture)
                continue;
            auto* texture = device.BoundTexture(stage);
            UploadTextureIfChanged(texture);
            draw.textures[stage] = Id(texture);
            Sampler& s = draw.samplers[stage];
            s.addressU = static_cast<uint8_t>(device.SamplerState(stage, D3DSAMP_ADDRESSU));
            s.addressV = static_cast<uint8_t>(device.SamplerState(stage, D3DSAMP_ADDRESSV));
            s.addressW = static_cast<uint8_t>(device.SamplerState(stage, D3DSAMP_ADDRESSW));
            s.minFilter = static_cast<uint8_t>(device.SamplerState(stage, D3DSAMP_MINFILTER));
            s.magFilter = static_cast<uint8_t>(device.SamplerState(stage, D3DSAMP_MAGFILTER));
            s.mipFilter = static_cast<uint8_t>(device.SamplerState(stage, D3DSAMP_MIPFILTER));
            s.maxAnisotropy = static_cast<uint8_t>(std::min<DWORD>(device.SamplerState(stage, D3DSAMP_MAXANISOTROPY), 16));
        }
    }

    // --- Vertices and indices ---

    bool Vertices(Device& device, const DrawCall& call, PipelineKey& key, Gpu::Draw& draw)
    {
        // The vertices this draw reads: [low, high).
        uint32_t low = call.indexed ? static_cast<uint32_t>(call.baseVertex + static_cast<int32_t>(call.firstVertex))
                                    : call.firstVertex;
        uint32_t high = low + call.vertexCount;
        draw.first = call.indexed ? call.firstIndex : call.firstVertex;
        draw.baseVertex = call.baseVertex;

        if (call.userVertices) { // DrawPrimitiveUP: the vertices go with the draw
            uint32_t from = call.indexed ? call.firstVertex : 0;
            draw.vertexOffsets[0] =
                m_frame->Put(static_cast<const uint8_t*>(call.userVertices) + from * call.userStride,
                             static_cast<size_t>(call.vertexCount) * call.userStride);
            key.strides[0] = static_cast<uint16_t>(call.userStride);
            draw.baseVertex = -static_cast<int32_t>(from);
            if (!call.indexed)
                draw.first = 0;
            return true;
        }

        bool streams[4] = {};
        for (uint8_t i = 0; i < key.shader.elementCount; ++i)
            if (key.shader.elements[i].stream < 4)
                streams[key.shader.elements[i].stream] = true;

        // Any stream that can't be drawn from its GPU copy (changed since this frame drew from it): all go with the
        // draw, so the vertex numbers stay the same across streams.
        bool inline_ = false;
        for (uint32_t s = 0; s < 4; ++s) {
            if (!streams[s])
                continue;
            const Device::Stream& stream = device.StreamSource(s);
            if (!stream.buffer)
                return false;
            key.strides[s] = static_cast<uint16_t>(stream.stride);
            Known& known = Buffer(stream.buffer, BufferKind::Vertex, stream.buffer->Length());
            if (known.version != stream.buffer->Version() && known.usedFrame == m_frameNumber)
                inline_ = true;
        }
        for (uint32_t s = 0; s < 4; ++s) {
            if (!streams[s])
                continue;
            const Device::Stream& stream = device.StreamSource(s);
            if (inline_) {
                uint64_t begin = stream.offset + static_cast<uint64_t>(low) * stream.stride;
                uint64_t size = static_cast<uint64_t>(high - low) * stream.stride;
                if (begin + size > stream.buffer->Length())
                    return false;
                draw.vertexOffsets[s] = m_frame->Put(stream.buffer->Data() + begin, size);
                draw.vertexBuffers[s] = 0;
            } else {
                Bring(stream.buffer, m_buffers[stream.buffer]);
                draw.vertexBuffers[s] = Id(stream.buffer);
                draw.vertexOffsets[s] = stream.offset;
            }
        }
        if (inline_) { // (vertex `low` is now the first)
            if (call.indexed)
                draw.baseVertex = call.baseVertex - static_cast<int32_t>(low);
            else
                draw.first = 0;
        }
        return true;
    }

    bool Indices(Device& device, const DrawCall& call, PipelineKey& key, Gpu::Draw& draw, uint32_t count)
    {
        bool fan = call.type == D3DPT_TRIANGLEFAN;
        if (!call.indexed && !fan) {
            draw.indexOffset = ~0u;
            return true;
        }
        if (!call.indexed) { // a fan of vertices: indices made for it
            std::vector<uint32_t> list;
            list.reserve(count);
            for (uint32_t i = 1; i + 1 < call.primitiveCount + 2; ++i)
                list.insert(list.end(), {draw.first, draw.first + i, draw.first + i + 1});
            draw.indexBuffer = 0;
            draw.indexOffset = m_frame->Put(list.data(), list.size() * 4);
            draw.first = 0;
            key.indexFormat = 32;
            return true;
        }
        const uint8_t* source;
        bool wide;
        uint32_t length;
        IndexBuffer* buffer = nullptr;
        if (call.userIndices) {
            source = static_cast<const uint8_t*>(call.userIndices);
            wide = call.userIndexFormat == D3DFMT_INDEX32;
            length = ~0u;
        } else {
            buffer = device.Indices();
            if (!buffer)
                return false;
            source = buffer->Data();
            wide = buffer->Format() == D3DFMT_INDEX32;
            length = buffer->Length();
        }
        uint32_t size = wide ? 4 : 2;
        uint32_t read = fan ? call.primitiveCount + 2 : count;
        if (length != ~0u && (call.firstIndex + read) * size > length)
            return false;
        key.indexFormat = static_cast<uint8_t>(wide ? 32 : 16);
        if (fan || call.userIndices) {
            const uint8_t* from = source + call.firstIndex * size;
            auto index = [&](uint32_t i) {
                if (wide) {
                    uint32_t v;
                    std::memcpy(&v, from + i * 4, 4);
                    return v;
                }
                uint16_t v;
                std::memcpy(&v, from + i * 2, 2);
                return static_cast<uint32_t>(v);
            };
            if (fan) {
                std::vector<uint32_t> list;
                list.reserve(count);
                for (uint32_t i = 1; i + 1 < read; ++i)
                    list.insert(list.end(), {index(0), index(i), index(i + 1)});
                draw.indexOffset = m_frame->Put(list.data(), list.size() * 4);
                key.indexFormat = 32;
            } else {
                draw.indexOffset = m_frame->Put(from, static_cast<size_t>(read) * size);
            }
            draw.indexBuffer = 0;
            draw.first = 0;
            return true;
        }
        Known& known = Buffer(buffer, wide ? BufferKind::Index32 : BufferKind::Index16, buffer->Length());
        if (known.version != buffer->Version() && known.usedFrame == m_frameNumber) {
            draw.indexBuffer = 0;
            draw.indexOffset = m_frame->Put(source + call.firstIndex * size, static_cast<size_t>(read) * size);
            draw.first = 0;
        } else {
            Bring(buffer, known);
            draw.indexBuffer = Id(buffer);
            draw.indexOffset = 0;
        }
        return true;
    }

    // A buffer's record (defined on first sight).
    template <typename B>
    Known& Buffer(B* buffer, BufferKind kind, uint32_t length)
    {
        auto [it, added] = m_buffers.try_emplace(buffer);
        if (added)
            m_frame->Add(Op::DefineBuffer, DefineBuffer{Id(buffer), kind, (length + 3) & ~3u});
        return it->second;
    }

    // Its GPU copy brought up to date (before any draw of this frame reads it: it wasn't read yet this frame).
    template <typename B>
    void Bring(B* buffer, Known& known)
    {
        if (known.version != buffer->Version()) {
            DirtyRange dirty = buffer->Dirty().Take();
            // (WebGPU writes whole 4-byte words: the range widened to them)
            uint32_t begin = 0, end = (buffer->Length() + 3) & ~3u;
            if (known.version != ~0u && !dirty.Empty()) { // (the first upload is the whole buffer)
                begin = dirty.begin & ~3u;
                end = std::min((dirty.end + 3) & ~3u, end);
            }
            UploadBuffer up = {};
            up.id = Id(buffer);
            up.offset = begin;
            up.size = end - begin;
            // (past the end of a length that isn't a multiple of 4: zeros)
            up.data = m_frame->Put(nullptr, up.size);
            uint32_t copy = std::min(end, buffer->Length()) - begin;
            std::memcpy(m_frame->data.data() + up.data, buffer->Data() + begin, copy);
            std::memset(m_frame->data.data() + up.data + copy, 0, up.size - copy);
            m_frame->Add(Op::UploadBuffer, up);
            known.version = buffer->Version();
        }
        known.usedFrame = m_frameNumber;
    }

    // --- Pipelines ---

    void FillKey(Device& device, const DrawCall& call, PipelineKey& key)
    {
        ShaderKey& s = key.shader;
        if (VertexDeclaration* declaration = device.Declaration()) {
            for (const D3DVERTEXELEMENT9& e : declaration->Elements()) {
                if (e.Stream == 0xFF || s.elementCount == 16)
                    break;
                s.elements[s.elementCount++] = {static_cast<uint8_t>(e.Stream), static_cast<uint8_t>(e.Type),
                                                static_cast<uint8_t>(e.Usage), static_cast<uint8_t>(e.UsageIndex),
                                                e.Offset};
            }
        } else {
            s.elementCount = ElementsOfFvf(device.Fvf(), s.elements);
        }
        for (uint8_t i = 0; i < s.elementCount; ++i)
            if (s.elements[i].usage == D3DDECLUSAGE_POSITIONT)
                s.pretransformed = 1;

        if (PixelShader* shader = device.CurrentPixelShader())
            s.pixelShader = ShaderId(shader);
        auto rs = [&](D3DRENDERSTATETYPE state) { return device.RenderState(state); };
        if (!s.pretransformed && rs(D3DRS_LIGHTING)) {
            s.lighting = 1;
            s.normalizeNormals = rs(D3DRS_NORMALIZENORMALS) ? 1 : 0;
            s.localViewer = rs(D3DRS_LOCALVIEWER) ? 1 : 0;
            s.colorVertex = rs(D3DRS_COLORVERTEX) ? 1 : 0;
            s.diffuseSource = static_cast<uint8_t>(rs(D3DRS_DIFFUSEMATERIALSOURCE));
            s.ambientSource = static_cast<uint8_t>(rs(D3DRS_AMBIENTMATERIALSOURCE));
            s.specularSource = static_cast<uint8_t>(rs(D3DRS_SPECULARMATERIALSOURCE));
            s.emissiveSource = static_cast<uint8_t>(rs(D3DRS_EMISSIVEMATERIALSOURCE));
            for (const auto& [index, on] : device.EnabledLights()) {
                if (!on || s.lightCount == 8)
                    continue;
                auto it = device.Lights().find(index);
                if (it != device.Lights().end())
                    s.lightTypes[s.lightCount++] = static_cast<uint8_t>(it->second.Type);
            }
        }
        s.specular = rs(D3DRS_SPECULARENABLE) && !s.pixelShader ? 1 : 0; // (lit specular, and its add after the stages)
        if (rs(D3DRS_FOGENABLE)) {
            if (rs(D3DRS_FOGTABLEMODE) != D3DFOG_NONE)
                s.fogMode = static_cast<uint8_t>(rs(D3DRS_FOGTABLEMODE));
            else if (rs(D3DRS_FOGVERTEXMODE) != D3DFOG_NONE && !s.pretransformed)
                s.fogMode = static_cast<uint8_t>(4 + rs(D3DRS_FOGVERTEXMODE));
            else
                s.fogMode = 8; // the specular colour's alpha
            s.rangeFog = rs(D3DRS_RANGEFOGENABLE) ? 1 : 0;
        }
        if (!s.pretransformed && rs(D3DRS_VERTEXBLEND) != D3DVBF_DISABLE && rs(D3DRS_VERTEXBLEND) <= D3DVBF_3WEIGHTS)
            s.vertexBlend = static_cast<uint8_t>(rs(D3DRS_VERTEXBLEND));
        if (rs(D3DRS_ALPHATESTENABLE) && rs(D3DRS_ALPHAFUNC) != D3DCMP_ALWAYS)
            s.alphaFunc = static_cast<uint8_t>(rs(D3DRS_ALPHAFUNC));

        for (DWORD stage = 0; stage < 8; ++stage) {
            auto ts = [&](D3DTEXTURESTAGESTATETYPE type) { return device.TextureStageState(stage, type); };
            Stage& st = s.stages[stage];
            IDirect3DBaseTexture9* texture = device.BoundTexture(stage);
            if (s.pixelShader) {
                // (ps.1.x samples t<n> with stage n's coordinates and texture)
                if (!texture)
                    continue;
            } else {
                if (ts(D3DTSS_COLOROP) == D3DTOP_DISABLE)
                    break;
                st.colorOp = static_cast<uint8_t>(ts(D3DTSS_COLOROP));
                st.colorArg0 = static_cast<uint8_t>(ts(D3DTSS_COLORARG0));
                st.colorArg1 = static_cast<uint8_t>(ts(D3DTSS_COLORARG1));
                st.colorArg2 = static_cast<uint8_t>(ts(D3DTSS_COLORARG2));
                st.alphaOp = static_cast<uint8_t>(ts(D3DTSS_ALPHAOP));
                st.alphaArg0 = static_cast<uint8_t>(ts(D3DTSS_ALPHAARG0));
                st.alphaArg1 = static_cast<uint8_t>(ts(D3DTSS_ALPHAARG1));
                st.alphaArg2 = static_cast<uint8_t>(ts(D3DTSS_ALPHAARG2));
                st.resultArg = static_cast<uint8_t>(ts(D3DTSS_RESULTARG));
                s.stageCount = static_cast<uint8_t>(stage + 1);
            }
            DWORD index = ts(D3DTSS_TEXCOORDINDEX);
            st.texcoordIndex = static_cast<uint8_t>(index & 0xFFFF);
            st.texgen = s.pretransformed ? 0 : static_cast<uint8_t>(index >> 16);
            st.transform = s.pretransformed ? 0 : static_cast<uint8_t>(ts(D3DTSS_TEXTURETRANSFORMFLAGS));
            if (texture)
                st.texture = texture->GetType() == D3DRTYPE_CUBETEXTURE ? 2 : 1;
        }

        key.topology = static_cast<uint8_t>(call.type);
        key.cull = static_cast<uint8_t>(rs(D3DRS_CULLMODE));
        key.fill = static_cast<uint8_t>(rs(D3DRS_FILLMODE));
        key.zEnable = rs(D3DRS_ZENABLE) ? 1 : 0;
        key.zWrite = rs(D3DRS_ZWRITEENABLE) ? 1 : 0;
        key.zFunc = static_cast<uint8_t>(rs(D3DRS_ZFUNC));
        key.stencilEnable = rs(D3DRS_STENCILENABLE) ? 1 : 0;
        if (key.stencilEnable) {
            key.stencilFunc = static_cast<uint8_t>(rs(D3DRS_STENCILFUNC));
            key.stencilFail = static_cast<uint8_t>(rs(D3DRS_STENCILFAIL));
            key.stencilZFail = static_cast<uint8_t>(rs(D3DRS_STENCILZFAIL));
            key.stencilPass = static_cast<uint8_t>(rs(D3DRS_STENCILPASS));
            key.stencilReadMask = static_cast<uint8_t>(rs(D3DRS_STENCILMASK));
            key.stencilWriteMask = static_cast<uint8_t>(rs(D3DRS_STENCILWRITEMASK));
        }
        key.blendEnable = rs(D3DRS_ALPHABLENDENABLE) ? 1 : 0;
        if (key.blendEnable) {
            key.srcBlend = static_cast<uint8_t>(rs(D3DRS_SRCBLEND));
            key.dstBlend = static_cast<uint8_t>(rs(D3DRS_DESTBLEND));
            key.blendOp = static_cast<uint8_t>(rs(D3DRS_BLENDOP));
            if (rs(D3DRS_SEPARATEALPHABLENDENABLE)) {
                key.separateAlpha = 1;
                key.srcBlendAlpha = static_cast<uint8_t>(rs(D3DRS_SRCBLENDALPHA));
                key.dstBlendAlpha = static_cast<uint8_t>(rs(D3DRS_DESTBLENDALPHA));
                key.blendOpAlpha = static_cast<uint8_t>(rs(D3DRS_BLENDOPALPHA));
            }
        }
        key.colorWrite = static_cast<uint8_t>(rs(D3DRS_COLORWRITEENABLE) & 0xF);
        Surface* color = device.RenderTarget();
        key.colorFormat = static_cast<uint8_t>(color && color != device.BackBuffer() ? FormatOf(color->Format())
                                                                                     : Format::BGRA8);
        Target target = TargetOf(device);
        if (target.depth)
            key.depthFormat = static_cast<uint8_t>(FormatOf(device.DepthStencil()->Format()));
        else
            key.zEnable = key.zWrite = key.stencilEnable = 0;
        float bias = AsFloat(rs(D3DRS_DEPTHBIAS));
        key.depthBias = static_cast<int32_t>(bias * (key.depthFormat == static_cast<uint8_t>(Format::Depth16)
                                                         ? 65535.0f
                                                         : 16777215.0f));
        key.slopeBias = AsFloat(rs(D3DRS_SLOPESCALEDEPTHBIAS));
    }

    uint32_t PipelineId(const PipelineKey& key)
    {
        auto [it, added] = m_pipelines.try_emplace(key, static_cast<uint32_t>(m_pipelines.size() + 1));
        if (added)
            m_frame->Add(Op::DefinePipeline, DefinePipeline{it->second, key});
        return it->second;
    }

    uint32_t ShaderId(PixelShader* shader)
    {
        const auto& tokens = shader->Tokens();
        if (tokens.size() < 4 || tokens[2] != 0x54584554) // (only ours: the text after 'TEXT')
            return 0;
        const char* text = reinterpret_cast<const char*>(&tokens[3]);
        size_t length = strnlen(text, (tokens.size() - 3) * 4);
        uint32_t id = Fnv(text, length);
        if (m_shaders.emplace(id, true).second)
            m_frame->Add(Op::DefineShader, DefineShader{id}, text, static_cast<uint32_t>(length + 1));
        return id;
    }

    // --- Uniforms ---

    void Uniforms(Device& device, const PipelineKey& key, Gpu::Draw& draw)
    {
        const ShaderKey& s = key.shader;
        DrawUniforms& u = m_drawUniforms;
        std::memset(&u, 0, sizeof(u));
        const float* view = &device.Transform(D3DTS_VIEW).m[0][0];
        const float* projection = &device.Transform(D3DTS_PROJECTION).m[0][0];
        int matrices = s.vertexBlend ? s.vertexBlend + 1 : 1;
        bool normals = s.lighting;
        for (int i = 0; i < 8; ++i)
            normals = normals || (s.stages[i].texgen == 1 || s.stages[i].texgen == 3);
        for (int i = 0; i < matrices; ++i) {
            Multiply(&device.Transform(static_cast<D3DTRANSFORMSTATETYPE>(D3DTS_WORLDMATRIX(i))).m[0][0], view,
                     u.worldView[i]);
            if (normals)
                NormalMatrix(u.worldView[i], u.normal[i]);
        }
        std::memcpy(u.projection, projection, sizeof(u.projection));
        for (int i = 0; i < 8; ++i)
            if (s.stages[i].transform)
                std::memcpy(u.texture[i],
                            &device.Transform(static_cast<D3DTRANSFORMSTATETYPE>(D3DTS_TEXTURE0 + i)).m[0][0], 64);
        float width = static_cast<float>(draw.target.width ? draw.target.width : 1);
        float height = static_cast<float>(draw.target.height ? draw.target.height : 1);
        // (Direct3D 9's pixel centres are at whole numbers, WebGPU's at halves: everything moves by half a pixel)
        u.screen[0] = 2.0f / width;
        u.screen[1] = -2.0f / height;
        u.screen[2] = 0.5f;
        u.screen[3] = 0.5f;
        const D3DVIEWPORT9& vp = device.Viewport();
        u.pixelOffset[0] = 1.0f / static_cast<float>(vp.Width ? vp.Width : 1);
        u.pixelOffset[1] = -1.0f / static_cast<float>(vp.Height ? vp.Height : 1);
        auto rs = [&](D3DRENDERSTATETYPE state) { return device.RenderState(state); };
        if (s.fogMode) {
            u.fog[0] = AsFloat(rs(D3DRS_FOGSTART));
            u.fog[1] = AsFloat(rs(D3DRS_FOGEND));
            u.fog[2] = AsFloat(rs(D3DRS_FOGDENSITY));
            u.fog[3] = u.fog[1] != u.fog[0] ? 1.0f / (u.fog[1] - u.fog[0]) : 0.0f;
            ColorOf(rs(D3DRS_FOGCOLOR), u.fogColor);
        }
        ColorOf(rs(D3DRS_TEXTUREFACTOR), u.textureFactor);
        u.alphaRef[0] = static_cast<float>(rs(D3DRS_ALPHAREF) & 0xFF) / 255.0f;
        if (s.pixelShader)
            std::memcpy(u.constants, device.PixelConstants(), sizeof(u.constants));
        draw.drawUniforms = PutUniforms(u, m_lastDraw, m_lastDrawOffset);

        draw.lightingUniforms = ~0u;
        if (!s.lighting)
            return;
        LightingUniforms& l = m_lightingUniforms;
        std::memset(&l, 0, sizeof(l));
        const D3DMATERIAL9& material = device.Material();
        ColorOf(material.Diffuse, l.diffuse);
        ColorOf(material.Ambient, l.ambient);
        ColorOf(material.Specular, l.specular);
        ColorOf(material.Emissive, l.emissive);
        l.power[0] = material.Power;
        ColorOf(rs(D3DRS_AMBIENT), l.globalAmbient);
        int n = 0;
        for (const auto& [index, on] : device.EnabledLights()) {
            if (!on || n == 8)
                continue;
            auto it = device.Lights().find(index);
            if (it == device.Lights().end())
                continue;
            const D3DLIGHT9& light = it->second;
            Light& out = l.lights[n++];
            ColorOf(light.Diffuse, out.diffuse);
            ColorOf(light.Specular, out.specular);
            ColorOf(light.Ambient, out.ambient);
            float position[3] = {light.Position.x, light.Position.y, light.Position.z};
            float direction[3] = {light.Direction.x, light.Direction.y, light.Direction.z};
            TransformPoint(view, position, out.position);
            TransformVector(view, direction, out.direction);
            float length = std::sqrt(out.direction[0] * out.direction[0] + out.direction[1] * out.direction[1] +
                                     out.direction[2] * out.direction[2]);
            if (length > 0)
                for (int k = 0; k < 3; ++k)
                    out.direction[k] /= length;
            out.position[3] = light.Range;
            out.direction[3] = light.Falloff;
            out.attenuation[0] = light.Attenuation0;
            out.attenuation[1] = light.Attenuation1;
            out.attenuation[2] = light.Attenuation2;
            float cosTheta = std::cos(light.Theta / 2), cosPhi = std::cos(light.Phi / 2);
            out.spot[0] = cosTheta;
            out.spot[1] = cosTheta - cosPhi > 1e-6f ? 1.0f / (cosTheta - cosPhi) : 1e6f;
            out.spot[2] = cosPhi;
        }
        draw.lightingUniforms = PutUniforms(l, m_lastLighting, m_lastLightingOffset);
    }

    // The uniforms in the frame's data (256-byte aligned, as WebGPU's dynamic offsets are) - or the last ones'
    // offset again when they're the same.
    template <typename U>
    uint32_t PutUniforms(const U& uniforms, U& last, uint32_t& lastOffset)
    {
        if (lastOffset != ~0u && std::memcmp(&uniforms, &last, sizeof(U)) == 0)
            return lastOffset;
        last = uniforms;
        lastOffset = m_frame->Put(&uniforms, sizeof(U), 256);
        return lastOffset;
    }

    std::mutex m_lock;
    std::unique_ptr<Frame> m_frame;
    uint64_t m_frameNumber = 0;
    uint32_t m_draws = 0;
    std::chrono::steady_clock::time_point m_last;
    std::unordered_map<const void*, Known> m_textures, m_buffers;
    std::unordered_map<PipelineKey, uint32_t, PipelineHash, PipelineEqual> m_pipelines;
    std::unordered_map<uint32_t, bool> m_shaders;
    DrawUniforms m_drawUniforms, m_lastDraw;
    LightingUniforms m_lightingUniforms, m_lastLighting;
    uint32_t m_lastDrawOffset = ~0u, m_lastLightingOffset = ~0u;
};

} // namespace

void InstallRecorder()
{
    static Recorder recorder;
    Device::SetBackend(&recorder);
}

// (installed before the game makes its device: start-up)
__attribute__((constructor)) void InstallRecorderAtStart() { InstallRecorder(); }

} // namespace Gpu
