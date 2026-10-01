#version 440

layout(std140, binding = 1) uniform FragParams {
  float strength;
  float lutSize;
};
layout(binding = 2) uniform sampler2D image;
layout(binding = 3) uniform sampler2D lut;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

vec3 sampleLut(vec3 color) {
  float scaledB = color.b * (lutSize - 1.0);
  float bz0 = floor(scaledB);
  float bz1 = min(bz0 + 1.0, lutSize - 1.0);
  float blend = fract(scaledB);

  vec2 texSize = vec2(lutSize * lutSize, lutSize);
  vec2 uv0 = vec2(bz0 * lutSize + color.r * (lutSize - 1.0) + 0.5, color.g * (lutSize - 1.0) + 0.5) / texSize;
  vec2 uv1 = vec2(bz1 * lutSize + color.r * (lutSize - 1.0) + 0.5, color.g * (lutSize - 1.0) + 0.5) / texSize;

  return mix(texture(lut, uv0).rgb, texture(lut, uv1).rgb, blend);
}

void main(void) {
  vec4 original = texture(image, vTexCoord);
  vec3 graded = sampleLut(original.rgb);
  fragColor = vec4(mix(original.rgb, graded, strength * 0.01), original.a);
}