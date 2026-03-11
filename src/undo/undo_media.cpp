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

#include "undo_media.h"

#include "panels/panels.h"
#include "panels/project.h"
#include "project/footage.h"
#include "project/media.h"
#include "project/previewgenerator.h"

AddMediaCommand::AddMediaCommand(MediaPtr iitem, Media *iparent) :
  item(iitem),
  parent(iparent),
  done_(false)
{
  doRedo();
}

void AddMediaCommand::doUndo() {
  olive::project_model.removeChild(parent, item.get());
  done_ = false;
}

void AddMediaCommand::doRedo() {
  if (!done_) {
    olive::project_model.appendChild(parent, item);
    done_ = true;
  }
}

DeleteMediaCommand::DeleteMediaCommand(MediaPtr i) :
  item(i),
  parent(i->parentItem())
{
}

void DeleteMediaCommand::doUndo() {
  olive::project_model.appendChild(parent, item);
}

void DeleteMediaCommand::doRedo() {
  olive::project_model.removeChild(parent, item.get());
}

ReplaceMediaCommand::ReplaceMediaCommand(MediaPtr i, QString s) {
  item = i;
  new_filename = s;
  old_filename = item->to_footage()->url;
}

void ReplaceMediaCommand::replace(QString& filename) {
  // close any clips currently using this media
  QVector<Media*> all_sequences = panel_project->list_all_project_sequences();
  for (int i=0;i<all_sequences.size();i++) {
    Sequence* s = all_sequences.at(i)->to_sequence().get();
    for (int j=0;j<s->clips.size();j++) {
      ClipPtr c = s->clips.at(j);
      if (c != nullptr && c->media() == item.get() && c->IsOpen()) {
        c->Close(true);
        c->replaced = true;
      }
    }
  }

  // replace media
  QStringList files;
  files.append(filename);
  panel_project->process_file_list(files, false, item, nullptr);
  PreviewGenerator::AnalyzeMedia(item.get());
}

void ReplaceMediaCommand::doUndo() {
  replace(old_filename);


}

void ReplaceMediaCommand::doRedo() {
  replace(new_filename);
}

MediaMove::MediaMove() {}

void MediaMove::doUndo() {
  for (int i=0;i<items.size();i++) {
    olive::project_model.moveChild(items.at(i), froms.at(i));
  }
}

void MediaMove::doRedo() {
  if (to == nullptr) to = olive::project_model.get_root();
  froms.resize(items.size());
  for (int i=0;i<items.size();i++) {
    Media* parent = items.at(i)->parentItem();
    froms[i] = parent;
    olive::project_model.moveChild(items.at(i), to);
  }
}

MediaRename::MediaRename(Media* iitem, QString ito) {
  item = iitem;
  from = iitem->get_name();
  to = ito;
}

void MediaRename::doUndo() {
  item->set_name(from);

}

void MediaRename::doRedo() {
  item->set_name(to);
}

UpdateFootageTooltip::UpdateFootageTooltip(Media *i) {
  item = i;
}

void UpdateFootageTooltip::doUndo() {
  redo();
}

void UpdateFootageTooltip::doRedo() {
  item->update_tooltip();
}
