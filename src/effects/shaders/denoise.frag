#version 440
// Spatial denoise, one bilateral pass: a gaussian over the (2r+1)^2 window where each tap is also weighted down by its
// colour distance to the centre, so flat areas smooth out and edges stay put. Temporal denoise (hqdn3d) stays Frei0r.
layout(std140, binding = 1) uniform FragParams {
    vec2 resolution;
    float radius;
    float strength;
    float detail;
};
layout(binding = 2) uniform sampler2D myTexture;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

// constant loop bounds for HLSL/MSL, taps outside the runtime radius are skipped
const int MAX_RADIUS = 6;

void main(void) {
	vec4 c = texture(myTexture, vTexCoord);
	// strength 0 is the identity
	if (strength <= 0.0) {
		fragColor = c;
		return;
	}
	// a transparent pixel stays (0,0,0,0)
	if (c.a <= 0.0) {
		fragColor = vec4(0.0);
		return;
	}

	// input is premultiplied: filter the straight colour, premultiply again with the centre alpha
	vec3 center = c.rgb / c.a;
	// radius in render-target pixels, rounded down
	// CBA: like Gaussian Blur it looks wider at reduced preview resolution, scale by the preview divider if it matters
	int r = int(radius);
	float sigma_s = max(float(r) * 0.5, 0.5);
	float sigma_r = mix(0.02, 0.3, strength * 0.01);
	float ks = -0.5 / (sigma_s * sigma_s);
	// range distance is the per-channel mean |d|^2 / 3, so sigma_r is per channel
	float kr = -0.5 / (3.0 * sigma_r * sigma_r);

	// the centre always counts, weight 1
	vec3 sum = center;
	float wsum = 1.0;
	for (int dy = -MAX_RADIUS; dy <= MAX_RADIUS; dy++) {
		for (int dx = -MAX_RADIUS; dx <= MAX_RADIUS; dx++) {
			if (abs(dx) > r || abs(dy) > r || (dx == 0 && dy == 0)) continue;
			vec4 t = texture(myTexture, vTexCoord + vec2(float(dx), float(dy)) / resolution);
			vec3 rgb = t.a > 0.0 ? t.rgb / t.a : vec3(0.0);
			vec3 d = rgb - center;
			// transparent taps get weight 0
			float w = t.a > 0.0 ? exp(float(dx * dx + dy * dy) * ks + dot(d, d) * kr) : 0.0;
			sum += rgb * w;
			wsum += w;
		}
	}

	// Detail blends the centre back in, up to half
	vec3 rgb = mix(sum / wsum, center, detail * 0.005);
	fragColor = vec4(clamp(rgb, 0.0, 1.0) * c.a, c.a);
}
