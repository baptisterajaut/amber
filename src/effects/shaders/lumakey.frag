/* Luma key simple program
Based on Edward Cannon's Simple Chroma Key (adaptation by Olive Team)
Feel free to modify and use at will */

#version 440
layout(std140, binding = 1) uniform FragParams {
    float loc;
    float hic;
    bool invert;
};
layout(binding = 2) uniform sampler2D tex;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main(void) {
	vec4 texture_color = texture(tex,vTexCoord);

	float luma = max(max(texture_color.r,texture_color.g), texture_color.b) + min(min(texture_color.r,texture_color.g), texture_color.b);

	luma /= 2.0;

	if (luma > hic/100.0) {
        texture_color.a = (invert ? 0.0 : 1.0);
    } else if (luma < loc/100.0) {
        texture_color.a = (invert ? 1.0 : 0.0);
    } else {
        texture_color.a = (invert ? 1.0-luma : luma);
    }

	texture_color.rgb *= texture_color.a;
	fragColor = texture_color;
}
