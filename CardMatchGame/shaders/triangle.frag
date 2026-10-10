#version 450

layout(location = 0) in vec4 fragColor; // comes from the vertex shaders output

layout(location = 0) out vec4 outColor; // written to framebuffer

void main() {
    outColor = fragColor; // just output whatever this quads color was
}