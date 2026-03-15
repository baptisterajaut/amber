#version 440

/*layout(location = 0) in vec4 vertTexCoord;
layout(binding = 2) uniform sampler2D input_tex;
//uniform vec2 pixels;

void main(void) {
	// Grouping texcoord variables in order to make it work in the GMA 950. See post #13
	// in this thread:
	// http://www.idevgames.com/forums/thread-3467.html
	vec2 texOffset = vec2(1.0);

	vec2 tc0 = vertTexCoord.st + vec2(-texOffset.s, -texOffset.t);
	vec2 tc1 = vertTexCoord.st + vec2(         0.0, -texOffset.t);
	vec2 tc2 = vertTexCoord.st + vec2(+texOffset.s, -texOffset.t);
	vec2 tc3 = vertTexCoord.st + vec2(-texOffset.s,          0.0);
	vec2 tc4 = vertTexCoord.st + vec2(         0.0,          0.0);
	vec2 tc5 = vertTexCoord.st + vec2(+texOffset.s,          0.0);
	vec2 tc6 = vertTexCoord.st + vec2(-texOffset.s, +texOffset.t);
	vec2 tc7 = vertTexCoord.st + vec2(         0.0, +texOffset.t);
	vec2 tc8 = vertTexCoord.st + vec2(+texOffset.s, +texOffset.t);

	vec4 col0 = texture(input_tex, tc0);
	vec4 col1 = texture(input_tex, tc1);
	vec4 col2 = texture(input_tex, tc2);
	vec4 col3 = texture(input_tex, tc3);
	vec4 col4 = texture(input_tex, tc4);
	vec4 col5 = texture(input_tex, tc5);
	vec4 col6 = texture(input_tex, tc6);
	vec4 col7 = texture(input_tex, tc7);
	vec4 col8 = texture(input_tex, tc8);

	vec4 sum = 8.0 * col4 - (col0 + col1 + col2 + col3 + col5 + col6 + col7 + col8);
	fragColor = vec4(sum.rgb, 1.0);// * vertColor;
}*/

layout(location = 0) in vec4 vertTexCoord;
layout(binding = 2) uniform sampler2D input_tex;
//uniform vec2 pixels;
layout(location = 0) out vec4 fragColor;

void main(void)
{
	vec2 pixels = vec2(16.0, 9.0);

  	vec2 p = vertTexCoord.st;

	p.x -= mod(p.x, 1.0 / pixels.x);
	p.y -= mod(p.y, 1.0 / pixels.y);

	vec3 col = texture(input_tex, p).rgb;
	fragColor = vec4(col, 1.0);
}
