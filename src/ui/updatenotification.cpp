#include "updatenotification.h"

#include <QNetworkAccessManager>
#include <QStatusBar>

#include "mainwindow.h"

UpdateNotification olive::update_notifier;

UpdateNotification::UpdateNotification()
{

}

void UpdateNotification::check()
{
#if defined(GITHASH) && defined(UPDATEMSG)
  QNetworkAccessManager* manager = new QNetworkAccessManager();

  connect(manager, SIGNAL(finished(QNetworkReply *)), this, SLOT(finished_slot(QNetworkReply *)));
  connect(manager, SIGNAL(finished(QNetworkReply *)), manager, SLOT(deleteLater()));

  QString update_url = QString("https://github.com/baptisterajaut/amber/releases?version=0&hash=%1");

  QNetworkRequest request(QUrl(update_url.arg(GITHASH)));
  manager->get(request);
#endif
}

void UpdateNotification::finished_slot(QNetworkReply *reply)
{
  QString response = QString::fromUtf8(reply->readAll());

  if (response == "1") {
    olive::MainWindow->statusBar()->showMessage(tr("An update is available. "
                                                   "Visit github.com/baptisterajaut/amber to download it."));
  }
}
