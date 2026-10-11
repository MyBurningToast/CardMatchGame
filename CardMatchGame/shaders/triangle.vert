#version 450

// push constants are small per draw call data sent straight from the cpu (no buffer or descriptor set needed)
// layout must match the QuadInstance struct
layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;
    vec4 uvRect; // u0, v0, u1, v1: the part of the texture this quad shows
} pc;

layout(location = 0) in vec2 inPosition; // from vertex buffer
layout(location = 1) in vec2 inUV;       // from vertex buffer. (0,0) to (1,1) across the whole quad

layout(location = 0) out vec4 fragColor; // given to the fragment shader
layout(location = 1) out vec2 fragUV;    // given to the fragment shader

void main() {
    gl_Position = pc.model * vec4(inPosition, 0.0, 1.0);
    fragColor = pc.color; // same color for every vertex

    /*
    Note to self:
    https://learnopengl.com/Getting-started/Textures
    uv = a position inside a texture, 0-1 on each axis, independent of pixel size. (0,0) is one corner, (1,1) is the opposite
    u is horizontal, v is vertical. x/y mean something else in shaders, so textures use u/v
    a glyph's rect in the atlas is 2 corners: (u0, v0) and (u1, v1).
    vec4 components are always named x, y, z, w, so .xy = first two numbers, .zw = last two
    */

    // remap the quads 0-1 uv into this quads rectangle of the atlas
    // (0,0) becomes (u0,v0) and (1,1) becomes (u1,v1)
    fragUV = mix(pc.uvRect.xy, pc.uvRect.zw, inUV);
}