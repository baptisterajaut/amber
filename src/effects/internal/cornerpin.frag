#version 440
// Shared with cornerpin.vert, keep both declarations identical.
layout(std140, binding = 1) uniform CornerPinParams {
    vec2 p0;
    vec2 p1;
    vec2 p2;
    vec2 p3;
    bool perspective;
    vec2 t_bl;
    vec2 t_br;
    vec2 t_tl;
    vec2 t_tr;
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
		// inverse bilinear: find (u, v) with q = u*b1 + v*b2 + u*v*b3, v = 0 on the p0/p1 edge
		float A = Wedge2D(b2, b3);
		float B = Wedge2D(b3, q) - Wedge2D(b1, b2);
		float C = Wedge2D(b1, q);

		float v;
		if (abs(A) < 0.001) {
			v = -C/B;
		} else {
			// the valid root depends on the quad's winding, take the one inside the quad
			float root = sqrt(max(B*B - 4.0*A*C, 0.0));
			v = 0.5 * (-B + root) / A;
			if (v < -0.001 || v > 1.001) v = 0.5 * (-B - root) / A;
		}

		vec2 denom = b1 + v * b3;
		float u;
		if (abs(denom.x) > abs(denom.y)) {
			u = (q.x - b2.x * v) / denom.x;
		} else {
			u = (q.y - b2.y * v) / denom.y;
		}

		vec2 uv = mix(mix(t_bl, t_br, u), mix(t_tl, t_tr, u), v);
		fragColor = texture(tex, uv);
	}
}
