// Direct3D 9's fixed-function pipeline and ps.1.x pixel shaders as WGSL (see gpu_shaders.h).

#include "web/gpu_shaders.h"

#include <d3d9.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace Gpu {

namespace {

const char kCommon[] = R"(
struct Draw {
    worldView: array<mat4x4f, 4>,
    projection: mat4x4f,
    normal: array<mat4x4f, 4>,
    texture: array<mat4x4f, 8>,
    screen: vec4f,
    pixelOffset: vec4f,
    fog: vec4f,
    fogColor: vec4f,
    textureFactor: vec4f,
    alphaRef: vec4f,
    constants: array<vec4f, 8>,
};
struct Light {
    diffuse: vec4f,
    specular: vec4f,
    ambient: vec4f,
    position: vec4f,
    direction: vec4f,
    attenuation: vec4f,
    spot: vec4f,
};
struct Lighting {
    diffuse: vec4f,
    ambient: vec4f,
    specular: vec4f,
    emissive: vec4f,
    power: vec4f,
    globalAmbient: vec4f,
    lights: array<Light, 8>,
};
@group(0) @binding(0) var<uniform> u: Draw;
@group(0) @binding(1) var<uniform> l: Lighting;
)";

// The vertex element as a vec4f (Direct3D's defaults for what it lacks: 0, 0, 0, 1).
std::string AsVec4(uint8_t type, const std::string& name)
{
    switch (type) {
    case D3DDECLTYPE_FLOAT1: return "vec4f(" + name + ", 0.0, 0.0, 1.0)";
    case D3DDECLTYPE_FLOAT2:
    case D3DDECLTYPE_SHORT2N:
    case D3DDECLTYPE_USHORT2N:
    case D3DDECLTYPE_FLOAT16_2: return "vec4f(" + name + ", 0.0, 1.0)";
    case D3DDECLTYPE_SHORT2: return "vec4f(vec2f(" + name + "), 0.0, 1.0)";
    case D3DDECLTYPE_FLOAT3: return "vec4f(" + name + ", 1.0)";
    case D3DDECLTYPE_D3DCOLOR: return name + ".zyxw"; // (its bytes are B, G, R, A)
    case D3DDECLTYPE_UBYTE4:
    case D3DDECLTYPE_SHORT4: return "vec4f(" + name + ")";
    default: return name;
    }
}

const char* WgslType(uint8_t type)
{
    switch (type) {
    case D3DDECLTYPE_FLOAT1: return "f32";
    case D3DDECLTYPE_FLOAT2:
    case D3DDECLTYPE_SHORT2N:
    case D3DDECLTYPE_USHORT2N:
    case D3DDECLTYPE_FLOAT16_2: return "vec2f";
    case D3DDECLTYPE_SHORT2: return "vec2i";
    case D3DDECLTYPE_FLOAT3: return "vec3f";
    case D3DDECLTYPE_UBYTE4: return "vec4u";
    case D3DDECLTYPE_SHORT4: return "vec4i";
    default: return "vec4f";
    }
}

std::string Num(float value)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.9g", value);
    std::string s = text;
    if (s.find_first_of(".eEn") == std::string::npos)
        s += ".0";
    return s;
}

// --- The vertex shader ---------------------------------------------------------------------------------------------

struct Inputs {
    int position = -1, blendWeight = -1, normal = -1, color0 = -1, color1 = -1;
    int texcoord[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    int texcoordSize[8] = {};
};

int ComponentCount(uint8_t type)
{
    switch (type) {
    case D3DDECLTYPE_FLOAT1: return 1;
    case D3DDECLTYPE_FLOAT2:
    case D3DDECLTYPE_SHORT2:
    case D3DDECLTYPE_SHORT2N:
    case D3DDECLTYPE_USHORT2N:
    case D3DDECLTYPE_FLOAT16_2: return 2;
    case D3DDECLTYPE_FLOAT3: return 3;
    default: return 4;
    }
}

Inputs FindInputs(const ShaderKey& key)
{
    Inputs in;
    for (int i = 0; i < key.elementCount; ++i) {
        const VertexElement& e = key.elements[i];
        switch (e.usage) {
        case D3DDECLUSAGE_POSITION:
        case D3DDECLUSAGE_POSITIONT:
            if (e.usageIndex == 0)
                in.position = i;
            break;
        case D3DDECLUSAGE_BLENDWEIGHT: in.blendWeight = i; break;
        case D3DDECLUSAGE_NORMAL:
            if (e.usageIndex == 0)
                in.normal = i;
            break;
        case D3DDECLUSAGE_COLOR:
            if (e.usageIndex == 0)
                in.color0 = i;
            else if (e.usageIndex == 1)
                in.color1 = i;
            break;
        case D3DDECLUSAGE_TEXCOORD:
            if (e.usageIndex < 8) {
                in.texcoord[e.usageIndex] = i;
                in.texcoordSize[e.usageIndex] = ComponentCount(e.type);
            }
            break;
        default: break;
        }
    }
    return in;
}

// The stages whose coordinates the pixel side reads.
bool StageUsed(const ShaderKey& key, int stage)
{
    return key.pixelShader ? key.stages[stage].texture != 0 : stage < key.stageCount;
}

void VertexShader(const ShaderKey& key, std::ostringstream& s)
{
    Inputs in = FindInputs(key);
    s << "struct VertexIn {\n";
    for (int i = 0; i < key.elementCount; ++i)
        s << "    @location(" << i << ") a" << i << ": " << WgslType(key.elements[i].type) << ",\n";
    s << "};\n";
    s << "struct Varyings {\n    @builtin(position) position: vec4f,\n    @location(0) color0: vec4f,\n"
         "    @location(1) color1: vec4f,\n    @location(2) fog: f32,\n";
    for (int i = 0; i < 8; ++i)
        if (StageUsed(key, i))
            s << "    @location(" << 3 + i << ") tc" << i << ": vec4f,\n";
    s << "};\n\n";

    s << "@vertex fn vs(v: VertexIn) -> Varyings {\n    var out: Varyings;\n";
    auto input = [&](int index) { return AsVec4(key.elements[index].type, "v.a" + std::to_string(index)); };
    s << "    let p = " << (in.position >= 0 ? input(in.position) : "vec4f(0.0, 0.0, 0.0, 1.0)") << ";\n";
    s << "    let c0 = " << (in.color0 >= 0 ? input(in.color0) : "vec4f(1.0)") << ";\n";
    s << "    let c1 = " << (in.color1 >= 0 ? input(in.color1) : "vec4f(0.0)") << ";\n";

    bool needsView = !key.pretransformed;
    if (key.pretransformed) {
        // Screen pixels (Direct3D's centres at whole numbers): to clip space, w the reciprocal of rhw.
        s << "    let w = select(1.0 / p.w, 1.0, p.w == 0.0);\n";
        s << "    let ndc = (p.xy + u.screen.zw) * u.screen.xy + vec2f(-1.0, 1.0);\n";
        s << "    out.position = vec4f(ndc * w, p.z * w, w);\n";
        s << "    let viewPos = vec3f(0.0);\n    let n = vec3f(0.0, 0.0, 1.0);\n";
    } else {
        if (key.vertexBlend) {
            // Weights: the given ones, the last 1 - their sum.
            int matrices = key.vertexBlend + 1;
            s << "    let bw = " << (in.blendWeight >= 0 ? input(in.blendWeight) : "vec4f(1.0, 0.0, 0.0, 0.0)")
              << ";\n";
            s << "    var weights = array<f32, 4>(0.0, 0.0, 0.0, 0.0);\n    var rest = 1.0;\n";
            for (int i = 0; i + 1 < matrices; ++i)
                s << "    weights[" << i << "] = bw[" << i << "];\n    rest -= bw[" << i << "];\n";
            s << "    weights[" << matrices - 1 << "] = rest;\n";
            s << "    var vp = vec4f(0.0);\n    var vn = vec3f(0.0);\n";
            s << "    for (var i = 0; i < " << matrices << "; i++) {\n";
            s << "        vp += weights[i] * (u.worldView[i] * vec4f(p.xyz, 1.0));\n";
            if (in.normal >= 0)
                s << "        vn += weights[i] * (u.normal[i] * vec4f(v.a" << in.normal << ", 0.0)).xyz;\n";
            s << "    }\n    let viewPos = vp.xyz;\n";
            s << "    var n = vn;\n";
        } else {
            s << "    let viewPos = (u.worldView[0] * vec4f(p.xyz, 1.0)).xyz;\n";
            if (in.normal >= 0)
                s << "    var n = (u.normal[0] * vec4f(v.a" << in.normal << ", 0.0)).xyz;\n";
            else
                s << "    var n = vec3f(0.0, 0.0, 0.0);\n";
        }
        if (key.normalizeNormals)
            s << "    n = normalize(n);\n";
        s << "    var clip = u.projection * vec4f(viewPos, 1.0);\n";
        s << "    clip = vec4f(clip.xy + u.pixelOffset.xy * clip.w, clip.zw);\n";
        s << "    out.position = clip;\n";
    }
    (void)needsView;

    // Colours: lit, or the vertex's.
    if (key.lighting) {
        auto source = [&](uint8_t src, const char* material) -> std::string {
            if (key.colorVertex && src == D3DMCS_COLOR1 && in.color0 >= 0)
                return "c0";
            if (key.colorVertex && src == D3DMCS_COLOR2 && in.color1 >= 0)
                return "c1";
            return std::string("l.") + material;
        };
        s << "    let md = " << source(key.diffuseSource, "diffuse") << ";\n";
        s << "    let ma = " << source(key.ambientSource, "ambient") << ";\n";
        s << "    let ms = " << source(key.specularSource, "specular") << ";\n";
        s << "    let me = " << source(key.emissiveSource, "emissive") << ";\n";
        s << "    var ambient = l.globalAmbient.rgb;\n    var diffuse = vec3f(0.0);\n    var specular = vec3f(0.0);\n";
        s << "    let toEye = " << (key.localViewer ? "normalize(-viewPos)" : "vec3f(0.0, 0.0, 1.0)") << ";\n";
        for (int i = 0; i < key.lightCount; ++i) {
            std::string L = "l.lights[" + std::to_string(i) + "]";
            s << "    {\n";
            if (key.lightTypes[i] == D3DLIGHT_DIRECTIONAL) {
                s << "        let toLight = -" << L << ".direction.xyz;\n        let att = 1.0;\n";
            } else {
                s << "        let d = " << L << ".position.xyz - viewPos;\n        let dist = length(d);\n";
                s << "        let toLight = d / max(dist, 1e-6);\n";
                s << "        var att = select(0.0, 1.0 / max(" << L << ".attenuation.x + " << L
                  << ".attenuation.y * dist + " << L << ".attenuation.z * dist * dist, 1e-6), dist <= " << L
                  << ".position.w);\n";
                if (key.lightTypes[i] == D3DLIGHT_SPOT) {
                    s << "        let rho = dot(-toLight, " << L << ".direction.xyz);\n";
                    s << "        let spot = select(select(pow(max((rho - " << L << ".spot.z) * " << L
                      << ".spot.y, 0.0), " << L << ".direction.w), 0.0, rho <= " << L << ".spot.z), 1.0, rho > "
                      << L << ".spot.x);\n";
                    s << "        att *= spot;\n";
                }
            }
            s << "        let nl = max(dot(n, toLight), 0.0);\n";
            s << "        ambient += " << L << ".ambient.rgb * att;\n";
            s << "        diffuse += " << L << ".diffuse.rgb * nl * att;\n";
            if (key.specular)
                s << "        if (nl > 0.0) { specular += " << L << ".specular.rgb * pow(max(dot(n, normalize(toLight + "
                     "toEye)), 0.0), l.power.x) * att; }\n";
            s << "    }\n";
        }
        s << "    out.color0 = clamp(vec4f(me.rgb + ambient * ma.rgb + diffuse * md.rgb, md.a), vec4f(0.0), "
             "vec4f(1.0));\n";
        s << "    out.color1 = clamp(vec4f(specular * ms.rgb, c1.a), vec4f(0.0), vec4f(1.0));\n";
    } else {
        s << "    out.color0 = c0;\n    out.color1 = c1;\n";
    }

    // Fog: the vertex's factor (vertex fog, or the specular alpha), or the eye distance for the pixel's (table) fog.
    if (key.fogMode >= 1 && key.fogMode <= 3)
        s << "    out.fog = " << (key.pretransformed ? "p.z" : (key.rangeFog ? "length(viewPos)" : "abs(viewPos.z)"))
          << ";\n";
    else if (key.fogMode >= 5 && key.fogMode <= 7) {
        s << "    let fd = " << (key.rangeFog ? "length(viewPos)" : "abs(viewPos.z)") << ";\n";
        if (key.fogMode == 4 + D3DFOG_LINEAR)
            s << "    out.fog = clamp((u.fog.y - fd) * u.fog.w, 0.0, 1.0);\n";
        else if (key.fogMode == 4 + D3DFOG_EXP)
            s << "    out.fog = clamp(exp(-u.fog.z * fd), 0.0, 1.0);\n";
        else
            s << "    out.fog = clamp(exp(-(u.fog.z * fd) * (u.fog.z * fd)), 0.0, 1.0);\n";
    } else if (key.fogMode == 8)
        s << "    out.fog = out.color1.a;\n";
    else
        s << "    out.fog = 1.0;\n";

    // Texture coordinates per stage: the vertex's set or generated, then transformed.
    for (int i = 0; i < 8; ++i) {
        if (!StageUsed(key, i))
            continue;
        const Stage& st = key.stages[i];
        std::string tc = "out.tc" + std::to_string(i);
        int size = 4;
        switch (st.texgen) {
        case 1: s << "    " << tc << " = vec4f(n, 1.0);\n"; size = 3; break;
        case 2: s << "    " << tc << " = vec4f(viewPos, 1.0);\n"; size = 3; break;
        case 3:
        case 4:
            s << "    {\n        let e = normalize(viewPos);\n        let r = e - 2.0 * dot(n, e) * n;\n";
            if (st.texgen == 3)
                s << "        " << tc << " = vec4f(r, 1.0);\n";
            else
                s << "        let m = 2.0 * sqrt(r.x * r.x + r.y * r.y + (r.z + 1.0) * (r.z + 1.0));\n        " << tc
                  << " = vec4f(r.x / m + 0.5, -r.y / m + 0.5, 0.0, 1.0);\n";
            s << "    }\n";
            size = st.texgen == 3 ? 3 : 2;
            break;
        default: {
            int set = st.texcoordIndex & 7;
            if (in.texcoord[set] >= 0) {
                s << "    " << tc << " = " << input(in.texcoord[set]) << ";\n";
                size = in.texcoordSize[set];
            } else {
                s << "    " << tc << " = vec4f(0.0, 0.0, 0.0, 1.0);\n";
                size = 2;
            }
            break;
        }
        }
        int count = st.transform & 0xFF & ~D3DTTFF_PROJECTED;
        if (count) {
            // (the coordinates padded with a 1 after the last: a 2D set's translation is in _31 / _32)
            std::string padded;
            switch (size) {
            case 1: padded = "vec4f(" + tc + ".x, 1.0, 0.0, 0.0)"; break;
            case 2: padded = "vec4f(" + tc + ".xy, 1.0, 0.0)"; break;
            case 3: padded = "vec4f(" + tc + ".xyz, 1.0)"; break;
            default: padded = tc; break;
            }
            s << "    " << tc << " = u.texture[" << i << "] * " << padded << ";\n";
            if (st.transform & D3DTTFF_PROJECTED) {
                const char* last = count == 2 ? "y" : count == 3 ? "z" : "w";
                s << "    " << tc << " = vec4f(" << tc << ".xyz / " << tc << "." << last << ", 1.0);\n";
            }
        }
    }
    s << "    return out;\n}\n\n";
}

// --- The pixel side --------------------------------------------------------------------------------------------------

void Textures(const ShaderKey& key, std::ostringstream& s)
{
    for (int i = 0; i < 8; ++i) {
        if (!key.stages[i].texture || !StageUsed(key, i))
            continue;
        s << "@group(1) @binding(" << i * 2 << ") var t" << i << ": "
          << (key.stages[i].texture == 2 ? "texture_cube<f32>" : "texture_2d<f32>") << ";\n";
        s << "@group(1) @binding(" << i * 2 + 1 << ") var s" << i << ": sampler;\n";
    }
}

// Stage i's texel (sampled once, at the top of the shader: WGSL wants uniform control flow for it).
void SampleTextures(const ShaderKey& key, std::ostringstream& s)
{
    for (int i = 0; i < 8; ++i) {
        if (!StageUsed(key, i))
            continue;
        if (!key.stages[i].texture)
            s << "    let tex" << i << " = vec4f(0.0, 0.0, 0.0, 1.0);\n";
        else if (key.stages[i].texture == 2)
            s << "    let tex" << i << " = textureSample(t" << i << ", s" << i << ", f.tc" << i << ".xyz);\n";
        else
            s << "    let tex" << i << " = textureSample(t" << i << ", s" << i << ", f.tc" << i << ".xy);\n";
    }
}

std::string Arg(uint8_t arg, int stage, bool alpha)
{
    std::string value;
    switch (arg & D3DTA_SELECTMASK) {
    case D3DTA_DIFFUSE: value = "f.color0"; break;
    case D3DTA_CURRENT: value = stage == 0 ? "f.color0" : "current"; break;
    case D3DTA_TEXTURE: value = "tex" + std::to_string(stage); break;
    case D3DTA_TFACTOR: value = "u.textureFactor"; break;
    case D3DTA_SPECULAR: value = "f.color1"; break;
    case D3DTA_TEMP: value = "temp"; break;
    default: value = "vec4f(1.0)"; break; // (D3DTA_CONSTANT: per-stage constants - not used)
    }
    if (arg & D3DTA_ALPHAREPLICATE)
        value = "vec4f(" + value + ".a)";
    if (arg & D3DTA_COMPLEMENT)
        value = "(vec4f(1.0) - " + value + ")";
    (void)alpha;
    return value;
}

std::string Op(uint8_t op, uint8_t a0, uint8_t a1, uint8_t a2, int stage, bool alpha)
{
    std::string x = Arg(a1, stage, alpha), y = Arg(a2, stage, alpha), z = Arg(a0, stage, alpha);
    std::string current = stage == 0 ? "f.color0" : "current";
    std::string tex = "tex" + std::to_string(stage);
    switch (op) {
    case D3DTOP_SELECTARG1: return x;
    case D3DTOP_SELECTARG2: return y;
    case D3DTOP_MODULATE: return x + " * " + y;
    case D3DTOP_MODULATE2X: return "(" + x + " * " + y + " * 2.0)";
    case D3DTOP_MODULATE4X: return "(" + x + " * " + y + " * 4.0)";
    case D3DTOP_ADD: return "(" + x + " + " + y + ")";
    case D3DTOP_ADDSIGNED: return "(" + x + " + " + y + " - vec4f(0.5))";
    case D3DTOP_ADDSIGNED2X: return "((" + x + " + " + y + " - vec4f(0.5)) * 2.0)";
    case D3DTOP_SUBTRACT: return "(" + x + " - " + y + ")";
    case D3DTOP_ADDSMOOTH: return "(" + x + " + " + y + " - " + x + " * " + y + ")";
    case D3DTOP_BLENDDIFFUSEALPHA: return "mix(" + y + ", " + x + ", f.color0.a)";
    case D3DTOP_BLENDTEXTUREALPHA: return "mix(" + y + ", " + x + ", " + tex + ".a)";
    case D3DTOP_BLENDFACTORALPHA: return "mix(" + y + ", " + x + ", u.textureFactor.a)";
    case D3DTOP_BLENDTEXTUREALPHAPM: return "(" + x + " + " + y + " * (1.0 - " + tex + ".a))";
    case D3DTOP_BLENDCURRENTALPHA: return "mix(" + y + ", " + x + ", " + current + ".a)";
    case D3DTOP_MODULATEALPHA_ADDCOLOR: return "(" + x + " + " + x + ".a * " + y + ")";
    case D3DTOP_MODULATECOLOR_ADDALPHA: return "(" + x + " * " + y + " + vec4f(" + x + ".a))";
    case D3DTOP_MODULATEINVALPHA_ADDCOLOR: return "(" + x + " + (1.0 - " + x + ".a) * " + y + ")";
    case D3DTOP_MODULATEINVCOLOR_ADDALPHA: return "((vec4f(1.0) - " + x + ") * " + y + " + vec4f(" + x + ".a))";
    case D3DTOP_DOTPRODUCT3:
        return "vec4f(saturate(dot((" + x + ".rgb - vec3f(0.5)) * 2.0, (" + y + ".rgb - vec3f(0.5)) * 2.0)))";
    case D3DTOP_MULTIPLYADD: return "(" + z + " + " + x + " * " + y + ")";
    case D3DTOP_LERP: return "mix(" + y + ", " + x + ", " + z + ")"; // (arg0 * arg1 + (1 - arg0) * arg2)
    default: return x;
    }
}

void FixedStages(const ShaderKey& key, std::ostringstream& s)
{
    s << "    var current = f.color0;\n    var temp = vec4f(0.0);\n";
    for (int i = 0; i < key.stageCount; ++i) {
        const Stage& st = key.stages[i];
        std::string color = Op(st.colorOp, st.colorArg0, st.colorArg1, st.colorArg2, i, false);
        std::string alpha = st.alphaOp == D3DTOP_DISABLE
                                ? (i == 0 ? "f.color0" : "current")
                                : Op(st.alphaOp, st.alphaArg0, st.alphaArg1, st.alphaArg2, i, true);
        std::string target = (st.resultArg & D3DTA_SELECTMASK) == D3DTA_TEMP ? "temp" : "current";
        s << "    " << target << " = saturate(vec4f((" << color << ").rgb, (" << alpha << ").a));\n";
    }
    s << "    var color = current;\n";
    if (key.specular)
        s << "    color = vec4f(saturate(color.rgb + f.color1.rgb), color.a);\n";
}

// --- ps.1.x ------------------------------------------------------------------------------------------------------------

std::string Trim(std::string text)
{
    size_t a = text.find_first_not_of(" \t\r");
    size_t b = text.find_last_not_of(" \t\r");
    return a == std::string::npos ? std::string() : text.substr(a, b - a + 1);
}

std::vector<std::string> SplitOperands(const std::string& text)
{
    std::vector<std::string> out;
    std::string current;
    for (char c : text) {
        if (c == ',') {
            out.push_back(Trim(current));
            current.clear();
        } else {
            current += c;
        }
    }
    if (!Trim(current).empty())
        out.push_back(Trim(current));
    return out;
}

// A source operand: register, swizzle and modifiers.
std::string Source(std::string operand, const std::vector<std::string>& defined)
{
    bool negate = false, complement = false;
    if (operand.rfind("1-", 0) == 0) {
        complement = true;
        operand = Trim(operand.substr(2));
    }
    if (!operand.empty() && operand[0] == '-') {
        negate = true;
        operand = Trim(operand.substr(1));
    }
    std::string swizzle;
    size_t dot = operand.find('.');
    if (dot != std::string::npos) {
        swizzle = operand.substr(dot + 1);
        operand = operand.substr(0, dot);
    }
    std::string modifier;
    size_t underscore = operand.find('_');
    if (underscore != std::string::npos) {
        modifier = operand.substr(underscore + 1);
        operand = operand.substr(0, underscore);
    }
    for (char& c : operand)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string value;
    char kind = operand.empty() ? '?' : operand[0];
    int index = operand.size() > 1 ? std::atoi(operand.c_str() + 1) : 0;
    switch (kind) {
    case 'r': value = "r" + std::to_string(index); break;
    case 't': value = "t" + std::to_string(index); break;
    case 'v': value = index == 0 ? "f.color0" : "f.color1"; break;
    case 'c':
        value = index < static_cast<int>(defined.size()) && !defined[index].empty()
                    ? defined[index]
                    : "u.constants[" + std::to_string(index) + "]";
        break;
    default: value = "vec4f(0.0)"; break;
    }
    if (swizzle == "a" || swizzle == "aaaa" || swizzle == "w")
        value = "vec4f(" + value + ".a)";
    else if (swizzle == "b" || swizzle == "bbbb" || swizzle == "z")
        value = "vec4f(" + value + ".b)";
    else if (swizzle == "r" || swizzle == "x")
        value = "vec4f(" + value + ".r)";
    else if (swizzle == "g" || swizzle == "y")
        value = "vec4f(" + value + ".g)";
    if (modifier == "bias")
        value = "(" + value + " - vec4f(0.5))";
    else if (modifier == "bx2")
        value = "((" + value + " - vec4f(0.5)) * 2.0)";
    else if (modifier == "x2")
        value = "(" + value + " * 2.0)";
    if (complement)
        value = "(vec4f(1.0) - " + value + ")";
    if (negate)
        value = "(-" + value + ")";
    return value;
}

// The ps.1.x text as WGSL statements ending with `var color` (r0). Returns false for what it can't read.
bool TranslatePixelShader(const char* text, const ShaderKey& key, std::ostringstream& s)
{
    std::vector<std::string> defined(8);
    std::ostringstream body;
    body << "    var r0 = vec4f(0.0);\n    var r1 = vec4f(0.0);\n";
    for (int i = 0; i < 4; ++i)
        body << "    var t" << i << " = " << (StageUsed(key, i) ? "tex" + std::to_string(i) : "vec4f(0.0)") << ";\n";
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        size_t comment = line.find_first_of(";");
        if (comment != std::string::npos)
            line = line.substr(0, comment);
        comment = line.find("//");
        if (comment != std::string::npos)
            line = line.substr(0, comment);
        line = Trim(line);
        if (line.empty())
            continue;
        if (line[0] == '+')
            line = Trim(line.substr(1)); // (a co-issued instruction: runs in turn here)
        size_t space = line.find_first_of(" \t");
        std::string opcode = line.substr(0, space);
        std::string operandText = space == std::string::npos ? "" : line.substr(space + 1);
        for (char& c : opcode)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (opcode.rfind("ps", 0) == 0)
            continue;
        std::vector<std::string> ops = SplitOperands(operandText);
        if (opcode == "def") {
            if (ops.size() == 5 && ops[0].size() > 1) {
                int index = std::atoi(ops[0].c_str() + 1);
                if (index >= 0 && index < 8)
                    defined[index] = "vec4f(" + Num(std::strtof(ops[1].c_str(), nullptr)) + ", " +
                                     Num(std::strtof(ops[2].c_str(), nullptr)) + ", " +
                                     Num(std::strtof(ops[3].c_str(), nullptr)) + ", " +
                                     Num(std::strtof(ops[4].c_str(), nullptr)) + ")";
            }
            continue;
        }
        if (opcode == "tex" || opcode == "texld")
            continue; // (t<n> holds stage n's texel already)
        // The instruction's own modifiers: mul_x2, add_sat, ...
        std::string base = opcode, scale;
        bool saturate = false;
        size_t underscore;
        while ((underscore = base.rfind('_')) != std::string::npos) {
            std::string modifier = base.substr(underscore + 1);
            base = base.substr(0, underscore);
            if (modifier == "sat")
                saturate = true;
            else if (modifier == "x2")
                scale = " * 2.0";
            else if (modifier == "x4")
                scale = " * 4.0";
            else if (modifier == "x8")
                scale = " * 8.0";
            else if (modifier == "d2")
                scale = " * 0.5";
            else if (modifier == "d4")
                scale = " * 0.25";
            else if (modifier == "d8")
                scale = " * 0.125";
        }
        if (ops.empty())
            return false;
        std::string destination = ops[0], mask;
        size_t dot = destination.find('.');
        if (dot != std::string::npos) {
            mask = destination.substr(dot + 1);
            destination = destination.substr(0, dot);
        }
        for (char& c : destination)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        std::vector<std::string> src;
        for (size_t i = 1; i < ops.size(); ++i)
            src.push_back(Source(ops[i], defined));
        std::string value;
        if (base == "mov" && src.size() >= 1)
            value = src[0];
        else if (base == "add" && src.size() >= 2)
            value = src[0] + " + " + src[1];
        else if (base == "sub" && src.size() >= 2)
            value = src[0] + " - " + src[1];
        else if (base == "mul" && src.size() >= 2)
            value = src[0] + " * " + src[1];
        else if (base == "mad" && src.size() >= 3)
            value = src[0] + " * " + src[1] + " + " + src[2];
        else if (base == "lrp" && src.size() >= 3)
            value = "mix(" + src[2] + ", " + src[1] + ", " + src[0] + ")";
        else if (base == "dp3" && src.size() >= 2)
            value = "vec4f(dot((" + src[0] + ").rgb, (" + src[1] + ").rgb))";
        else if (base == "dp4" && src.size() >= 2)
            value = "vec4f(dot(" + src[0] + ", " + src[1] + "))";
        else if (base == "cnd" && src.size() >= 3)
            value = "select(" + src[2] + ", " + src[1] + ", (" + src[0] + ").a > 0.5)";
        else if (base == "cmp" && src.size() >= 3)
            value = "select(" + src[2] + ", " + src[1] + ", " + src[0] + " >= vec4f(0.0))";
        else if (base == "nop")
            continue;
        else
            return false;
        value = "(" + value + ")" + scale;
        if (saturate)
            value = "saturate(" + value + ")";
        std::string write = mask.empty() || mask == "rgba" || mask == "xyzw" ? "" : mask;
        if (write == "rgb" || write == "xyz")
            body << "    " << destination << " = vec4f((" << value << ").rgb, " << destination << ".a);\n";
        else if (write == "a" || write == "w")
            body << "    " << destination << " = vec4f(" << destination << ".rgb, (" << value << ").a);\n";
        else
            body << "    " << destination << " = " << value << ";\n";
    }
    s << body.str();
    s << "    var color = saturate(r0);\n";
    return true;
}

void Finish(const ShaderKey& key, std::ostringstream& s)
{
    // Fog (Direct3D 9 fogs after the pixel shader), then the alpha test.
    if (key.fogMode >= 1 && key.fogMode <= 3) {
        if (key.fogMode == D3DFOG_LINEAR)
            s << "    let fogFactor = clamp((u.fog.y - f.fog) * u.fog.w, 0.0, 1.0);\n";
        else if (key.fogMode == D3DFOG_EXP)
            s << "    let fogFactor = clamp(exp(-u.fog.z * f.fog), 0.0, 1.0);\n";
        else
            s << "    let fogFactor = clamp(exp(-(u.fog.z * f.fog) * (u.fog.z * f.fog)), 0.0, 1.0);\n";
        s << "    color = vec4f(mix(u.fogColor.rgb, color.rgb, fogFactor), color.a);\n";
    } else if (key.fogMode) {
        s << "    color = vec4f(mix(u.fogColor.rgb, color.rgb, f.fog), color.a);\n";
    }
    if (key.alphaFunc) {
        static const char* kCompare[9] = {"", "false", "a < r", "a == r", "a <= r", "a > r", "a != r", "a >= r",
                                          "true"};
        s << "    {\n        let a = round(color.a * 255.0);\n        let r = round(u.alphaRef.x * 255.0);\n";
        s << "        if (!(" << kCompare[key.alphaFunc < 9 ? key.alphaFunc : 8] << ")) { discard; }\n    }\n";
    }
    s << "    return color;\n}\n";
}

} // namespace

const char* VertexFormatName(uint8_t type)
{
    switch (type) {
    case D3DDECLTYPE_FLOAT1: return "float32";
    case D3DDECLTYPE_FLOAT2: return "float32x2";
    case D3DDECLTYPE_FLOAT3: return "float32x3";
    case D3DDECLTYPE_FLOAT4: return "float32x4";
    case D3DDECLTYPE_D3DCOLOR: return "unorm8x4";
    case D3DDECLTYPE_UBYTE4: return "uint8x4";
    case D3DDECLTYPE_SHORT2: return "sint16x2";
    case D3DDECLTYPE_SHORT4: return "sint16x4";
    case D3DDECLTYPE_UBYTE4N: return "unorm8x4";
    case D3DDECLTYPE_SHORT2N: return "snorm16x2";
    case D3DDECLTYPE_SHORT4N: return "snorm16x4";
    case D3DDECLTYPE_USHORT2N: return "unorm16x2";
    case D3DDECLTYPE_USHORT4N: return "unorm16x4";
    case D3DDECLTYPE_FLOAT16_2: return "float16x2";
    case D3DDECLTYPE_FLOAT16_4: return "float16x4";
    default: return nullptr;
    }
}

std::string GenerateWgsl(const ShaderKey& key, const char* pixelShader)
{
    std::ostringstream s;
    s << kCommon;
    VertexShader(key, s);
    Textures(key, s);
    s << "@fragment fn fs(f: Varyings) -> @location(0) vec4f {\n";
    SampleTextures(key, s);
    std::ostringstream pixel;
    if (key.pixelShader && pixelShader && TranslatePixelShader(pixelShader, key, pixel))
        s << pixel.str();
    else
        FixedStages(key, s);
    Finish(key, s);
    return s.str();
}

} // namespace Gpu
