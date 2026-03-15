#version 440

layout(std140, binding = 1) uniform FragUniforms {
    vec4 color_mult;
};

layout(binding = 2) uniform sampler2D tex;

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = texture(tex, vTexCoord) * color_mult;
}
