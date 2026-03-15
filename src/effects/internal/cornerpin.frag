#version 440
layout(std140, binding = 1) uniform CornerPinParams {
    vec2 p0;
    vec2 p1;
    vec2 p2;
    vec2 p3;
    bool perspective;
};
layout(binding = 2) uniform sampler2D tex;
layout(location = 0) in vec2 q;
layout(location = 1) in vec2 b1;
layout(location = 2) in vec2 b2;
layout(location = 3) in vec2 b3;
layout(location = 4) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

float Wedge2D(vec2 v, vec2 w) {
	return (v.x*w.y) - (v.y*w.x);
}

void main(void) {
	if (perspective) {
		fragColor = texture(tex, vTexCoord);
	} else {
		float A = Wedge2D(b2, b3);
		float B = Wedge2D(b3, q) - Wedge2D(b1, b2);
		float C = Wedge2D(b1, q);

		vec2 uv;

		// solve for v
		if (abs(A) < 0.001) {
			uv.y = -C/B;
		} else {
			float discrim = B*B - 4.0*A*C;
			uv.y = 0.5 * (-B + sqrt(discrim)) / A;
		}

		// solve for u
		vec2 denom = b1 + uv.y * b3;
		if (abs(denom.x) > abs(denom.y)) {
			uv.x = (q.x - b2.x * uv.y) / denom.x;
		} else {
			uv.x = (q.y - b2.y * uv.y) / denom.y;
		}

		uv.y = 1.0 - uv.y;

		fragColor = texture(tex, uv);
	}
}
