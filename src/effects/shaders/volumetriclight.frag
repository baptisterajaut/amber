#version 440
layout(std140, binding = 1) uniform FragParams {
    vec2 resolution; // screen resolution
    float amount;
};
layout(binding = 2) uniform sampler2D tex0;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

#define T texture(tex0,.5+(p.xy*=.992))

void main()  {
	float alpha = texture(tex0, vTexCoord)[3];

	vec3 p = gl_FragCoord.xyz/vec3(resolution, 1.0)-.5;
	vec3 o = T.rgb;
	for (float i=0.;i<amount;i++) {
		p.z += pow(max(0.,.5-length(T.rg)),2.)*exp(-i*.08);
	}
	fragColor=vec4(o*o+p.z, alpha);
}
