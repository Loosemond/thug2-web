// The page's side of the frames the game records (gpu_frame.h): drawn with WebGPU on the page's thread, one each
// animation frame. Resources are made and filled as the commands say (textures converted from Direct3D 9's formats
// here), pipelines generated from their keys (gpu_shaders.cpp) when first drawn with, and each frame's data - the
// vertices and indices that go with draws, and every draw's uniforms - is one buffer the draws index into. The game
// draws to its own back buffer, copied to the canvas at Present.

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <webgpu/webgpu_cpp.h>

#include <d3d9.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "web/game_files.h"
#include "web/gpu_frame.h"
#include "web/gpu_shaders.h"

namespace Gpu {

namespace {

// --- Texture formats ---------------------------------------------------------------------------------------------------

bool IsDxt(Format f) { return f == Format::DXT1 || f == Format::DXT3 || f == Format::DXT5; }

void Rgb565(uint16_t c, uint8_t* out)
{
    out[0] = static_cast<uint8_t>(((c >> 11) & 31) * 255 / 31);
    out[1] = static_cast<uint8_t>(((c >> 5) & 63) * 255 / 63);
    out[2] = static_cast<uint8_t>((c & 31) * 255 / 31);
}

// One 4x4 block of DXT1/3/5 to RGBA8 (`out`: 16 pixels).
void DecodeBlock(Format format, const uint8_t* block, uint8_t* out)
{
    const uint8_t* color = format == Format::DXT1 ? block : block + 8;
    uint16_t c0 = static_cast<uint16_t>(color[0] | color[1] << 8), c1 = static_cast<uint16_t>(color[2] | color[3] << 8);
    uint8_t palette[4][4];
    Rgb565(c0, palette[0]);
    Rgb565(c1, palette[1]);
    palette[0][3] = palette[1][3] = 255;
    if (format != Format::DXT1 || c0 > c1) {
        for (int k = 0; k < 3; ++k) {
            palette[2][k] = static_cast<uint8_t>((2 * palette[0][k] + palette[1][k]) / 3);
            palette[3][k] = static_cast<uint8_t>((palette[0][k] + 2 * palette[1][k]) / 3);
        }
        palette[2][3] = palette[3][3] = 255;
    } else {
        for (int k = 0; k < 3; ++k) {
            palette[2][k] = static_cast<uint8_t>((palette[0][k] + palette[1][k]) / 2);
            palette[3][k] = 0;
        }
        palette[2][3] = 255;
        palette[3][3] = 0;
    }
    uint32_t bits = color[4] | color[5] << 8 | color[6] << 16 | static_cast<uint32_t>(color[7]) << 24;
    for (int i = 0; i < 16; ++i)
        std::memcpy(out + i * 4, palette[(bits >> (i * 2)) & 3], 4);
    if (format == Format::DXT3) {
        for (int i = 0; i < 16; ++i) {
            uint8_t nibble = static_cast<uint8_t>((block[i / 2] >> ((i & 1) * 4)) & 15);
            out[i * 4 + 3] = static_cast<uint8_t>(nibble * 17);
        }
    } else if (format == Format::DXT5) {
        uint8_t a[8] = {block[0], block[1]};
        if (a[0] > a[1])
            for (int k = 1; k < 7; ++k)
                a[k + 1] = static_cast<uint8_t>(((7 - k) * a[0] + k * a[1]) / 7);
        else {
            for (int k = 1; k < 5; ++k)
                a[k + 1] = static_cast<uint8_t>(((5 - k) * a[0] + k * a[1]) / 5);
            a[6] = 0;
            a[7] = 255;
        }
        uint64_t abits = 0;
        for (int k = 0; k < 6; ++k)
            abits |= static_cast<uint64_t>(block[2 + k]) << (8 * k);
        for (int i = 0; i < 16; ++i)
            out[i * 4 + 3] = a[(abits >> (3 * i)) & 7];
    }
}

// A level's pixels as RGBA8 rows (width * 4 bytes each), from Direct3D 9's layout.
void ToRgba8(Format format, uint32_t width, uint32_t height, uint32_t pitch, const uint8_t* in,
             std::vector<uint8_t>& out)
{
    out.assign(static_cast<size_t>(width) * height * 4, 0);
    if (IsDxt(format)) {
        uint32_t blockBytes = format == Format::DXT1 ? 8 : 16;
        uint8_t pixels[64];
        for (uint32_t by = 0; by < (height + 3) / 4; ++by)
            for (uint32_t bx = 0; bx < (width + 3) / 4; ++bx) {
                DecodeBlock(format, in + by * pitch + bx * blockBytes, pixels);
                for (uint32_t y = 0; y < 4 && by * 4 + y < height; ++y)
                    for (uint32_t x = 0; x < 4 && bx * 4 + x < width; ++x)
                        std::memcpy(&out[((by * 4 + y) * width + bx * 4 + x) * 4], pixels + (y * 4 + x) * 4, 4);
            }
        return;
    }
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* row = in + static_cast<size_t>(y) * pitch;
        uint8_t* o = &out[static_cast<size_t>(y) * width * 4];
        for (uint32_t x = 0; x < width; ++x, o += 4) {
            switch (format) {
            case Format::BGRA8:
            case Format::BGRX8:
                o[0] = row[x * 4 + 2];
                o[1] = row[x * 4 + 1];
                o[2] = row[x * 4];
                o[3] = format == Format::BGRX8 ? 255 : row[x * 4 + 3];
                break;
            case Format::R5G6B5: {
                uint16_t c = static_cast<uint16_t>(row[x * 2] | row[x * 2 + 1] << 8);
                Rgb565(c, o);
                o[3] = 255;
                break;
            }
            case Format::A1R5G5B5:
            case Format::X1R5G5B5: {
                uint16_t c = static_cast<uint16_t>(row[x * 2] | row[x * 2 + 1] << 8);
                o[0] = static_cast<uint8_t>(((c >> 10) & 31) * 255 / 31);
                o[1] = static_cast<uint8_t>(((c >> 5) & 31) * 255 / 31);
                o[2] = static_cast<uint8_t>((c & 31) * 255 / 31);
                o[3] = format == Format::X1R5G5B5 || (c & 0x8000) ? 255 : 0;
                break;
            }
            case Format::A4R4G4B4: {
                uint16_t c = static_cast<uint16_t>(row[x * 2] | row[x * 2 + 1] << 8);
                o[0] = static_cast<uint8_t>(((c >> 8) & 15) * 17);
                o[1] = static_cast<uint8_t>(((c >> 4) & 15) * 17);
                o[2] = static_cast<uint8_t>((c & 15) * 17);
                o[3] = static_cast<uint8_t>(((c >> 12) & 15) * 17);
                break;
            }
            case Format::A8: o[3] = row[x]; break;
            case Format::L8: o[0] = o[1] = o[2] = row[x], o[3] = 255; break;
            case Format::A8L8: o[0] = o[1] = o[2] = row[x * 2], o[3] = row[x * 2 + 1]; break;
            default: o[0] = 255, o[1] = 0, o[2] = 255, o[3] = 255; break; // (unknown: magenta)
            }
        }
    }
}

// --- State mappings ----------------------------------------------------------------------------------------------------

wgpu::CompareFunction Compare(uint8_t f)
{
    static const wgpu::CompareFunction k[] = {
        wgpu::CompareFunction::Always,       wgpu::CompareFunction::Never,   wgpu::CompareFunction::Less,
        wgpu::CompareFunction::Equal,        wgpu::CompareFunction::LessEqual, wgpu::CompareFunction::Greater,
        wgpu::CompareFunction::NotEqual,     wgpu::CompareFunction::GreaterEqual, wgpu::CompareFunction::Always};
    return f < 9 ? k[f] : wgpu::CompareFunction::Always;
}

wgpu::StencilOperation StencilOp(uint8_t op)
{
    switch (op) {
    case D3DSTENCILOP_ZERO: return wgpu::StencilOperation::Zero;
    case D3DSTENCILOP_REPLACE: return wgpu::StencilOperation::Replace;
    case D3DSTENCILOP_INCRSAT: return wgpu::StencilOperation::IncrementClamp;
    case D3DSTENCILOP_DECRSAT: return wgpu::StencilOperation::DecrementClamp;
    case D3DSTENCILOP_INVERT: return wgpu::StencilOperation::Invert;
    case D3DSTENCILOP_INCR: return wgpu::StencilOperation::IncrementWrap;
    case D3DSTENCILOP_DECR: return wgpu::StencilOperation::DecrementWrap;
    default: return wgpu::StencilOperation::Keep;
    }
}

wgpu::BlendFactor Factor(uint8_t f, bool source)
{
    switch (f) {
    case D3DBLEND_ZERO: return wgpu::BlendFactor::Zero;
    case D3DBLEND_ONE: return wgpu::BlendFactor::One;
    case D3DBLEND_SRCCOLOR: return wgpu::BlendFactor::Src;
    case D3DBLEND_INVSRCCOLOR: return wgpu::BlendFactor::OneMinusSrc;
    case D3DBLEND_SRCALPHA: return wgpu::BlendFactor::SrcAlpha;
    case D3DBLEND_INVSRCALPHA: return wgpu::BlendFactor::OneMinusSrcAlpha;
    case D3DBLEND_DESTALPHA: return wgpu::BlendFactor::DstAlpha;
    case D3DBLEND_INVDESTALPHA: return wgpu::BlendFactor::OneMinusDstAlpha;
    case D3DBLEND_DESTCOLOR: return wgpu::BlendFactor::Dst;
    case D3DBLEND_INVDESTCOLOR: return wgpu::BlendFactor::OneMinusDst;
    case D3DBLEND_SRCALPHASAT: return wgpu::BlendFactor::SrcAlphaSaturated;
    case D3DBLEND_BOTHSRCALPHA: return source ? wgpu::BlendFactor::SrcAlpha : wgpu::BlendFactor::OneMinusSrcAlpha;
    case D3DBLEND_BOTHINVSRCALPHA: return source ? wgpu::BlendFactor::OneMinusSrcAlpha : wgpu::BlendFactor::SrcAlpha;
    case D3DBLEND_BLENDFACTOR: return wgpu::BlendFactor::Constant;
    case D3DBLEND_INVBLENDFACTOR: return wgpu::BlendFactor::OneMinusConstant;
    default: return source ? wgpu::BlendFactor::One : wgpu::BlendFactor::Zero;
    }
}

wgpu::BlendOperation BlendOp(uint8_t op)
{
    switch (op) {
    case D3DBLENDOP_SUBTRACT: return wgpu::BlendOperation::Subtract;
    case D3DBLENDOP_REVSUBTRACT: return wgpu::BlendOperation::ReverseSubtract;
    case D3DBLENDOP_MIN: return wgpu::BlendOperation::Min;
    case D3DBLENDOP_MAX: return wgpu::BlendOperation::Max;
    default: return wgpu::BlendOperation::Add;
    }
}

wgpu::BlendComponent Blend(uint8_t op, uint8_t src, uint8_t dst)
{
    wgpu::BlendComponent c;
    c.operation = BlendOp(op);
    bool minMax = c.operation == wgpu::BlendOperation::Min || c.operation == wgpu::BlendOperation::Max;
    c.srcFactor = minMax ? wgpu::BlendFactor::One : Factor(src, true);
    c.dstFactor = minMax ? wgpu::BlendFactor::One : Factor(dst, false);
    if (src == D3DBLEND_BOTHSRCALPHA || src == D3DBLEND_BOTHINVSRCALPHA) {
        c.srcFactor = Factor(src, true);
        c.dstFactor = Factor(src, false);
    }
    return c;
}

wgpu::VertexFormat VertexFormat(uint8_t type)
{
    switch (type) {
    case D3DDECLTYPE_FLOAT1: return wgpu::VertexFormat::Float32;
    case D3DDECLTYPE_FLOAT2: return wgpu::VertexFormat::Float32x2;
    case D3DDECLTYPE_FLOAT3: return wgpu::VertexFormat::Float32x3;
    case D3DDECLTYPE_FLOAT4: return wgpu::VertexFormat::Float32x4;
    case D3DDECLTYPE_D3DCOLOR:
    case D3DDECLTYPE_UBYTE4N: return wgpu::VertexFormat::Unorm8x4;
    case D3DDECLTYPE_UBYTE4: return wgpu::VertexFormat::Uint8x4;
    case D3DDECLTYPE_SHORT2: return wgpu::VertexFormat::Sint16x2;
    case D3DDECLTYPE_SHORT4: return wgpu::VertexFormat::Sint16x4;
    case D3DDECLTYPE_SHORT2N: return wgpu::VertexFormat::Snorm16x2;
    case D3DDECLTYPE_SHORT4N: return wgpu::VertexFormat::Snorm16x4;
    case D3DDECLTYPE_USHORT2N: return wgpu::VertexFormat::Unorm16x2;
    case D3DDECLTYPE_USHORT4N: return wgpu::VertexFormat::Unorm16x4;
    case D3DDECLTYPE_FLOAT16_2: return wgpu::VertexFormat::Float16x2;
    case D3DDECLTYPE_FLOAT16_4: return wgpu::VertexFormat::Float16x4;
    default: return wgpu::VertexFormat::Float32x4;
    }
}

wgpu::AddressMode Address(uint8_t mode)
{
    switch (mode) {
    case D3DTADDRESS_MIRROR:
    case D3DTADDRESS_MIRRORONCE: return wgpu::AddressMode::MirrorRepeat;
    case D3DTADDRESS_CLAMP:
    case D3DTADDRESS_BORDER: return wgpu::AddressMode::ClampToEdge;
    default: return wgpu::AddressMode::Repeat;
    }
}

wgpu::StringView View(const std::string& s) { return wgpu::StringView{s.data(), s.size()}; }

// The stages whose texture a shader samples (as gpu_shaders.cpp declares them).
bool Samples(const ShaderKey& key, int stage)
{
    if (!key.stages[stage].texture)
        return false;
    return key.pixelShader ? true : stage < key.stageCount;
}

void Log(const char* format, const char* detail = "")
{
    static int count = 0;
    if (++count <= 40)
        std::fprintf(stderr, format, detail);
}

extern "C" double thug2_audio_mix_ms(); // (port/src/dsound/mixer.cpp)

// --- The renderer ------------------------------------------------------------------------------------------------------

class Renderer {
public:
    void Start()
    {
        m_instance = wgpu::CreateInstance(nullptr);
        wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector canvas;
        canvas.selector = "#canvas";
        wgpu::SurfaceDescriptor surface;
        surface.nextInChain = &canvas;
        m_surface = m_instance.CreateSurface(&surface);
        wgpu::RequestAdapterOptions options;
        options.powerPreference = wgpu::PowerPreference::HighPerformance;
        m_instance.RequestAdapter(
            &options, wgpu::CallbackMode::AllowSpontaneous,
            [this](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter, wgpu::StringView message) {
                if (status != wgpu::RequestAdapterStatus::Success) {
                    std::fprintf(stderr, "WebGPU: no adapter (%.*s)\n", static_cast<int>(message.length), message.data);
                    return;
                }
                m_adapter = adapter;
                RequestDevice();
            });
    }

private:
    struct Texture {
        wgpu::Texture texture;
        wgpu::TextureView view;   // for sampling (2D or cube)
        wgpu::TextureView target; // level 0, as an attachment
        TextureKind kind = TextureKind::Plain;
        Format format = Format::None;
        wgpu::TextureFormat gpuFormat = wgpu::TextureFormat::RGBA8Unorm;
        uint32_t width = 1, height = 1, levels = 1;
        bool bc = false;
    };
    struct Buffer {
        wgpu::Buffer buffer;
        uint32_t size = 0;
    };
    struct Pipeline {
        wgpu::RenderPipeline pipeline;
        uint32_t textureLayout = 0; // (the texture bind group layout's key)
        int8_t slotOfStream[4] = {-1, -1, -1, -1};
        bool failed = false;
        bool pending = false; // (still compiling)
    };

    void RequestDevice()
    {
        wgpu::DeviceDescriptor desc;
        std::vector<wgpu::FeatureName> features;
        // (the page's ?nobc turns them off: phones' GPUs have no BC textures, the DXT ones are decoded instead)
        bool bcAllowed = !EM_ASM_INT({ return Module.thug2NoBc ? 1 : 0; });
        if (bcAllowed && m_adapter.HasFeature(wgpu::FeatureName::TextureCompressionBC)) {
            features.push_back(wgpu::FeatureName::TextureCompressionBC);
            m_bc = true;
        }
        desc.requiredFeatureCount = features.size();
        desc.requiredFeatures = features.data();
        desc.SetUncapturedErrorCallback(
            [](const wgpu::Device&, wgpu::ErrorType, wgpu::StringView message) {
                std::string text(message.data, message.length);
                Log("WebGPU error: %s\n", text.c_str());
            });
        desc.SetDeviceLostCallback(wgpu::CallbackMode::AllowSpontaneous,
                                   [](const wgpu::Device&, wgpu::DeviceLostReason, wgpu::StringView message) {
                                       std::fprintf(stderr, "WebGPU: device lost (%.*s)\n",
                                                    static_cast<int>(message.length), message.data);
                                   });
        m_adapter.RequestDevice(
            &desc, wgpu::CallbackMode::AllowSpontaneous,
            [this](wgpu::RequestDeviceStatus status, wgpu::Device device, wgpu::StringView message) {
                if (status != wgpu::RequestDeviceStatus::Success) {
                    std::fprintf(stderr, "WebGPU: no device (%.*s)\n", static_cast<int>(message.length), message.data);
                    return;
                }
                m_device = device;
                m_queue = device.GetQueue();
                Ready();
            });
    }

    void Ready()
    {
        wgpu::SurfaceCapabilities caps;
        m_surface.GetCapabilities(m_adapter, &caps);
        m_surfaceFormat = caps.formatCount ? caps.formats[0] : wgpu::TextureFormat::BGRA8Unorm;
        std::fprintf(stderr, "WebGPU ready (%s, BC textures %s)\n",
                     m_surfaceFormat == wgpu::TextureFormat::BGRA8Unorm ? "bgra8unorm" : "rgba8unorm",
                     m_bc ? "on" : "off");
        MakeFixedObjects();
        emscripten_request_animation_frame_loop(
            [](double time, void* self) -> bool { return static_cast<Renderer*>(self)->Frame(time); }, this);
    }

    void MakeFixedObjects()
    {
        // Group 0: the draw's and the lighting uniforms, both in the frame's buffer at dynamic offsets.
        wgpu::BindGroupLayoutEntry entries[2];
        for (int i = 0; i < 2; ++i) {
            entries[i].binding = static_cast<uint32_t>(i);
            entries[i].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
            entries[i].buffer.type = wgpu::BufferBindingType::Uniform;
            entries[i].buffer.hasDynamicOffset = true;
            entries[i].buffer.minBindingSize = i == 0 ? sizeof(DrawUniforms) : sizeof(LightingUniforms);
        }
        wgpu::BindGroupLayoutDescriptor layout;
        layout.entryCount = 2;
        layout.entries = entries;
        m_uniformLayout = m_device.CreateBindGroupLayout(&layout);

        // Stand-ins for missing textures: 1x1 opaque black, 2D and cube.
        for (int cube = 0; cube < 2; ++cube) {
            wgpu::TextureDescriptor desc;
            desc.size = {1, 1, cube ? 6u : 1u};
            desc.format = wgpu::TextureFormat::RGBA8Unorm;
            desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
            wgpu::Texture texture = m_device.CreateTexture(&desc);
            for (uint32_t layer = 0; layer < desc.size.depthOrArrayLayers; ++layer) {
                const uint8_t black[4] = {0, 0, 0, 255};
                wgpu::TexelCopyTextureInfo where;
                where.texture = texture;
                where.origin = {0, 0, layer};
                wgpu::TexelCopyBufferLayout data;
                data.bytesPerRow = 4;
                wgpu::Extent3D size = {1, 1, 1};
                m_queue.WriteTexture(&where, black, 4, &data, &size);
            }
            wgpu::TextureViewDescriptor view;
            view.dimension = cube ? wgpu::TextureViewDimension::Cube : wgpu::TextureViewDimension::e2D;
            (cube ? m_blackCube : m_black2D) = texture.CreateView(&view);
        }
        Grow(1 << 20);
    }

    // The frame's buffer at least `size` bytes (its bind group made again when it's replaced).
    void Grow(size_t size)
    {
        size = std::max<size_t>(size + sizeof(LightingUniforms) + 256, 1 << 20);
        if (m_frameBuffer && m_frameBufferSize >= size)
            return;
        size_t grown = std::max(size, m_frameBufferSize * 2);
        wgpu::BufferDescriptor desc;
        desc.size = (grown + 3) & ~size_t{3};
        desc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::Index | wgpu::BufferUsage::Uniform |
                     wgpu::BufferUsage::CopyDst;
        m_frameBuffer = m_device.CreateBuffer(&desc);
        m_frameBufferSize = desc.size;
        wgpu::BindGroupEntry entries[2];
        entries[0].binding = 0;
        entries[0].buffer = m_frameBuffer;
        entries[0].size = sizeof(DrawUniforms);
        entries[1].binding = 1;
        entries[1].buffer = m_frameBuffer;
        entries[1].size = sizeof(LightingUniforms);
        wgpu::BindGroupDescriptor group;
        group.layout = m_uniformLayout;
        group.entryCount = 2;
        group.entries = entries;
        m_uniformGroup = m_device.CreateBindGroup(&group);
    }

    // --- Each animation frame ---

    bool Frame(double time)
    {
        std::unique_ptr<Gpu::Frame> frame = Frames().Pop();
        if (!frame)
            return true;
        double start = emscripten_get_now();
        Draw(*frame);
        m_pageMs += emscripten_get_now() - start;
        Stats(time, frame->recordedMs);
        Frames().Recycle(std::move(frame));
        return true;
    }

    void Draw(Gpu::Frame& frame)
    {
        // The frame's data, all at once (the draws index into it).
        frame.data.resize((frame.data.size() + 3) & ~size_t{3});
        Grow(frame.data.size());
        if (!frame.data.empty())
            m_queue.WriteBuffer(m_frameBuffer, 0, frame.data.data(), frame.data.size());
        m_encoder = m_device.CreateCommandEncoder();
        m_blitSlot = 0;

        const uint8_t* at = frame.commands.data();
        const uint8_t* end = at + frame.commands.size();
        while (at < end) {
            Header header;
            std::memcpy(&header, at, sizeof(header));
            const uint8_t* body = at + sizeof(header);
            switch (header.op) {
            case Op::DefineTexture: DefineTexture(Read<Gpu::DefineTexture>(body)); break;
            case Op::UploadTexture: UploadTexture(Read<Gpu::UploadTexture>(body), frame); break;
            case Op::DefineBuffer: DefineBuffer(Read<Gpu::DefineBuffer>(body)); break;
            case Op::UploadBuffer: UploadBuffer(Read<Gpu::UploadBuffer>(body), frame); break;
            case Op::Release: Release(Read<Gpu::Release>(body).id); break;
            case Op::DefinePipeline: {
                auto define = Read<DefinePipeline>(body);
                m_keys[define.id] = define.key;
                break;
            }
            case Op::DefineShader:
                m_shaders[Read<DefineShader>(body).id] =
                    reinterpret_cast<const char*>(body + sizeof(DefineShader));
                break;
            case Op::Clear: Clear(Read<Gpu::Clear>(body)); break;
            case Op::Draw: DrawCall(Read<Gpu::Draw>(body)); break;
            case Op::Stretch: Stretch(Read<Gpu::Stretch>(body)); break;
            case Op::Present: Present(Read<Gpu::Present>(body).backBuffer); break;
            }
            at += header.size;
        }
        EndPass();
        if (m_blitSlot)
            m_queue.WriteBuffer(m_blitBuffer, 0, m_blitParams.data(), m_blitSlot * 256);
        wgpu::CommandBuffer commands = m_encoder.Finish();
        m_queue.Submit(1, &commands);
        m_encoder = nullptr;
    }

    template <typename T>
    static T Read(const uint8_t* body)
    {
        T value;
        std::memcpy(&value, body, sizeof(T));
        return value;
    }

    // --- Resources ---

    void DefineTexture(const Gpu::DefineTexture& d)
    {
        Texture t;
        t.kind = d.kind;
        t.format = d.format;
        t.width = std::max(d.width, 1u);
        t.height = std::max(d.height, 1u);
        t.levels = std::max<uint32_t>(d.levels, 1);
        wgpu::TextureDescriptor desc;
        desc.size = {t.width, t.height, d.kind == TextureKind::Cube ? 6u : 1u};
        desc.mipLevelCount = t.levels;
        switch (d.kind) {
        case TextureKind::Depth:
            t.gpuFormat = d.format == Format::Depth16 ? wgpu::TextureFormat::Depth16Unorm
                                                      : wgpu::TextureFormat::Depth24PlusStencil8;
            desc.usage = wgpu::TextureUsage::RenderAttachment;
            desc.mipLevelCount = t.levels = 1;
            break;
        case TextureKind::Target:
            t.gpuFormat = m_surfaceFormat;
            desc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding |
                         wgpu::TextureUsage::CopySrc | wgpu::TextureUsage::CopyDst;
            desc.mipLevelCount = t.levels = 1;
            break;
        default:
            t.bc = m_bc && IsDxt(d.format) && t.width % 4 == 0 && t.height % 4 == 0;
            t.gpuFormat = !t.bc                          ? wgpu::TextureFormat::RGBA8Unorm
                          : d.format == Format::DXT1 ? wgpu::TextureFormat::BC1RGBAUnorm
                          : d.format == Format::DXT3 ? wgpu::TextureFormat::BC2RGBAUnorm
                                                     : wgpu::TextureFormat::BC3RGBAUnorm;
            desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
            break;
        }
        desc.format = t.gpuFormat;
        t.texture = m_device.CreateTexture(&desc);
        wgpu::TextureViewDescriptor view;
        view.dimension = d.kind == TextureKind::Cube ? wgpu::TextureViewDimension::Cube
                                                     : wgpu::TextureViewDimension::e2D;
        if (d.kind != TextureKind::Depth)
            t.view = t.texture.CreateView(&view);
        if (d.kind == TextureKind::Target || d.kind == TextureKind::Depth) {
            wgpu::TextureViewDescriptor level;
            level.dimension = wgpu::TextureViewDimension::e2D;
            level.mipLevelCount = 1;
            t.target = t.texture.CreateView(&level);
        }
        m_textures[d.id] = std::move(t);
    }

    void UploadTexture(const Gpu::UploadTexture& u, const Gpu::Frame& frame)
    {
        auto it = m_textures.find(u.id);
        if (it == m_textures.end() || u.level >= it->second.levels)
            return;
        Texture& t = it->second;
        uint32_t width = std::max(t.width >> u.level, 1u), height = std::max(t.height >> u.level, 1u);
        const uint8_t* pixels = frame.data.data() + u.data;
        wgpu::TexelCopyTextureInfo where;
        where.texture = t.texture;
        where.mipLevel = u.level;
        where.origin = {0, 0, u.face};
        wgpu::TexelCopyBufferLayout layout;
        wgpu::Extent3D size;
        if (t.bc) {
            // (blocks: a level smaller than 4x4 still has one)
            uint32_t blocksX = (width + 3) / 4, blocksY = (height + 3) / 4;
            layout.bytesPerRow = u.pitch;
            layout.rowsPerImage = blocksY;
            size = {blocksX * 4, blocksY * 4, 1};
            m_queue.WriteTexture(&where, pixels, static_cast<size_t>(u.pitch) * blocksY, &layout, &size);
            return;
        }
        ToRgba8(t.format, width, height, u.pitch, pixels, m_scratch);
        layout.bytesPerRow = width * 4;
        layout.rowsPerImage = height;
        size = {width, height, 1};
        m_queue.WriteTexture(&where, m_scratch.data(), m_scratch.size(), &layout, &size);
    }

    void DefineBuffer(const Gpu::DefineBuffer& d)
    {
        wgpu::BufferDescriptor desc;
        desc.size = std::max<uint64_t>((d.size + 3) & ~3u, 4);
        desc.usage = (d.kind == BufferKind::Vertex ? wgpu::BufferUsage::Vertex : wgpu::BufferUsage::Index) |
                     wgpu::BufferUsage::CopyDst;
        m_buffers[d.id] = {m_device.CreateBuffer(&desc), static_cast<uint32_t>(desc.size)};
    }

    void UploadBuffer(const Gpu::UploadBuffer& u, const Gpu::Frame& frame)
    {
        auto it = m_buffers.find(u.id);
        if (it == m_buffers.end() || u.offset + u.size > it->second.size || u.size == 0)
            return;
        m_queue.WriteBuffer(it->second.buffer, u.offset, frame.data.data() + u.data, u.size);
    }

    void Release(uint32_t id)
    {
        if (m_textures.erase(id))
            m_textureGroups.clear(); // (bind groups naming it: an id can come back as another texture)
        m_buffers.erase(id);
    }

    // --- Passes ---

    void EndPass()
    {
        if (m_pass) {
            m_pass.End();
            m_pass = nullptr;
        }
        m_passTarget = {};
    }

    // A pass on the target: Load what's there, or Clear the parts the flags name.
    bool BeginPass(const Target& target, uint32_t clearFlags = 0, uint32_t color = 0, float z = 1, uint32_t stencil = 0)
    {
        auto colorIt = m_textures.find(target.color);
        if (colorIt == m_textures.end() || !colorIt->second.target)
            return false;
        EndPass();
        wgpu::RenderPassColorAttachment colorAttachment;
        colorAttachment.view = colorIt->second.target;
        colorAttachment.loadOp = clearFlags & D3DCLEAR_TARGET ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
        colorAttachment.storeOp = wgpu::StoreOp::Store;
        colorAttachment.clearValue = {((color >> 16) & 0xFF) / 255.0, ((color >> 8) & 0xFF) / 255.0,
                                      (color & 0xFF) / 255.0, ((color >> 24) & 0xFF) / 255.0};
        wgpu::RenderPassDescriptor pass;
        pass.colorAttachmentCount = 1;
        pass.colorAttachments = &colorAttachment;
        wgpu::RenderPassDepthStencilAttachment depthAttachment;
        auto depthIt = target.depth ? m_textures.find(target.depth) : m_textures.end();
        if (depthIt != m_textures.end() && depthIt->second.target) {
            depthAttachment.view = depthIt->second.target;
            depthAttachment.depthLoadOp = clearFlags & D3DCLEAR_ZBUFFER ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
            depthAttachment.depthStoreOp = wgpu::StoreOp::Store;
            depthAttachment.depthClearValue = std::clamp(z, 0.0f, 1.0f);
            if (depthIt->second.gpuFormat == wgpu::TextureFormat::Depth24PlusStencil8) {
                depthAttachment.stencilLoadOp =
                    clearFlags & D3DCLEAR_STENCIL ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
                depthAttachment.stencilStoreOp = wgpu::StoreOp::Store;
                depthAttachment.stencilClearValue = stencil;
            }
            pass.depthStencilAttachment = &depthAttachment;
        }
        m_pass = m_encoder.BeginRenderPass(&pass);
        m_passTarget = target;
        m_passWidth = colorIt->second.width;
        m_passHeight = colorIt->second.height;
        if (pass.depthStencilAttachment == nullptr)
            m_passTarget.depth = 0;
        m_boundPipeline = nullptr;
        return true;
    }

    bool SameTarget(const Target& a, const Target& b) { return a.color == b.color && a.depth == b.depth; }

    void Clear(const Gpu::Clear& c)
    {
        bool whole = c.rect[0] < 0 && c.viewport[0] == 0 && c.viewport[1] == 0 &&
                     static_cast<uint32_t>(c.viewport[2]) >= c.target.width &&
                     static_cast<uint32_t>(c.viewport[3]) >= c.target.height;
        if (!whole) {
            Log("partial clears not drawn yet\n");
            return;
        }
        BeginPass(c.target, c.flags, c.color, c.z, c.stencil);
    }

    // --- Draws ---

    // A draw's pipeline, or null while it compiles: it's made in the background (CreateRenderPipelineAsync), and the
    // draws needing it are skipped until it's ready - a frame or two without the thing, where waiting for the compile
    // held up the whole frame (each new material that came into view stuttered).
    Pipeline* PipelineFor(uint32_t id)
    {
        auto it = m_pipelines.find(id);
        if (it != m_pipelines.end())
            return it->second.failed || it->second.pending ? nullptr : &it->second;
        Pipeline& p = m_pipelines[id];
        auto keyIt = m_keys.find(id);
        if (keyIt == m_keys.end()) {
            p.failed = true;
            return nullptr;
        }
        const PipelineKey& key = keyIt->second;
        ++m_newPipelines;
        const char* text = nullptr;
        if (key.shader.pixelShader) {
            auto s = m_shaders.find(key.shader.pixelShader);
            if (s != m_shaders.end())
                text = s->second.c_str();
        }
        wgpu::ShaderModule module = ShaderModuleFor(GenerateWgsl(key.shader, text));

        // Vertex buffers: the streams used, packed into slots.
        std::vector<wgpu::VertexAttribute> attributes[4];
        for (int i = 0; i < key.shader.elementCount; ++i) {
            const VertexElement& e = key.shader.elements[i];
            if (e.stream >= 4)
                continue;
            wgpu::VertexAttribute a;
            a.format = VertexFormat(e.type);
            a.offset = e.offset;
            a.shaderLocation = static_cast<uint32_t>(i);
            attributes[e.stream].push_back(a);
        }
        std::vector<wgpu::VertexBufferLayout> buffers;
        for (int s = 0; s < 4; ++s) {
            if (attributes[s].empty())
                continue;
            p.slotOfStream[s] = static_cast<int8_t>(buffers.size());
            wgpu::VertexBufferLayout layout;
            layout.arrayStride = key.strides[s];
            layout.stepMode = wgpu::VertexStepMode::Vertex;
            layout.attributeCount = attributes[s].size();
            layout.attributes = attributes[s].data();
            buffers.push_back(layout);
        }

        wgpu::RenderPipelineDescriptor desc;
        desc.vertex.module = module;
        desc.vertex.entryPoint = "vs";
        desc.vertex.bufferCount = buffers.size();
        desc.vertex.buffers = buffers.data();
        switch (key.topology) {
        case D3DPT_POINTLIST: desc.primitive.topology = wgpu::PrimitiveTopology::PointList; break;
        case D3DPT_LINELIST: desc.primitive.topology = wgpu::PrimitiveTopology::LineList; break;
        case D3DPT_LINESTRIP: desc.primitive.topology = wgpu::PrimitiveTopology::LineStrip; break;
        case D3DPT_TRIANGLESTRIP: desc.primitive.topology = wgpu::PrimitiveTopology::TriangleStrip; break;
        default: desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList; break;
        }
        bool strip = key.topology == D3DPT_LINESTRIP || key.topology == D3DPT_TRIANGLESTRIP;
        if (strip && key.indexFormat)
            desc.primitive.stripIndexFormat =
                key.indexFormat == 32 ? wgpu::IndexFormat::Uint32 : wgpu::IndexFormat::Uint16;
        desc.primitive.frontFace = wgpu::FrontFace::CCW;
        desc.primitive.cullMode = key.cull == D3DCULL_CW    ? wgpu::CullMode::Back
                                  : key.cull == D3DCULL_CCW ? wgpu::CullMode::Front
                                                            : wgpu::CullMode::None;
        wgpu::DepthStencilState depth;
        if (key.depthFormat) {
            depth.format = key.depthFormat == static_cast<uint8_t>(Format::Depth16)
                               ? wgpu::TextureFormat::Depth16Unorm
                               : wgpu::TextureFormat::Depth24PlusStencil8;
            depth.depthWriteEnabled = key.zEnable && key.zWrite;
            depth.depthCompare = key.zEnable ? Compare(key.zFunc) : wgpu::CompareFunction::Always;
            if (key.stencilEnable && depth.format == wgpu::TextureFormat::Depth24PlusStencil8) {
                wgpu::StencilFaceState face;
                face.compare = Compare(key.stencilFunc);
                face.failOp = StencilOp(key.stencilFail);
                face.depthFailOp = StencilOp(key.stencilZFail);
                face.passOp = StencilOp(key.stencilPass);
                depth.stencilFront = depth.stencilBack = face;
                depth.stencilReadMask = key.stencilReadMask;
                depth.stencilWriteMask = key.stencilWriteMask;
            } else {
                depth.stencilReadMask = depth.stencilWriteMask = 0;
            }
            depth.depthBias = key.depthBias;
            depth.depthBiasSlopeScale = key.slopeBias;
            desc.depthStencil = &depth;
        }
        wgpu::BlendState blend;
        wgpu::ColorTargetState color;
        color.format = m_surfaceFormat;
        color.writeMask = static_cast<wgpu::ColorWriteMask>(key.colorWrite);
        if (key.blendEnable) {
            blend.color = Blend(key.blendOp, key.srcBlend, key.dstBlend);
            blend.alpha = key.separateAlpha ? Blend(key.blendOpAlpha, key.srcBlendAlpha, key.dstBlendAlpha)
                                            : Blend(key.blendOp, key.srcBlend, key.dstBlend);
            color.blend = &blend;
        }
        wgpu::FragmentState fragment;
        fragment.module = module;
        fragment.entryPoint = "fs";
        fragment.targetCount = 1;
        fragment.targets = &color;
        desc.fragment = &fragment;

        // Bind group layouts: the uniforms, and the textures the shader samples.
        uint32_t mask = 0;
        for (int i = 0; i < 8; ++i)
            if (Samples(key.shader, i))
                mask |= (key.shader.stages[i].texture == 2 ? 3u : 1u) << (i * 2);
        p.textureLayout = mask;
        wgpu::BindGroupLayout layouts[2] = {m_uniformLayout, TextureLayout(mask)};
        wgpu::PipelineLayoutDescriptor layoutDesc;
        layoutDesc.bindGroupLayoutCount = mask ? 2 : 1;
        layoutDesc.bindGroupLayouts = layouts;
        desc.layout = m_device.CreatePipelineLayout(&layoutDesc);
        p.pending = true;
        m_device.CreateRenderPipelineAsync(
            &desc, wgpu::CallbackMode::AllowSpontaneous,
            [this, id](wgpu::CreatePipelineAsyncStatus status, wgpu::RenderPipeline pipeline, wgpu::StringView message) {
                Pipeline& made = m_pipelines[id];
                made.pending = false;
                if (status == wgpu::CreatePipelineAsyncStatus::Success && pipeline)
                    made.pipeline = std::move(pipeline);
                else {
                    made.failed = true;
                    Log("pipeline not made: %s\n", std::string(message.data, message.length).c_str());
                }
            });
        return nullptr;
    }

    // One module per shader text: pipelines differing only in their states (blending, depth, culling) share it.
    wgpu::ShaderModule ShaderModuleFor(std::string wgsl)
    {
        auto it = m_modules.find(wgsl);
        if (it != m_modules.end())
            return it->second;
        wgpu::ShaderSourceWGSL source;
        source.code = View(wgsl);
        wgpu::ShaderModuleDescriptor desc;
        desc.nextInChain = &source;
        return m_modules[std::move(wgsl)] = m_device.CreateShaderModule(&desc);
    }

    wgpu::BindGroupLayout TextureLayout(uint32_t mask)
    {
        if (!mask)
            return nullptr;
        auto it = m_textureLayouts.find(mask);
        if (it != m_textureLayouts.end())
            return it->second;
        std::vector<wgpu::BindGroupLayoutEntry> entries;
        for (int i = 0; i < 8; ++i) {
            uint32_t bits = (mask >> (i * 2)) & 3;
            if (!bits)
                continue;
            wgpu::BindGroupLayoutEntry texture;
            texture.binding = static_cast<uint32_t>(i * 2);
            texture.visibility = wgpu::ShaderStage::Fragment;
            texture.texture.sampleType = wgpu::TextureSampleType::Float;
            texture.texture.viewDimension = bits == 3 ? wgpu::TextureViewDimension::Cube
                                                      : wgpu::TextureViewDimension::e2D;
            entries.push_back(texture);
            wgpu::BindGroupLayoutEntry sampler;
            sampler.binding = static_cast<uint32_t>(i * 2 + 1);
            sampler.visibility = wgpu::ShaderStage::Fragment;
            sampler.sampler.type = wgpu::SamplerBindingType::Filtering;
            entries.push_back(sampler);
        }
        wgpu::BindGroupLayoutDescriptor desc;
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        return m_textureLayouts[mask] = m_device.CreateBindGroupLayout(&desc);
    }

    wgpu::Sampler SamplerFor(const Sampler& s)
    {
        uint64_t key;
        std::memcpy(&key, &s, sizeof(key));
        auto it = m_samplers.find(key);
        if (it != m_samplers.end())
            return it->second;
        wgpu::SamplerDescriptor desc;
        desc.addressModeU = Address(s.addressU);
        desc.addressModeV = Address(s.addressV);
        desc.addressModeW = Address(s.addressW);
        bool linearMin = s.minFilter >= D3DTEXF_LINEAR, linearMag = s.magFilter >= D3DTEXF_LINEAR;
        desc.minFilter = linearMin ? wgpu::FilterMode::Linear : wgpu::FilterMode::Nearest;
        desc.magFilter = linearMag ? wgpu::FilterMode::Linear : wgpu::FilterMode::Nearest;
        desc.mipmapFilter = s.mipFilter >= D3DTEXF_LINEAR ? wgpu::MipmapFilterMode::Linear
                                                          : wgpu::MipmapFilterMode::Nearest;
        if (s.mipFilter == D3DTEXF_NONE)
            desc.lodMaxClamp = 0;
        if ((s.minFilter == D3DTEXF_ANISOTROPIC || s.maxAnisotropy > 1) && linearMin && linearMag &&
            s.mipFilter >= D3DTEXF_LINEAR)
            desc.maxAnisotropy = std::max<uint16_t>(s.maxAnisotropy, 1);
        return m_samplers[key] = m_device.CreateSampler(&desc);
    }

    wgpu::BindGroup TextureGroup(const Pipeline& p, const Gpu::Draw& d)
    {
        // (the key: the layout, each sampled stage's texture and sampler)
        std::string key(reinterpret_cast<const char*>(&p.textureLayout), 4);
        for (int i = 0; i < 8; ++i)
            if ((p.textureLayout >> (i * 2)) & 3) {
                key.append(reinterpret_cast<const char*>(&d.textures[i]), 4);
                key.append(reinterpret_cast<const char*>(&d.samplers[i]), sizeof(Sampler));
            }
        auto it = m_textureGroups.find(key);
        if (it != m_textureGroups.end())
            return it->second;
        std::vector<wgpu::BindGroupEntry> entries;
        for (int i = 0; i < 8; ++i) {
            uint32_t bits = (p.textureLayout >> (i * 2)) & 3;
            if (!bits)
                continue;
            wgpu::BindGroupEntry texture;
            texture.binding = static_cast<uint32_t>(i * 2);
            auto t = m_textures.find(d.textures[i]);
            bool usable = t != m_textures.end() && t->second.view &&
                          (bits == 3) == (t->second.kind == TextureKind::Cube) && d.textures[i] != m_passTarget.color;
            texture.textureView = usable ? t->second.view : bits == 3 ? m_blackCube : m_black2D;
            entries.push_back(texture);
            wgpu::BindGroupEntry sampler;
            sampler.binding = static_cast<uint32_t>(i * 2 + 1);
            sampler.sampler = SamplerFor(d.samplers[i]);
            entries.push_back(sampler);
        }
        wgpu::BindGroupDescriptor desc;
        desc.layout = TextureLayout(p.textureLayout);
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        wgpu::BindGroup group = m_device.CreateBindGroup(&desc);
        if (m_textureGroups.size() > 8192)
            m_textureGroups.clear();
        return m_textureGroups[key] = group;
    }

    void DrawCall(const Gpu::Draw& d)
    {
        if (!m_pass || !SameTarget(m_passTarget, d.target)) {
            Target target = d.target;
            if (!BeginPass(target))
                return;
        }
        Pipeline* p = PipelineFor(d.pipeline);
        if (!p)
            return;
        const PipelineKey& key = m_keys[d.pipeline];
        if (key.depthFormat && !m_passTarget.depth)
            return; // (the pipeline wants a depth buffer the pass hasn't got)
        if (p->pipeline.Get() != m_boundPipeline) {
            m_pass.SetPipeline(p->pipeline);
            m_boundPipeline = p->pipeline.Get();
        }
        uint32_t offsets[2] = {d.drawUniforms, d.lightingUniforms == ~0u ? 0u : d.lightingUniforms};
        m_pass.SetBindGroup(0, m_uniformGroup, 2, offsets);
        if (p->textureLayout)
            m_pass.SetBindGroup(1, TextureGroup(*p, d));
        for (int s = 0; s < 4; ++s) {
            if (p->slotOfStream[s] < 0)
                continue;
            if (d.vertexBuffers[s] == 0) {
                m_pass.SetVertexBuffer(static_cast<uint32_t>(p->slotOfStream[s]), m_frameBuffer, d.vertexOffsets[s]);
            } else {
                auto b = m_buffers.find(d.vertexBuffers[s]);
                if (b == m_buffers.end() || d.vertexOffsets[s] >= b->second.size)
                    return;
                m_pass.SetVertexBuffer(static_cast<uint32_t>(p->slotOfStream[s]), b->second.buffer,
                                       d.vertexOffsets[s]);
            }
        }
        // The viewport (the whole target for screen-space vertices), the scissor within it.
        float vx = d.viewport[0], vy = d.viewport[1], vw = d.viewport[2], vh = d.viewport[3];
        float fw = static_cast<float>(m_passWidth), fh = static_cast<float>(m_passHeight);
        float sx0 = std::max(vx, 0.0f), sy0 = std::max(vy, 0.0f);
        float sx1 = std::min(vx + vw, fw), sy1 = std::min(vy + vh, fh);
        if (key.shader.pretransformed)
            m_pass.SetViewport(0, 0, fw, fh, 0, 1);
        else
            m_pass.SetViewport(std::clamp(vx, 0.0f, fw), std::clamp(vy, 0.0f, fh),
                               std::max(std::min(vw, fw - std::clamp(vx, 0.0f, fw)), 1.0f),
                               std::max(std::min(vh, fh - std::clamp(vy, 0.0f, fh)), 1.0f),
                               std::clamp(d.viewport[4], 0.0f, 1.0f), std::clamp(d.viewport[5], 0.0f, 1.0f));
        if (d.scissor[2] > d.scissor[0] || d.scissor[3] > d.scissor[1]) {
            sx0 = std::max(sx0, static_cast<float>(d.scissor[0]));
            sy0 = std::max(sy0, static_cast<float>(d.scissor[1]));
            sx1 = std::min(sx1, static_cast<float>(d.scissor[2]));
            sy1 = std::min(sy1, static_cast<float>(d.scissor[3]));
        }
        if (sx1 <= sx0 || sy1 <= sy0)
            return;
        m_pass.SetScissorRect(static_cast<uint32_t>(sx0), static_cast<uint32_t>(sy0),
                              static_cast<uint32_t>(sx1 - sx0), static_cast<uint32_t>(sy1 - sy0));
        m_pass.SetStencilReference(d.stencilRef);
        if (key.blendEnable) {
            wgpu::Color factor = {((d.blendFactor >> 16) & 0xFF) / 255.0, ((d.blendFactor >> 8) & 0xFF) / 255.0,
                                  (d.blendFactor & 0xFF) / 255.0, ((d.blendFactor >> 24) & 0xFF) / 255.0};
            m_pass.SetBlendConstant(&factor);
        }
        if (d.indexOffset != ~0u) {
            wgpu::IndexFormat format = key.indexFormat == 32 ? wgpu::IndexFormat::Uint32 : wgpu::IndexFormat::Uint16;
            if (d.indexBuffer == 0) {
                m_pass.SetIndexBuffer(m_frameBuffer, format, d.indexOffset);
            } else {
                auto b = m_buffers.find(d.indexBuffer);
                if (b == m_buffers.end())
                    return;
                m_pass.SetIndexBuffer(b->second.buffer, format);
            }
            m_pass.DrawIndexed(d.count, 1, d.first, d.baseVertex, 0);
        } else {
            m_pass.Draw(d.count, 1, d.first, 0);
        }
        ++m_drawCount;
    }

    // --- Copies ---

    // A rectangle of one target drawn into a rectangle of another (filtered), or copied when they're the same size.
    void Stretch(const Gpu::Stretch& s)
    {
        auto from = m_textures.find(s.source), to = m_textures.find(s.destination);
        if (from == m_textures.end() || to == m_textures.end() || !from->second.view || !to->second.target ||
            s.source == s.destination)
            return;
        EndPass();
        const Texture& a = from->second;
        const Texture& b = to->second;
        int32_t sr[4] = {s.sourceRect[0], s.sourceRect[1], s.sourceRect[2], s.sourceRect[3]};
        int32_t dr[4] = {s.destinationRect[0], s.destinationRect[1], s.destinationRect[2], s.destinationRect[3]};
        if (sr[0] < 0)
            sr[0] = 0, sr[1] = 0, sr[2] = static_cast<int32_t>(a.width), sr[3] = static_cast<int32_t>(a.height);
        if (dr[0] < 0)
            dr[0] = 0, dr[1] = 0, dr[2] = static_cast<int32_t>(b.width), dr[3] = static_cast<int32_t>(b.height);
        Blit(a, sr, b, dr, s.linear != 0);
    }

    void Blit(const Texture& a, const int32_t* sr, const Texture& b, const int32_t* dr, bool linear)
    {
        if (sr[2] - sr[0] == dr[2] - dr[0] && sr[3] - sr[1] == dr[3] - dr[1] && a.gpuFormat == b.gpuFormat) {
            wgpu::TexelCopyTextureInfo src, dst;
            src.texture = a.texture;
            src.origin = {static_cast<uint32_t>(sr[0]), static_cast<uint32_t>(sr[1]), 0};
            dst.texture = b.texture;
            dst.origin = {static_cast<uint32_t>(dr[0]), static_cast<uint32_t>(dr[1]), 0};
            wgpu::Extent3D size = {static_cast<uint32_t>(sr[2] - sr[0]), static_cast<uint32_t>(sr[3] - sr[1]), 1};
            m_encoder.CopyTextureToTexture(&src, &dst, &size);
            return;
        }
        BlitWithPipeline(a, sr, b.target, b.width, b.height, dr, linear);
    }

    // The blit pipeline: a triangle covering the destination rectangle, sampling the source rectangle.
    void BlitWithPipeline(const Texture& a, const int32_t* sr, const wgpu::TextureView& target, uint32_t width,
                          uint32_t height, const int32_t* dr, bool linear)
    {
        if (!m_blitPipeline)
            MakeBlit();
        if (m_blitSlot >= kBlitSlots)
            return;
        float* params = reinterpret_cast<float*>(m_blitParams.data() + m_blitSlot * 256);
        params[0] = static_cast<float>(sr[0]) / static_cast<float>(a.width);
        params[1] = static_cast<float>(sr[1]) / static_cast<float>(a.height);
        params[2] = static_cast<float>(sr[2] - sr[0]) / static_cast<float>(a.width);
        params[3] = static_cast<float>(sr[3] - sr[1]) / static_cast<float>(a.height);
        wgpu::BindGroupEntry entries[3];
        entries[0].binding = 0;
        entries[0].buffer = m_blitBuffer;
        entries[0].size = 16;
        entries[1].binding = 1;
        entries[1].textureView = a.view;
        entries[2].binding = 2;
        entries[2].sampler = linear ? m_linear : m_nearest;
        wgpu::BindGroupDescriptor group;
        group.layout = m_blitLayout;
        group.entryCount = 3;
        group.entries = entries;
        wgpu::BindGroup bind = m_device.CreateBindGroup(&group);
        wgpu::RenderPassColorAttachment color;
        color.view = target;
        color.loadOp = wgpu::LoadOp::Load;
        color.storeOp = wgpu::StoreOp::Store;
        wgpu::RenderPassDescriptor pass;
        pass.colorAttachmentCount = 1;
        pass.colorAttachments = &color;
        wgpu::RenderPassEncoder encoder = m_encoder.BeginRenderPass(&pass);
        encoder.SetPipeline(m_blitPipeline);
        uint32_t offset = m_blitSlot * 256;
        encoder.SetBindGroup(0, bind, 1, &offset);
        float x = std::clamp(static_cast<float>(dr[0]), 0.0f, static_cast<float>(width));
        float y = std::clamp(static_cast<float>(dr[1]), 0.0f, static_cast<float>(height));
        encoder.SetViewport(x, y, std::max(std::min(static_cast<float>(dr[2] - dr[0]), width - x), 1.0f),
                            std::max(std::min(static_cast<float>(dr[3] - dr[1]), height - y), 1.0f), 0, 1);
        encoder.Draw(3);
        encoder.End();
        ++m_blitSlot;
    }

    void MakeBlit()
    {
        static const char kWgsl[] = R"(
struct Rect { origin: vec2f, size: vec2f };
@group(0) @binding(0) var<uniform> rect: Rect;
@group(0) @binding(1) var source: texture_2d<f32>;
@group(0) @binding(2) var linearOrNot: sampler;
struct Out { @builtin(position) position: vec4f, @location(0) uv: vec2f };
@vertex fn vs(@builtin(vertex_index) i: u32) -> Out {
    let corner = vec2f(f32((i << 1u) & 2u), f32(i & 2u));
    var out: Out;
    out.position = vec4f(corner * vec2f(2.0, -2.0) + vec2f(-1.0, 1.0), 0.0, 1.0);
    out.uv = rect.origin + corner * rect.size;
    return out;
}
@fragment fn fs(in: Out) -> @location(0) vec4f { return textureSample(source, linearOrNot, in.uv); }
)";
        wgpu::ShaderSourceWGSL source;
        source.code = kWgsl;
        wgpu::ShaderModuleDescriptor moduleDesc;
        moduleDesc.nextInChain = &source;
        wgpu::ShaderModule module = m_device.CreateShaderModule(&moduleDesc);
        wgpu::BindGroupLayoutEntry entries[3];
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Vertex;
        entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[0].buffer.hasDynamicOffset = true;
        entries[1].binding = 1;
        entries[1].visibility = wgpu::ShaderStage::Fragment;
        entries[1].texture.sampleType = wgpu::TextureSampleType::Float;
        entries[2].binding = 2;
        entries[2].visibility = wgpu::ShaderStage::Fragment;
        entries[2].sampler.type = wgpu::SamplerBindingType::Filtering;
        wgpu::BindGroupLayoutDescriptor layout;
        layout.entryCount = 3;
        layout.entries = entries;
        m_blitLayout = m_device.CreateBindGroupLayout(&layout);
        wgpu::PipelineLayoutDescriptor pipelineLayout;
        pipelineLayout.bindGroupLayoutCount = 1;
        pipelineLayout.bindGroupLayouts = &m_blitLayout;
        wgpu::ColorTargetState color;
        color.format = m_surfaceFormat;
        wgpu::FragmentState fragment;
        fragment.module = module;
        fragment.entryPoint = "fs";
        fragment.targetCount = 1;
        fragment.targets = &color;
        wgpu::RenderPipelineDescriptor desc;
        desc.layout = m_device.CreatePipelineLayout(&pipelineLayout);
        desc.vertex.module = module;
        desc.vertex.entryPoint = "vs";
        desc.fragment = &fragment;
        m_blitPipeline = m_device.CreateRenderPipeline(&desc);
        wgpu::BufferDescriptor buffer;
        buffer.size = kBlitSlots * 256;
        buffer.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        m_blitBuffer = m_device.CreateBuffer(&buffer);
        m_blitParams.assign(kBlitSlots * 256, 0);
        wgpu::SamplerDescriptor sampler;
        sampler.minFilter = sampler.magFilter = wgpu::FilterMode::Linear;
        m_linear = m_device.CreateSampler(&sampler);
        sampler.minFilter = sampler.magFilter = wgpu::FilterMode::Nearest;
        m_nearest = m_device.CreateSampler(&sampler);
    }

    // The back buffer onto the canvas (its size: the canvas takes the back buffer's).
    void Present(uint32_t backBuffer)
    {
        EndPass();
        auto it = m_textures.find(backBuffer);
        if (it == m_textures.end())
            return;
        const Texture& b = it->second;
        if (b.width != m_canvasWidth || b.height != m_canvasHeight) {
            wgpu::SurfaceConfiguration config;
            config.device = m_device;
            config.format = m_surfaceFormat;
            config.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopyDst;
            config.width = b.width;
            config.height = b.height;
            config.alphaMode = wgpu::CompositeAlphaMode::Opaque;
            m_surface.Configure(&config);
            m_canvasWidth = b.width;
            m_canvasHeight = b.height;
        }
        wgpu::SurfaceTexture canvas;
        m_surface.GetCurrentTexture(&canvas);
        if (canvas.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal &&
            canvas.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal)
            return;
        wgpu::TexelCopyTextureInfo src, dst;
        src.texture = b.texture;
        dst.texture = canvas.texture;
        wgpu::Extent3D size = {b.width, b.height, 1};
        m_encoder.CopyTextureToTexture(&src, &dst, &size);
        ++m_presented;
    }

    // Frames shown per second and the game's frame time, to the page (Module.thug2Stats) twice a second - with what
    // makes a frame late: the longest wait between two frames shown, the game's longest frame, the pipelines made
    // (each a shader compile) and the game's files fetched (the game waits for each).
    void Stats(double time, double gameMs)
    {
        m_gameMs += gameMs;
        m_worstGame = std::max(m_worstGame, gameMs);
        if (m_lastFrameTime > 0)
            m_worstGap = std::max(m_worstGap, time - m_lastFrameTime);
        m_lastFrameTime = time;
        ++m_statFrames;
        if (m_statStart == 0)
            m_statStart = time;
        if (time - m_statStart < 500)
            return;
        double seconds = (time - m_statStart) / 1000.0;
        double fps = m_presented / seconds;
        double game = m_gameMs / m_statFrames;
        unsigned files = 0;
        double filesMs = 0;
        Thug2Web::TakeFetchStats(files, filesMs);
        double mixMs = thug2_audio_mix_ms() / seconds;
        EM_ASM({ if (Module.thug2Stats) Module.thug2Stats($0, $1, $2, $3, $4, $5, $6, $7, $8, $9, $10); }, fps, game,
               0.0, m_drawCount / m_statFrames, m_pageMs / m_statFrames, m_worstGap, m_worstGame, m_newPipelines,
               files, filesMs, mixMs);
        m_statStart = time;
        m_presented = 0;
        m_statFrames = 0;
        m_gameMs = 0;
        m_pageMs = 0;
        m_drawCount = 0;
        m_worstGap = m_worstGame = 0;
        m_newPipelines = 0;
    }

    static constexpr uint32_t kBlitSlots = 64;

    wgpu::Instance m_instance;
    wgpu::Adapter m_adapter;
    wgpu::Device m_device;
    wgpu::Queue m_queue;
    wgpu::Surface m_surface;
    wgpu::TextureFormat m_surfaceFormat = wgpu::TextureFormat::BGRA8Unorm;
    bool m_bc = false;
    uint32_t m_canvasWidth = 0, m_canvasHeight = 0;

    std::unordered_map<uint32_t, Texture> m_textures;
    std::unordered_map<uint32_t, Buffer> m_buffers;
    std::unordered_map<uint32_t, PipelineKey> m_keys;
    std::unordered_map<uint32_t, std::string> m_shaders;
    std::unordered_map<uint32_t, Pipeline> m_pipelines;
    std::unordered_map<std::string, wgpu::ShaderModule> m_modules;
    std::unordered_map<uint32_t, wgpu::BindGroupLayout> m_textureLayouts;
    std::unordered_map<std::string, wgpu::BindGroup> m_textureGroups;
    std::unordered_map<uint64_t, wgpu::Sampler> m_samplers;
    wgpu::BindGroupLayout m_uniformLayout;
    wgpu::BindGroup m_uniformGroup;
    wgpu::Buffer m_frameBuffer;
    size_t m_frameBufferSize = 0;
    wgpu::TextureView m_black2D, m_blackCube;
    std::vector<uint8_t> m_scratch;

    wgpu::RenderPipeline m_blitPipeline;
    wgpu::BindGroupLayout m_blitLayout;
    wgpu::Buffer m_blitBuffer;
    std::vector<uint8_t> m_blitParams;
    uint32_t m_blitSlot = 0;
    wgpu::Sampler m_linear, m_nearest;

    wgpu::CommandEncoder m_encoder;
    wgpu::RenderPassEncoder m_pass;
    Target m_passTarget = {};
    uint32_t m_passWidth = 0, m_passHeight = 0;
    WGPURenderPipeline m_boundPipeline = nullptr;

    double m_statStart = 0, m_gameMs = 0;
    uint32_t m_presented = 0, m_statFrames = 0, m_drawCount = 0;
    double m_lastFrameTime = 0, m_worstGap = 0, m_worstGame = 0;
    uint32_t m_newPipelines = 0;
    double m_pageMs = 0; // (the page thread's time turning the frames into WebGPU calls)
};

} // namespace

} // namespace Gpu

// From the page, once the build has started (its thread).
extern "C" EMSCRIPTEN_KEEPALIVE void thug2_renderer_start()
{
    static Gpu::Renderer renderer;
    renderer.Start();
}
