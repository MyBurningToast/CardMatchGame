#version 450

layout(location = 0) in vec4 fragColor; // comes from the vertex shaders output
layout(location = 1) in vec2 fragUV; // comes from the vertex shaders output

layout(location = 0) out vec4 outColor; // written to framebuffer

void main() {
    // TEMPORARY DEBUG: blend the uv into the card color so can see the uvs working
    // TODO: removed once we sample a real texture
    outColor = mix(fragColor, vec4(fragUV, 0.0, 1.0), 0.5);
}