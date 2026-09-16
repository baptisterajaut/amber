#version 440

layout(std140, binding = 1) uniform FragParams {
  vec2 resolution;
  float region_x;
  float region_y;
  float region_w;
  float region_h;                      
  float pixel_size;
  float feather;
};
layout(binding = 2) uniform sampler2D image;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;
                                                                   
void main(void) {
  vec2 uv = vTexCoord;

  float rx = region_x * 0.01;
  float ry = region_y * 0.01;
  float rw = region_w * 0.01;
  float rh = region_h * 0.01;                                         
  float fe = feather * 0.01;

  float inside_x = smoothstep(rx, rx + fe, uv.x) * smoothstep(rx + rw, rx + rw - fe, uv.x);
  float inside_y = smoothstep(ry, ry + fe, uv.y) * smoothstep(ry + rh, ry + rh - fe, uv.y);
  float strength = inside_x * inside_y;
// interpolate the GRID SIZE itself: at strength 0 the grid matches the
// native resolution (blocks are ~1 real pixel wide, i.e. no visible
// pixelation at all), at strength 1 it's your chosen pixel_size. The
// block size grows smoothly across the feather band instead of two
// fixed images fading into each other.
  vec2 grid = mix(resolution, vec2(pixel_size), strength);
  vec2 sample_uv = floor(uv * grid) / grid;

  fragColor = texture(image, sample_uv);
}                                                 