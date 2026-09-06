#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
} ubo;

layout(location = 0) in vec2 inPosition;

void main() {
    gl_Position = ubo.model * vec4(inPosition, 0.0, 1.0);
}