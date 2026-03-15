#version 440
layout(std140, binding = 1) uniform FragParams {
    float gamma_cent; // 0.6
    float numColors; // 8.0
};
layout(binding = 2) uniform sampler2D sceneTex; // 0
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
	float gamma = gamma_cent*0.01;
	vec4 color = texture(sceneTex, vTexCoord);
	vec3 c = color.rgb;
	c = pow(c, vec3(gamma, gamma, gamma));
	c = c * numColors;
	c = floor(c);
	c = c / numColors;
	c = pow(c, vec3(1.0/gamma));
	fragColor = vec4(c, color.a);
}
