#version 440

layout(std140, binding = 1) uniform FragUniforms {
    int format_type;  // 0 = YUV420P, 1 = NV12
    int color_space;  // 0 = BT.709, 1 = BT.601
    int color_range;  // 0 = limited (Y 16-235), 1 = full (Y 0-255)
};

layout(binding = 2) uniform sampler2D y_tex;
layout(binding = 3) uniform sampler2D u_tex;
layout(binding = 4) uniform sampler2D v_tex;

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    float y = texture(y_tex, vTexCoord).r;
    float u, v;

    if (format_type == 1) {
        // NV12: UV interleaved in RG8 texture
        vec2 uv = texture(u_tex, vTexCoord).rg;
        u = uv.r;
        v = uv.g;
    } else {
        // YUV420P: separate U and V planes
        u = texture(u_tex, vTexCoord).r;
        v = texture(v_tex, vTexCoord).r;
    }

    // Chroma is always centred on 128; luma only carries the 16/255 pedestal in limited range.
    float y_offset = (color_range == 1) ? 0.0 : 16.0/255.0;
    vec3 yuv = vec3(y, u, v) - vec3(y_offset, 128.0/255.0, 128.0/255.0);

    // GLSL mat3 is column-major: mat3(col0, col1, col2)
    // BT.709 full range YCbCr to RGB
    mat3 bt709 = mat3(
        1.0,  1.0,       1.0,
        0.0, -0.187324,  1.855600,
        1.574800, -0.468124,  0.0
    );
    // BT.601 full range YCbCr to RGB
    mat3 bt601 = mat3(
        1.0,  1.0,       1.0,
        0.0, -0.344136,  1.772000,
        1.402000, -0.714136,  0.0
    );

    mat3 conv = (color_space == 1) ? bt601 : bt709;
    // Limited range stores the same colours on a narrower scale (Y on 219 codes, chroma on 224),
    // so stretch each input component back up before the matrix. Scaling the columns of conv does
    // exactly that: (conv * S) * yuv == conv * (S * yuv).
    if (color_range != 1) {
        conv = conv * mat3(255.0/219.0, 0.0, 0.0,
                           0.0, 255.0/224.0, 0.0,
                           0.0, 0.0, 255.0/224.0);
    }
    vec3 rgb = clamp(conv * yuv, 0.0, 1.0);
    fragColor = vec4(rgb, 1.0);
}
