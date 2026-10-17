#version 440
layout(std140, binding = 1) uniform FragParams {
    float level;
    bool invert;
};
layout(binding = 2) uniform sampler2D myTexture;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main(void) {
	vec4 tex_color = texture(myTexture, vTexCoord);
	// input is premultiplied: threshold the straight colour, premultiply again on output
	vec3 rgb = tex_color.a > 0.0 ? tex_color.rgb / tex_color.a : vec3(0.0);

	float luma = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
	float v = luma >= level * 0.01 ? 1.0 : 0.0;
	if (invert) v = 1.0 - v;

	fragColor = vec4(vec3(v) * tex_color.a, tex_color.a);
}
