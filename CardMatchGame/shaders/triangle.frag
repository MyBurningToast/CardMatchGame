#version 450

// input from the vertex shader
// Vulkan automatically interpolates this across the triangles surface between the 3 vertex values
layout(location = 0) in vec3 fragColor;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(fragColor, 1.0);
}
