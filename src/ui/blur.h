#ifndef BLUR_H
#define BLUR_H

// Redirects to core/blur.h. The blur function moved from amber::ui to amber namespace.
#include "core/blur.h"

// Backward-compat alias for existing callers using amber::ui::blur()
namespace amber {
namespace ui {
using ::amber::blur;
}
}  // namespace amber

#endif  // BLUR_H
