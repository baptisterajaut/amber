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

#include <QAction>
#include <QApplication>
#include <QAudioDevice>
#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMediaDevices>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QVector>

#include "core/path.h"
#include "dialogs/newsequencedialog.h"
#include "global/config.h"
#include "global/global.h"
#include "panels/panels.h"
#include "rendering/audio.h"
#include "rendering/audio_ui.h"
#include "ui/columnedgridlayout.h"
#include "ui/labelslider.h"
#include "ui/mainwindow.h"
#include "ui/styling.h"

KeySequenceEditor::KeySequenceEditor(QWidget* parent, QAction* a) : QKeySequenceEdit(parent), action(a) {
  setKeySequence(action->shortcut());
}

void KeySequenceEditor::set_action_shortcut() { action->setShortcut(keySequence()); }

void KeySequenceEditor::reset_to_default() { setKeySequence(action->property("default").toString()); }

QString KeySequenceEditor::action_name() { return action->property("id").toString(); }

QString KeySequenceEditor::export_shortcut() {
  QString ks = keySequence().toString();
  if (ks != action->property("default")) {
    return action->property("id").toString() + "\t" + ks;
  }
  return nullptr;
}

PreferencesDialog::PreferencesDialog(QWidget* parent) : QDialog(parent) {
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
    for (const auto& preview_file_str : preview_file_list) {
      // use filename to determine whether this is a thumbnail or a waveform
      int identifier_char_index = qMax(0, preview_file_str.size() - 2);

      // find identifier char
      while (identifier_char_index >= 0 && preview_file_str.at(identifier_char_index).unicode() >= 48 &&
             preview_file_str.at(identifier_char_index).unicode() <= 57) {
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

void PreferencesDialog::AddBoolPair(QCheckBox* ui, bool* value, bool restart_required) {
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

  for (int i = 0; i < key_shortcut_items.size(); i++) {
    if (!key_shortcut_actions.at(i)->property("id").isNull()) {
      KeySequenceEditor* editor = new KeySequenceEditor(keyboard_tree, key_shortcut_actions.at(i));
      keyboard_tree->setItemWidget(key_shortcut_items.at(i), 1, editor);
      key_shortcut_fields.append(editor);
    }
  }
}

void PreferencesDialog::save_config_from_ui() {
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
  amber::CurrentConfig.default_still_length = static_cast<int>(default_still_length_slider->value());
  amber::CurrentConfig.autorecovery_enabled = autorecovery_enabled_check->isChecked();
  amber::CurrentConfig.autorecovery_interval = static_cast<int>(autorecovery_interval_slider->value());
  amber::CurrentConfig.autorecovery_max = static_cast<int>(autorecovery_max_slider->value());
  amber::Global->reconfigure_autorecovery();
  amber::CurrentConfig.snap_outgoing_modifier = snap_outgoing_modifier_combo->currentIndex();
  amber::CurrentConfig.sticky_keyframe_type = sticky_keyframe_type_check->isChecked();
  amber::CurrentConfig.default_keyframe_type = default_keyframe_type_combo->currentData().toInt();
  amber::CurrentConfig.language_file = language_combobox->currentData().toString();
  amber::CurrentConfig.default_sequence_width = default_sequence.width;
  amber::CurrentConfig.default_sequence_height = default_sequence.height;
  amber::CurrentConfig.default_sequence_framerate = default_sequence.frame_rate;
  amber::CurrentConfig.default_sequence_audio_frequency = default_sequence.audio_frequency;
  amber::CurrentConfig.default_sequence_audio_channel_layout = default_sequence.audio_layout;
  for (int i = 0; i < bool_ui.size(); i++) {
    *bool_value[i] = bool_ui.at(i)->isChecked();
  }
  amber::CurrentConfig.style = static_cast<amber::styling::Style>(ui_style->currentData().toInt());
}

void PreferencesDialog::handle_preview_resolution_changes() {
  bool thumb_changed = (amber::CurrentConfig.thumbnail_resolution != thumbnail_res_spinbox->value());
  bool wave_changed = (amber::CurrentConfig.waveform_resolution != waveform_res_spinbox->value());

  if (!thumb_changed && !wave_changed) return;

  char delete_match = 0;
  if (thumb_changed) {
    amber::CurrentConfig.thumbnail_resolution = thumbnail_res_spinbox->value();
    delete_match = 't';
  }
  if (wave_changed) {
    amber::CurrentConfig.waveform_resolution = waveform_res_spinbox->value();
    delete_match = (delete_match == 't') ? char(1) : 'w';
  }
  delete_previews(delete_match);
}

bool PreferencesDialog::AnyBoolRequiresRestart() const {
  for (int i = 0; i < bool_restart_required.size(); i++) {
    if (bool_restart_required.at(i) && bool_ui.at(i)->isChecked() != *bool_value.at(i)) return true;
  }
  return false;
}

bool PreferencesDialog::SettingsRequireRestart() const {
  return AnyBoolRequiresRestart() || amber::CurrentConfig.thumbnail_resolution != thumbnail_res_spinbox->value() ||
         amber::CurrentConfig.waveform_resolution != waveform_res_spinbox->value() ||
         amber::CurrentConfig.css_path != custom_css_fn->text() ||
         amber::CurrentConfig.style != static_cast<amber::styling::Style>(ui_style->currentData().toInt());
}

bool PreferencesDialog::AudioSettingsChanged() const {
  return amber::CurrentConfig.preferred_audio_output != audio_output_devices->currentData().toString() ||
         amber::CurrentConfig.preferred_audio_input != audio_input_devices->currentData().toString() ||
         amber::CurrentConfig.audio_rate != audio_sample_rate->currentData().toInt();
}

void PreferencesDialog::accept() {
  // Validate whether the specified CSS file exists
  if (!custom_css_fn->text().isEmpty() && !QFileInfo::exists(custom_css_fn->text())) {
    QMessageBox::critical(this, tr("Invalid CSS File"), tr("CSS file '%1' does not exist.").arg(custom_css_fn->text()));
    return;
  }

  bool reload_effects = (amber::CurrentConfig.effect_textbox_lines != effect_textbox_lines_field->value());
  bool reinit_audio = AudioSettingsChanged();
  bool restart_after_saving = false;

  if (SettingsRequireRestart()) {
    int ret = QMessageBox::question(this, "Restart Required",
                                    "Some of the changed settings will require a restart of Amber. Would you like "
                                    "to restart now?",
                                    QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    if (ret == QMessageBox::Cancel) return;
    if (ret == QMessageBox::Yes) {
      if (!amber::Global->can_close_project()) return;
      restart_after_saving = true;
    }
  }

  bool reload_language =
      (!restart_after_saving && amber::CurrentConfig.language_file != language_combobox->currentData().toString());

  save_config_from_ui();
  handle_preview_resolution_changes();

  for (auto key_shortcut_field : key_shortcut_fields) {
    key_shortcut_field->set_action_shortcut();
  }

  if (reinit_audio) init_audio();
  if (reload_effects) panel_effect_controls->Reload();
  if (reload_language) amber::Global->load_translation_from_config();

  QDialog::accept();

  if (restart_after_saving) {
    amber::Global->set_modified(false);
    amber::MainWindow->close();
    QProcess::startDetached(QApplication::applicationFilePath(), {amber::ActiveProjectFilename});
  }
}

void PreferencesDialog::reset_default_shortcut() {
  QList<QTreeWidgetItem*> items = keyboard_tree->selectedItems();
  for (int i = 0; i < items.size(); i++) {
    QTreeWidgetItem* item = keyboard_tree->selectedItems().at(i);
    static_cast<KeySequenceEditor*>(keyboard_tree->itemWidget(item, 1))->reset_to_default();
  }
}

void PreferencesDialog::reset_all_shortcuts() {
  if (QMessageBox::question(this, tr("Confirm Reset All Shortcuts"),
                            tr("Are you sure you wish to reset all keyboard shortcuts to their defaults?"),
                            QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
    for (auto key_shortcut_field : key_shortcut_fields) {
      key_shortcut_field->reset_to_default();
    }
  }
}

bool PreferencesDialog::refine_shortcut_list(const QString& s, QTreeWidgetItem* parent) {
  if (parent == nullptr) {
    for (int i = 0; i < keyboard_tree->topLevelItemCount(); i++) {
      refine_shortcut_list(s, keyboard_tree->topLevelItem(i));
    }
  } else {
    parent->setExpanded(!s.isEmpty());

    bool all_children_are_hidden = !s.isEmpty();

    for (int i = 0; i < parent->childCount(); i++) {
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
        if (index == 0 || (index > 0 && ba.at(index - 1) == '\n')) {
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
      QMessageBox::critical(this, tr("Error saving shortcuts"), tr("Failed to open file for reading"));
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
  if (QMessageBox::question(this, tr("Delete All Previews"), tr("Are you sure you want to delete all previews?"),
                            QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
    delete_previews(1);
    QMessageBox::information(this, tr("Previews Deleted"),
                             tr("All previews deleted successfully. You may have to re-open your current project for "
                                "changes to take effect."),
                             QMessageBox::Ok);
  }
}

void PreferencesDialog::edit_default_sequence_settings() {
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
  language_combobox->setToolTip(tr("Choose the language for the menus and buttons in the application.\n\nChanges will be applied immediately to all menus and windows."));

  // add default language (en-US)
  language_combobox->addItem(QLocale::languageToString(QLocale("en-US").language()));

  // add languages from file
  QList<QString> translation_paths = get_language_paths();

  // iterate through all language search paths
  for (const auto& translation_path : translation_paths) {
    QDir translation_dir(translation_path);
    if (translation_dir.exists()) {
      QStringList translation_files = translation_dir.entryList({"*.qm"}, QDir::Files | QDir::NoDotAndDotDot);
      for (const auto& translation_file : translation_files) {
        // get path of translation relative to the application path
        QString locale_full_path = translation_dir.filePath(translation_file);
        QString locale_relative_path = QDir(get_app_path()).relativeFilePath(locale_full_path);

        QFileInfo locale_file(translation_file);
        QString locale_file_basename = locale_file.baseName();
        QString locale_str = locale_file_basename.mid(locale_file_basename.lastIndexOf('_') + 1);
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
  imgSeqFormatEdit->setToolTip(tr("List the file types that can be loaded as a series of images (like .png or .jpg).\n\nSeparate multiple formats with spaces or commas."));

  general_layout->addWidget(imgSeqFormatEdit, row, 1, 1, 4);

  row++;

  // General -> Thumbnail and Waveform Resolution
  general_layout->addWidget(new QLabel(tr("Thumbnail Resolution:"), this), row, 0);

  thumbnail_res_spinbox = new QSpinBox(this);
  thumbnail_res_spinbox->setMinimum(0);
  thumbnail_res_spinbox->setMaximum(INT_MAX);
  thumbnail_res_spinbox->setValue(amber::CurrentConfig.thumbnail_resolution);
  thumbnail_res_spinbox->setToolTip(tr("Sets how sharp the small picture previews of video clips look in the media list.\n\nHigher values make them look nicer but use more computer power and memory."));
  general_layout->addWidget(thumbnail_res_spinbox, row, 1);

  general_layout->addWidget(new QLabel(tr("Waveform Resolution:"), this), row, 2);

  waveform_res_spinbox = new QSpinBox(this);
  waveform_res_spinbox->setMinimum(0);
  waveform_res_spinbox->setMaximum(INT_MAX);
  waveform_res_spinbox->setValue(amber::CurrentConfig.waveform_resolution);
  waveform_res_spinbox->setToolTip(tr("Sets how detailed the audio soundwave squiggles look on the timeline tracks.\n\nHigher resolutions look better but can make scrolling the timeline slower."));
  general_layout->addWidget(waveform_res_spinbox, row, 3);

  QPushButton* delete_preview_btn = new QPushButton(tr("Delete Previews"));
  delete_preview_btn->setToolTip(tr("Cleans up all saved temporary thumbnail and audio files to free up disk space.\n\nThis is safe to do, but the app will have to rebuild them next time you open the project."));
  general_layout->addWidget(delete_preview_btn, row, 4);
  connect(delete_preview_btn, &QPushButton::clicked, this, &PreferencesDialog::delete_all_previews);

  row++;

  // General -> Default Still Image Length
  general_layout->addWidget(new QLabel(tr("Default Still Image Length:"), this), row, 0);
  default_still_length_slider = new LabelSlider(this);
  default_still_length_slider->SetDisplayType(LabelSlider::FrameNumber);
  default_still_length_slider->SetMinimum(1);
  default_still_length_slider->SetMaximum(9999);
  default_still_length_slider->SetValue(amber::CurrentConfig.default_still_length);
  default_still_length_slider->setToolTip(tr("The default length/duration that will be used when you import a photo or flat color into the timeline."));
  if (amber::ActiveSequence != nullptr) {
    default_still_length_slider->SetFrameRate(amber::ActiveSequence->frame_rate);
  }
  general_layout->addWidget(default_still_length_slider, row, 1, 1, 4);

  row++;

  // General -> Use Software Fallbacks When Possible
  QCheckBox* use_software_fallbacks_checkbox = new QCheckBox(tr("Use Software Fallbacks When Possible"));
  use_software_fallbacks_checkbox->setToolTip(tr("Tells the app to use safe, standard software drawing if the graphic card behaves badly.\n\nHelps fix visual glitches, but will run slower than hardware graphics."));
  AddBoolPair(use_software_fallbacks_checkbox, &amber::CurrentConfig.use_software_fallback, true);
  general_layout->addWidget(use_software_fallbacks_checkbox, row, 0, 1, 4);

  row++;

  // General -> Hardware Decoding
  QCheckBox* hardware_decoding_checkbox = new QCheckBox(tr("Hardware Decoding (VAAPI/D3D11VA/VideoToolbox)"));
  hardware_decoding_checkbox->setToolTip(tr(
      "Uses your graphics card to decode video files much faster, saving CPU power.\n\nMay cause crashes on older computer graphics cards. Requires restart."));
  AddBoolPair(hardware_decoding_checkbox, &amber::CurrentConfig.hardware_decoding, true);
  general_layout->addWidget(hardware_decoding_checkbox, row, 0, 1, 4);

  row++;

  // General -> Default Sequence Settings
  QPushButton* default_sequence_settings = new QPushButton(tr("Default Sequence Settings"));
  default_sequence_settings->setToolTip(tr("Set the default width, height, frame rate, and sound layout for newly created sequences."));
  connect(default_sequence_settings, &QPushButton::clicked, this, &PreferencesDialog::edit_default_sequence_settings);
  general_layout->addWidget(default_sequence_settings);

  row++;

  // General -> Auto-Recovery
  QGroupBox* autorecovery_group = new QGroupBox(tr("Auto-Recovery"), this);
  QGridLayout* ar_grid = new QGridLayout(autorecovery_group);

  autorecovery_enabled_check = new QCheckBox(tr("Enable Auto-Recovery"), this);
  autorecovery_enabled_check->setChecked(amber::CurrentConfig.autorecovery_enabled);
  autorecovery_enabled_check->setToolTip(tr("Automatically save backups of your project periodically to prevent data loss in case of a crash."));
  ar_grid->addWidget(autorecovery_enabled_check, 0, 0, 1, 2);

  ar_grid->addWidget(new QLabel(tr("Interval (minutes):"), this), 1, 0);
  autorecovery_interval_slider = new LabelSlider(this);
  autorecovery_interval_slider->SetMinimum(1);
  autorecovery_interval_slider->SetMaximum(60);
  autorecovery_interval_slider->SetValue(amber::CurrentConfig.autorecovery_interval);
  autorecovery_interval_slider->setEnabled(amber::CurrentConfig.autorecovery_enabled);
  autorecovery_interval_slider->setToolTip(tr("How often the editor should automatically save a backup of your project."));
  ar_grid->addWidget(autorecovery_interval_slider, 1, 1);

  ar_grid->addWidget(new QLabel(tr("Maximum Versions:"), this), 2, 0);
  autorecovery_max_slider = new LabelSlider(this);
  autorecovery_max_slider->SetMinimum(1);
  autorecovery_max_slider->SetMaximum(20);
  autorecovery_max_slider->SetValue(amber::CurrentConfig.autorecovery_max);
  autorecovery_max_slider->setEnabled(amber::CurrentConfig.autorecovery_enabled);
  autorecovery_max_slider->setToolTip(tr("The maximum number of auto-saved backup files to keep in storage."));
  ar_grid->addWidget(autorecovery_max_slider, 2, 1);

  connect(autorecovery_enabled_check, &QCheckBox::toggled, autorecovery_interval_slider, &QWidget::setEnabled);
  connect(autorecovery_enabled_check, &QCheckBox::toggled, autorecovery_max_slider, &QWidget::setEnabled);

  general_layout->addWidget(autorecovery_group, row, 0, 1, 5);

  tabWidget->addTab(general_tab, tr("General"));

  // Behavior
  QWidget* behavior_tab = new QWidget(this);
  tabWidget->addTab(behavior_tab, tr("Behavior"));

  ColumnedGridLayout* behavior_tab_layout = new ColumnedGridLayout(behavior_tab, 2);

  QCheckBox* add_default_effects_to_clips = new QCheckBox(tr("Add Default Effects to New Clips"));
  add_default_effects_to_clips->setToolTip(tr("Automatically add standard effects like position, scale, and volume adjustments to new clips."));
  AddBoolPair(add_default_effects_to_clips, &amber::CurrentConfig.add_default_effects_to_clips);
  behavior_tab_layout->Add(add_default_effects_to_clips);

  QCheckBox* auto_seek_to_beginning =
      new QCheckBox(tr("Automatically Seek to the Beginning When Playing at the End of a Sequence"));
  auto_seek_to_beginning->setToolTip(tr("When playback reaches the end of the timeline, jump back to the start and play again."));
  AddBoolPair(auto_seek_to_beginning, &amber::CurrentConfig.auto_seek_to_beginning);
  behavior_tab_layout->Add(auto_seek_to_beginning);

  QCheckBox* selecting_also_seeks = new QCheckBox(tr("Selecting Also Seeks"));
  selecting_also_seeks->setToolTip(tr("Clicking on a clip automatically jumps the red timeline playhead to the start of that clip."));
  AddBoolPair(selecting_also_seeks, &amber::CurrentConfig.select_also_seeks);
  behavior_tab_layout->Add(selecting_also_seeks);

  QCheckBox* edit_tool_also_seeks = new QCheckBox(tr("Edit Tool Also Seeks"));
  edit_tool_also_seeks->setToolTip(tr("Clicking with the edit/select tool on empty track space moves the playhead to that point."));
  AddBoolPair(edit_tool_also_seeks, &amber::CurrentConfig.edit_tool_also_seeks);
  behavior_tab_layout->Add(edit_tool_also_seeks);

  QCheckBox* edit_tool_selects_links = new QCheckBox(tr("Edit Tool Selects Links"));
  edit_tool_selects_links->setToolTip(tr("Clicking on a clip also selects any other clips that are linked to it (like a video and its audio)."));
  AddBoolPair(edit_tool_selects_links, &amber::CurrentConfig.edit_tool_selects_links);
  behavior_tab_layout->Add(edit_tool_selects_links);

  QCheckBox* seek_also_selects = new QCheckBox(tr("Seek Also Selects"));
  seek_also_selects->setToolTip(tr("Moving the red playhead will automatically highlight the clips it passes over."));
  AddBoolPair(seek_also_selects, &amber::CurrentConfig.seek_also_selects);
  behavior_tab_layout->Add(seek_also_selects);

  QCheckBox* snap_to_outgoing_clip = new QCheckBox(tr("Snap Playhead to Last Frame of Outgoing Clip"));
  snap_to_outgoing_clip->setToolTip(
      tr("When snapping the playhead to a clip boundary, show the last frame of the outgoing clip instead of the first "
         "frame of the incoming clip"));
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
    snap_outgoing_modifier_combo->setToolTip(
        tr("Hold this key while seeking to invert the snap-to-outgoing-clip behavior"));
    mod_layout->addWidget(snap_outgoing_modifier_combo);
    mod_layout->addStretch();
    behavior_tab_layout->Add(mod_row);
  }

  QCheckBox* seek_to_end_of_pastes = new QCheckBox(tr("Seek to the End of Pastes"));
  seek_to_end_of_pastes->setToolTip(tr("After pasting clips, the playhead will automatically jump to the end of the newly pasted clips."));
  AddBoolPair(seek_to_end_of_pastes, &amber::CurrentConfig.paste_seeks);
  behavior_tab_layout->Add(seek_to_end_of_pastes);

  QCheckBox* scroll_wheel_zooms = new QCheckBox(tr("Scroll Wheel Zooms"));
  scroll_wheel_zooms->setToolTip(tr("Moving your mouse scroll wheel directly zooms the timeline in and out instead of scrolling it.\n\nHold CTRL to toggle this setting."));
  AddBoolPair(scroll_wheel_zooms, &amber::CurrentConfig.scroll_zooms);
  behavior_tab_layout->Add(scroll_wheel_zooms);

  QCheckBox* invert_timeline_scroll_axes = new QCheckBox(tr("Invert Timeline Scroll Axes"));
  invert_timeline_scroll_axes->setToolTip(tr("Swap vertical and horizontal scrolling: rolling the wheel vertically moves the timeline left/right."));
  AddBoolPair(invert_timeline_scroll_axes, &amber::CurrentConfig.invert_timeline_scroll_axes);
  behavior_tab_layout->Add(invert_timeline_scroll_axes);

  QCheckBox* enable_drag_files_to_timeline = new QCheckBox(tr("Enable Drag Files to Timeline"));
  enable_drag_files_to_timeline->setToolTip(tr("Drag video and audio files from outside the app straight onto a timeline track to import them."));
  AddBoolPair(enable_drag_files_to_timeline, &amber::CurrentConfig.enable_drag_files_to_timeline);
  behavior_tab_layout->Add(enable_drag_files_to_timeline);

  QCheckBox* autoscale_by_default = new QCheckBox(tr("Auto-Scale By Default"));
  autoscale_by_default->setToolTip(tr("Automatically stretches or shrinks imported videos/photos to match the sequence frame size."));
  AddBoolPair(autoscale_by_default, &amber::CurrentConfig.autoscale_by_default);
  behavior_tab_layout->Add(autoscale_by_default);

  QCheckBox* enable_seek_to_import = new QCheckBox(tr("Auto-Seek to Imported Clips"));
  enable_seek_to_import->setToolTip(tr("Jumps the timeline playhead to the starting position of newly imported/added clips."));
  AddBoolPair(enable_seek_to_import, &amber::CurrentConfig.enable_seek_to_import);
  behavior_tab_layout->Add(enable_seek_to_import);

  QCheckBox* enable_drop_on_media_to_replace = new QCheckBox(tr("Drop Files on Media to Replace"));
  enable_drop_on_media_to_replace->setToolTip(tr("Drag a new file and drop it on top of an existing clip in the media panel to swap the source file everywhere."));
  AddBoolPair(enable_drop_on_media_to_replace, &amber::CurrentConfig.drop_on_media_to_replace);
  behavior_tab_layout->Add(enable_drop_on_media_to_replace);

  QCheckBox* enable_hover_focus = new QCheckBox(tr("Enable Hover Focus"));
  enable_hover_focus->setToolTip(tr("Automatically focuses a panel just by moving your mouse pointer over it without needing to click."));
  AddBoolPair(enable_hover_focus, &amber::CurrentConfig.hover_focus);
  behavior_tab_layout->Add(enable_hover_focus);

  QCheckBox* set_name_and_marker = new QCheckBox(tr("Ask For Name When Setting Marker"));
  set_name_and_marker->setToolTip(tr("Pops up a prompt asking you to type a name whenever you create a timeline marker."));
  AddBoolPair(set_name_and_marker, &amber::CurrentConfig.set_name_with_marker);
  behavior_tab_layout->Add(set_name_and_marker);

  QCheckBox* reopen_recent_project = new QCheckBox(tr("Re-open Recent Project on Startup"));
  reopen_recent_project->setToolTip(tr("Automatically opens the project you were working on last time when you launch the application."));
  AddBoolPair(reopen_recent_project, &amber::CurrentConfig.reopen_recent_project);
  behavior_tab_layout->Add(reopen_recent_project);

  QCheckBox* middle_click_edge_scroll = new QCheckBox(tr("Enable Middle-Click Edge Scrolling"));
  middle_click_edge_scroll->setToolTip(tr("Allows you to middle-click and move your cursor near the timeline viewport edges to automatically scroll the tracks in that direction."));
  AddBoolPair(middle_click_edge_scroll, &amber::CurrentConfig.middle_click_edge_scroll);
  behavior_tab_layout->Add(middle_click_edge_scroll);

  QCheckBox* snap_animation = new QCheckBox(tr("Animate Clip Snapping"));
  snap_animation->setToolTip(tr("When enabled, clips smoothly glide (lerp) to their snapped position instead of jumping abruptly. Gives a subtle, satisfying animation during drag-and-snap operations."));
  AddBoolPair(snap_animation, &amber::CurrentConfig.snap_animation);
  behavior_tab_layout->Add(snap_animation);

  QCheckBox* clip_outline_on_move_only = new QCheckBox(tr("Show Clip Outline Only When Moving"));
  clip_outline_on_move_only->setToolTip(tr("When enabled, the white inner-highlight border on clips is only drawn while you are actively dragging a clip. When disabled, the outline is always visible.\n\nUseful if you prefer a cleaner timeline look at rest."));
  AddBoolPair(clip_outline_on_move_only, &amber::CurrentConfig.clip_outline_on_move_only);
  behavior_tab_layout->Add(clip_outline_on_move_only);

  QWidget* frame_skip_row = new QWidget(behavior_tab);
  QHBoxLayout* frame_skip_layout = new QHBoxLayout(frame_skip_row);
  frame_skip_layout->setContentsMargins(0, 0, 0, 0);
  frame_skip_layout->addWidget(new QLabel(tr("Jump Step:"), behavior_tab));
  frame_skip_step_field = new QSpinBox(behavior_tab);
  frame_skip_step_field->setMinimum(1);
  frame_skip_step_field->setMaximum(999);
  frame_skip_step_field->setValue(amber::CurrentConfig.frame_skip_step);
  frame_skip_step_field->setToolTip(tr("Sets the number of frames the playhead will jump forward or backward when using jump shortcuts."));
  frame_skip_layout->addWidget(frame_skip_step_field);
  frame_skip_layout->addStretch();
  behavior_tab_layout->Add(frame_skip_row);

  // Keyframe defaults
  QWidget* keyframe_defaults_row = new QWidget(behavior_tab);
  QHBoxLayout* kf_layout = new QHBoxLayout(keyframe_defaults_row);
  kf_layout->setContentsMargins(0, 0, 0, 0);

  sticky_keyframe_type_check = new QCheckBox(tr("Use Last Keyframe Type as Default"), behavior_tab);
  sticky_keyframe_type_check->setToolTip(
      tr("When enabled, changing a keyframe's type also updates the default type for new keyframes"));
  sticky_keyframe_type_check->setChecked(amber::CurrentConfig.sticky_keyframe_type);
  kf_layout->addWidget(sticky_keyframe_type_check);

  kf_layout->addWidget(new QLabel(tr("Default Type:"), behavior_tab));
  default_keyframe_type_combo = new QComboBox(behavior_tab);
  default_keyframe_type_combo->addItem(tr("Linear"), 0);
  default_keyframe_type_combo->addItem(tr("Bezier"), 1);
  default_keyframe_type_combo->addItem(tr("Hold"), 2);
  default_keyframe_type_combo->setCurrentIndex(qBound(0, amber::CurrentConfig.default_keyframe_type, 2));
  default_keyframe_type_combo->setEnabled(!amber::CurrentConfig.sticky_keyframe_type);
  kf_layout->addWidget(default_keyframe_type_combo);
  kf_layout->addStretch();

  connect(sticky_keyframe_type_check, &QCheckBox::toggled, this,
          [this](bool checked) { default_keyframe_type_combo->setEnabled(!checked); });

  behavior_tab_layout->Add(keyframe_defaults_row);

  // Appearance
  QWidget* appearance_tab = new QWidget(this);
  tabWidget->addTab(appearance_tab, tr("Appearance"));

  row = 0;

  QGridLayout* appearance_layout = new QGridLayout(appearance_tab);

  // Appearance -> Theme
  appearance_layout->addWidget(new QLabel(tr("Theme")), row, 0);

  ui_style = new QComboBox();
  ui_style->addItem(tr("Amber Dark (Default)"), amber::styling::kAmberDefaultDark);
  ui_style->addItem(tr("Amber Light"), amber::styling::kAmberDefaultLight);
  ui_style->addItem(tr("Native"), amber::styling::kNativeDarkIcons);
  ui_style->addItem(tr("Native (Light Icons)"), amber::styling::kNativeLightIcons);
  ui_style->setCurrentIndex(amber::CurrentConfig.style);
  ui_style->setToolTip(tr("Pick a beautiful dark, light, or native window theme for the editor's buttons and backgrounds."));
  appearance_layout->addWidget(ui_style, row, 1, 1, 2);

  row++;

#ifdef Q_OS_WIN
  // Native menu styling is only available on Windows. Environments like Ubuntu and Mac use the native menu system by
  // default
  QCheckBox* native_menus = new QCheckBox(tr("Use Native Menu Styling"));
  native_menus->setToolTip(tr("Enable standard native window menus instead of custom-styled menus."));
  AddBoolPair(native_menus, &amber::CurrentConfig.use_native_menu_styling, true);
  appearance_layout->addWidget(native_menus, row, 0, 1, 3);

  row++;
#endif

  // Appearance -> Custom CSS
  appearance_layout->addWidget(new QLabel(tr("Custom CSS:"), this), row, 0);

  custom_css_fn = new QLineEdit(general_tab);
  custom_css_fn->setText(amber::CurrentConfig.css_path);
  custom_css_fn->setToolTip(tr("Specify a custom styles file (.css) to design and change the colors and shapes of the user interface."));
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
  effect_textbox_lines_field->setToolTip(tr("Set the default height (in number of text lines) for text entry boxes inside the effect properties."));
  appearance_layout->addWidget(effect_textbox_lines_field, row, 1, 1, 2);

  row++;

  // Appearance -> Effect Panel Shrinkable
  QCheckBox* effect_panel_shrinkable =
      new QCheckBox(tr("Allow Effect Properties panel to be smaller than its content"));
  effect_panel_shrinkable->setToolTip(
      tr("Allow the effect settings panel to be squished smaller than its default size, adding scrollbars.\n\nA horizontal scrollbar will appear to access clipped content."));
  AddBoolPair(effect_panel_shrinkable, &amber::CurrentConfig.effect_panel_shrinkable);
  appearance_layout->addWidget(effect_panel_shrinkable, row, 0, 1, 3);

  row++;
  appearance_layout->setRowStretch(row, 1);

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
  upcoming_queue_spinbox->setToolTip(tr("Amount of future video frames to pre-load in your RAM so playback runs smoothly without freezing."));
  memory_usage_layout->addWidget(upcoming_queue_spinbox, 0, 1);
  upcoming_queue_type = new QComboBox(playback_tab);
  upcoming_queue_type->addItem(tr("frames"));
  upcoming_queue_type->addItem(tr("seconds"));
  upcoming_queue_type->setCurrentIndex(amber::CurrentConfig.upcoming_queue_type);
  upcoming_queue_type->setToolTip(tr("Select whether upcoming pre-loaded frames are counted in frames or seconds of duration."));
  memory_usage_layout->addWidget(upcoming_queue_type, 0, 2);
  memory_usage_layout->addWidget(new QLabel(tr("Previous Frame Queue:"), playback_tab), 1, 0);
  previous_queue_spinbox = new QDoubleSpinBox(playback_tab);
  previous_queue_spinbox->setValue(amber::CurrentConfig.previous_queue_size);
  previous_queue_spinbox->setToolTip(tr("Amount of past video frames kept in memory so scrubbing backward is instant."));
  memory_usage_layout->addWidget(previous_queue_spinbox, 1, 1);
  previous_queue_type = new QComboBox(playback_tab);
  previous_queue_type->addItem(tr("frames"));
  previous_queue_type->addItem(tr("seconds"));
  previous_queue_type->setCurrentIndex(amber::CurrentConfig.previous_queue_type);
  previous_queue_type->setToolTip(tr("Select whether previously loaded memory frames are counted in frames or seconds of duration."));
  memory_usage_layout->addWidget(previous_queue_type, 1, 2);
  playback_tab_layout->addWidget(memory_usage_group);
  playback_tab_layout->addStretch();

  tabWidget->addTab(playback_tab, tr("Playback"));

  // Audio
  QWidget* audio_tab = new QWidget(this);

  QGridLayout* audio_tab_layout = new QGridLayout(audio_tab);

  row = 0;

  // Audio -> Output Device

  audio_tab_layout->addWidget(new QLabel(tr("Output Device:")), row, 0);

  audio_output_devices = new QComboBox();
  audio_output_devices->addItem(tr("Default"), "");
  audio_output_devices->setToolTip(tr("The speakers or headphones where the editor's sound will play."));

  // list all available audio output devices
  QList<QAudioDevice> devs = QMediaDevices::audioOutputs();
  bool found_preferred_device = false;
  for (const auto& dev : devs) {
    audio_output_devices->addItem(dev.description(), dev.description());
    if (!found_preferred_device && dev.description() == amber::CurrentConfig.preferred_audio_output) {
      audio_output_devices->setCurrentIndex(audio_output_devices->count() - 1);
      found_preferred_device = true;
    }
  }

  audio_tab_layout->addWidget(audio_output_devices, row, 1);

  row++;

  // Audio -> Input Device

  audio_tab_layout->addWidget(new QLabel(tr("Input Device:")), row, 0);

  audio_input_devices = new QComboBox();
  audio_input_devices->addItem(tr("Default"), "");
  audio_input_devices->setToolTip(tr("The microphone or source to capture voiceovers and recording."));

  // list all available audio input devices
  devs = QMediaDevices::audioInputs();
  found_preferred_device = false;
  for (const auto& dev : devs) {
    audio_input_devices->addItem(dev.description(), dev.description());
    if (!found_preferred_device && dev.description() == amber::CurrentConfig.preferred_audio_input) {
      audio_input_devices->setCurrentIndex(audio_input_devices->count() - 1);
      found_preferred_device = true;
    }
  }

  audio_tab_layout->addWidget(audio_input_devices, row, 1);

  row++;

  // Audio -> Sample Rate

  audio_tab_layout->addWidget(new QLabel(tr("Sample Rate:")), row, 0);

  audio_sample_rate = new QComboBox();
  audio_sample_rate->setToolTip(tr("The frequency quality of the audio engine. Higher numbers sound cleaner but use slightly more CPU."));
  combobox_audio_sample_rates(audio_sample_rate);
  for (int i = 0; i < audio_sample_rate->count(); i++) {
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
  recordingComboBox->setToolTip(tr("Record audio in either single-channel (mono) or dual-channel (stereo) track mode."));
  audio_tab_layout->addWidget(recordingComboBox, row, 1);

  row++;

  // Audio -> Audio Scrubbing
  QCheckBox* enable_audio_scrubbing = new QCheckBox(tr("Audio Scrubbing"));
  enable_audio_scrubbing->setToolTip(tr("Play short bursts of sound while dragging the playhead so you can hear exactly where clips start/stop.\n\nCan sound scratchy or high-pitched when dragging very fast."));
  AddBoolPair(enable_audio_scrubbing, &amber::CurrentConfig.enable_audio_scrubbing);
  audio_tab_layout->addWidget(enable_audio_scrubbing, row, 0, 1, 2);

  row++;
  audio_tab_layout->setRowStretch(row, 1);

  tabWidget->addTab(audio_tab, tr("Audio"));

  // Shortcuts
  QWidget* shortcut_tab = new QWidget(this);

  QVBoxLayout* shortcut_layout = new QVBoxLayout(shortcut_tab);

  QLineEdit* key_search_line = new QLineEdit(shortcut_tab);
  key_search_line->setPlaceholderText(tr("Search for action or shortcut"));
  connect(key_search_line, &QLineEdit::textChanged, this, [this](const QString& s) { refine_shortcut_list(s); });

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
  buttonBox->setStandardButtons(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);

  verticalLayout->addWidget(buttonBox);

  connect(buttonBox, &QDialogButtonBox::accepted, this, &PreferencesDialog::accept);
  connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
