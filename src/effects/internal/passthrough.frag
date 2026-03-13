#version 110

uniform sampler2D tex;
uniform vec4 color_mult;

varying vec2 vTexCoord;

void main() {
  gl_FragColor = texture2D(tex, vTexCoord) * color_mult;
}
