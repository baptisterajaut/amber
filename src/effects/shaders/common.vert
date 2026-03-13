#version 110

uniform mat4 mvp_matrix;

attribute vec2 a_position;
attribute vec2 a_texcoord;

varying vec2 vTexCoord;

void main() {
    vTexCoord = a_texcoord;
    gl_Position = mvp_matrix * vec4(a_position, 0.0, 1.0);
}
