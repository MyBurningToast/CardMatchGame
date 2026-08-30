#version 450

// Hardcoded triangle positions
// TODO: add vertex buffer instead
vec2 positions[3] = vec2[](
    vec2(0.0, -0.5),
    vec2(0.5, 0.5),
    vec2(-0.5, 0.5)
);

// one color per vertex
vec3 colors[3] = vec3[](
    vec3(1.0, 0.0, 0.0), // red
    vec3(0.0, 1.0, 0.0), // green
    vec3(0.0, 0.0, 1.0) // blue
);

// output to the fragment shader
// location 0 must match the input in the frag shader
layout(location = 0) out vec3 fragColor;

void main() {
    // gl_VertexIndex tells us which of the 3 points we are processing
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    fragColor = colors[gl_VertexIndex]; // pass this vertexs color onward
}
