#version 440
layout(std140, binding = 1) uniform FragParams {
    vec2 resolution;
    float amount;
    float xoff;
    float yoff;
};
layout(binding = 2) uniform sampler2D tex0;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

const float PI = 3.1415926535;

vec2 distort(vec2 p, vec2 offset) {
    float theta  = atan(p.y, p.x);
    float radius = length(p);
    radius = pow(radius, (1.0+amount*0.01));
    p.x = radius * cos(theta) + offset.x;
    p.y = radius * sin(theta) + offset.y;
    return 0.5 * (p + 1.0);
}

void main(void) {
	vec2 offset = vec2(xoff/resolution.x, yoff/resolution.y);
	vec2 xy = 2.0 * vTexCoord - 1.0 - offset;
	vec2 uv;
	float d = length(xy);
	uv = distort(xy, offset);
	if (uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0) {
		fragColor = texture(tex0, uv);
	} else {
		discard;
	}
}
