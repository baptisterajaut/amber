#version 440
// Mode 0 = Invert alpha, 1 = Threshold alpha at Level, 2 = Show Alpha (opaque matte view).
// The one effect where a transparent pixel may legitimately become opaque (Invert, Show Alpha).
layout(std140, binding = 1) uniform FragParams {
    int mode;
    float level;
};
layout(binding = 2) uniform sampler2D myTexture;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main(void) {
	vec4 tex_color = texture(myTexture, vTexCoord);
	float a = tex_color.a;
	// input is premultiplied: re-multiply the straight colour by the new alpha
	vec3 rgb = clamp(a > 0.0 ? tex_color.rgb / a : vec3(0.0), 0.0, 1.0);

	if (mode == 2) {
		fragColor = vec4(vec3(a), 1.0);
	} else {
		float new_a = mode == 0 ? 1.0 - a : (a > 0.0 && a >= level * 0.01 ? 1.0 : 0.0);
		fragColor = vec4(rgb * new_a, new_a);
	}
}
