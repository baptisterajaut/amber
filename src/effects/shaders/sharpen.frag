#version 440

layout(std140, binding = 1) uniform FragParams {
  vec2 resolution;
  float amount;
  float radius;
};
layout(binding = 2) uniform sampler2D image;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main(void) {
  vec2 texel = radius / resolution;

  vec4 original = texture(image, vTexCoord);

  // cheap 4-neighbor blur approximation rather than a full multi-tap
  // gaussian - good enough to isolate "detail" for unsharp masking,
  // much cheaper per-pixel than the real thing
  vec4 blurred = original;
  blurred += texture(image, vTexCoord + vec2( texel.x, 0.0));
  blurred += texture(image, vTexCoord + vec2(-texel.x, 0.0));
  blurred += texture(image, vTexCoord + vec2(0.0,  texel.y));
  blurred += texture(image, vTexCoord + vec2(0.0, -texel.y));
  blurred *= 0.2; // average of 5 samples (center + 4 neighbors)

  vec4 detail = original - blurred;
  fragColor = original + detail * (amount * 0.01);
  fragColor.a = original.a; // don't sharpen the alpha channel
}
