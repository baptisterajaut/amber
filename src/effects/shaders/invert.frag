#version 440
layout(std140, binding = 1) uniform FragParams {
    float amount;
};
layout(binding = 2) uniform sampler2D myTexture;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main(void) {
	vec4 textureColor = texture(myTexture, vTexCoord);
	float amount_val = amount * 0.01;
	vec3 col = textureColor.rgb+((vec3(1.0)-textureColor.rgb-textureColor.rgb)*vec3(amount_val));
	fragColor = vec4(col, textureColor.a);
}
