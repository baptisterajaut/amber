#version 110

uniform sampler2D y_tex;
uniform sampler2D u_tex;
uniform sampler2D v_tex;
uniform int format_type; // 0 = YUV420P, 1 = NV12

varying vec2 vTexCoord;

void main() {
    float y = texture2D(y_tex, vTexCoord).r;
    float u, v;

    if (format_type == 1) {
        // NV12: UV interleaved in RG8 texture
        vec2 uv = texture2D(u_tex, vTexCoord).rg;
        u = uv.r;
        v = uv.g;
    } else {
        // YUV420P: separate U and V planes
        u = texture2D(u_tex, vTexCoord).r;
        v = texture2D(v_tex, vTexCoord).r;
    }

    // BT.709 limited range YCbCr to RGB
    vec3 yuv = vec3(y, u, v) - vec3(16.0/255.0, 128.0/255.0, 128.0/255.0);
    mat3 bt709 = mat3(
        1.164384,  1.164384,  1.164384,
        0.0,      -0.213249,  2.112402,
        1.792741, -0.532909,  0.0
    );
    vec3 rgb = clamp(bt709 * yuv, 0.0, 1.0);
    gl_FragColor = vec4(rgb, 1.0);
}
