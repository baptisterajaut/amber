#version 440
layout(std140, binding = 1) uniform FragParams {
    vec2 resolution;
    float centerX;
    float centerY;
    float radius;
    float thickness;
    float feather;
    vec3 ringColor;
};
layout(binding = 2) uniform sampler2D image;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main(void) {
  vec4 original = texture(image, vTexCoord);

  float ar = resolution.x / resolution.y;

  // center everything at the origin first, THEN scale x by aspect ratio -
  // keeps the true center exact for any frame shape, unlike scaling first
  vec2 p = (vTexCoord - 0.5);
  p.x *= ar;

  vec2 center = vec2((centerX - 50.0) * 0.01 * ar, (centerY - 50.0) * 0.01);
  float dist = distance(p, center);

  float r = radius * 0.01;
  float halfThick = thickness * 0.005;
  float fe = max(feather * 0.01, 0.0001);

  float ringDist = abs(dist - r);
  float ringMask = 1.0 - smoothstep(halfThick - fe, halfThick, ringDist);

  fragColor = vec4(mix(original.rgb, ringColor, ringMask), original.a);
}
