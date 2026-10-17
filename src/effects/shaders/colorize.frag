#version 440
layout(std140, binding = 1) uniform FragParams {
    float hue;
    float saturation;
    float lightness;
};
layout(binding = 2) uniform sampler2D myTexture;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

vec3 hsl2rgb(float h, float s, float l) {
	vec3 rgb = clamp(abs(mod(h * 6.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);
	float c = (1.0 - abs(2.0 * l - 1.0)) * s;
	return l + c * (rgb - 0.5);
}

void main(void) {
	vec4 tex_color = texture(myTexture, vTexCoord);
	// input is premultiplied: work on the straight colour, premultiply again on output
	vec3 rgb = tex_color.a > 0.0 ? tex_color.rgb / tex_color.a : vec3(0.0);

	float luma = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
	float l = lightness * 0.01;
	luma = l < 0.0 ? luma * (1.0 + l) : luma + (1.0 - luma) * l;
	rgb = hsl2rgb(hue / 360.0, saturation * 0.01, luma);

	fragColor = vec4(clamp(rgb, 0.0, 1.0) * tex_color.a, tex_color.a);
}
