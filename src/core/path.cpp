/***

    Olive - Non-Linear Video Editor
    Copyright (C) 2019  Olive Team

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#include "path.h"

#include <QStandardPaths>
#include <QFileInfo>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>


QString get_app_path() {
  return QCoreApplication::applicationDirPath();
}

QDir get_app_dir() {
  return QDir(get_app_path());
}

QString get_data_path() {
  QDir app_dir = get_app_dir();
  if (QFileInfo::exists(app_dir.filePath("portable"))) {
    return app_dir.absolutePath();
  } else {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  }
}

QDir get_data_dir() {
  return QDir(get_data_path());
}

QString get_config_path() {
  QDir app_dir = get_app_dir();
  if (QFileInfo::exists(app_dir.filePath("portable"))) {
    return app_dir.absolutePath();
  } else {
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
  }
}

QDir get_config_dir() {
  return QDir(get_config_path());
}

QList<QString> get_effects_paths() {
  // returns a list of the effects paths to search

  QList<QString> effects_paths;

  // get current app working directory
  QDir app_dir = get_app_dir();

  // "effects" subfolder in program folder - best for Windows
  effects_paths.append(app_dir.filePath("effects"));

  // "Effects" folder one level above the program's directory - best for Mac
  effects_paths.append(app_dir.filePath("../Effects"));

  // folder in share folder - best for Linux
  effects_paths.append(app_dir.filePath("../share/amber-editor/effects"));

  // user path - best for linux
  effects_paths.append(QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("effects"));

  // Olive will also accept a manually provided folder with an environment variable
  QString env_path(qgetenv("OLIVE_EFFECTS_PATH"));
  if (!env_path.isEmpty()) effects_paths.append(env_path);

  return effects_paths;
}

QString get_file_hash(const QString& filename) {
  QFileInfo file_info(filename);

  QString cache_file = filename.mid(filename.lastIndexOf('/')+1)
      + QString::number(file_info.size())
      + QString::number(file_info.lastModified().toMSecsSinceEpoch());

  return QCryptographicHash::hash(cache_file.toUtf8(), QCryptographicHash::Md5).toHex();
}

QList<QString> get_language_paths() {
  QList<QString> language_paths;

  // get current app working directory
  QDir app_dir = get_app_dir();

  // program folder itself - for running from the build directory
  language_paths.append(app_dir.absolutePath());

  // subfolder in program folder - best for Windows
  language_paths.append(app_dir.filePath("ts"));

  // folder one level above the program's directory - best for Mac
  language_paths.append(app_dir.filePath("../Translations"));

  // folder in share folder - best for Linux
  language_paths.append(app_dir.filePath("../share/amber-editor/ts"));

  // Olive will also accept a manually provided folder with an environment variable
  QString env_path(qgetenv("OLIVE_LANG_PATH"));
  if (!env_path.isEmpty()) language_paths.append(env_path);

  return language_paths;
}

static QString comparable_path(const QString& path) {
  QFileInfo info(path);
  QString canonical = info.canonicalFilePath();
  return canonical.isEmpty() ? QDir::cleanPath(info.absoluteFilePath()) : canonical;
}

bool same_file_path(const QString& a, const QString& b) {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
  const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
  const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
  return comparable_path(a).compare(comparable_path(b), cs) == 0;
}

QString proxy_file_path(const QString& source, const QString& folder, const QStringList& avoid) {
  const QString name = QFileInfo(source).completeBaseName();
  const QDir dir(folder);
  for (int i = 0;; i++) {
    QString candidate =
        dir.filePath(i == 0 ? name + ".mov" : name + ".proxy" + (i > 1 ? QString::number(i) : QString()) + ".mov");
    bool taken = same_file_path(candidate, source);
    for (const QString& path : avoid) taken = taken || same_file_path(candidate, path);
    if (!taken) return candidate;
  }
}
