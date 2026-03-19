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

#include "preferencesdialog.h"

#include <QMenuBar>
#include <QAction>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QLineEdit>
#include <QLabel>
#include <QComboBox>
#include <QGroupBox>
#include <QCheckBox>
#include <QRadioButton>
#include <QDialogButtonBox>
#include <QTreeWidget>
#include <QVector>
#include <QPushButton>
#include <QTreeWidgetItem>
#include <QList>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QApplication>
#include <QProcess>
#include <QDebug>

#include "global/global.h"
#include "global/config.h"
#include "core/path.h"
#include "ui/styling.h"
#include "rendering/audio.h"
#include "rendering/audio_ui.h"
#include "panels/panels.h"
#include "ui/columnedgridlayout.h"
#include "ui/mainwindow.h"
#include "dialogs/newsequencedialog.h"

KeySequenceEditor::KeySequenceEditor(QWidget* parent, QAction* a)
  : QKeySequenceEdit(parent), action(a) {
  setKeySequence(action->shortcut());
}

void KeySequenceEditor::set_action_shortcut() {
  action->setShortcut(keySequence());
}

void KeySequenceEditor::reset_to_default() {
  setKeySequence(action->property("default").toString());
}

QString KeySequenceEditor::action_name() {
  return action->property("id").toString();
}

QString KeySequenceEditor::export_shortcut() {
  QString ks = keySequence().toString();
  if (ks != action->property("default")) {
    return action->property("id").toString() + "\t" + ks;
  }
  return nullptr;
}

PreferencesDialog::PreferencesDialog(QWidget *parent) :
  QDialog(parent)
{
  setWindowTitle(tr("Preferences"));

  setup_ui();

  setup_kbd_shortcuts(amber::MainWindow->menuBar());

  // set up default sequence
  default_sequence.name = tr("Default Sequence");
  default_sequence.width = amber::CurrentConfig.default_sequence_width;
  default_sequence.height = amber::CurrentConfig.default_sequence_height;
  default_sequence.frame_rate = amber::CurrentConfig.default_sequence_framerate;
  default_sequence.audio_frequency = amber::CurrentConfig.default_sequence_audio_frequency;
  default_sequence.audio_layout = amber::CurrentConfig.default_sequence_audio_channel_layout;
}

void PreferencesDialog::setup_kbd_shortcut_worker(QMenu* menu, QTreeWidgetItem* parent) {
  QList<QAction*> actions = menu->actions();
  for (auto a : actions) {
    if (!a->isSeparator() && a->property("keyignore").isNull()) {
      QTreeWidgetItem* item = new QTreeWidgetItem(parent);
      item->setText(0, a->text().replace("&", ""));

      parent->addChild(item);

      if (a->menu() != nullptr) {
        item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
        setup_kbd_shortcut_worker(a->menu(), item);
      } else {
        key_shortcut_items.append(item);
        key_shortcut_actions.append(a);
      }
    }
  }
}

void PreferencesDialog::delete_previews(char type) {
  if (type != 't' && type != 'w' && type != 1) return;

  QDir preview_path(get_data_path() + "/previews");

  if (type == 1) {
    // indiscriminately delete everything
    preview_path.removeRecursively();
  } else {
    QStringList preview_file_list = preview_path.entryList(QDir::Files | QDir::NoDotAndDotDot);
    for (const auto & preview_file_str : preview_file_list) {

      // use filename to determine whether this is a thumbnail or a waveform
      int identifier_char_index = qMax(0, preview_file_str.size()-2);

      // find identifier char
      while (identifier_char_index >= 0
             && preview_file_str.at(identifier_char_index).unicode() >= 48
             && preview_file_str.at(identifier_char_index).unicode() <= 57) {
        identifier_char_index--;
      }

      // thumbnails will have a 't' towards the end of the filenames, waveforms will have a 'w'
      // if they match the type of preview we're deleting, remove them
      if (preview_file_str.at(identifier_char_index) == type) {
        QFile::remove(preview_path.filePath(preview_file_str));
      }
    }
  }
}

void PreferencesDialog::AddBoolPair(QCheckBox *ui, bool *value, bool restart_required)
{
  bool_ui.append(ui);
  bool_value.append(value);
  bool_restart_required.append(restart_required);

  ui->setChecked(*value);
}

void PreferencesDialog::setup_kbd_shortcuts(QMenuBar* menubar) {
  QList<QAction*> menus = menubar->actions();

  for (auto i : menus) {
    QMenu* menu = i->menu();

    QTreeWidgetItem* item = new QTreeWidgetItem(keyboard_tree);
    item->setText(0, menu->title().replace("&", ""));

    keyboard_tree->addTopLevelItem(item);

    setup_kbd_shortcut_worker(menu, item);
  }

  for (int i=0;i<key_shortcut_items.size();i++) {
    if (!key_shortcut_actions.at(i)->property("id").isNull()) {
      KeySequenceEditor* editor = new KeySequenceEditor(keyboard_tree, key_shortcut_actions.at(i));
      keyboard_tree->setItemWidget(key_shortcut_items.at(i), 1, editor);
      key_shortcut_fields.append(editor);
    }
  }
}

void PreferencesDialog::accept() {
  bool restart_after_saving = false;
  bool reinit_audio = false;
  bool reload_language = false;
  bool reload_effects = false;

  // Validate whether the specified CSS file exists
  if (!custom_css_fn->text().isEmpty() && !QFileInfo::exists(custom_css_fn->text())) {
    QMessageBox::critical(
          this,
          tr("Invalid CSS File"),
          tr("CSS file '%1' does not exist.").arg(custom_css_fn->text())
          );
    return;
  }

  // Validate whether the effects panel should refresh itself
  if (amber::CurrentConfig.effect_textbox_lines != effect_textbox_lines_field->value()) {
    reload_effects = true;
  }

  bool bool_requires_restart = false;
  for (int i=0;i<bool_restart_required.size();i++) {
    if (bool_restart_required.at(i)
        && bool_ui.at(i)->isChecked() != *bool_value.at(i)) {
      bool_requires_restart = true;
      break;
    }
  }

  // Check if any settings will require a restart of Olive
  if (bool_requires_restart
      || amber::CurrentConfig.thumbnail_resolution != thumbnail_res_spinbox->value()
      || amber::CurrentConfig.waveform_resolution != waveform_res_spinbox->value()
      || amber::CurrentConfig.css_path != custom_css_fn->text()
      || amber::CurrentConfig.style != static_cast<amber::styling::Style>(ui_style->currentData().toInt())) {

    // any changes to these settings will require a restart - ask the user if we should do one now or later

    int ret = QMessageBox::question(this,
                                    "Restart Required",
                                    "Some of the changed settings will require a restart of Olive. Would you like "
                                    "to restart now?",
                                    QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);

    if (ret == QMessageBox::Cancel) {
      // Return to Preferences dialog without saving any settings
      return;
    } else if (ret == QMessageBox::Yes) {

      // Check if we can close the current project. If not, we'll treat it as if the user clicked "Cancel".
      if (amber::Global->can_close_project()) {
        restart_after_saving = true;
      } else {
        return;
      }
    }
    // Selecting "No" will save the settings and not restart. They will become active next time Olive opens.

  }

  // Audio settings may require the audio device to be re-initiated.
  if (amber::CurrentConfig.preferred_audio_output != audio_output_devices->currentData().toString()
      || amber::CurrentConfig.preferred_audio_input != audio_input_devices->currentData().toString()
      || amber::CurrentConfig.audio_rate != audio_sample_rate->currentData().toInt()) {
    reinit_audio = true;
  }

  // see if the language file should be reloaded (not necessary if the app is restarting anyway)
  if (!restart_after_saving
      && amber::CurrentConfig.language_file != language_combobox->currentData().toString()) {
    reload_language = true;
  }

  // save settings from UI to backend
  amber::CurrentConfig.css_path = custom_css_fn->text();
  amber::CurrentConfig.recording_mode = recordingComboBox->currentIndex() + 1;
  amber::CurrentConfig.img_seq_formats = imgSeqFormatEdit->text();
  amber::CurrentConfig.upcoming_queue_size = upcoming_queue_spinbox->value();
  amber::CurrentConfig.upcoming_queue_type = upcoming_queue_type->currentIndex();
  amber::CurrentConfig.previous_queue_size = previous_queue_spinbox->value();
  amber::CurrentConfig.previous_queue_type = previous_queue_type->currentIndex();

  amber::CurrentConfig.preferred_audio_output = audio_output_devices->currentData().toString();
  amber::CurrentConfig.preferred_audio_input = audio_input_devices->currentData().toString();
  amber::CurrentConfig.audio_rate = audio_sample_rate->currentData().toInt();

  amber::CurrentConfig.effect_textbox_lines = effect_textbox_lines_field->value();
  amber::CurrentConfig.frame_skip_step = frame_skip_step_field->value();
  amber::CurrentConfig.snap_outgoing_modifier = snap_outgoing_modifier_combo->currentIndex();
  amber::CurrentConfig.language_file = language_combobox->currentData().toString();

  amber::CurrentConfig.default_sequence_width = default_sequence.width;
  amber::CurrentConfig.default_sequence_height = default_sequence.height;
  amber::CurrentConfig.default_sequence_framerate = default_sequence.frame_rate;
  amber::CurrentConfig.default_sequence_audio_frequency = default_sequence.audio_frequency;
  amber::CurrentConfig.default_sequence_audio_channel_layout = default_sequence.audio_layout;

  for (int i=0;i<bool_ui.size();i++) {
    *bool_value[i] = bool_ui.at(i)->isChecked();
  }

  amber::CurrentConfig.style = static_cast<amber::styling::Style>(ui_style->currentData().toInt());

  // Check if the thumbnail or waveform icon
  if (amber::CurrentConfig.thumbnail_resolution != thumbnail_res_spinbox->value()
      || amber::CurrentConfig.waveform_resolution != waveform_res_spinbox->value()) {
    // we're changing the size of thumbnails and waveforms, so let's delete them and regenerate them next start

    // delete nothing
    char delete_match = 0;

    if (amber::CurrentConfig.thumbnail_resolution != thumbnail_res_spinbox->value()) {
      // delete existing thumbnails
      amber::CurrentConfig.thumbnail_resolution = thumbnail_res_spinbox->value();

      // delete only thumbnails
      delete_match = 't';
    }

    if (amber::CurrentConfig.waveform_resolution != waveform_res_spinbox->value()) {
      // delete existing waveforms
      amber::CurrentConfig.waveform_resolution = waveform_res_spinbox->value();

      // if we're already deleting thumbnails
      if (delete_match == 't') {
        // delete all
        delete_match = 1;
      } else {
        // just delete waveforms
        delete_match = 'w';
      }
    }

    delete_previews(delete_match);
  }

  // Save keyboard shortcuts
  for (auto key_shortcut_field : key_shortcut_fields) {
    key_shortcut_field->set_action_shortcut();
  }

  // Audio settings may require the audio device to be re-initiated.
  if (reinit_audio) {
    init_audio();
  }

  if (reload_effects) {
    panel_effect_controls->Reload();
  }

  // reload language file if it changed
  if (reload_language) {
    amber::Global->load_translation_from_config();
  }

  QDialog::accept();

  if (restart_after_saving) {
    // since we already ran can_close_project(), bypass checking again by running set_modified(false)
    amber::Global->set_modified(false);

    amber::MainWindow->close();

    QProcess::startDetached(QApplication::applicationFilePath(), { amber::ActiveProjectFilename });
  }
}

void PreferencesDialog::reset_default_shortcut() {
  QList<QTreeWidgetItem*> items = keyboard_tree->selectedItems();
  for (int i=0;i<items.size();i++) {
    QTreeWidgetItem* item = keyboard_tree->selectedItems().at(i);
    static_cast<KeySequenceEditor*>(keyboard_tree->itemWidget(item, 1))->reset_to_default();
  }
}

void PreferencesDialog::reset_all_shortcuts() {
  if (QMessageBox::question(
        this,
        tr("Confirm Reset All Shortcuts"),
        tr("Are you sure you wish to reset all keyboard shortcuts to their defaults?"),
        QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
    for (auto key_shortcut_field : key_shortcut_fields) {
      key_shortcut_field->reset_to_default();
    }
  }
}

bool PreferencesDialog::refine_shortcut_list(const QString &s, QTreeWidgetItem* parent) {
  if (parent == nullptr) {
    for (int i=0;i<keyboard_tree->topLevelItemCount();i++) {
      refine_shortcut_list(s, keyboard_tree->topLevelItem(i));
    }
  } else {
    parent->setExpanded(!s.isEmpty());

    bool all_children_are_hidden = !s.isEmpty();

    for (int i=0;i<parent->childCount();i++) {
      QTreeWidgetItem* item = parent->child(i);
      if (item->childCount() > 0) {
        all_children_are_hidden = refine_shortcut_list(s, item);
      } else {
        item->setHidden(false);
        if (s.isEmpty()) {
          all_children_are_hidden = false;
        } else {
          QString shortcut;
          if (keyboard_tree->itemWidget(item, 1) != nullptr) {
            shortcut = static_cast<QKeySequenceEdit*>(keyboard_tree->itemWidget(item, 1))->keySequence().toString();
          }
          if (item->text(0).contains(s, Qt::CaseInsensitive) || shortcut.contains(s, Qt::CaseInsensitive)) {
            all_children_are_hidden = false;
          } else {
            item->setHidden(true);
          }
        }
      }
    }

    if (parent->text(0).contains(s, Qt::CaseInsensitive)) all_children_are_hidden = false;

    parent->setHidden(all_children_are_hidden);

    return all_children_are_hidden;
  }
  return true;
}

void PreferencesDialog::load_shortcut_file() {
  QString fn = QFileDialog::getOpenFileName(this, tr("Import Keyboard Shortcuts"));
  if (!fn.isEmpty()) {
    QFile f(fn);
    if (f.exists() && f.open(QFile::ReadOnly)) {
      QByteArray ba = f.readAll();
      f.close();
      for (auto key_shortcut_field : key_shortcut_fields) {
        int index = ba.indexOf(key_shortcut_field->action_name().toUtf8());
        if (index == 0 || (index > 0 && ba.at(index-1) == '\n')) {
          while (index < ba.size() && ba.at(index) != '\t') index++;
          QString ks;
          index++;
          while (index < ba.size() && ba.at(index) != '\n') {
            ks.append(ba.at(index));
            index++;
          }
          key_shortcut_field->setKeySequence(ks);
        } else {
          key_shortcut_field->reset_to_default();
        }
      }
    } else {
      QMessageBox::critical(
            this,
            tr("Error saving shortcuts"),
            tr("Failed to open file for reading")
            );
    }
  }
}

void PreferencesDialog::save_shortcut_file() {
  QString fn = QFileDialog::getSaveFileName(this, tr("Export Keyboard Shortcuts"));
  if (!fn.isEmpty()) {
    QFile f(fn);
    if (f.open(QFile::WriteOnly)) {
      bool start = true;
      for (auto key_shortcut_field : key_shortcut_fields) {
        QString s = key_shortcut_field->export_shortcut();
        if (!s.isEmpty()) {
          if (!start) f.write("\n");
          f.write(s.toUtf8());
          start = false;
        }
      }
      f.close();
      QMessageBox::information(this, tr("Export Shortcuts"), tr("Shortcuts exported successfully"));
    } else {
      QMessageBox::critical(this, tr("Error saving shortcuts"), tr("Failed to open file for writing"));
    }
  }
}

void PreferencesDialog::browse_css_file() {
  QString fn = QFileDialog::getOpenFileName(this, tr("Browse for CSS file"));
  if (!fn.isEmpty()) {
    custom_css_fn->setText(fn);
  }
}

void PreferencesDialog::delete_all_previews() {
  if (QMessageBox::question(this,
                            tr("Delete All Previews"),
                            tr("Are you sure you want to delete all previews?"),
                            QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
    delete_previews(1);
    QMessageBox::information(this,
                             tr("Previews Deleted"),
                             tr("All previews deleted successfully. You may have to re-open your current project for changes to take effect."),
                             QMessageBox::Ok);
  }
}

void PreferencesDialog::edit_default_sequence_settings()
{
  NewSequenceDialog nsd(this, nullptr, &default_sequence);
  nsd.SetNameEditable(false);
  nsd.exec();
}

void PreferencesDialog::setup_ui() {
  QVBoxLayout* verticalLayout = new QVBoxLayout(this);
  QTabWidget* tabWidget = new QTabWidget(this);

  // row counter used to ease adding new rows
  int row = 0;

  // General
  QWidget* general_tab = new QWidget(this);
  QGridLayout* general_layout = new QGridLayout(general_tab);

  // General -> Language
  general_layout->addWidget(new QLabel(tr("Language:")), row, 0);

  language_combobox = new QComboBox();

  // add default language (en-US)
  language_combobox->addItem(QLocale::languageToString(QLocale("en-US").language()));

  // add languages from file
  QList<QString> translation_paths = get_language_paths();

  // iterate through all language search paths
  for (const auto & translation_path : translation_paths) {
    QDir translation_dir(translation_path);
    if (translation_dir.exists()) {
      QStringList translation_files = translation_dir.entryList({"*.qm"}, QDir::Files | QDir::NoDotAndDotDot);
      for (const auto & translation_file : translation_files) {
        // get path of translation relative to the application path
        QString locale_full_path = translation_dir.filePath(translation_file);
        QString locale_relative_path = QDir(get_app_path()).relativeFilePath(locale_full_path);

        QFileInfo locale_file(translation_file);
        QString locale_file_basename = locale_file.baseName();
        QString locale_str = locale_file_basename.mid(locale_file_basename.lastIndexOf('_')+1);
        language_combobox->addItem(QLocale(locale_str).nativeLanguageName(), locale_relative_path);

        if (amber::CurrentConfig.language_file == locale_relative_path) {
          language_combobox->setCurrentIndex(language_combobox->count() - 1);
        }
      }
    }
  }

  general_layout->addWidget(language_combobox, row, 1, 1, 4);

  row++;

  // General -> Image Sequence Formats
  general_layout->addWidget(new QLabel(tr("Image sequence formats:"), this), row, 0);

  imgSeqFormatEdit = new QLineEdit(general_tab);
  imgSeqFormatEdit->setText(amber::CurrentConfig.img_seq_formats);

  general_layout->addWidget(imgSeqFormatEdit, row, 1, 1, 4);

  row++;

  // General -> Thumbnail and Waveform Resolution
  general_layout->addWidget(new QLabel(tr("Thumbnail Resolution:"), this), row, 0);

  thumbnail_res_spinbox = new QSpinBox(this);
  thumbnail_res_spinbox->setMinimum(0);
  thumbnail_res_spinbox->setMaximum(INT_MAX);
  thumbnail_res_spinbox->setValue(amber::CurrentConfig.thumbnail_resolution);
  general_layout->addWidget(thumbnail_res_spinbox, row, 1);

  general_layout->addWidget(new QLabel(tr("Waveform Resolution:"), this), row, 2);

  waveform_res_spinbox = new QSpinBox(this);
  waveform_res_spinbox->setMinimum(0);
  waveform_res_spinbox->setMaximum(INT_MAX);
  waveform_res_spinbox->setValue(amber::CurrentConfig.waveform_resolution);
  general_layout->addWidget(waveform_res_spinbox, row, 3);

  QPushButton* delete_preview_btn = new QPushButton(tr("Delete Previews"));
  general_layout->addWidget(delete_preview_btn, row, 4);
  connect(delete_preview_btn, &QPushButton::clicked, this, &PreferencesDialog::delete_all_previews);

  row++;

  // General -> Use Software Fallbacks When Possible
  QCheckBox* use_software_fallbacks_checkbox = new QCheckBox(tr("Use Software Fallbacks When Possible"));
  AddBoolPair(use_software_fallbacks_checkbox, &amber::CurrentConfig.use_software_fallback, true);
  general_layout->addWidget(use_software_fallbacks_checkbox, row, 0, 1, 4);

  row++;

  // General -> Hardware Decoding
  QCheckBox* hardware_decoding_checkbox = new QCheckBox(tr("Hardware Decoding (VAAPI/D3D11VA/VideoToolbox)"));
  hardware_decoding_checkbox->setToolTip(tr("Use GPU-accelerated video decoding when available. Falls back to software if unsupported. Requires restart."));
  AddBoolPair(hardware_decoding_checkbox, &amber::CurrentConfig.hardware_decoding, true);
  general_layout->addWidget(hardware_decoding_checkbox, row, 0, 1, 4);

  row++;

  // General -> Default Sequence Settings
  QPushButton* default_sequence_settings = new QPushButton(tr("Default Sequence Settings"));
  connect(default_sequence_settings, &QPushButton::clicked, this, &PreferencesDialog::edit_default_sequence_settings);
  general_layout->addWidget(default_sequence_settings);

  tabWidget->addTab(general_tab, tr("General"));

  // Behavior
  QWidget* behavior_tab = new QWidget(this);
  tabWidget->addTab(behavior_tab, tr("Behavior"));

  ColumnedGridLayout* behavior_tab_layout = new ColumnedGridLayout(behavior_tab, 2);

  QCheckBox* add_default_effects_to_clips = new QCheckBox(tr("Add Default Effects to New Clips"));
  AddBoolPair(add_default_effects_to_clips, &amber::CurrentConfig.add_default_effects_to_clips);
  behavior_tab_layout->Add(add_default_effects_to_clips);

  QCheckBox* auto_seek_to_beginning = new QCheckBox(tr("Automatically Seek to the Beginning When Playing at the End of a Sequence"));
  AddBoolPair(auto_seek_to_beginning, &amber::CurrentConfig.auto_seek_to_beginning);
  behavior_tab_layout->Add(auto_seek_to_beginning);

  QCheckBox* selecting_also_seeks = new QCheckBox(tr("Selecting Also Seeks"));
  AddBoolPair(selecting_also_seeks, &amber::CurrentConfig.select_also_seeks);
  behavior_tab_layout->Add(selecting_also_seeks);

  QCheckBox* edit_tool_also_seeks = new QCheckBox(tr("Edit Tool Also Seeks"));
  AddBoolPair(edit_tool_also_seeks, &amber::CurrentConfig.edit_tool_also_seeks);
  behavior_tab_layout->Add(edit_tool_also_seeks);

  QCheckBox* edit_tool_selects_links = new QCheckBox(tr("Edit Tool Selects Links"));
  AddBoolPair(edit_tool_selects_links, &amber::CurrentConfig.edit_tool_selects_links);
  behavior_tab_layout->Add(edit_tool_selects_links);

  QCheckBox* seek_also_selects = new QCheckBox(tr("Seek Also Selects"));
  AddBoolPair(seek_also_selects, &amber::CurrentConfig.seek_also_selects);
  behavior_tab_layout->Add(seek_also_selects);

  QCheckBox* snap_to_outgoing_clip = new QCheckBox(tr("Snap Playhead to Last Frame of Outgoing Clip"));
  snap_to_outgoing_clip->setToolTip(tr("When snapping the playhead to a clip boundary, show the last frame of the outgoing clip instead of the first frame of the incoming clip"));
  AddBoolPair(snap_to_outgoing_clip, &amber::CurrentConfig.snap_to_outgoing_clip);
  behavior_tab_layout->Add(snap_to_outgoing_clip);

  {
    QWidget* mod_row = new QWidget();
    QHBoxLayout* mod_layout = new QHBoxLayout(mod_row);
    mod_layout->setContentsMargins(0, 0, 0, 0);
    mod_layout->addWidget(new QLabel(tr("Invert Snap Modifier:")));
    snap_outgoing_modifier_combo = new QComboBox();
    snap_outgoing_modifier_combo->addItem(tr("Shift"));
    snap_outgoing_modifier_combo->addItem(tr("Ctrl"));
    snap_outgoing_modifier_combo->addItem(tr("Alt"));
    snap_outgoing_modifier_combo->setCurrentIndex(qBound(0, amber::CurrentConfig.snap_outgoing_modifier, 2));
    snap_outgoing_modifier_combo->setToolTip(tr("Hold this key while seeking to invert the snap-to-outgoing-clip behavior"));
    mod_layout->addWidget(snap_outgoing_modifier_combo);
    mod_layout->addStretch();
    behavior_tab_layout->Add(mod_row);
  }

  QCheckBox* seek_to_end_of_pastes = new QCheckBox(tr("Seek to the End of Pastes"));
  AddBoolPair(seek_to_end_of_pastes, &amber::CurrentConfig.paste_seeks);
  behavior_tab_layout->Add(seek_to_end_of_pastes);

  QCheckBox* scroll_wheel_zooms = new QCheckBox(tr("Scroll Wheel Zooms"));
  scroll_wheel_zooms->setToolTip(tr("Hold CTRL to toggle this setting"));
  AddBoolPair(scroll_wheel_zooms, &amber::CurrentConfig.scroll_zooms);
  behavior_tab_layout->Add(scroll_wheel_zooms);

  QCheckBox* invert_timeline_scroll_axes = new QCheckBox(tr("Invert Timeline Scroll Axes"));
  AddBoolPair(invert_timeline_scroll_axes, &amber::CurrentConfig.invert_timeline_scroll_axes);
  behavior_tab_layout->Add(invert_timeline_scroll_axes);

  QCheckBox* enable_drag_files_to_timeline = new QCheckBox(tr("Enable Drag Files to Timeline"));
  AddBoolPair(enable_drag_files_to_timeline, &amber::CurrentConfig.enable_drag_files_to_timeline);
  behavior_tab_layout->Add(enable_drag_files_to_timeline);

  QCheckBox* autoscale_by_default = new QCheckBox(tr("Auto-Scale By Default"));
  AddBoolPair(autoscale_by_default, &amber::CurrentConfig.autoscale_by_default);
  behavior_tab_layout->Add(autoscale_by_default);

  QCheckBox* enable_seek_to_import = new QCheckBox(tr("Auto-Seek to Imported Clips"));
  AddBoolPair(enable_seek_to_import, &amber::CurrentConfig.enable_seek_to_import);
  behavior_tab_layout->Add(enable_seek_to_import);

  QCheckBox* enable_audio_scrubbing = new QCheckBox(tr("Audio Scrubbing"));
  AddBoolPair(enable_audio_scrubbing, &amber::CurrentConfig.enable_audio_scrubbing);
  behavior_tab_layout->Add(enable_audio_scrubbing);

  QCheckBox* enable_drop_on_media_to_replace = new QCheckBox(tr("Drop Files on Media to Replace"));
  AddBoolPair(enable_drop_on_media_to_replace, &amber::CurrentConfig.drop_on_media_to_replace);
  behavior_tab_layout->Add(enable_drop_on_media_to_replace);

  QCheckBox* enable_hover_focus = new QCheckBox(tr("Enable Hover Focus"));
  AddBoolPair(enable_hover_focus, &amber::CurrentConfig.hover_focus);
  behavior_tab_layout->Add(enable_hover_focus);

  QCheckBox* set_name_and_marker = new QCheckBox(tr("Ask For Name When Setting Marker"));
  AddBoolPair(set_name_and_marker, &amber::CurrentConfig.set_name_with_marker);
  behavior_tab_layout->Add(set_name_and_marker);

  QWidget* frame_skip_row = new QWidget(behavior_tab);
  QHBoxLayout* frame_skip_layout = new QHBoxLayout(frame_skip_row);
  frame_skip_layout->setContentsMargins(0, 0, 0, 0);
  frame_skip_layout->addWidget(new QLabel(tr("Jump Step:"), behavior_tab));
  frame_skip_step_field = new QSpinBox(behavior_tab);
  frame_skip_step_field->setMinimum(1);
  frame_skip_step_field->setMaximum(999);
  frame_skip_step_field->setValue(amber::CurrentConfig.frame_skip_step);
  frame_skip_layout->addWidget(frame_skip_step_field);
  frame_skip_layout->addStretch();
  behavior_tab_layout->Add(frame_skip_row);

  // Appearance
  QWidget* appearance_tab = new QWidget(this);
  tabWidget->addTab(appearance_tab, tr("Appearance"));

  row = 0;

  QGridLayout* appearance_layout = new QGridLayout(appearance_tab);

  // Appearance -> Theme
  appearance_layout->addWidget(new QLabel(tr("Theme")), row, 0);

  ui_style = new QComboBox();
  ui_style->addItem(tr("Amber Dark (Default)"), amber::styling::kOliveDefaultDark);
  ui_style->addItem(tr("Amber Light"), amber::styling::kOliveDefaultLight);
  ui_style->addItem(tr("Native"), amber::styling::kNativeDarkIcons);
  ui_style->addItem(tr("Native (Light Icons)"), amber::styling::kNativeLightIcons);
  ui_style->setCurrentIndex(amber::CurrentConfig.style);
  appearance_layout->addWidget(ui_style, row, 1, 1, 2);

  row++;

#ifdef Q_OS_WIN
  // Native menu styling is only available on Windows. Environments like Ubuntu and Mac use the native menu system by
  // default
  QCheckBox* native_menus = new QCheckBox(tr("Use Native Menu Styling"));
  AddBoolPair(native_menus, &amber::CurrentConfig.use_native_menu_styling, true);
  appearance_layout->addWidget(native_menus, row, 0, 1, 3);

  row++;
#endif

  // Appearance -> Custom CSS
  appearance_layout->addWidget(new QLabel(tr("Custom CSS:"), this), row, 0);

  custom_css_fn = new QLineEdit(general_tab);
  custom_css_fn->setText(amber::CurrentConfig.css_path);
  appearance_layout->addWidget(custom_css_fn, row, 1);

  QPushButton* custom_css_browse = new QPushButton(tr("Browse"), general_tab);
  connect(custom_css_browse, &QPushButton::clicked, this, &PreferencesDialog::browse_css_file);
  appearance_layout->addWidget(custom_css_browse, row, 2);

  row++;

  // Appearance -> Effect Textbox Lines
  appearance_layout->addWidget(new QLabel(tr("Effect Textbox Lines:"), this), row, 0);

  effect_textbox_lines_field = new QSpinBox(general_tab);
  effect_textbox_lines_field->setMinimum(1);
  effect_textbox_lines_field->setValue(amber::CurrentConfig.effect_textbox_lines);
  appearance_layout->addWidget(effect_textbox_lines_field, row, 1, 1, 2);

  row++;

  // Appearance -> Effect Panel Shrinkable
  QCheckBox* effect_panel_shrinkable = new QCheckBox(tr("Allow Effect Properties panel to be smaller than its content"));
  effect_panel_shrinkable->setToolTip(tr("When enabled, the Effect Properties panel can be resized narrower than its content. "
                                         "A horizontal scrollbar will appear to access clipped content."));
  AddBoolPair(effect_panel_shrinkable, &amber::CurrentConfig.effect_panel_shrinkable);
  appearance_layout->addWidget(effect_panel_shrinkable, row, 0, 1, 3);

  row++;

  // Playback
  QWidget* playback_tab = new QWidget(this);
  QVBoxLayout* playback_tab_layout = new QVBoxLayout(playback_tab);

  // Playback -> Memory Usage
  QGroupBox* memory_usage_group = new QGroupBox(playback_tab);
  memory_usage_group->setTitle(tr("Memory Usage"));
  QGridLayout* memory_usage_layout = new QGridLayout(memory_usage_group);
  memory_usage_layout->addWidget(new QLabel(tr("Upcoming Frame Queue:"), playback_tab), 0, 0);
  upcoming_queue_spinbox = new QDoubleSpinBox(playback_tab);
  upcoming_queue_spinbox->setValue(amber::CurrentConfig.upcoming_queue_size);
  memory_usage_layout->addWidget(upcoming_queue_spinbox, 0, 1);
  upcoming_queue_type = new QComboBox(playback_tab);
  upcoming_queue_type->addItem(tr("frames"));
  upcoming_queue_type->addItem(tr("seconds"));
  upcoming_queue_type->setCurrentIndex(amber::CurrentConfig.upcoming_queue_type);
  memory_usage_layout->addWidget(upcoming_queue_type, 0, 2);
  memory_usage_layout->addWidget(new QLabel(tr("Previous Frame Queue:"), playback_tab), 1, 0);
  previous_queue_spinbox = new QDoubleSpinBox(playback_tab);
  previous_queue_spinbox->setValue(amber::CurrentConfig.previous_queue_size);
  memory_usage_layout->addWidget(previous_queue_spinbox, 1, 1);
  previous_queue_type = new QComboBox(playback_tab);
  previous_queue_type->addItem(tr("frames"));
  previous_queue_type->addItem(tr("seconds"));
  previous_queue_type->setCurrentIndex(amber::CurrentConfig.previous_queue_type);
  memory_usage_layout->addWidget(previous_queue_type, 1, 2);
  playback_tab_layout->addWidget(memory_usage_group);

  tabWidget->addTab(playback_tab, tr("Playback"));

  // Audio
  QWidget* audio_tab = new QWidget(this);

  QGridLayout* audio_tab_layout = new QGridLayout(audio_tab);

  row = 0;

  // Audio -> Output Device

  audio_tab_layout->addWidget(new QLabel(tr("Output Device:")), row, 0);

  audio_output_devices = new QComboBox();
  audio_output_devices->addItem(tr("Default"), "");

  // list all available audio output devices
  QList<QAudioDevice> devs = QMediaDevices::audioOutputs();
  bool found_preferred_device = false;
  for (const auto & dev : devs) {
    audio_output_devices->addItem(dev.description(), dev.description());
    if (!found_preferred_device
        && dev.description() == amber::CurrentConfig.preferred_audio_output) {
      audio_output_devices->setCurrentIndex(audio_output_devices->count()-1);
      found_preferred_device = true;
    }
  }

  audio_tab_layout->addWidget(audio_output_devices, row, 1);

  row++;

  // Audio -> Input Device

  audio_tab_layout->addWidget(new QLabel(tr("Input Device:")), row, 0);

  audio_input_devices = new QComboBox();
  audio_input_devices->addItem(tr("Default"), "");

  // list all available audio input devices
  devs = QMediaDevices::audioInputs();
  found_preferred_device = false;
  for (const auto & dev : devs) {
    audio_input_devices->addItem(dev.description(), dev.description());
    if (!found_preferred_device
        && dev.description() == amber::CurrentConfig.preferred_audio_input) {
      audio_input_devices->setCurrentIndex(audio_input_devices->count()-1);
      found_preferred_device = true;
    }
  }

  audio_tab_layout->addWidget(audio_input_devices, row, 1);

  row++;

  // Audio -> Sample Rate

  audio_tab_layout->addWidget(new QLabel(tr("Sample Rate:")), row, 0);

  audio_sample_rate = new QComboBox();
  combobox_audio_sample_rates(audio_sample_rate);
  for (int i=0;i<audio_sample_rate->count();i++) {
    if (audio_sample_rate->itemData(i).toInt() == amber::CurrentConfig.audio_rate) {
      audio_sample_rate->setCurrentIndex(i);
      break;
    }
  }

  audio_tab_layout->addWidget(audio_sample_rate, row, 1);

  row++;

  // Audio -> Audio Recording
  audio_tab_layout->addWidget(new QLabel(tr("Audio Recording:"), this), row, 0);

  recordingComboBox = new QComboBox(general_tab);
  recordingComboBox->addItem(tr("Mono"));
  recordingComboBox->addItem(tr("Stereo"));
  recordingComboBox->setCurrentIndex(amber::CurrentConfig.recording_mode - 1);
  audio_tab_layout->addWidget(recordingComboBox, row, 1);

  row++;

  tabWidget->addTab(audio_tab, tr("Audio"));

  // Shortcuts
  QWidget* shortcut_tab = new QWidget(this);

  QVBoxLayout* shortcut_layout = new QVBoxLayout(shortcut_tab);

  QLineEdit* key_search_line = new QLineEdit(shortcut_tab);
  key_search_line->setPlaceholderText(tr("Search for action or shortcut"));
  connect(key_search_line, &QLineEdit::textChanged, this, [this](const QString& s){ refine_shortcut_list(s); });

  shortcut_layout->addWidget(key_search_line);

  keyboard_tree = new QTreeWidget(shortcut_tab);
  QTreeWidgetItem* tree_header = keyboard_tree->headerItem();
  tree_header->setText(0, tr("Action"));
  tree_header->setText(1, tr("Shortcut"));
  shortcut_layout->addWidget(keyboard_tree);

  QHBoxLayout* reset_shortcut_layout = new QHBoxLayout(shortcut_tab);

  QPushButton* import_shortcut_button = new QPushButton(tr("Import"), shortcut_tab);
  reset_shortcut_layout->addWidget(import_shortcut_button);
  connect(import_shortcut_button, &QPushButton::clicked, this, &PreferencesDialog::load_shortcut_file);

  QPushButton* export_shortcut_button = new QPushButton(tr("Export"), shortcut_tab);
  reset_shortcut_layout->addWidget(export_shortcut_button);
  connect(export_shortcut_button, &QPushButton::clicked, this, &PreferencesDialog::save_shortcut_file);

  reset_shortcut_layout->addStretch();

  QPushButton* reset_selected_shortcut_button = new QPushButton(tr("Reset Selected"), shortcut_tab);
  reset_shortcut_layout->addWidget(reset_selected_shortcut_button);
  connect(reset_selected_shortcut_button, &QPushButton::clicked, this, &PreferencesDialog::reset_default_shortcut);

  QPushButton* reset_all_shortcut_button = new QPushButton(tr("Reset All"), shortcut_tab);
  reset_shortcut_layout->addWidget(reset_all_shortcut_button);
  connect(reset_all_shortcut_button, &QPushButton::clicked, this, &PreferencesDialog::reset_all_shortcuts);

  shortcut_layout->addLayout(reset_shortcut_layout);

  tabWidget->addTab(shortcut_tab, tr("Keyboard"));

  verticalLayout->addWidget(tabWidget);

  QDialogButtonBox* buttonBox = new QDialogButtonBox(this);
  buttonBox->setOrientation(Qt::Horizontal);
  buttonBox->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);

  verticalLayout->addWidget(buttonBox);

  connect(buttonBox, &QDialogButtonBox::accepted, this, &PreferencesDialog::accept);
  connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
