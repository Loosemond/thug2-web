// The headless run's Direct3D 9 report (THUG2_D3D_TRACE=<frames>): every different way the game draws - the vertex
// layout, the pixel shader, the render and texture stage states that matter, the textures' formats - counted over the
// first <frames> frames, then printed once. What the WebGPU backend has to cover.

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

#include <emscripten/em_js.h>

#include "d3d9/device.h"

namespace {

using namespace D3D9;

std::string Hex(DWORD value)
{
    char text[16];
    std::snprintf(text, sizeof(text), "%X", static_cast<unsigned>(value));
    return text;
}

std::string Layout(const Device& device)
{
    if (VertexDeclaration* declaration = device.Declaration()) {
        std::string text = "decl[";
        for (const D3DVERTEXELEMENT9& e : declaration->Elements()) {
            if (e.Stream == 0xFF)
                break;
            char item[48];
            std::snprintf(item, sizeof(item), "s%u+%u t%u u%u.%u ", e.Stream, e.Offset, e.Type, e.Usage, e.UsageIndex);
            text += item;
        }
        return text + "]";
    }
    return "fvf " + Hex(device.Fvf());
}

const char* FormatName(D3DFORMAT format)
{
    switch (format) {
    case D3DFMT_A8R8G8B8: return "ARGB8";
    case D3DFMT_X8R8G8B8: return "XRGB8";
    case D3DFMT_R5G6B5: return "565";
    case D3DFMT_A1R5G5B5: return "1555";
    case D3DFMT_A4R4G4B4: return "4444";
    case D3DFMT_A8: return "A8";
    case D3DFMT_L8: return "L8";
    case D3DFMT_A8L8: return "A8L8";
    case D3DFMT_P8: return "P8";
    case D3DFMT_DXT1: return "DXT1";
    case D3DFMT_DXT2: return "DXT2";
    case D3DFMT_DXT3: return "DXT3";
    case D3DFMT_DXT4: return "DXT4";
    case D3DFMT_DXT5: return "DXT5";
    default: return nullptr;
    }
}

std::string TextureText(IDirect3DBaseTexture9* texture)
{
    if (!texture)
        return "-";
    D3DSURFACE_DESC desc = {};
    switch (texture->GetType()) {
    case D3DRTYPE_TEXTURE: static_cast<IDirect3DTexture9*>(texture)->GetLevelDesc(0, &desc); break;
    case D3DRTYPE_CUBETEXTURE: static_cast<IDirect3DCubeTexture9*>(texture)->GetLevelDesc(0, &desc); break;
    default: return "?";
    }
    const char* name = FormatName(desc.Format);
    std::string text = name ? name : "fmt" + std::to_string(desc.Format);
    if (desc.Usage & D3DUSAGE_RENDERTARGET)
        text += "/rt";
    if (texture->GetType() == D3DRTYPE_CUBETEXTURE)
        text += "/cube";
    return text;
}

class TraceBackend final : public Backend {
public:
    explicit TraceBackend(uint64_t frames) : m_frames(frames) {}

    void Clear(Device& device, DWORD count, const D3DRECT*, DWORD flags, D3DCOLOR color, float, DWORD) override
    {
        if (Tracing(device))
            ++m_states["clear flags " + Hex(flags) + (count ? " rects" : "") + " color " + Hex(color)];
    }

    void Draw(Device& device, const DrawCall& call) override
    {
        if (!Tracing(device))
            return;
        std::string key = Layout(device);
        key += call.userVertices ? " UP" : "";
        key += " prim" + std::to_string(call.type);
        if (PixelShader* shader = device.CurrentPixelShader()) {
            const auto& tokens = shader->Tokens();
            std::string source = tokens.size() > 3 ? reinterpret_cast<const char*>(&tokens[3]) : "?";
            for (char& c : source)
                if (c == '\n' || c == '\t')
                    c = ' ';
            key += "\n    ps: " + source;
        }
        if (device.CurrentVertexShader())
            key += "\n    vs: (game's own)";
        auto rs = [&](D3DRENDERSTATETYPE state) { return Hex(device.RenderState(state)); };
        key += "\n    blend " + rs(D3DRS_ALPHABLENDENABLE) + " " + rs(D3DRS_SRCBLEND) + "/" + rs(D3DRS_DESTBLEND) +
               " op" + rs(D3DRS_BLENDOP) + " atest " + rs(D3DRS_ALPHATESTENABLE) + " f" + rs(D3DRS_ALPHAFUNC) +
               " z " + rs(D3DRS_ZENABLE) + rs(D3DRS_ZWRITEENABLE) + " f" + rs(D3DRS_ZFUNC) + " cull " +
               rs(D3DRS_CULLMODE) + " fog " + rs(D3DRS_FOGENABLE) + " t" + rs(D3DRS_FOGTABLEMODE) + " v" +
               rs(D3DRS_FOGVERTEXMODE) + " light " + rs(D3DRS_LIGHTING) + " spec " + rs(D3DRS_SPECULARENABLE) +
               " cv " + rs(D3DRS_COLORVERTEX) + " src " + rs(D3DRS_DIFFUSEMATERIALSOURCE) +
               rs(D3DRS_AMBIENTMATERIALSOURCE) + rs(D3DRS_EMISSIVEMATERIALSOURCE) + " vblend " +
               rs(D3DRS_VERTEXBLEND) + rs(D3DRS_INDEXEDVERTEXBLENDENABLE) + " stencil " + rs(D3DRS_STENCILENABLE) +
               " cw " + rs(D3DRS_COLORWRITEENABLE) + " tg " + rs(D3DRS_NORMALIZENORMALS);
        if (device.RenderState(D3DRS_LIGHTING)) {
            int lights = 0;
            for (const auto& [index, on] : device.EnabledLights())
                lights += on;
            key += " lights " + std::to_string(lights);
        }
        for (DWORD stage = 0; stage < 8; ++stage) {
            auto ts = [&](D3DTEXTURESTAGESTATETYPE type) { return Hex(device.TextureStageState(stage, type)); };
            bool shaderStage = device.CurrentPixelShader() != nullptr;
            if (!shaderStage && device.TextureStageState(stage, D3DTSS_COLOROP) == D3DTOP_DISABLE)
                break;
            if (shaderStage && !device.BoundTexture(stage))
                break;
            key += "\n    t" + std::to_string(stage) + " " + TextureText(device.BoundTexture(stage));
            if (!shaderStage)
                key += " c" + ts(D3DTSS_COLOROP) + "(" + ts(D3DTSS_COLORARG1) + "," + ts(D3DTSS_COLORARG2) + ") a" +
                       ts(D3DTSS_ALPHAOP) + "(" + ts(D3DTSS_ALPHAARG1) + "," + ts(D3DTSS_ALPHAARG2) + ")";
            key += " tci " + ts(D3DTSS_TEXCOORDINDEX) + " ttf " + ts(D3DTSS_TEXTURETRANSFORMFLAGS) + " addr " +
                   Hex(device.SamplerState(stage, D3DSAMP_ADDRESSU)) +
                   Hex(device.SamplerState(stage, D3DSAMP_ADDRESSV)) + " filt " +
                   Hex(device.SamplerState(stage, D3DSAMP_MINFILTER)) +
                   Hex(device.SamplerState(stage, D3DSAMP_MAGFILTER)) +
                   Hex(device.SamplerState(stage, D3DSAMP_MIPFILTER));
        }
        if (Surface* target = device.RenderTarget(); target && target->Width() != device.PresentParameters().BackBufferWidth)
            key += "\n    target " + std::to_string(target->Width()) + "x" + std::to_string(target->Height());
        ++m_states[key];
        ++m_draws;
    }

    void Present(Device& device) override
    {
        if (device.FrameNumber() + 1 == m_frames)
            Report();
    }

private:
    bool Tracing(const Device& device) const { return device.FrameNumber() < m_frames; }

    void Report()
    {
        std::printf("\n=== D3D9 trace: %llu frames, %llu draws, %zu kinds ===\n",
                    static_cast<unsigned long long>(m_frames), static_cast<unsigned long long>(m_draws),
                    m_states.size());
        for (const auto& [key, count] : m_states)
            std::printf("%6llu  %s\n", static_cast<unsigned long long>(count), key.c_str());
        std::printf("=== end of trace ===\n\n");
        std::fflush(stdout);
    }

    uint64_t m_frames;
    uint64_t m_draws = 0;
    std::map<std::string, uint64_t> m_states;
};

// (Node's environment, which getenv doesn't see)
EM_JS(int, TraceFrames, (), {
    return typeof process != "undefined" && process.env.THUG2_D3D_TRACE ? parseInt(process.env.THUG2_D3D_TRACE, 10) : 0;
});

__attribute__((constructor)) void InstallTrace()
{
    if (int frames = TraceFrames()) {
        static TraceBackend backend(static_cast<uint64_t>(frames));
        Device::SetBackend(&backend);
    }
}

} // namespace
