#version 440
layout(std140, binding = 1) uniform FragParams {
    float in_black;
    float in_white;
    float gamma;
    float out_black;
    float out_white;
    int channel;
};
layout(binding = 2) uniform sampler2D myTexture;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main(void) {
	vec4 tex_color = texture(myTexture, vTexCoord);
	// input is premultiplied: work on the straight colour, premultiply again on output
	vec3 rgb = tex_color.a > 0.0 ? tex_color.rgb / tex_color.a : vec3(0.0);

	float ib = in_black * 0.01;
	float iw = in_white * 0.01;
	vec3 c = clamp((rgb - ib) / max(iw - ib, 1e-4), 0.0, 1.0);
	// gamma > 100 brightens midtones (Photoshop convention)
	c = pow(c, vec3(100.0 / max(gamma, 1.0)));
	// output black > output white is allowed and inverts
	c = mix(vec3(out_black * 0.01), vec3(out_white * 0.01), c);

	// 0 = RGB, 1 = Red, 2 = Green, 3 = Blue: unselected channels pass through
	vec3 mask = vec3(1.0);
	if (channel == 1) mask = vec3(1.0, 0.0, 0.0);
	else if (channel == 2) mask = vec3(0.0, 1.0, 0.0);
	else if (channel == 3) mask = vec3(0.0, 0.0, 1.0);
	rgb = mix(rgb, c, mask);

	fragColor = vec4(clamp(rgb, 0.0, 1.0) * tex_color.a, tex_color.a);
}
