#ifndef CORE_KEYFRAME_H
#define CORE_KEYFRAME_H

#include <QVariant>

class EffectKeyframe {
 public:
  EffectKeyframe() = default;

  int type{0};
  long time{0};
  QVariant data;

  // only for bezier type
  double pre_handle_x{-40};
  double pre_handle_y{0};
  double post_handle_x{40};
  double post_handle_y{0};
};

#endif  // CORE_KEYFRAME_H
