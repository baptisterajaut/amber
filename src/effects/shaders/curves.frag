#version 440
layout(binding = 2) uniform sampler2D myTexture;
layout(binding = 3) uniform sampler2D curveLut;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

// 256 x 1 lookup: R/G/B = the channel curves after the master curve, A = the luma curve. Sampled at texel centres,
// so an 8-bit input lands exactly on its entry and anything in between interpolates linearly.
vec4 lut(float v) {
	return texture(curveLut, vec2((clamp(v, 0.0, 1.0) * 255.0 + 0.5) / 256.0, 0.5));
}

void main(void) {
	vec4 tex_color = texture(myTexture, vTexCoord);
	// input is premultiplied: work on the straight colour, premultiply again on output
	vec3 rgb = tex_color.a > 0.0 ? clamp(tex_color.rgb / tex_color.a, 0.0, 1.0) : vec3(0.0);
	rgb = vec3(lut(rgb.r).r, lut(rgb.g).g, lut(rgb.b).b);

	// Luma curve: a scale in the mids and highs keeps the hue; near black a shift instead, since scaling a lifted
	// black would turn its chroma noise into saturated speckle
	float y = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
	float y_out = lut(y).a;
	vec3 scaled = rgb * (y_out / max(y, 1e-4));
	vec3 shifted = rgb + (y_out - y);
	rgb = mix(shifted, scaled, smoothstep(0.0, 0.05, y));

	fragColor = vec4(clamp(rgb, 0.0, 1.0) * tex_color.a, tex_color.a);
}
