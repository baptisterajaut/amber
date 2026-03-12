#include "playbutton.h"

PlayButton::PlayButton(QWidget* parent)
    : QPushButton(parent), play_text(QStringLiteral(">")), pause_text(QStringLiteral("||")) {
  setText(play_text);
}
