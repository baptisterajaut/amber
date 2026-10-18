#ifndef SUBTITLEEDITOR_H
#define SUBTITLEEDITOR_H

#include <QDialog>
#include <QElapsedTimer>
#include <QTimer>
#include <memory>

#include "effects/internal/srtparser.h"

class Clip;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class SubtitleCellDelegate;
class SubtitleEffect;

/**
 * @brief Floating window listing the cues of one Subtitle effect: click a cue to seek to it, edit its text and times in
 * place, add, delete, shift, import and export .srt. Every gesture is one undo step (SetSubtitleCues).
 *
 * Times are shown in sequence time, in the user's timecode format. The effect keeps clip-local milliseconds (its
 * timecode, (playhead - timeline_in + clip_in) / frame rate, clip speed not involved), so a cue shows from the first
 * frame whose effect time reaches its start: what the table says is what the viewer shows.
 *
 * Omnipresent like DebugDialog: created on first Open(), then shown and hidden.
 */
// CBA: one clip at a time, SRT only; per-cue style, waveform and timeline dragging if asked
class SubtitleEditor : public QDialog {
  Q_OBJECT
 public:
  explicit SubtitleEditor(QWidget* parent = nullptr);

  // Shows the window (created on first use, parented to the main window), aimed at clip when one is given
  static void Open(Clip* clip = nullptr);

 protected:
  void changeEvent(QEvent* e) override;
  void showEvent(QShowEvent* e) override;
  void hideEvent(QHideEvent* e) override;

 private:
  void Retranslate();

  // Re-picks the target clip and brings the table, the playhead highlight and the status line up to date
  void refresh();
  void rebuild_table(bool keep_selection);
  void update_playhead_rows();
  void update_status();
  void update_buttons();
  void show_message(const QString& message);
  void apply_filter();

  void add_cue();
  void delete_cues();
  void shift_selected();
  void import_srt();
  void export_srt();
  void row_clicked(int row);
  void apply_edit(int row, int column, const QString& value);

  // Pushes the new cue list as one undo step and selects the cue at select_index (after the sort) when >= 0
  void commit(QVector<SubtitleCue> cues, const QString& name, int select_index = -1);

  QVector<int> selected_rows() const;

  // Sequence frame <-> the effect's clip-local time, through the target clip's timeline in and clip in
  long cue_ms_to_frame(qint64 ms) const;
  qint64 frame_to_cue_ms(long frame) const;

  // Parses a typed timecode in the user's format; false when it isn't one. allow_negative accepts a leading '-'.
  bool parse_timecode(const QString& text, bool allow_negative, long* frames) const;
  QString timecode(long frames) const;

  QLabel* header_label_;
  QLineEdit* filter_edit_;
  QStackedWidget* stack_;
  QTableWidget* table_;
  SubtitleCellDelegate* delegate_;
  QLabel* hint_label_;
  QPushButton* add_button_;
  QPushButton* delete_button_;
  QPushButton* shift_button_;
  QPushButton* import_button_;
  QPushButton* export_button_;
  QLabel* status_label_;

  // CBA: no playhead-moved / selection-changed signal exists, so a poll while visible (undo/redo refresh at once)
  QTimer poll_timer_;

  // The target: the clip is checked against the active sequence on every refresh, so a deleted clip is let go
  std::weak_ptr<Clip> clip_;
  SubtitleEffect* effect_{nullptr};

  // What the table was built from
  QVector<SubtitleCue> cues_;
  int revision_{-1};
  long timeline_in_{0};
  long timeline_out_{0};
  long clip_in_{0};
  double frame_rate_{0};
  int timecode_view_{-1};

  QVector<int> playhead_rows_;
  long last_playhead_{-1};

  QString message_;
  QElapsedTimer message_age_;
};

#endif  // SUBTITLEEDITOR_H
