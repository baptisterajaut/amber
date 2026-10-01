#include "headlessappcontext.h"

#include <QDebug>
#include <QIcon>

int HeadlessAppContext::showMessage(const QString& title, const QString& text, int type) {
  // type is a QMessageBox::Icon: 2 = Warning, 3 = Critical. The return value is unused by every caller.
  if (type >= 2) {
    qWarning().noquote() << title + ":" << text;
  } else {
    qInfo().noquote() << title + ":" << text;
  }
  return 0;
}

bool HeadlessAppContext::showQuestion(const QString& title, const QString& text, bool) {
  qWarning().noquote() << title + ":" << text << "-> answered Yes (headless)";
  return true;
}

QIcon HeadlessAppContext::mediaTypeIcon(int) { return {}; }
