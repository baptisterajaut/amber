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

#ifndef UNDO_MEDIA_H
#define UNDO_MEDIA_H

#include "undoactions.h"

class AddMediaCommand : public AmberAction {
public:
  AddMediaCommand(MediaPtr iitem, Media* iparent);
  void doUndo() override;
  void doRedo() override;
private:
  MediaPtr item;
  Media* parent;
  bool done_{false};
};

class DeleteMediaCommand : public AmberAction {
public:
  explicit DeleteMediaCommand(const MediaPtr& i);
  void doUndo() override;
  void doRedo() override;
private:
  MediaPtr item;
  Media* parent;
};

class ReplaceMediaCommand : public AmberAction {
public:
  ReplaceMediaCommand(MediaPtr, QString);
  void doUndo() override;
  void doRedo() override;
private:
  MediaPtr item;
  QString old_filename;
  QString new_filename;
  void replace(QString& filename);
};

class MediaMove : public AmberAction {
public:
  MediaMove();
  QVector<MediaPtr> items;
  Media* to;
  void doUndo() override;
  void doRedo() override;
private:
  QVector<Media*> froms;
};

class MediaRename : public AmberAction {
public:
  MediaRename(Media* iitem, QString to);
  void doUndo() override;
  void doRedo() override;
private:
  Media* item;
  QString from;
  QString to;
};

class UpdateFootageTooltip : public AmberAction {
public:
  explicit UpdateFootageTooltip(Media* i);
  void doUndo() override;
  void doRedo() override;
private:
  Media* item;
};

#endif // UNDO_MEDIA_H
