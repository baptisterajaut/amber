#version 440
layout(std140, binding = 1) uniform FragParams {
    float size;
};
layout(binding = 2) uniform sampler2D tex0;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

#define M_PI 3.1415926535897932384626433832795

void main(void) {
	float maxFactor = 2.0 - (size * 0.01);

	vec2 uv;
	vec2 xy = 2.0 * vTexCoord - 1.0;
	float d = length(xy);
	if (d < (2.0-maxFactor)) 	{
		d = length(xy * maxFactor);
		float z = sqrt(1.0 - d * d);
		float r = atan(d, z) / M_PI;
		float phi = atan(xy.y, xy.x);

		uv.x = r * cos(phi) + 0.5;
		uv.y = r * sin(phi) + 0.5;
	} else {
		uv = vTexCoord;
	}
	vec4 c = texture(tex0, uv);
	fragColor = c;
}
