#include "icons.h"

#include <QDebug>
#include <QPainter>

#include "global/global.h"
#include "global/config.h"
#include "ui/styling.h"

QIcon amber::icon::LeftArrow;
QIcon amber::icon::RightArrow;
QIcon amber::icon::UpArrow;
QIcon amber::icon::DownArrow;
QIcon amber::icon::Diamond;
QIcon amber::icon::Clock;

QIcon amber::icon::MediaVideo;
QIcon amber::icon::MediaAudio;
QIcon amber::icon::MediaImage;
QIcon amber::icon::MediaError;
QIcon amber::icon::MediaSequence;
QIcon amber::icon::MediaFolder;

QIcon amber::icon::ViewerGoToStart;
QIcon amber::icon::ViewerPrevFrame;
QIcon amber::icon::ViewerPlay;
QIcon amber::icon::ViewerPause;
QIcon amber::icon::ViewerNextFrame;
QIcon amber::icon::ViewerGoToEnd;

QIcon amber::icon::CreateIconFromSVG(const QString &path, bool create_disabled)
{
  QIcon icon;

  QPainter p;

  // Draw the icon as a solid color
  QPixmap normal(path);

  // Color the icon dark if the user is using a dark theme
  if (amber::styling::UseDarkIcons()) {
    p.begin(&normal);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(normal.rect(), QColor(32, 32, 32));
    p.end();
  }

  icon.addPixmap(normal, QIcon::Normal, QIcon::On);

  if (create_disabled) {

    // Create semi-transparent disabled icon
    QPixmap disabled(normal.size());
    disabled.fill(Qt::transparent);

    // draw semi-transparent version of icon for the disabled variant
    p.begin(&disabled);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    p.setOpacity(0.5);
    p.drawPixmap(0, 0, normal);
    p.end();

    icon.addPixmap(disabled, QIcon::Disabled, QIcon::On);

  }

  return icon;
}

void amber::icon::Initialize()
{
  qInfo() << "Initializing icons";

  LeftArrow = CreateIconFromSVG(":/icons/tri-left.svg", false);
  RightArrow = CreateIconFromSVG(":/icons/tri-right.svg", false);
  UpArrow = CreateIconFromSVG(":/icons/tri-up.svg", false);
  DownArrow = CreateIconFromSVG(":/icons/tri-down.svg", false);
  Diamond = CreateIconFromSVG(":/icons/diamond.svg", false);
  Clock = CreateIconFromSVG(":/icons/clock.svg", false);

  MediaVideo = CreateIconFromSVG(":/icons/videosource.svg");
  MediaAudio = CreateIconFromSVG(":/icons/audiosource.svg");
  MediaImage = CreateIconFromSVG(":/icons/imagesource.svg", false);
  MediaError = CreateIconFromSVG(":/icons/error.svg", false);
  MediaSequence = CreateIconFromSVG(":/icons/sequence.svg", false);
  MediaFolder = CreateIconFromSVG(":/icons/folder.svg", false);

  ViewerGoToStart = CreateIconFromSVG(QStringLiteral(":/icons/prev.svg"));
  ViewerPrevFrame = CreateIconFromSVG(QStringLiteral(":/icons/rew.svg"));
  ViewerPlay = CreateIconFromSVG(QStringLiteral(":/icons/play.svg"));
  ViewerPause = CreateIconFromSVG(":/icons/pause.svg", false);
  ViewerNextFrame = CreateIconFromSVG(QStringLiteral(":/icons/ff.svg"));
  ViewerGoToEnd = CreateIconFromSVG(QStringLiteral(":/icons/next.svg"));

  qInfo() << "Finished initializing icons";
}
