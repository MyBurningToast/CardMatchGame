#version 450

layout(location = 0) in vec4 fragColor; // comes from the vertex shaders output
layout(location = 1) in vec2 fragUV; // comes from the vertex shaders output

// the texture + sampler bound with the descriptor set (set 0, binding 0)
layout(set = 0, binding = 0) uniform sampler2D tex;

layout(location = 0) out vec4 outColor; // written to framebuffer

void main() {
    // the texture is single channel (R8)
    // .r is the 0.0 - 1.0 brightness at this uv
    float t = texture(tex, fragUV).r;

    // darken the card color by the checkerboard for testing
    // TODO: this becomes the alpha of a font glyph instead
    outColor = vec4(fragColor.rgb * t, fragColor.a);
}