#version 440
layout(std140, binding = 1) uniform FragParams {
    vec2 resolution;
    float radius;
    int iteration;
    bool horiz_blur;
    bool vert_blur;
};
layout(binding = 2) uniform sampler2D image;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main(void) {
	float rad = ceil(radius);

	float divider = 1.0 / rad;
	vec4 color = vec4(0.0);
	bool radius_is_zero = (rad == 0.0);

	if (iteration == 0 && horiz_blur && !radius_is_zero) {
		for (float x=-rad+0.5;x<=rad;x+=2.0) {
			color += texture(image, (vec2(gl_FragCoord.x+x, gl_FragCoord.y))/resolution)*(divider);
		}
		fragColor = color;
	} else if (iteration == 1 && vert_blur && !radius_is_zero) {
		for (float x=-rad+0.5;x<=rad;x+=2.0) {
			color += texture(image, (vec2(gl_FragCoord.x, gl_FragCoord.y+x))/resolution)*(divider);
		}
		fragColor = color;
	} else {
		fragColor = texture(image, vTexCoord);
	}
}
