#version 440
layout(std140, binding = 1) uniform FragParams {
    float frequency;
    float intensity;
    float evolution;
    bool vertical;
};
layout(binding = 2) uniform sampler2D myTexture;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main(void) {
	float x = vTexCoord.x;
	float y = vTexCoord.y;

	if (vertical) {
		x -= sin((vTexCoord.y-(evolution*0.01))*frequency)*intensity*0.01;
	} else {
		y -= sin((vTexCoord.x-(evolution*0.01))*frequency)*intensity*0.01;
	}

	if (y < 0.0 || y > 1.0 || x < 0.0 || x > 1.0) {
		discard;
	} else {
		vec4 textureColor = texture(myTexture, vec2(x, y));
		fragColor = vec4(
			textureColor.r,
			textureColor.g,
			textureColor.b,
			textureColor.a
		);
	}
}
