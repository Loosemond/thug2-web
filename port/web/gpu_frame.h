#pragma once

// A frame as the game's thread records it (gpu_recorder.cpp: the Direct3D 9 device's backend) for the page's thread
// to draw with WebGPU (gpu_renderer.cpp). Commands are records in a byte stream, each a Header then its struct; what
// they upload (pixels, vertices, uniforms) is in the frame's data. Resources are named by their Direct3D 9 object's
// address; the renderer keeps a GPU copy of each until its Release.

#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace Gpu {

enum class Op : uint32_t {
    DefineTexture, // a texture, cube texture or render target / depth surface of its own
    UploadTexture, // one level (one face's) pixels, as Direct3D 9 laid them out
    DefineBuffer,  // a vertex or index buffer
    UploadBuffer,  // bytes of one
    Release,       // a resource deleted
    DefinePipeline,
    DefineShader,  // a pixel shader's text (ps.1.x)
    Clear,
    Draw,
    Stretch,
    Present,
};

struct Header {
    Op op;
    uint32_t size; // of the record, this header included
};

// The texture formats as the renderer takes them (converted from Direct3D 9's on the page's thread).
enum class Format : uint8_t {
    None,
    BGRA8,     // A8R8G8B8
    BGRX8,     // X8R8G8B8 (alpha 1)
    R5G6B5,
    A1R5G5B5,
    X1R5G5B5,
    A4R4G4B4,
    A8,
    L8,
    A8L8,
    DXT1,
    DXT3,      // (and DXT2)
    DXT5,      // (and DXT4)
    Depth24S8, // depth surfaces
    Depth16,
};

enum class TextureKind : uint8_t { Plain, Cube, Target, Depth };

struct DefineTexture {
    uint32_t id;
    TextureKind kind;
    Format format;
    uint16_t levels;
    uint32_t width, height;
};

struct UploadTexture {
    uint32_t id;
    uint16_t face, level;
    uint32_t data, size; // in the frame's data
    uint32_t pitch;      // bytes per row (of blocks, for DXT)
};

enum class BufferKind : uint8_t { Vertex, Index16, Index32 };

struct DefineBuffer {
    uint32_t id;
    BufferKind kind;
    uint32_t size;
};

struct UploadBuffer {
    uint32_t id;
    uint32_t offset;
    uint32_t data, size;
};

struct Release {
    uint32_t id;
};

// --- Pipelines ---------------------------------------------------------------------------------------------------

// A vertex element (from the FVF or the declaration): Direct3D 9's D3DDECLTYPE / D3DDECLUSAGE.
struct VertexElement {
    uint8_t stream, type, usage, usageIndex;
    uint16_t offset;
    uint16_t pad = 0;
};

// One fixed-function texture stage (D3DTOP / D3DTA values), or what a stage's texture is for a pixel shader.
struct Stage {
    uint8_t colorOp, colorArg0, colorArg1, colorArg2;
    uint8_t alphaOp, alphaArg0, alphaArg1, alphaArg2;
    uint8_t resultArg;
    uint8_t texcoordIndex; // the vertex's set
    uint8_t texgen;        // D3DTSS_TCI_* >> 16 (0: the vertex's coordinates)
    uint8_t transform;     // D3DTSS_TEXTURETRANSFORMFLAGS (count | D3DTTFF_PROJECTED)
    uint8_t texture;       // 0 none, 1 2D, 2 cube
    uint8_t pad[3] = {};
};

// What the shaders are generated from: all that changes the WGSL. Zeroed before filling (compared bytewise).
struct ShaderKey {
    VertexElement elements[16];
    uint8_t elementCount;
    uint8_t pretransformed; // D3DFVF_XYZRHW / POSITIONT: screen pixels
    uint8_t lighting, specular, normalizeNormals, localViewer, colorVertex;
    uint8_t diffuseSource, ambientSource, specularSource, emissiveSource; // D3DMCS_*
    uint8_t lightCount;
    uint8_t lightTypes[8]; // D3DLIGHTTYPE
    uint8_t fogMode;       // 0 none; else D3DFOG_* of the table (pixel) fog, or 4 + the vertex fog's
    uint8_t rangeFog;
    uint8_t vertexBlend;   // D3DVBF_* weights (0 none)
    uint8_t alphaFunc;     // D3DCMP_* (0: no alpha test)
    uint8_t stageCount;
    uint8_t pad[3] = {};
    uint32_t pixelShader;  // a DefineShader id (0: the fixed-function stages)
    Stage stages[8];
};

struct PipelineKey {
    ShaderKey shader;
    uint16_t strides[4];
    uint8_t topology;      // D3DPRIMITIVETYPE (fans arrive as lists)
    uint8_t indexFormat;   // 0, 16, 32 (strips need it)
    uint8_t cull;          // D3DCULL
    uint8_t fill;          // D3DFILLMODE
    uint8_t zEnable, zWrite, zFunc;
    uint8_t stencilEnable, stencilFunc, stencilFail, stencilZFail, stencilPass;
    uint8_t stencilReadMask, stencilWriteMask;
    uint8_t blendEnable, srcBlend, dstBlend, blendOp;
    uint8_t separateAlpha, srcBlendAlpha, dstBlendAlpha, blendOpAlpha;
    uint8_t colorWrite;    // D3DCOLORWRITEENABLE mask
    uint8_t colorFormat;   // the target's: Format (None: no color target)
    uint8_t depthFormat;   // Format::Depth24S8 / Depth16 / None
    uint8_t pad = 0;
    int32_t depthBias;     // (D3DRS_DEPTHBIAS's float, as WebGPU's integer units)
    float slopeBias;
};

struct DefinePipeline {
    uint32_t id;
    PipelineKey key;
};

// The pixel shader's text follows the record (NUL-terminated).
struct DefineShader {
    uint32_t id;
};

// --- Drawing -------------------------------------------------------------------------------------------------------

// What a draw's shaders read (WGSL's Draw struct: gpu_shaders.cpp - keep the two in step). 16-byte aligned rows.
struct DrawUniforms {
    float worldView[4][16]; // (one per blend weight; [0] without blending)
    float projection[16];
    float normal[4][16];    // the inverse transposes of worldView (upper 3x3 used)
    float texture[8][16];   // texture transforms
    float screen[4];        // pretransformed vertices: 2 / width, -2 / height, the half-pixel offset x, y
    float pixelOffset[4];   // transformed vertices: the half-pixel offset in clip space (x, y per w)
    float fog[4];           // start, end, density, 1 / (end - start)
    float fogColor[4];
    float textureFactor[4];
    float alphaRef[4];
    float constants[8][4];  // the pixel shader's c0-c7
};

// Lighting (WGSL's Lighting struct): the material, the ambient, the lights in view space.
struct Light {
    float diffuse[4], specular[4], ambient[4];
    float position[4];  // (w: range)
    float direction[4]; // (w: falloff)
    float attenuation[4]; // 0, 1, 2, and cos(phi / 2)
    float spot[4];        // cos(theta / 2), 1 / (cos(theta / 2) - cos(phi / 2))
};
struct LightingUniforms {
    float diffuse[4], ambient[4], specular[4], emissive[4];
    float power[4];
    float globalAmbient[4];
    Light lights[8];
};

// A stage's sampler (D3DSAMP_* values).
struct Sampler {
    uint8_t addressU, addressV, addressW;
    uint8_t minFilter, magFilter, mipFilter;
    uint8_t maxAnisotropy;
    uint8_t pad = 0;
};

struct Target {
    uint32_t color; // a surface: the back buffer, a render target of its own, or a target texture's level 0
    uint32_t depth;
    uint32_t width, height;
};

struct Clear {
    Target target;
    uint32_t flags; // D3DCLEAR_*
    uint32_t color; // D3DCOLOR
    float z;
    uint32_t stencil;
    int32_t rect[4]; // (all -1: the whole target)
    int32_t viewport[6]; // x, y, width, height, min z, max z (x1000)
};

struct Draw {
    Target target;
    uint32_t pipeline;
    uint32_t vertexBuffers[4]; // buffer ids, or 0: the frame's data
    uint32_t vertexOffsets[4]; // bytes (into the buffer, or the frame's data)
    uint32_t indexBuffer;      // 0 with indexOffset != ~0u: the frame's data
    uint32_t indexOffset;      // ~0u: not indexed
    uint32_t count;            // vertices (or indices)
    uint32_t first;            // first vertex (or index)
    int32_t baseVertex;
    uint32_t drawUniforms;     // offsets in the frame's data
    uint32_t lightingUniforms; // (~0u: none)
    uint32_t textures[8];
    Sampler samplers[8];
    float viewport[6];         // x, y, width, height, min z, max z
    int32_t scissor[4];        // (all 0: off)
    uint32_t stencilRef;
    uint32_t blendFactor;      // D3DRS_BLENDFACTOR
};

struct Present {
    uint32_t backBuffer;
};

struct Stretch {
    uint32_t source, destination; // surfaces
    int32_t sourceRect[4], destinationRect[4]; // left, top, right, bottom (-1: the whole surface)
    uint32_t linear;
};

// --- Frames --------------------------------------------------------------------------------------------------------

struct Frame {
    std::vector<uint8_t> commands;
    std::vector<uint8_t> data;
    uint64_t number = 0;
    double recordedMs = 0; // the game's time for the frame (from the last Present to this one)

    template <typename T>
    void Add(Op op, const T& record, const void* extra = nullptr, uint32_t extraSize = 0)
    {
        Header header{op, static_cast<uint32_t>(sizeof(Header) + sizeof(T) + extraSize)};
        size_t at = commands.size();
        commands.resize(at + header.size);
        std::memcpy(commands.data() + at, &header, sizeof(header));
        std::memcpy(commands.data() + at + sizeof(header), &record, sizeof(T));
        if (extraSize)
            std::memcpy(commands.data() + at + sizeof(header) + sizeof(T), extra, extraSize);
    }
    // Bytes into the data (aligned to `alignment`); returns their offset.
    uint32_t Put(const void* bytes, size_t size, size_t alignment = 4)
    {
        size_t at = (data.size() + alignment - 1) & ~(alignment - 1);
        data.resize(at + size);
        if (bytes && size)
            std::memcpy(data.data() + at, bytes, size);
        return static_cast<uint32_t>(at);
    }
    void Clear()
    {
        commands.clear();
        data.clear();
    }
};

// Frames handed from the game's thread to the page's: at most `depth` waiting (the game waits for room at Present -
// the page's refresh paces it, as a swap chain's would).
class FrameQueue {
public:
    explicit FrameQueue(size_t depth) : m_depth(depth) {}

    void Push(std::unique_ptr<Frame> frame)
    {
        std::unique_lock lock(m_lock);
        m_room.wait(lock, [this] { return m_frames.size() < m_depth; });
        m_frames.push_back(std::move(frame));
    }
    // The oldest frame, or null (never waits: the page's thread).
    std::unique_ptr<Frame> Pop()
    {
        std::lock_guard lock(m_lock);
        if (m_frames.empty())
            return nullptr;
        auto frame = std::move(m_frames.front());
        m_frames.pop_front();
        m_room.notify_all();
        return frame;
    }
    // A drawn frame's buffers back, for the game's thread to reuse.
    void Recycle(std::unique_ptr<Frame> frame)
    {
        std::lock_guard lock(m_lock);
        if (m_spare.size() < 4)
            m_spare.push_back(std::move(frame));
    }
    std::unique_ptr<Frame> Spare()
    {
        std::lock_guard lock(m_lock);
        if (m_spare.empty())
            return std::make_unique<Frame>();
        auto frame = std::move(m_spare.back());
        m_spare.pop_back();
        frame->Clear();
        return frame;
    }

private:
    size_t m_depth;
    std::mutex m_lock;
    std::condition_variable m_room;
    std::deque<std::unique_ptr<Frame>> m_frames;
    std::vector<std::unique_ptr<Frame>> m_spare;
};

FrameQueue& Frames();

} // namespace Gpu
