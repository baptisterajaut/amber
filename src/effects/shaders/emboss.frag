/* Original source code: https://www.youtube.com/watch?v=I23zT7iu_Y4
Adapted */

#version 440
layout(std140, binding = 1) uniform FragParams {
    vec2 resolution;
    float contrast100;
    float level;
    bool invert;
    bool hslmode; // luminance is 2*V
};
layout(binding = 2) uniform sampler2D tex;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {

    float contrast = contrast100 / 100.0;

	vec2 onePixel = vec2(floor(level) /  resolution.x, floor(level) /  resolution.y); // calculating the size of one pixel on the screen for the current resolution

	vec3 color = vec3(.5); // initialize color with half value on all channels
    if (invert) {
        color += texture(tex, vTexCoord - onePixel).rgb * contrast;
	    color -= texture(tex, vTexCoord + onePixel).rgb * contrast;
    } else {
	    color -= texture(tex, vTexCoord - onePixel).rgb * contrast;
	    color += texture(tex, vTexCoord + onePixel).rgb * contrast;
	}

	// original color
	vec4 color0 = texture(tex,vTexCoord);

	// grayscale
    float gray = (color.r + color.g + color.b) / (hslmode ? 3.0 : 1.5);

    //self-multiply
    color = color0.rgb * gray;

	fragColor = vec4(color * color0.a, color0.a);
}
