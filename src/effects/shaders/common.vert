#version 440
layout(std140, binding = 0) uniform VertexUniforms {
    mat4 mvp_matrix;
} vu;  // named so GLSL output doesn't auto-name it _25 and clash with a fragment block
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_texcoord;
layout(location = 0) out vec2 vTexCoord;

void main() {
    vTexCoord = a_texcoord;
    gl_Position = vu.mvp_matrix * vec4(a_position, 0.0, 1.0);
}
