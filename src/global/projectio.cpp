#include "projectio.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>

#include "core/audio.h"
#include "core/path.h"
#include "global/config.h"

ProjectIO* amber::project_io = nullptr;

ProjectIO::ProjectIO(QObject* parent) : QObject(parent) {
  project_file_filter_ = tr("Amber Project %1").arg("(*.ove)");
}

const QString& ProjectIO::projectFilename() const { return project_filename_; }

void ProjectIO::setProjectFilename(const QString& path) {
  project_filename_ = path;
  emit projectFilenameChanged(path);
}

bool ProjectIO::isModified() const { return modified_; }

void ProjectIO::setModified(bool modified) {
  modified_ = modified;
  changed_since_last_autorecovery_ = modified;
  emit modifiedChanged(modified);
}

QStringList& ProjectIO::recentProjects() { return recent_projects_; }

QString ProjectIO::recentProjectListFile() const { return get_data_dir().filePath("recents"); }

void ProjectIO::saveRecentProjects() {
  QFile f(recentProjectListFile());
  if (f.open(QFile::WriteOnly | QFile::Truncate | QFile::Text)) {
    QTextStream out(&f);
    for (const auto& path : recent_projects_) {
      out << path << "\n";
    }
    f.close();
  }
}

void ProjectIO::addRecentProject(const QString& url) {
  recent_projects_.removeAll(url);
  recent_projects_.prepend(url);
  if (recent_projects_.size() > 10) {
    recent_projects_.removeLast();
  }
  saveRecentProjects();
}

const QString& ProjectIO::autorecoveryFilename() const { return autorecovery_filename_; }

void ProjectIO::initAutorecovery() {
  QString data_dir = get_data_path();
  if (!data_dir.isEmpty()) {
    autorecovery_filename_ = data_dir + "/autorecovery.ove";
    autorecovery_timer_.setInterval(amber::CurrentConfig.autorecovery_interval * 60000);
    connect(&autorecovery_timer_, &QTimer::timeout, this, &ProjectIO::saveAutorecoveryFile);
    if (amber::CurrentConfig.autorecovery_enabled) {
      autorecovery_timer_.start();
    }
  }
}

void ProjectIO::reconfigureAutorecovery() {
  autorecovery_timer_.stop();
  if (amber::CurrentConfig.autorecovery_enabled) {
    autorecovery_timer_.setInterval(amber::CurrentConfig.autorecovery_interval * 60000);
    autorecovery_timer_.start();
  }
}

void ProjectIO::saveAutorecoveryFile() {
  if (changed_since_last_autorecovery_) {
    changed_since_last_autorecovery_ = false;
    emit autorecoverySaveRequested();
  }
}

void ProjectIO::setRenderingState(bool rendering) {
  audio_rendering = rendering;
  if (rendering) {
    autorecovery_timer_.stop();
  } else if (amber::CurrentConfig.autorecovery_enabled) {
    autorecovery_timer_.start();
  }
}

void ProjectIO::setSequence(SequencePtr s, bool record_history) {
  if (record_history && amber::ActiveSequence != nullptr && s != nullptr && amber::ActiveSequence != s) {
    sequence_history_.append(amber::ActiveSequence);
  }

  if (s == nullptr) {
    sequence_history_.clear();
  }

  amber::ActiveSequence = s;
  emit sequenceChanged(s);
}

void ProjectIO::goBackSequence() {
  if (sequence_history_.isEmpty()) return;
  SequencePtr prev = sequence_history_.takeLast();
  amber::ActiveSequence = prev;
  emit sequenceChanged(prev);
}

bool ProjectIO::canGoBack() const { return !sequence_history_.isEmpty(); }

const QVector<SequencePtr>& ProjectIO::sequenceHistory() const { return sequence_history_; }

void ProjectIO::clearSequenceHistory() { sequence_history_.clear(); }

ProjectIO::CloseResult ProjectIO::canCloseProject() const {
  if (!modified_) return Clean;
  return NeedsSave;
}

const QString& ProjectIO::projectFileFilter() const { return project_file_filter_; }
