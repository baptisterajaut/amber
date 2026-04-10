#ifndef CORE_STYLE_H
#define CORE_STYLE_H

#include <cstdint>

namespace amber {
namespace styling {

/**
 * @brief Officially supported styles to use in Amber
 */
enum Style : uint8_t {
  /**
    Qt Fusion-based cross-platform UI. The default styling of Amber. Can also be heavily customized with a CSS
    file.
    */
  kAmberDefaultDark,

  /**
    Qt Fusion-based cross-platform UI. The default styling of Amber. Can also be heavily customized with a CSS
    file. This will use the
    */
  kAmberDefaultLight,

  /**
    Use current OS's native styling (or at least Qt's default). Most UIs use a light theming, so this will
    automatically implement dark icons/UI elements.
    */
  kNativeDarkIcons,

  /**
    Use current OS's native styling (or at least Qt's default). Most UIs use a light theming, but in case one
    doesn't, this option will provide light icons for use with a dark theme.
    */
  kNativeLightIcons
};

}  // namespace styling
}  // namespace amber

#endif  // CORE_STYLE_H
