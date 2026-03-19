#ifndef STYLING_H
#define STYLING_H

#include <QColor>

#include "core/style.h"

namespace amber {
  namespace styling {

    /**
     * @brief Return whether to use dark icons or light icons
     * @return
     *
     * **TRUE** if icons should be dark.
     */
    bool UseDarkIcons();

    /**
     * @brief Return whether to use native UI or Fusion
     * @return
     *
     * **TRUE** if UI should use native styling
     */
    bool UseNativeUI();

    /**
     * @brief Return the current icon color based on Config::use_dark_icons.
     *
     * Also used by some other UI elements like the lines and text on the TimelineHeader
     *
     * @return
     *
     * Either white or black depending on Config::use_dark_icons
     */
    QColor GetIconColor();
  }
}

#endif // STYLING_H
