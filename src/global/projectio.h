#ifndef PROJECTIO_H
#define PROJECTIO_H

#include <QObject>
#include <QStringList>
#include <QTimer>

#include "engine/sequence.h"

class ProjectIO : public QObject {
  Q_OBJECT
 public:
  explicit ProjectIO(QObject* parent = nullptr);

  const QString& projectFilename() const;
  void setProjectFilename(const QString& path);

  bool isModified() const;
  void setModified(bool modified);

  QStringList& recentProjects();
  QString recentProjectListFile() const;
  void saveRecentProjects();
  void addRecentProject(const QString& url);

  const QString& autorecoveryFilename() const;
  void initAutorecovery();
  void reconfigureAutorecovery();
  void saveAutorecoveryFile();

  void setRenderingState(bool rendering);

  void setSequence(SequencePtr s, bool record_history = false);
  void goBackSequence();
  bool canGoBack() const;
  const QVector<SequencePtr>& sequenceHistory() const;
  void clearSequenceHistory();

  enum CloseResult { Clean, NeedsSave, Cancel };
  CloseResult canCloseProject() const;

  const QString& projectFileFilter() const;

 signals:
  void projectFilenameChanged(const QString& path);
  void modifiedChanged(bool modified);
  void projectCleared();
  void sequenceChanged(SequencePtr s);
  void autorecoverySaveRequested();

 private:
  QString project_filename_;
  QString project_file_filter_;
  bool modified_{false};
  bool changed_since_last_autorecovery_{false};

  QStringList recent_projects_;
  QString autorecovery_filename_;
  QTimer autorecovery_timer_;

  QVector<SequencePtr> sequence_history_;
};

namespace amber {
extern ProjectIO* project_io;
}

#endif  // PROJECTIO_H
