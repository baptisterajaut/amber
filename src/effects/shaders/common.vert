#version 440
layout(std140, binding = 0) uniform VertexUniforms {
    mat4 mvp_matrix;
};
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_texcoord;
layout(location = 0) out vec2 vTexCoord;

void main() {
    vTexCoord = a_texcoord;
    gl_Position = mvp_matrix * vec4(a_position, 0.0, 1.0);
}
