#version 450

// Hardcoded triangle positions
// TODO: add vertex buffer instead
vec2 positions[3] = vec2[](
    vec2(0.0, -0.5),
    vec2(0.5, 0.5),
    vec2(-0.5, 0.5)
);

void main() {
    // gl_VertexIndex tells us which of the 3 points we are processing
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}