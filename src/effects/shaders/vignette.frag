#version 440
layout(std140, binding = 1) uniform FragParams {
    vec2 resolution;
    float lensRadiusX;
    float lensRadiusY;
    float centerX;
    float centerY;
    bool circular;
    bool invert;
    // uniform vec2 lensRadius; // 0.45, 0.38
};
layout(binding = 2) uniform sampler2D sceneTex; // 0
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main(void) {
	if (lensRadiusX == 0.0) {
		discard;
	}
	vec4 c = texture(sceneTex, vTexCoord);
	vec2 vignetteCoord = vTexCoord;
	if (circular) {
		float ar = (resolution.x/resolution.y);
		vignetteCoord.x *= ar;
		vignetteCoord.x -= (1.0-(1.0/ar));
	}
	float dist = distance(vignetteCoord, vec2(0.5 + centerX*0.01, 0.5 + centerY*0.01));
	float size = (lensRadiusX*0.01);

	if (invert)
	{c *= 1.0 - smoothstep(size, size*0.99*(1.0-lensRadiusY*0.01), dist);}
	else
	{c *= smoothstep(size, size*0.99*(1.0-lensRadiusY*0.01), dist);}

	fragColor = c;
}
