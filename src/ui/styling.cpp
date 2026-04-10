#include "styling.h"

#include "global/config.h"

bool amber::styling::UseDarkIcons()
{
  return amber::CurrentConfig.style == kAmberDefaultLight || amber::CurrentConfig.style == kNativeDarkIcons;
}

QColor amber::styling::GetIconColor()
{
  if (UseDarkIcons()) {
    return Qt::black;
  } else {
    return Qt::white;
  }
}



bool amber::styling::UseNativeUI()
{
  return amber::CurrentConfig.style == kNativeLightIcons || amber::CurrentConfig.style == kNativeDarkIcons;
}
