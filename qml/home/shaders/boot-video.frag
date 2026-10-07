#version 440

// YUV to RGB for the boot splash clip. The planes stay 8-bit textures; colorMatrix
// carries the frame's colour space and range. Compiled into boot-video.frag.qsb with
//   qsb --glsl "100 es,120,150" --hlsl 50 --msl 12 -o boot-video.frag.qsb boot-video.frag
// and boot-video.vert the same way plus -b, the batchable variant Qt Quick requires.

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    mat4 colorMatrix;
    float qt_Opacity;
    float interleaved;
};

layout(binding = 1) uniform sampler2D luma;
// NV12 keeps both chroma channels in chromaU; YUV420P has one plane each.
layout(binding = 2) uniform sampler2D chromaU;
layout(binding = 3) uniform sampler2D chromaV;

void main()
{
    float y = texture(luma, texCoord).r;
    vec2 uv = interleaved > 0.5 ? texture(chromaU, texCoord).rg
                                : vec2(texture(chromaU, texCoord).r, texture(chromaV, texCoord).r);
    fragColor = colorMatrix * vec4(y, uv, 1.0) * qt_Opacity;
}
