#version 440

// Drop Shadow pass (compose_sequence): gaussian blur of the source alpha along one axis, sampled at an offset,
// output as the premultiplied shadow colour. radius 0 is a single tap (hard shadow, or no blur on that axis).
layout(std140, binding = 1) uniform ShadowParams {
    vec4 color;       // premultiplied shadow colour, opacity included
    vec2 texel_step;  // one texel along the blur axis, in UV
    vec2 offset;      // shadow displacement in UV (subtracted from the sample position)
    float sigma;
    int radius;
} sp;  // named so the GLSL output doesn't auto-name it and clash with the vertex block

layout(binding = 2) uniform sampler2D tex;

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    vec2 uv = vTexCoord - sp.offset;
    float a = 0.0;
    if (sp.radius <= 0) {
        a = textureLod(tex, uv, 0.0).a;
    } else {
        float k = -0.5 / (sp.sigma * sp.sigma);
        float wsum = 0.0;
        for (int i = -sp.radius; i <= sp.radius; i++) {
            float w = exp(float(i * i) * k);
            a += textureLod(tex, uv + float(i) * sp.texel_step, 0.0).a * w;
            wsum += w;
        }
        a /= wsum;
    }
    fragColor = sp.color * a;
}
