#version 440
layout(std140, binding = 1) uniform FragParams {
    vec3 shadows;
    vec3 highlights;
    float amount;
};
layout(binding = 2) uniform sampler2D myTexture;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main(void) {
	vec4 tex_color = texture(myTexture, vTexCoord);
	// input is premultiplied: work on the straight colour, premultiply again on output
	vec3 rgb = tex_color.a > 0.0 ? tex_color.rgb / tex_color.a : vec3(0.0);

	float luma = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
	vec3 tinted = mix(shadows, highlights, luma);
	rgb = mix(rgb, tinted, amount * 0.01);

	fragColor = vec4(clamp(rgb, 0.0, 1.0) * tex_color.a, tex_color.a);
}
