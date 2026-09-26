#version 440
layout(std140, binding = 0) uniform VertexUniforms {
    mat4 mvp_matrix;
} vu;  // named so GLSL output doesn't auto-name it _25 and clash with a fragment block
// Shared with cornerpin.frag, keep both declarations identical.
// p0..p3 are the pinned corners in the same space as a_position (BL, BR, TL, TR),
// t_* the texture coordinates of those corners.
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
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_texcoord;
layout(location = 0) out vec2 q;
layout(location = 1) out vec2 b1;
layout(location = 2) out vec2 b2;
layout(location = 3) out vec2 b3;
layout(location = 4) out vec2 vTexCoord;

void main() {
    gl_Position = vu.mvp_matrix * vec4(a_position, 0.0, 1.0);
    vTexCoord = a_texcoord;

    // bilinear inputs, only read by the fragment shader when perspective is off
    q = a_position - p0;
    b1 = p1 - p0;
    b2 = p2 - p0;
    b3 = p0 - p1 - p2 + p3;

    if (perspective) {
        // Projective interpolation: k_i = (d_i + d_opp) / d_opp, d being the distance from the corner to the
        // diagonals' intersection. The GPU interpolates attr/w and 1/w linearly, so 1/w must be proportional
        // to k: dividing the position by k makes the varyings interpolate as a homography.
        vec2 da = p3 - p0;  // diagonal BL -> TR
        vec2 db = p1 - p2;  // diagonal TL -> BR
        float denom = da.x * db.y - da.y * db.x;
        if (abs(denom) > 1e-6) {
            vec2 w = p2 - p0;
            float s = (w.x * db.y - w.y * db.x) / denom;
            vec2 mid = p0 + s * da;

            float d_bl = length(mid - p0);
            float d_br = length(mid - p1);
            float d_tl = length(mid - p2);
            float d_tr = length(mid - p3);

            // vertex order is the clip's triangle strip: TL, TR, BL, BR
            float k;
            if (gl_VertexIndex == 0) {
                k = (d_tl + d_br) / d_br;
            } else if (gl_VertexIndex == 1) {
                k = (d_tr + d_bl) / d_bl;
            } else if (gl_VertexIndex == 2) {
                k = (d_bl + d_tr) / d_tr;
            } else {
                k = (d_br + d_tl) / d_tl;
            }
            gl_Position /= k;
        }
    }
}
