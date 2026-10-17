#version 440
layout(std140, binding = 1) uniform FragParams {
    float left;
    float top;
    float right;
    float bottom;
    float feather;
    float amount_val;
    bool invert;
};
layout(binding = 2) uniform sampler2D myTexture;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main(void) {
	vec4 textureColor = texture(myTexture, vec2(vTexCoord.x, vTexCoord.y));
	float alpha = textureColor.a;
	if (feather == 0.0) {
		if (vTexCoord.x < (left*0.01) || vTexCoord.y < (top*0.01) || vTexCoord.x > (1.0-(right*0.01)) || vTexCoord.y > (1.0-(bottom*0.01))) {
			alpha = 0.0;
		}
	} else {
		float f = pow(2.0, 10.0-(feather*0.1));
		if (left > 0.0) alpha = alpha * clamp(((vTexCoord.x+(0.5/f))-(left*0.01))*f, 0.0, 1.0); // left
		if (top > 0.0) alpha = alpha * clamp(((vTexCoord.y+(0.5/f))-(top*0.01))*f, 0.0, 1.0); // top
		if (right > 0.0) alpha = alpha * clamp((((1.0-vTexCoord.x)+(0.5/f))-(right*0.01))*f, 0.0, 1.0); // right
		if (bottom > 0.0) alpha = alpha * clamp((((1.0-vTexCoord.y)+(0.5/f))-(bottom*0.01))*f, 0.0, 1.0); // bottom
	}

	// input is premultiplied: scale the straight colour by the new alpha (scaling the premultiplied one multiplied by
	// alpha twice and darkened semi-transparent edges)
	vec3 rgb = textureColor.a > 0.0 ? textureColor.rgb / textureColor.a : vec3(0.0);

	if (invert)
	{fragColor = vec4(
		rgb.r*(1.0 - alpha),
		rgb.g*(1.0 - alpha),
		rgb.b*(1.0 - alpha),
		1.0 - alpha
	);}
	else{
		fragColor = vec4(
		rgb.r*(alpha),
		rgb.g*(alpha),
		rgb.b*(alpha),
		alpha
	);}
}
