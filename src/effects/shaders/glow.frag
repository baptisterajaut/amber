#version 440
// Glow, 3 passes (iterations="3" original="1"):
//   0 = horizontal gaussian of the bright pass, 1 = vertical gaussian, 2 = blend the halo over the original input.
layout(std140, binding = 1) uniform FragParams {
    vec2 resolution;
    float threshold;
    float radius;
    float intensity;
    int blend_mode;
    int iteration;
    float render_scale;  // auto: radius is in sequence pixels, the render target may be smaller (preview divider)
};
layout(binding = 2) uniform sampler2D image;
layout(binding = 3) uniform sampler2D originalTexture;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

// input is premultiplied: threshold the straight colour, keep the result premultiplied
vec4 bright(vec4 c) {
	vec3 rgb = c.a > 0.0 ? c.rgb / c.a : vec3(0.0);
	float t = threshold * 0.01;
	float luma = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
	float k = max(luma - t, 0.0) / max(1.0 - t, 1e-4);
	return vec4(rgb * k * c.a, k * c.a);
}

float gaussian(float x, float sigma) {
	return exp(-0.5 * (x * x) / (sigma * sigma));
}

void main(void) {
	// radius in sequence pixels, converted to render-target pixels, sampled via gl_FragCoord like gaussianblur.frag
	float r = radius * render_scale;
	float sigma = r / 3.0;
	float rad = ceil(r);

	if (iteration == 0) {
		if (rad == 0.0) {
			fragColor = bright(texture(image, vTexCoord));
			return;
		}
		// one tap per texel centre: the bright pass isn't linear, so no bilinear two-texel taps here
		vec4 color = vec4(0.0);
		float sum = 0.0;
		for (float x = -rad; x <= rad; x += 1.0) {
			float w = gaussian(x, sigma);
			color += bright(texture(image, vec2(gl_FragCoord.x + x, gl_FragCoord.y) / resolution)) * w;
			sum += w;
		}
		fragColor = color / sum;
	} else if (iteration == 1) {
		if (rad == 0.0) {
			fragColor = texture(image, vTexCoord);
			return;
		}
		// plain premultiplied blur: bilinear taps between two texels, like gaussianblur.frag
		vec4 color = vec4(0.0);
		float sum = 0.0;
		for (float y = -rad + 0.5; y <= rad; y += 2.0) {
			float w = gaussian(y, sigma);
			color += texture(image, vec2(gl_FragCoord.x, gl_FragCoord.y + y) / resolution) * w;
			sum += w;
		}
		fragColor = color / sum;
	} else {
		vec4 o = texture(originalTexture, vTexCoord);
		vec3 g = texture(image, vTexCoord).rgb * (intensity * 0.01);
		// 0 = Add, 1 = Screen (on premultiplied colour)
		vec3 rgb = blend_mode == 1 ? vec3(1.0) - (vec3(1.0) - o.rgb) * (vec3(1.0) - g) : o.rgb + g;
		rgb = clamp(rgb, 0.0, 1.0);
		// the halo is light of its own: alpha covers it, so the output stays premultiplied (rgb <= a)
		fragColor = vec4(rgb, max(o.a, max(rgb.r, max(rgb.g, rgb.b))));
	}
}
