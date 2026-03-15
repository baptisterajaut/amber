#version 440
layout(std140, binding = 1) uniform FragParams {
    vec2 resolution;
    float red_amount;
    float green_amount;
    float blue_amount;
};
layout(binding = 2) uniform sampler2D tex;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main(void) {
	vec2 rOffset = vec2(red_amount*0.01)/resolution;
	vec2 gOffset = vec2(green_amount*0.01)/resolution;
	vec2 bOffset = vec2(blue_amount*0.01)/resolution;

    vec4 rValue = texture(tex, vTexCoord - rOffset);
    vec4 gValue = texture(tex, vTexCoord - gOffset);
    vec4 bValue = texture(tex, vTexCoord - bOffset);

    fragColor = vec4(rValue.r, gValue.g, bValue.b, texture(tex, vTexCoord).a);
}
