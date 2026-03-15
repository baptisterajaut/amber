#version 440

layout(binding = 2) uniform sampler2D tex;

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    vec4 c = texture(tex, vTexCoord);
    c.rgb *= c.a;
    fragColor = c;
}
