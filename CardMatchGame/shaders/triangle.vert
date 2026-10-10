#version 450

// push constants are small per draw call data sent straight from the cpu (no buffer or descriptor set needed)
// layout must match the QuadInstance struct
layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;
} pc;

layout(location = 0) in vec2 inPosition; // from vertex buffer
layout(location = 1) in vec2 inUV; // from vertex buffer

layout(location = 0) out vec4 fragColor; // given to the fragment shader
layout(location = 1) out vec2 fragUV; // given to the fragment shader (interpolated across the quad)

void main() {
    gl_Position = pc.model * vec4(inPosition, 0.0, 1.0);
    fragColor = pc.color; // same color for every vertex
    fragUV = inUV;
}
