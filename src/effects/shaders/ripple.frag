#version 440
layout(std140, binding = 1) uniform FragParams {
    vec2 resolution; // Screen resolution
    float speed;
    float intensity;
    float frequency;
    float xoff;
    float yoff;
    float time; // time in seconds
    bool reverse;
    bool stretch;
};
layout(binding = 2) uniform sampler2D tex0; // scene buffer
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main(void) {
	vec2 texCoord = vTexCoord;
	vec2 center = vec2(1.0);

	if (!stretch) {
		texCoord.x *= (resolution.x/resolution.y);
		center.x += (resolution.x/resolution.y)/2.0;
	}

	center += vec2(xoff, yoff)*0.01;

	float real_time = (reverse) ? -time : time;

	vec2 p = 2.0 * texCoord - center;
	float len = length(p);
	vec2 uv = vTexCoord + (p/len)*cos((frequency*0.01)*(len*12.0-real_time*(speed*0.05)))*(intensity*0.0005);
	fragColor = texture(tex0,uv);
}
