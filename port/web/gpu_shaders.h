#pragma once

#include <string>

#include "web/gpu_frame.h"

namespace Gpu {

// The WGSL for a draw's shaders: Direct3D 9's fixed-function vertex processing (transforms, vertex blending, lighting,
// fog, texture coordinates) for the key's vertex layout, then either the fixed-function texture stages or the pixel
// shader's ps.1.x text (`pixelShader`, null for none) translated - with fog and the alpha test after. Entry points
// "vs" and "fs". Bindings: group 0 the Draw (0) and Lighting (1) uniforms; group 1 stage n's texture (2n) and
// sampler (2n + 1), for the stages the key samples.
std::string GenerateWgsl(const ShaderKey& key, const char* pixelShader);

// The WGSL type a vertex element arrives as, and its WebGPU vertex format's name ("float32x3", ...): by D3DDECLTYPE.
const char* VertexFormatName(uint8_t declType);

} // namespace Gpu
