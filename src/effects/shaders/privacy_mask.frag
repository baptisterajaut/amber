#version 440

layout(std140, binding = 1) uniform FragParams {
  vec2 resolution;
  float centerX;
  float centerY;
  float maskRadius;
  float maskSoftness;
  float intensity;
  bool pixelateMode;
};

layout(binding = 2) uniform sampler2D sceneTex;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main(void) {
  if (maskRadius == 0.0) {
    fragColor = texture(sceneTex, vTexCoord);
    return;
  }

  // Aspect ratio, used to keep the circular mask round on non-square frames
  float ar = resolution.x / resolution.y;

  // Convert the 0-100 center/radius inputs to UV space, correcting for
  // aspect ratio so the mask stays circular regardless of frame shape
  vec2 aspectCoord = vec2(vTexCoord.x * ar, vTexCoord.y);
  vec2 aspectCenter = vec2((centerX * 0.01) * ar, centerY * 0.01);

  float dist = distance(aspectCoord, aspectCenter);
  float size = maskRadius * 0.01;

  // Smooth falloff at the mask's edge, controlled by maskSoftness
  float innerEdge = size * (1.0 - maskSoftness * 0.01);
  float maskFactor = smoothstep(size, innerEdge, dist);

  vec4 sourceColor = texture(sceneTex, vTexCoord);
  vec4 censoredColor = vec4(0.0);

  if (pixelateMode) {
    // Pixelate: sample from a coarser grid, size controlled by intensity
    float blocks = max(4.0, 200.0 - intensity * 1.8);
    vec2 pixelatedCoord =
        vec2(floor(vTexCoord.x * blocks) / blocks, floor(vTexCoord.y * (blocks / ar)) / (blocks / ar));
    censoredColor = texture(sceneTex, pixelatedCoord);
  } else {
    // Blur: simple 9-tap box blur, pixel-sized step so the blur radius
    // stays consistent regardless of the clip's resolution
    // CBA: 9 taps stay a blur only up to ~3 px apart (wider reads as ghost copies), so this tops out
    // as a light blur; a separable multi-tap pass if it needs to hide more
    vec2 texel = (intensity * 0.03) / resolution;
    vec4 blurAccumulator = vec4(0.0);

    blurAccumulator += texture(sceneTex, vTexCoord + vec2(-texel.x, -texel.y));
    blurAccumulator += texture(sceneTex, vTexCoord + vec2(0.0, -texel.y));
    blurAccumulator += texture(sceneTex, vTexCoord + vec2(texel.x, -texel.y));
    blurAccumulator += texture(sceneTex, vTexCoord + vec2(-texel.x, 0.0));
    blurAccumulator += texture(sceneTex, vTexCoord + vec2(0.0, 0.0));
    blurAccumulator += texture(sceneTex, vTexCoord + vec2(texel.x, 0.0));
    blurAccumulator += texture(sceneTex, vTexCoord + vec2(-texel.x, texel.y));
    blurAccumulator += texture(sceneTex, vTexCoord + vec2(0.0, texel.y));
    blurAccumulator += texture(sceneTex, vTexCoord + vec2(texel.x, texel.y));

    censoredColor = blurAccumulator / 9.0;
  }

  fragColor = mix(sourceColor, censoredColor, maskFactor);
}
