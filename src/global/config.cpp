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

#include "config.h"

#include <QFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "panels/panels.h"
#include "panels/project.h"

#include "debug.h"

Config olive::CurrentConfig;
RuntimeConfig olive::CurrentRuntimeConfig;

namespace {

struct BoolEntry {
  const char* name;
  bool Config::* field;
};
struct IntEntry {
  const char* name;
  int Config::* field;
};
struct DoubleEntry {
  const char* name;
  double Config::* field;
};
struct StringEntry {
  const char* name;
  QString Config::* field;
};

constexpr BoolEntry kBoolEntries[] = {
    {"HardwareDecoding", &Config::hardware_decoding},
    {"ShowTrackLines", &Config::show_track_lines},
    {"ScrollZooms", &Config::scroll_zooms},
    {"InvertTimelineScrollAxes", &Config::invert_timeline_scroll_axes},
    {"EditToolSelectsLinks", &Config::edit_tool_selects_links},
    {"EditToolAlsoSeeks", &Config::edit_tool_also_seeks},
    {"SelectAlsoSeeks", &Config::select_also_seeks},
    {"PasteSeeks", &Config::paste_seeks},
    {"RectifiedWaveforms", &Config::rectified_waveforms},
    {"ShowTitleSafeArea", &Config::show_title_safe_area},
    {"UseCustomTitleSafeRatio", &Config::use_custom_title_safe_ratio},
    {"EnableDragFilesToTimeline", &Config::enable_drag_files_to_timeline},
    {"AutoscaleByDefault", &Config::autoscale_by_default},
    {"EnableSeekToImport", &Config::enable_seek_to_import},
    {"AudioScrubbing", &Config::enable_audio_scrubbing},
    {"DropFileOnMediaToReplace", &Config::drop_on_media_to_replace},
    {"HoverFocus", &Config::hover_focus},
    {"SetNameWithMarker", &Config::set_name_with_marker},
    {"ShowProjectToolbar", &Config::show_project_toolbar},
    {"Loop", &Config::loop},
    {"SeekAlsoSelects", &Config::seek_also_selects},
    {"AutoSeekToBeginning", &Config::auto_seek_to_beginning},
    {"UseSoftwareFallback", &Config::use_software_fallback},
    {"CenterTimelineTimecodes", &Config::center_timeline_timecodes},
    {"AddDefaultEffectsToClips", &Config::add_default_effects_to_clips},
    {"NativeMenuStyling", &Config::use_native_menu_styling},
    {"LockedPanels", &Config::locked_panels},
    {"ShowWelcomeDialog", &Config::show_welcome_dialog},
};

constexpr IntEntry kIntEntries[] = {
    {"DefaultTransitionLength", &Config::default_transition_length},
    {"TimecodeView", &Config::timecode_view},
    {"RecordingMode", &Config::recording_mode},
    {"Autoscroll", &Config::autoscroll},
    {"AudioRate", &Config::audio_rate},
    {"ProjectViewType", &Config::project_view_type},
    {"PreviousFrameQueueType", &Config::previous_queue_type},
    {"UpcomingFrameQueueType", &Config::upcoming_queue_type},
    {"EffectTextboxLines", &Config::effect_textbox_lines},
    {"ThumbnailResolution", &Config::thumbnail_resolution},
    {"WaveformResolution", &Config::waveform_resolution},
    {"DefaultSequenceWidth", &Config::default_sequence_width},
    {"DefaultSequenceHeight", &Config::default_sequence_height},
    {"DefaultSequenceAudioFrequency", &Config::default_sequence_audio_frequency},
    {"DefaultSequenceAudioLayout", &Config::default_sequence_audio_channel_layout},
};

constexpr DoubleEntry kDoubleEntries[] = {
    {"CustomTitleSafeRatio", &Config::custom_title_safe_ratio},
    {"PreviousFrameQueueSize", &Config::previous_queue_size},
    {"UpcomingFrameQueueSize", &Config::upcoming_queue_size},
    {"DefaultSequenceFrameRate", &Config::default_sequence_framerate},
};

constexpr StringEntry kStringEntries[] = {
    {"ImageSequenceFormats", &Config::img_seq_formats},
    {"CSSPath", &Config::css_path},
    {"PreferredAudioOutput", &Config::preferred_audio_output},
    {"PreferredAudioInput", &Config::preferred_audio_input},
    {"LanguageFile", &Config::language_file},
};

}  // namespace

Config::Config()
    : img_seq_formats("jpg|jpeg|bmp|tiff|tif|psd|png|tga|jp2|gif")

{}

void Config::load(QString path) {
  QFile f(path);
  if (!f.exists() || !f.open(QIODevice::ReadOnly)) return;

  QXmlStreamReader stream(&f);

  while (!stream.atEnd()) {
    stream.readNext();
    if (!stream.isStartElement()) continue;

    auto name = stream.name();
    stream.readNext();
    auto text = stream.text();

    bool handled = false;
    for (const auto& e : kBoolEntries) {
      if (name == QLatin1String(e.name)) {
        this->*e.field = (text == QLatin1String("1"));
        handled = true;
        break;
      }
    }
    if (!handled) {
      for (const auto& e : kIntEntries) {
        if (name == QLatin1String(e.name)) {
          this->*e.field = text.toInt();
          handled = true;
          break;
        }
      }
    }
    if (!handled) {
      for (const auto& e : kDoubleEntries) {
        if (name == QLatin1String(e.name)) {
          this->*e.field = text.toDouble();
          handled = true;
          break;
        }
      }
    }
    if (!handled) {
      for (const auto& e : kStringEntries) {
        if (name == QLatin1String(e.name)) {
          this->*e.field = text.toString();
          handled = true;
          break;
        }
      }
    }
    if (!handled && name == QLatin1String("Style")) {
      style = static_cast<olive::styling::Style>(text.toInt());
    }
  }

  if (stream.hasError()) {
    qCritical() << "Error parsing config XML." << stream.errorString();
  }
  f.close();
}

void Config::save(QString path) {
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly)) {
    qCritical() << "Could not save configuration";
    return;
  }

  QXmlStreamWriter stream(&f);
  stream.setAutoFormatting(true);
  stream.writeStartDocument();                // doc
  stream.writeStartElement("Configuration");  // configuration

  stream.writeTextElement("Version", QString::number(olive::kSaveVersion));
  stream.writeTextElement("HardwareDecoding", QString::number(hardware_decoding));
  stream.writeTextElement("ShowTrackLines", QString::number(show_track_lines));
  stream.writeTextElement("ScrollZooms", QString::number(scroll_zooms));
  stream.writeTextElement("InvertTimelineScrollAxes", QString::number(invert_timeline_scroll_axes));
  stream.writeTextElement("EditToolSelectsLinks", QString::number(edit_tool_selects_links));
  stream.writeTextElement("EditToolAlsoSeeks", QString::number(edit_tool_also_seeks));
  stream.writeTextElement("SelectAlsoSeeks", QString::number(select_also_seeks));
  stream.writeTextElement("PasteSeeks", QString::number(paste_seeks));
  stream.writeTextElement("ImageSequenceFormats", img_seq_formats);
  stream.writeTextElement("RectifiedWaveforms", QString::number(rectified_waveforms));
  stream.writeTextElement("DefaultTransitionLength", QString::number(default_transition_length));
  stream.writeTextElement("TimecodeView", QString::number(timecode_view));
  stream.writeTextElement("ShowTitleSafeArea", QString::number(show_title_safe_area));
  stream.writeTextElement("UseCustomTitleSafeRatio", QString::number(use_custom_title_safe_ratio));
  stream.writeTextElement("CustomTitleSafeRatio", QString::number(custom_title_safe_ratio));
  stream.writeTextElement("EnableDragFilesToTimeline", QString::number(enable_drag_files_to_timeline));
  stream.writeTextElement("AutoscaleByDefault", QString::number(autoscale_by_default));
  stream.writeTextElement("RecordingMode", QString::number(recording_mode));
  stream.writeTextElement("EnableSeekToImport", QString::number(enable_seek_to_import));
  stream.writeTextElement("AudioScrubbing", QString::number(enable_audio_scrubbing));
  stream.writeTextElement("DropFileOnMediaToReplace", QString::number(drop_on_media_to_replace));
  stream.writeTextElement("Autoscroll", QString::number(autoscroll));
  stream.writeTextElement("AudioRate", QString::number(audio_rate));
  stream.writeTextElement("HoverFocus", QString::number(hover_focus));
  stream.writeTextElement("ProjectViewType", QString::number(project_view_type));
  stream.writeTextElement("SetNameWithMarker", QString::number(set_name_with_marker));
  stream.writeTextElement("ShowProjectToolbar", QString::number(panel_project->IsToolbarVisible()));
  stream.writeTextElement("PreviousFrameQueueSize", QString::number(previous_queue_size));
  stream.writeTextElement("PreviousFrameQueueType", QString::number(previous_queue_type));
  stream.writeTextElement("UpcomingFrameQueueSize", QString::number(upcoming_queue_size));
  stream.writeTextElement("UpcomingFrameQueueType", QString::number(upcoming_queue_type));
  stream.writeTextElement("Loop", QString::number(loop));
  stream.writeTextElement("SeekAlsoSelects", QString::number(seek_also_selects));
  stream.writeTextElement("AutoSeekToBeginning", QString::number(auto_seek_to_beginning));
  stream.writeTextElement("CSSPath", css_path);
  stream.writeTextElement("EffectTextboxLines", QString::number(effect_textbox_lines));
  stream.writeTextElement("UseSoftwareFallback", QString::number(use_software_fallback));
  stream.writeTextElement("CenterTimelineTimecodes", QString::number(center_timeline_timecodes));
  stream.writeTextElement("PreferredAudioOutput", preferred_audio_output);
  stream.writeTextElement("PreferredAudioInput", preferred_audio_input);
  stream.writeTextElement("LanguageFile", language_file);
  stream.writeTextElement("ThumbnailResolution", QString::number(thumbnail_resolution));
  stream.writeTextElement("WaveformResolution", QString::number(waveform_resolution));
  stream.writeTextElement("AddDefaultEffectsToClips", QString::number(add_default_effects_to_clips));
  stream.writeTextElement("Style", QString::number(style));
  stream.writeTextElement("NativeMenuStyling", QString::number(use_native_menu_styling));
  stream.writeTextElement("DefaultSequenceWidth", QString::number(default_sequence_width));
  stream.writeTextElement("DefaultSequenceHeight", QString::number(default_sequence_height));
  stream.writeTextElement("DefaultSequenceFrameRate", QString::number(default_sequence_framerate));
  stream.writeTextElement("DefaultSequenceAudioFrequency", QString::number(default_sequence_audio_frequency));
  stream.writeTextElement("DefaultSequenceAudioLayout", QString::number(default_sequence_audio_channel_layout));
  stream.writeTextElement("LockedPanels", QString::number(locked_panels));
  stream.writeTextElement("ShowWelcomeDialog", QString::number(show_welcome_dialog));

  stream.writeEndElement();   // configuration
  stream.writeEndDocument();  // doc
  f.close();
}

RuntimeConfig::RuntimeConfig()

    = default;
