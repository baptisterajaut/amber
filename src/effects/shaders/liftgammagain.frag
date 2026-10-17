#version 440
layout(std140, binding = 1) uniform FragParams {
    float lift_r;
    float lift_g;
    float lift_b;
    float gamma_r;
    float gamma_g;
    float gamma_b;
    float gain_r;
    float gain_g;
    float gain_b;
};
layout(binding = 2) uniform sampler2D myTexture;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main(void) {
	vec4 tex_color = texture(myTexture, vTexCoord);
	// input is premultiplied: work on the straight colour, premultiply again on output
	vec3 c = tex_color.a > 0.0 ? tex_color.rgb / tex_color.a : vec3(0.0);

	vec3 lift = vec3(lift_r, lift_g, lift_b) * 0.01;
	vec3 gamma = max(vec3(gamma_r, gamma_g, gamma_b), vec3(1.0)) * 0.01;
	vec3 gain = vec3(gain_r, gain_g, gain_b) * 0.01;

	c = gain * (c + lift * (vec3(1.0) - c));
	c = pow(max(c, vec3(0.0)), vec3(1.0) / gamma);

	fragColor = vec4(clamp(c, 0.0, 1.0) * tex_color.a, tex_color.a);
}
