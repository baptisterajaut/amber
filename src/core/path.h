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

#ifndef CORE_PATH_H
#define CORE_PATH_H

#include <QString>
#include <QDir>

QString get_app_path();
QString get_data_path();
QDir get_data_dir();
QString get_config_path();
QDir get_config_dir();
QList<QString> get_effects_paths();
QList<QString> get_language_paths();

// generate hash algorithm used to uniquely identify files
QString get_file_hash(const QString& filename);

// true if both paths name the same file: canonical paths (symlinks resolved) when the files exist, cleaned absolute
// paths otherwise; case-insensitive on Windows and macOS
bool same_file_path(const QString& a, const QString& b);

// Proxy file for the footage at `source` inside `folder`: "<name>.mov" (name = file name minus its last suffix, so
// "2024.05.01_trip.mp4" and "2024.05.02_trip.mp4" don't both become "2024.mov"), or "<name>.proxy.mov" (then
// "<name>.proxy2.mov"...) when that would be `source` itself or one of `avoid` (other sources or proxies of the same
// batch), so a proxy is never written over a source file
QString proxy_file_path(const QString& source, const QString& folder, const QStringList& avoid);

#endif // CORE_PATH_H
