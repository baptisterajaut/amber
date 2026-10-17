#version 440
layout(std140, binding = 1) uniform FragParams {
    float pixels_x;
    float pixels_y;
    bool bypass;
};
layout(binding = 2) uniform sampler2D input_tex;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main(void) {
	if (bypass) {
		fragColor = texture(input_tex, vTexCoord);
	} else {
		vec2 p = vTexCoord;

		p.x -= mod(p.x, 1.0 / max(pixels_x, 1.0));
		p.y -= mod(p.y, 1.0 / max(pixels_y, 1.0));

		fragColor = texture(input_tex, p);
	}
}
