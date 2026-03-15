#version 440
layout(std140, binding = 1) uniform FragParams {
    bool horiz;
    bool vert;
};
layout(binding = 2) uniform sampler2D myTexture;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main(void) {
	float x = vTexCoord.x;
	float y = vTexCoord.y;

	if (horiz) x = 1.0 - x;
	if (vert) y = 1.0 - y;

	vec4 textureColor = texture(myTexture, vec2(x, y));
	fragColor = vec4(
		textureColor.r,
		textureColor.g,
		textureColor.b,
		textureColor.a
	);
}
