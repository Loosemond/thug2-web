// The D3DX9 functions the game calls in the exe's statically linked copy (0x005BEB1A-0x005EEF20, not decompiled -
// docs/library_ranges.md upstream), at their addresses (REPLACE: hook/game.h). Each as documented for D3DX9, in
// float arithmetic. The game's camera code is right-handed (it looks down -z; its "infinite" projection writes
// -0.999999 into _33), so the look-at, perspective and ortho matrices are D3DX's RH ones.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "hook/hook.h"

namespace {

// out = a * b (row vectors); out may be a or b.
void* __stdcall MatrixMultiply(float* out, const float* a, const float* b)
{
    float r[16];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            r[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j] + a[i * 4 + 3] * b[12 + j];
    std::memcpy(out, r, sizeof(r));
    return out;
}

void* __stdcall MatrixTranspose(float* out, const float* m)
{
    float r[16];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            r[i * 4 + j] = m[j * 4 + i];
    std::memcpy(out, r, sizeof(r));
    return out;
}

// The inverse by cofactors; null (out untouched) if the determinant is 0. `determinant` may be null.
void* __stdcall MatrixInverse(float* out, float* determinant, const float* m)
{
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
             m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
             m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
             m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
              m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
             m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
             m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
             m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
              m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
             m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
             m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
              m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
              m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
             m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
              m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] +
              m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (determinant)
        *determinant = det;
    if (det == 0.0f)
        return nullptr;
    float scale = 1.0f / det;
    for (int i = 0; i < 16; ++i)
        out[i] = inv[i] * scale;
    return out;
}

void* __stdcall MatrixScaling(float* out, float x, float y, float z)
{
    const float r[16] = {x, 0, 0, 0, 0, y, 0, 0, 0, 0, z, 0, 0, 0, 0, 1};
    std::memcpy(out, r, sizeof(r));
    return out;
}

void* __stdcall MatrixRotationY(float* out, float angle)
{
    float s = std::sin(angle), c = std::cos(angle);
    const float r[16] = {c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, 0, 0, 0, 1};
    std::memcpy(out, r, sizeof(r));
    return out;
}

void Normalized(float* out, const float* v)
{
    float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    float scale = length != 0.0f ? 1.0f / length : 0.0f;
    out[0] = v[0] * scale;
    out[1] = v[1] * scale;
    out[2] = v[2] * scale;
}

void Cross(float* out, const float* a, const float* b)
{
    float r[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    std::memcpy(out, r, sizeof(r));
}

float Dot(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// D3DXMatrixLookAtRH: z = normalize(eye - at), x = normalize(up x z), y = z x x.
void* __stdcall MatrixLookAtRH(float* out, const float* eye, const float* at, const float* up)
{
    float z[3] = {eye[0] - at[0], eye[1] - at[1], eye[2] - at[2]}, x[3], y[3];
    Normalized(z, z);
    Cross(x, up, z);
    Normalized(x, x);
    Cross(y, z, x);
    const float r[16] = {x[0], y[0], z[0], 0, x[1], y[1], z[1], 0, x[2], y[2], z[2], 0,
                         -Dot(x, eye), -Dot(y, eye), -Dot(z, eye), 1};
    std::memcpy(out, r, sizeof(r));
    return out;
}

// D3DXMatrixPerspectiveRH (the view volume's width and height at the near plane).
void* __stdcall MatrixPerspectiveRH(float* out, float width, float height, float nearZ, float farZ)
{
    const float r[16] = {2 * nearZ / width, 0, 0, 0, 0, 2 * nearZ / height, 0, 0,
                         0, 0, farZ / (nearZ - farZ), -1, 0, 0, nearZ * farZ / (nearZ - farZ), 0};
    std::memcpy(out, r, sizeof(r));
    return out;
}

// D3DXMatrixOrthoRH
void* __stdcall MatrixOrthoRH(float* out, float width, float height, float nearZ, float farZ)
{
    const float r[16] = {2 / width, 0, 0, 0, 0, 2 / height, 0, 0, 0, 0, 1 / (nearZ - farZ), 0,
                         0, 0, nearZ / (nearZ - farZ), 1};
    std::memcpy(out, r, sizeof(r));
    return out;
}

void* __stdcall Vec3Normalize(float* out, const float* v)
{
    Normalized(out, v);
    return out;
}

// D3DXVec3Transform: (x, y, z, 1) * m, a 4-vector.
void* __stdcall Vec3Transform(float* out, const float* v, const float* m)
{
    float r[4];
    for (int j = 0; j < 4; ++j)
        r[j] = v[0] * m[j] + v[1] * m[4 + j] + v[2] * m[8 + j] + m[12 + j];
    std::memcpy(out, r, sizeof(r));
    return out;
}

// D3DXVec3TransformCoord: (x, y, z, 1) * m, divided by w.
void* __stdcall Vec3TransformCoord(float* out, const float* v, const float* m)
{
    float r[4];
    Vec3Transform(r, v, m);
    float scale = 1.0f / r[3];
    out[0] = r[0] * scale;
    out[1] = r[1] * scale;
    out[2] = r[2] * scale;
    return out;
}

// D3DXVec3TransformNormal: (x, y, z, 0) * m.
void* __stdcall Vec3TransformNormal(float* out, const float* v, const float* m)
{
    float r[3];
    for (int j = 0; j < 3; ++j)
        r[j] = v[0] * m[j] + v[1] * m[4 + j] + v[2] * m[8 + j];
    std::memcpy(out, r, sizeof(r));
    return out;
}

// D3DXVec4Transform
void* __stdcall Vec4Transform(float* out, const float* v, const float* m)
{
    float r[4];
    for (int j = 0; j < 4; ++j)
        r[j] = v[0] * m[j] + v[1] * m[4 + j] + v[2] * m[8 + j] + v[3] * m[12 + j];
    std::memcpy(out, r, sizeof(r));
    return out;
}

// An ID3DXBuffer (COM: QueryInterface, AddRef, Release, GetBufferPointer, GetBufferSize).
class Buffer {
public:
    explicit Buffer(std::vector<uint32_t> data) : m_data(std::move(data)) {}
    virtual long __stdcall QueryInterface(const void*, void** object)
    {
        *object = nullptr;
        return static_cast<long>(0x80004002); // E_NOINTERFACE
    }
    virtual unsigned long __stdcall AddRef() { return ++m_references; }
    virtual unsigned long __stdcall Release()
    {
        unsigned long left = --m_references;
        if (!left)
            delete this;
        return left;
    }
    virtual void* __stdcall GetBufferPointer() { return m_data.data(); }
    virtual unsigned long __stdcall GetBufferSize() { return static_cast<unsigned long>(m_data.size() * 4); }
    virtual ~Buffer() = default;

private:
    unsigned long m_references = 1;
    std::vector<uint32_t> m_data;
};

// D3DXAssembleShader, without assembling: the "tokens" are the version token, the source text in a comment ('TEXT'
// then the text, zero-padded) and the end token - a valid token stream the Direct3D 9 layer keeps whole, and whose
// backend translates the text (port/src/d3d9).
long __stdcall AssembleShader(const char* source, unsigned length, const void*, void*, unsigned, void** shader,
                              void** errors)
{
    if (errors)
        *errors = nullptr;
    std::string text(source, length);
    // The version line: the first word that isn't a comment, "ps.1.1" / "vs.1.1".
    size_t start = text.find_first_not_of(" \t\r\n");
    while (start != std::string::npos && (text[start] == ';' || text.compare(start, 2, "//") == 0)) {
        start = text.find('\n', start);
        if (start != std::string::npos)
            start = text.find_first_not_of(" \t\r\n", start);
    }
    if (start == std::string::npos || text.size() - start < 6 || text[start + 1] != 's' || text[start + 2] != '.') {
        if (shader)
            *shader = nullptr;
        return static_cast<long>(0x8876086C); // D3DERR_INVALIDCALL
    }
    uint32_t major = static_cast<uint32_t>(text[start + 3] - '0'), minor = static_cast<uint32_t>(text[start + 5] - '0');
    uint32_t version = (text[start] == 'p' ? 0xFFFF0000u : 0xFFFE0000u) | major << 8 | minor;
    uint32_t words = 1 + static_cast<uint32_t>((text.size() + 4) / 4); // 'TEXT', the text and its terminator
    std::vector<uint32_t> tokens(3 + words, 0);
    tokens[0] = version;
    tokens[1] = words << 16 | 0xFFFE;
    tokens[2] = 0x54584554; // 'TEXT'
    std::memcpy(&tokens[3], text.data(), text.size());
    tokens.back() = 0x0000FFFF;
    if (shader)
        *shader = new Buffer(std::move(tokens));
    return 0;
}
// D3DXSaveSurfaceToFileA (the screenshot key): not in this build.
long __stdcall SaveSurfaceToFile(const char*, int, void*, const void*, const void*)
{
    return static_cast<long>(0x80004005);
}

} // namespace

REPLACE(0x005BEF2D, Vec3Normalize);
REPLACE(0x005BF19F, Vec3Transform);
REPLACE(0x005BF2F5, Vec3TransformCoord);
REPLACE(0x005BF31F, Vec3TransformNormal);
REPLACE(0x005BF833, Vec4Transform);
REPLACE(0x005BFA8C, MatrixMultiply);
REPLACE(0x005BFCD5, MatrixTranspose);
REPLACE(0x005BFF98, MatrixInverse);
REPLACE(0x005C036B, MatrixScaling);
REPLACE(0x005C0509, MatrixRotationY);
REPLACE(0x005C0985, MatrixLookAtRH);
REPLACE(0x005C0ACD, MatrixPerspectiveRH);
REPLACE(0x005C0B39, MatrixOrthoRH);
REPLACE(0x005C2F76, SaveSurfaceToFile);
REPLACE(0x005C2FBD, AssembleShader);
