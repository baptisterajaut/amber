#version 440
// Brown radial lens model: negative K1 corrects barrel distortion, positive K1 corrects pincushion
layout(std140, binding = 1) uniform FragParams {
    vec2 resolution;
    float center_x;
    float center_y;
    float k1;
    float k2;
    float zoom;
};
layout(binding = 2) uniform sampler2D myTexture;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main(void) {
	// centre offset in pixels from the frame centre, same convention as Swirl
	vec2 c = vec2(0.5) + vec2(center_x, center_y) / resolution;
	float aspect = resolution.x / resolution.y;
	// aspect-correct, normalised so the half-diagonal is r = 1
	float half_diag = length(vec2(0.5 * aspect, 0.5));

	vec2 p = (vTexCoord - c) * vec2(aspect, 1.0) / half_diag;
	float r2 = dot(p, p);
	float f = 1.0 + (k1 * 0.01) * r2 + (k2 * 0.01) * r2 * r2;
	p *= f * (100.0 / max(zoom, 1.0));
	vec2 src = c + p * half_diag / vec2(aspect, 1.0);

	if (src.x < 0.0 || src.x > 1.0 || src.y < 0.0 || src.y > 1.0) {
		fragColor = vec4(0.0);
	} else {
		fragColor = texture(myTexture, src);
	}
}
