#version 440
// Multiplies the clip by a mask image (binding 3), pinned to the clip's own texture space, before Transform.
// vTexCoord.y = 0 is the clip's top on every backend, and so is the mask texture's first row: no flip anywhere.
layout(std140, binding = 1) uniform FragParams {
  vec2 resolution;
  vec2 mask_size;  // the loaded mask's pixel size, (0, 0) with no file or a failed decode
  int channel;     // 0 = alpha, 1 = luminance
  int fit;         // 0 = stretch, 1 = fit, 2 = fill
  float pos_x;
  float pos_y;
  float mask_scale;
  float feather;
  bool invert;
  float opacity;
  float render_scale;  // auto: render pixels per sequence pixel (below 1 at a preview divider)
};
layout(binding = 2) uniform sampler2D myTexture;
layout(binding = 3) uniform sampler2D maskTex;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

// The mask rectangle's size in clip UV: Stretch is the clip, Fit / Fill keep the mask's aspect inside / over it
vec2 maskRect() {
  vec2 rect = vec2(1.0);
  if (fit != 0) {
    vec2 k = resolution / mask_size;
    rect = mask_size * (fit == 1 ? min(k.x, k.y) : max(k.x, k.y)) / resolution;
  }
  return rect * (mask_scale * 0.01);
}

// The raw mask value at clip UV `uv`, 0 outside the mask rectangle
float maskAt(vec2 uv, vec2 rect) {
  vec2 m = (uv - vec2(pos_x, pos_y) * 0.01) / rect + 0.5;
  if (any(lessThan(m, vec2(0.0))) || any(greaterThan(m, vec2(1.0)))) return 0.0;
  vec4 c = textureLod(maskTex, m, 0.0);  // explicit LOD: called in divergent control flow (bounds test, feather loop)
  // The mask is straight alpha: luma of the colour times its alpha, so black-on-transparent and white-on-black work
  return channel == 1 ? dot(c.rgb, vec3(0.2126, 0.7152, 0.0722)) * c.a : c.a;
}

void main(void) {
  vec4 tex_color = texture(myTexture, vTexCoord);
  if (mask_size.x <= 0.0 || mask_size.y <= 0.0) {
    fragColor = tex_color;  // no mask: a passthrough, whatever Invert says
    return;
  }

  vec2 rect = maskRect();
  float m = maskAt(vTexCoord, rect);
  if (feather > 0.0) {
    // 24-tap golden-angle disk of radius `feather` sequence px (render_scale render px), one tap at the centre
    for (int i = 1; i < 24; i++) {
      float a = float(i) * 2.39996323;
      vec2 offset = vec2(cos(a), sin(a)) * (feather * render_scale * sqrt(float(i) / 23.0));
      m += maskAt(vTexCoord + offset / resolution, rect);
    }
    m /= 24.0;
  }
  if (invert) m = 1.0 - m;
  m = mix(1.0, m, opacity * 0.01);

  // input is premultiplied: scaling rgb and alpha together is the masked colour
  fragColor = tex_color * m;
}
