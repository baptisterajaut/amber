#version 440
// Swirl effect parameters
layout(std140, binding = 1) uniform FragParams {
    vec2 resolution;
    float radius;
    float angle;
    float center_x;
    float center_y;
};
layout(binding = 2) uniform sampler2D myTexture;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main(void) {
	vec2 center = vec2((resolution.x*0.5)+center_x, (resolution.y*0.5)+center_y);

	vec2 uv = vTexCoord.st;

	vec2 tc = uv * resolution;
	tc -= center;
	float dist = length(tc);
	if (dist < radius) {
		float percent = (radius - dist) / radius;
		float theta = percent * percent * (-angle*0.05) * 8.0;
		float s = sin(theta);
		float c = cos(theta);
		tc = vec2(dot(tc, vec2(c, -s)), dot(tc, vec2(s, c)));
	}
	tc += center;
	fragColor = texture(myTexture, tc / resolution);
}
