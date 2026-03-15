#version 440
layout(std140, binding = 1) uniform FragParams {
    vec2 resolution;
    float time;
    float stitching_size;
    bool invert;
};
layout(binding = 2) uniform sampler2D tex0;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

vec4 PostFX(sampler2D tex, vec2 uv, float time) {
	vec4 c = vec4(0.0);
	float size = stitching_size;
	vec2 cPos = uv * resolution;
	vec2 tlPos = floor(cPos / vec2(size, size));
	tlPos *= size;
	int remX = int(mod(cPos.x, size));
	int remY = int(mod(cPos.y, size));
	if (remX == 0 && remY == 0)
		tlPos = cPos;
	vec2 blPos = tlPos;
	blPos.y += (size - 1.0);
	if ((remX == remY) ||
		 (((int(cPos.x) - int(blPos.x)) == (int(blPos.y) - int(cPos.y)))))
	{
		if (!invert)
			c = vec4(0.2, 0.15, 0.05, 1.0);
		else
			c = texture(tex, tlPos * vec2(1.0/resolution.x, 1.0/resolution.y)) * 1.4;
	}
	else
	{
		if (!invert)
			c = texture(tex, tlPos * vec2(1.0/resolution.x, 1.0/resolution.y)) * 1.4;
		else
			c = vec4(0.0, 0.0, 0.0, 1.0);
	}
	return c;
}

void main(void) {
	vec2 uv = vTexCoord;
	fragColor = PostFX(tex0, uv, time);
}
