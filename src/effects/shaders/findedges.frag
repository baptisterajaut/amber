#version 440
// 3x3 Sobel edge detection. Mode 0 = Luma (grey edges), 1 = Color (per-channel edges).
layout(std140, binding = 1) uniform FragParams {
    vec2 resolution;
    float strength;
    int mode;
    bool invert;
};
layout(binding = 2) uniform sampler2D myTexture;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

// input is premultiplied: the edges are found on the straight colour of each tap
vec3 tap(vec2 offset) {
	vec4 c = texture(myTexture, vTexCoord + offset / resolution);
	return c.a > 0.0 ? c.rgb / c.a : vec3(0.0);
}

void main(void) {
	vec3 tl = tap(vec2(-1.0, -1.0));
	vec3 t  = tap(vec2( 0.0, -1.0));
	vec3 tr = tap(vec2( 1.0, -1.0));
	vec3 l  = tap(vec2(-1.0,  0.0));
	vec3 r  = tap(vec2( 1.0,  0.0));
	vec3 bl = tap(vec2(-1.0,  1.0));
	vec3 b  = tap(vec2( 0.0,  1.0));
	vec3 br = tap(vec2( 1.0,  1.0));

	vec3 gx = (tr + 2.0 * r + br) - (tl + 2.0 * l + bl);
	vec3 gy = (bl + 2.0 * b + br) - (tl + 2.0 * t + tr);

	vec3 mag;
	if (mode == 0) {
		// Sobel is linear: the gradient of the Rec.709 luma is the luma of the gradients
		vec3 w = vec3(0.2126, 0.7152, 0.0722);
		mag = vec3(length(vec2(dot(gx, w), dot(gy, w))));
	} else {
		mag = sqrt(gx * gx + gy * gy);
	}

	vec3 e = clamp(mag * strength * 0.01, 0.0, 1.0);
	if (invert) e = vec3(1.0) - e;

	float a = texture(myTexture, vTexCoord).a;
	fragColor = vec4(e * a, a);
}
