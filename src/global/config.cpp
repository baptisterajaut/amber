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
#include <QRegularExpression>
#include <QSaveFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QtMath>

#include "core/appcontext.h"

#include "debug.h"

Config amber::CurrentConfig;
RuntimeConfig amber::CurrentRuntimeConfig;

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
    {"ReopenRecentProject", &Config::reopen_recent_project},
    {"MiddleClickEdgeScroll", &Config::middle_click_edge_scroll},
    {"ShowGuides", &Config::show_guides},
    {"LockGuides", &Config::lock_guides},
    {"ShowFrameRate", &Config::show_frame_rate},
    {"SnapToOutgoingClip", &Config::snap_to_outgoing_clip},
    {"EffectPanelShrinkable", &Config::effect_panel_shrinkable},
    {"ShowColorLabels", &Config::show_color_labels},
    {"AutorecoveryEnabled", &Config::autorecovery_enabled},
    {"StickyKeyframeType", &Config::sticky_keyframe_type},
    {"ShowFrei0rEffects", &Config::show_frei0r_effects},
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
    {"FrameSkipStep", &Config::frame_skip_step},
    {"SnapOutgoingModifier", &Config::snap_outgoing_modifier},
    {"DefaultStillLength", &Config::default_still_length},
    {"AutorecoveryInterval", &Config::autorecovery_interval},
    {"AutorecoveryMax", &Config::autorecovery_max},
    {"DefaultKeyframeType", &Config::default_keyframe_type},
    {"PreviewResolutionDivider", &Config::preview_resolution_divider},
    {"ScrubHalfResolution", &Config::scrub_half_resolution},
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

namespace {

// Returns true if the element name was recognised and the config field updated.
bool apply_config_element(Config* cfg, const QStringView& name, const QStringView& text) {
  for (const auto& e : kBoolEntries) {
    if (name == QLatin1String(e.name)) {
      cfg->*e.field = (text == QLatin1String("1"));
      return true;
    }
  }
  for (const auto& e : kIntEntries) {
    if (name == QLatin1String(e.name)) {
      cfg->*e.field = text.toInt();
      return true;
    }
  }
  for (const auto& e : kDoubleEntries) {
    if (name == QLatin1String(e.name)) {
      cfg->*e.field = text.toDouble();
      return true;
    }
  }
  for (const auto& e : kStringEntries) {
    if (name == QLatin1String(e.name)) {
      cfg->*e.field = text.toString();
      return true;
    }
  }
  return false;
}

}  // namespace

void Config::load(QString path) {
  QFile f(path);
  if (!f.exists() || !f.open(QIODevice::ReadOnly)) return;

  QXmlStreamReader stream(&f);

  while (!stream.atEnd()) {
    stream.readNext();
    // The root element is skipped by name: reading past it would swallow the first setting when no whitespace
    // follows <Configuration>
    if (!stream.isStartElement() || stream.name() == QLatin1String("Configuration")) continue;

    // Copied: the name view is only valid until the next readNext()
    const QString name = stream.name().toString();
    stream.readNext();
    auto text = stream.text();

    if (!apply_config_element(this, name, text) && name == QLatin1String("Style")) {
      style = static_cast<amber::styling::Style>(text.toInt());
    } else if (name == QLatin1String("TrustedVstPlugin") && !text.isEmpty()) {
      // one element per path
      if (!trusted_vst_plugins.contains(text.toString())) trusted_vst_plugins.append(text.toString());
    }
  }

  if (stream.hasError()) {
    qCritical() << "Error parsing config XML." << stream.errorString();
  }
  f.close();

  // Clamp preview resolution divider to valid values
  if (preview_resolution_divider != 1 && preview_resolution_divider != 2 && preview_resolution_divider != 4) {
    preview_resolution_divider = 1;
  }

  // Unknown scrub mode -> Auto
  if (scrub_half_resolution < 0 || scrub_half_resolution > 2) {
    scrub_half_resolution = 0;
  }
}

void Config::save(QString path) {
  // Temp file + rename on commit(): a failed write keeps the previous config instead of truncating it
  QSaveFile f(path);
  // A writable file in a read-only directory has no room for the temp file: write it in place there, as before
  f.setDirectWriteFallback(true);
  if (!f.open(QIODevice::WriteOnly)) {
    qCritical() << "Could not save configuration";
    return;
  }

  QXmlStreamWriter stream(&f);
  stream.setAutoFormatting(true);
  stream.writeStartDocument();                // doc
  stream.writeStartElement("Configuration");  // configuration

  stream.writeTextElement("Version", QString::number(amber::kSaveVersion));
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
  stream.writeTextElement("ShowProjectToolbar", QString::number(amber::app_ctx->isToolbarVisible()));
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
  stream.writeTextElement("ScrubHalfResolution", QString::number(scrub_half_resolution));
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
  stream.writeTextElement("ReopenRecentProject", QString::number(reopen_recent_project));
  stream.writeTextElement("MiddleClickEdgeScroll", QString::number(middle_click_edge_scroll));
  stream.writeTextElement("FrameSkipStep", QString::number(frame_skip_step));
  stream.writeTextElement("ShowGuides", QString::number(show_guides));
  stream.writeTextElement("ShowFrameRate", QString::number(show_frame_rate));
  stream.writeTextElement("SnapToOutgoingClip", QString::number(snap_to_outgoing_clip));
  stream.writeTextElement("SnapOutgoingModifier", QString::number(snap_outgoing_modifier));
  stream.writeTextElement("EffectPanelShrinkable", QString::number(effect_panel_shrinkable));
  stream.writeTextElement("ShowColorLabels", QString::number(show_color_labels));
  stream.writeTextElement("DefaultStillLength", QString::number(default_still_length));
  stream.writeTextElement("AutorecoveryEnabled", QString::number(autorecovery_enabled));
  stream.writeTextElement("AutorecoveryInterval", QString::number(autorecovery_interval));
  stream.writeTextElement("AutorecoveryMax", QString::number(autorecovery_max));
  stream.writeTextElement("StickyKeyframeType", QString::number(sticky_keyframe_type));
  stream.writeTextElement("DefaultKeyframeType", QString::number(default_keyframe_type));
  stream.writeTextElement("ShowFrei0rEffects", QString::number(show_frei0r_effects));
  for (const QString& path : trusted_vst_plugins) stream.writeTextElement("TrustedVstPlugin", path);

  stream.writeEndElement();   // configuration
  stream.writeEndDocument();  // doc
  if (stream.hasError() || !f.commit()) {
    qCritical() << "Could not save configuration";
  }
}

RuntimeConfig::RuntimeConfig()

    = default;

long timecode_to_frame(const QString& s, int view, double frame_rate) {
  QList<QString> list = s.split(QRegularExpression("[:;]"));

  if (view == amber::kTimecodeFrames || (list.size() == 1 && view != amber::kTimecodeMilliseconds)) {
    return s.toLong();
  }

  int frRound = qRound(frame_rate);
  int hours, minutes, seconds, frames;

  if (view == amber::kTimecodeMilliseconds) {
    long milliseconds = s.toLong();

    hours = milliseconds / 3600000;
    milliseconds -= (hours * 3600000);
    minutes = milliseconds / 60000;
    milliseconds -= (minutes * 60000);
    seconds = milliseconds / 1000;
    milliseconds -= (seconds * 1000);
    frames = qRound64((milliseconds * 0.001) * frame_rate);

    seconds = qRound64(seconds * frame_rate);
    minutes = qRound64(minutes * frame_rate * 60);
    hours = qRound64(hours * frame_rate * 3600);
  } else {
    hours = ((list.size() > 0) ? list.at(0).toInt() : 0) * frRound * 3600;
    minutes = ((list.size() > 1) ? list.at(1).toInt() : 0) * frRound * 60;
    seconds = ((list.size() > 2) ? list.at(2).toInt() : 0) * frRound;
    frames = (list.size() > 3) ? list.at(3).toInt() : 0;
  }

  int f = (frames + seconds + minutes + hours);

  if ((view == amber::kTimecodeDrop || view == amber::kTimecodeMilliseconds) && frame_rate_is_droppable(frame_rate)) {
    // return drop
    int d;
    int m;

    int dropFrames =
        qRound(frame_rate *
               .066666);  // Number of frames to drop on the minute marks is the nearest integer to 6% of the framerate
    int framesPer10Minutes = qRound(frame_rate * 60 * 10);         // Number of frames per ten minutes
    int framesPerMinute = (qRound(frame_rate) * 60) - dropFrames;  // Number of frames per minute is the round of the
                                                                   // framerate * 60 minus the number of dropped frames

    d = f / framesPer10Minutes;
    f -= dropFrames * 9 * d;

    m = f % framesPer10Minutes;

    if (m > dropFrames) {
      f -= (dropFrames * ((m - dropFrames) / framesPerMinute));
    }
  }

  // return non-drop
  return f;
}

QString frame_to_timecode(long f, int view, double frame_rate) {
  if (view == amber::kTimecodeFrames) {
    return QString::number(f);
  }

  // return timecode
  int hours = 0;
  int mins = 0;
  int secs = 0;
  int frames = 0;
  QString token = ":";

  if ((view == amber::kTimecodeDrop || view == amber::kTimecodeMilliseconds) && frame_rate_is_droppable(frame_rate)) {
    // CONVERT A FRAME NUMBER TO DROP FRAME TIMECODE
    // Code by David Heidelberger, adapted from Andrew Duncan, further adapted for Olive by Olive Team
    // Given an int called framenumber and a double called framerate
    // Framerate should be 29.97, 59.94, or 23.976, otherwise the calculations will be off.

    int d;
    int m;

    int dropFrames =
        qRound(frame_rate *
               .066666);  // Number of frames to drop on the minute marks is the nearest integer to 6% of the framerate
    int framesPerHour = qRound(frame_rate * 60 * 60);  // Number of frqRound64ames in an hour
    int framesPer24Hours = framesPerHour * 24;         // Number of frames in a day - timecode rolls over after 24 hours
    int framesPer10Minutes = qRound(frame_rate * 60 * 10);         // Number of frames per ten minutes
    int framesPerMinute = (qRound(frame_rate) * 60) - dropFrames;  // Number of frames per minute is the round of the
                                                                   // framerate * 60 minus the number of dropped frames

    // If framenumber is greater than 24 hrs, next operation will rollover clock
    f = f % framesPer24Hours;  // % is the modulus operator, which returns a remainder. a % b = the remainder of a/b

    d = f / framesPer10Minutes;  // \ means integer division, which is a/b without a remainder. Some languages you could
                                 // use floor(a/b)
    m = f % framesPer10Minutes;

    // In the original post, the next line read m>1, which only worked for 29.97. Jean-Baptiste Mardelle correctly
    // pointed out that m should be compared to dropFrames.
    if (m > dropFrames) {
      f = f + (dropFrames * 9 * d) + dropFrames * ((m - dropFrames) / framesPerMinute);
    } else {
      f = f + dropFrames * 9 * d;
    }

    int frRound = qRound(frame_rate);
    frames = f % frRound;
    secs = (f / frRound) % 60;
    mins = ((f / frRound) / 60) % 60;
    hours = (((f / frRound) / 60) / 60);

    token = ";";
  } else {
    // non-drop timecode

    int int_fps = qRound(frame_rate);
    hours = f / (3600 * int_fps);
    mins = f / (60 * int_fps) % 60;
    secs = f / int_fps % 60;
    frames = f % int_fps;
  }
  if (view == amber::kTimecodeMilliseconds) {
    return QString::number((hours * 3600000) + (mins * 60000) + (secs * 1000) + qCeil(frames * 1000 / frame_rate));
  }
  return QString(QString::number(hours).rightJustified(2, '0') + ":" + QString::number(mins).rightJustified(2, '0') +
                 ":" + QString::number(secs).rightJustified(2, '0') + token +
                 QString::number(frames).rightJustified(2, '0'));
}

bool frame_rate_is_droppable(double rate) {
  return (qFuzzyCompare(rate, 23.976) || qFuzzyCompare(rate, 29.97) || qFuzzyCompare(rate, 59.94));
}
