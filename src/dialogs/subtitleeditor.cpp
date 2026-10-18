#include "subtitleeditor.h"

#include <QApplication>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QShortcut>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

#include "effects/internal/subtitleeffect.h"
#include "engine/clip.h"
#include "engine/sequence.h"
#include "engine/trackstate.h"
#include "engine/undo/undo_effect.h"
#include "engine/undo/undostack.h"
#include "global/config.h"
#include "global/global.h"
#include "panels/panels.h"
#include "panels/viewer.h"
#include "ui/mainwindow.h"

namespace {

enum Column { kColIndex, kColStart, kColEnd, kColDuration, kColText, kColCount };

// The string a cell's editor starts from (the cue text with real line breaks, the times as shown)
constexpr int kEditRole = Qt::UserRole + 1;

// Clicks (cellClicked) and edits address rows; row i always holds cue i (the filter hides rows, never removes them)

SubtitleEffect* subtitle_effect_of(Clip* c) {
  for (const EffectPtr& e : c->effects) {
    if (auto* s = qobject_cast<SubtitleEffect*>(e.get())) return s;
  }
  return nullptr;
}

ClipPtr find_clip(Sequence* seq, Clip* c) {
  for (const ClipPtr& p : seq->clips) {
    if (p.get() == c) return p;
  }
  return nullptr;
}

QString one_line(const QString& cue_text) {
  return cue_text_to_lines(cue_text).replace(QLatin1Char('\n'), QStringLiteral(" \u23CE "));
}

}  // namespace

// QLineEdit for the times, a multi-line QPlainTextEdit for the text: Enter commits, Shift+Enter breaks the line,
// Escape cancels. setModelData() never writes the model: it hands the value to the dialog, which validates it, pushes
// the undo step and rebuilds the table from the effect (a refused edit simply leaves the old value showing).
class SubtitleCellDelegate : public QStyledItemDelegate {
 public:
  // on_edit gets the value with the target epoch of when the editor opened, so the dialog can drop an edit begun on
  // another clip
  SubtitleCellDelegate(QObject* parent, std::function<int()> epoch,
                       std::function<void(int, int, const QString&, int)> on_edit)
      : QStyledItemDelegate(parent), epoch_(std::move(epoch)), on_edit_(std::move(on_edit)) {}

  // QAbstractItemView::state() is protected: the editor this delegate has open tells whether a cell is being edited
  bool editing() const { return open_editor_ != nullptr; }

  // Closes the open editor without committing it
  void cancel_edit() {
    if (open_editor_ != nullptr) emit closeEditor(open_editor_, QAbstractItemDelegate::RevertModelCache);
  }

  void destroyEditor(QWidget* editor, const QModelIndex& index) const override {
    if (editor == open_editor_) open_editor_ = nullptr;
    QStyledItemDelegate::destroyEditor(editor, index);
  }

  QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    open_epoch_ = epoch_();
    if (index.column() == kColText) {
      auto* editor = new QPlainTextEdit(parent);
      editor->setTabChangesFocus(true);
      open_editor_ = editor;
      return editor;
    }
    open_editor_ = QStyledItemDelegate::createEditor(parent, option, index);
    return open_editor_;
  }

  void setEditorData(QWidget* editor, const QModelIndex& index) const override {
    const QString value = index.data(kEditRole).toString();
    if (auto* text = qobject_cast<QPlainTextEdit*>(editor)) {
      text->setPlainText(value);
      text->selectAll();
    } else if (auto* line = qobject_cast<QLineEdit*>(editor)) {
      line->setText(value);
      line->selectAll();
    }
  }

  void setModelData(QWidget* editor, QAbstractItemModel*, const QModelIndex& index) const override {
    QString value;
    if (auto* text = qobject_cast<QPlainTextEdit*>(editor)) {
      value = text->toPlainText();
    } else if (auto* line = qobject_cast<QLineEdit*>(editor)) {
      value = line->text();
    } else {
      return;
    }
    on_edit_(index.row(), index.column(), value, open_epoch_);
  }

  void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
                            const QModelIndex& index) const override {
    if (index.column() == kColText) {
      // Room for three lines, over the rows below (the editor is a child of the viewport, not of the cell)
      QRect r = option.rect;
      r.setHeight(qMax(r.height(), editor->fontMetrics().lineSpacing() * 3 + 12));
      editor->setGeometry(r);
      return;
    }
    QStyledItemDelegate::updateEditorGeometry(editor, option, index);
  }

  bool eventFilter(QObject* object, QEvent* event) override {
    // The base filter lets Enter through to a text editor (a new paragraph): here it commits, Shift+Enter breaks
    if (event->type() == QEvent::KeyPress) {
      if (auto* text = qobject_cast<QPlainTextEdit*>(object)) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
          if (key->modifiers() & Qt::ShiftModifier) {
            text->insertPlainText(QStringLiteral("\n"));
          } else {
            emit commitData(text);
            emit closeEditor(text, QAbstractItemDelegate::NoHint);
          }
          return true;
        }
      }
    }
    return QStyledItemDelegate::eventFilter(object, event);
  }

 private:
  std::function<int()> epoch_;
  std::function<void(int, int, const QString&, int)> on_edit_;
  mutable QPointer<QWidget> open_editor_;
  mutable int open_epoch_{0};
};

namespace {
QPointer<SubtitleEditor> instance;

}  // namespace

SubtitleEditor::SubtitleEditor(QWidget* parent) : QDialog(parent) {
  resize(760, 480);

  QVBoxLayout* layout = new QVBoxLayout(this);

  QHBoxLayout* top = new QHBoxLayout();
  header_label_ = new QLabel(this);
  QFont bold = header_label_->font();
  bold.setBold(true);
  header_label_->setFont(bold);
  top->addWidget(header_label_, 1);
  filter_edit_ = new QLineEdit(this);
  filter_edit_->setClearButtonEnabled(true);
  filter_edit_->setMaximumWidth(240);
  top->addWidget(filter_edit_);
  layout->addLayout(top);

  stack_ = new QStackedWidget(this);

  table_ = new QTableWidget(0, kColCount, this);
  table_->verticalHeader()->setVisible(false);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  table_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed |
                          QAbstractItemView::AnyKeyPressed);
  table_->setWordWrap(false);
  table_->setTextElideMode(Qt::ElideRight);
  table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  table_->horizontalHeader()->setSectionResizeMode(kColText, QHeaderView::Stretch);
  table_->horizontalHeader()->setHighlightSections(false);
  delegate_ = new SubtitleCellDelegate(
      table_, [this]() { return target_epoch_; },
      [this](int row, int column, const QString& value, int epoch) { apply_edit(row, column, value, epoch); });
  table_->setItemDelegate(delegate_);
  stack_->addWidget(table_);

  hint_label_ = new QLabel(this);
  hint_label_->setAlignment(Qt::AlignCenter);
  hint_label_->setWordWrap(true);
  stack_->addWidget(hint_label_);
  layout->addWidget(stack_, 1);

  QHBoxLayout* buttons = new QHBoxLayout();
  add_button_ = new QPushButton(this);
  delete_button_ = new QPushButton(this);
  shift_button_ = new QPushButton(this);
  import_button_ = new QPushButton(this);
  export_button_ = new QPushButton(this);
  for (QPushButton* b : {add_button_, delete_button_, shift_button_, import_button_, export_button_}) {
    // Not default buttons: Enter in the table or an editor must not press one
    b->setAutoDefault(false);
  }
  buttons->addWidget(add_button_);
  buttons->addWidget(delete_button_);
  buttons->addWidget(shift_button_);
  buttons->addStretch();
  buttons->addWidget(import_button_);
  buttons->addWidget(export_button_);
  layout->addLayout(buttons);

  status_label_ = new QLabel(this);
  layout->addWidget(status_label_);

  connect(add_button_, &QPushButton::clicked, this, &SubtitleEditor::add_cue);
  connect(delete_button_, &QPushButton::clicked, this, &SubtitleEditor::delete_cues);
  connect(shift_button_, &QPushButton::clicked, this, &SubtitleEditor::shift_selected);
  connect(import_button_, &QPushButton::clicked, this, &SubtitleEditor::import_srt);
  connect(export_button_, &QPushButton::clicked, this, &SubtitleEditor::export_srt);
  connect(filter_edit_, &QLineEdit::textChanged, this, &SubtitleEditor::apply_filter);
  connect(table_, &QTableWidget::cellClicked, this, [this](int row, int) { row_clicked(row); });
  connect(table_, &QTableWidget::itemSelectionChanged, this, [this]() {
    update_status();
    update_buttons();
  });

  // Delete removes the selected cues, unless a cell editor has the key
  QAction* delete_action = new QAction(table_);
  delete_action->setShortcut(QKeySequence::Delete);
  delete_action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
  connect(delete_action, &QAction::triggered, this, &SubtitleEditor::delete_cues);
  table_->addAction(delete_action);

  // The main window's Undo/Redo shortcuts don't reach this window
  connect(new QShortcut(QKeySequence::Undo, this), &QShortcut::activated, amber::Global.get(), &AmberGlobal::undo);
  connect(new QShortcut(QKeySequence::Redo, this), &QShortcut::activated, amber::Global.get(), &AmberGlobal::redo);

  connect(&amber::UndoStack, &QUndoStack::indexChanged, this, [this]() {
    if (isVisible()) refresh();
  });

  poll_timer_.setInterval(200);
  connect(&poll_timer_, &QTimer::timeout, this, &SubtitleEditor::refresh);

  Retranslate();
}

void SubtitleEditor::Open(Clip* clip) {
  if (instance == nullptr) instance = new SubtitleEditor(amber::MainWindow);
  if (clip != nullptr && amber::ActiveSequence != nullptr) {
    ClipPtr p = find_clip(amber::ActiveSequence.get(), clip);
    if (p != nullptr && subtitle_effect_of(p.get()) != nullptr) instance->clip_ = p;
  }
  instance->show();
  instance->raise();
  instance->activateWindow();
  instance->refresh();
}

void SubtitleEditor::Retranslate() {
  setWindowTitle(tr("Subtitle Editor"));
  filter_edit_->setPlaceholderText(tr("Find..."));
  table_->setHorizontalHeaderLabels({tr("#"), tr("Start"), tr("End"), tr("Duration"), tr("Text")});
  hint_label_->setText(tr("Select a subtitle clip, or use File > Import Subtitle (.srt)."));
  add_button_->setText(tr("Add"));
  add_button_->setToolTip(tr("Add a subtitle at the playhead"));
  delete_button_->setText(tr("Delete"));
  shift_button_->setText(tr("Shift..."));
  shift_button_->setToolTip(tr("Move the selected subtitles, or all of them when none is selected"));
  import_button_->setText(tr("Import .srt..."));
  export_button_->setText(tr("Export .srt..."));
  refresh();
}

void SubtitleEditor::changeEvent(QEvent* e) {
  if (e->type() == QEvent::LanguageChange) {
    Retranslate();
  } else {
    QDialog::changeEvent(e);
  }
}

void SubtitleEditor::showEvent(QShowEvent* e) {
  QDialog::showEvent(e);
  poll_timer_.start();
  refresh();
}

void SubtitleEditor::hideEvent(QHideEvent* e) {
  poll_timer_.stop();
  QDialog::hideEvent(e);
}

void SubtitleEditor::refresh() {
  Sequence* seq = amber::ActiveSequence.get();

  // The target: the current one while it is still selected, else the first selected subtitle clip, else the topmost
  // one under the playhead, else the current one while it still exists (seeking off a clip mustn't empty the table)
  ClipPtr target;
  if (seq != nullptr) {
    ClipPtr current = clip_.lock();
    if (current != nullptr &&
        (find_clip(seq, current.get()) == nullptr || subtitle_effect_of(current.get()) == nullptr)) {
      current = nullptr;
    }

    ClipPtr first_selected;
    bool current_selected = false;
    for (Clip* c : seq->SelectedClips()) {
      if (subtitle_effect_of(c) == nullptr) continue;
      if (c == current.get()) current_selected = true;
      if (first_selected == nullptr) first_selected = find_clip(seq, c);
    }

    if (current_selected) {
      target = current;
    } else if (first_selected != nullptr) {
      target = first_selected;
    } else {
      for (const ClipPtr& c : seq->clips) {
        if (c == nullptr || c->track() >= 0 || seq->playhead < c->timeline_in() || seq->playhead >= c->timeline_out()) {
          continue;
        }
        if (subtitle_effect_of(c.get()) == nullptr) continue;
        if (target == nullptr || c->track() < target->track()) target = c;
      }
      if (target == nullptr) target = current;
    }
  }

  SubtitleEffect* effect = target != nullptr ? subtitle_effect_of(target.get()) : nullptr;
  const bool target_changed = target != clip_.lock() || effect != effect_;
  clip_ = target;
  effect_ = effect;
  if (target_changed) {
    // An edit begun on the previous clip must not land, by row, on this one: drop the open editor, and should a commit
    // still come through, apply_edit() refuses it by epoch. The table is rebuilt whatever the revision numbers say.
    target_epoch_++;
    if (delegate_->editing()) {
      delegate_->cancel_edit();
      show_message(tr("Edit cancelled: the subtitle clip changed."));
    }
    revision_ = -1;
  }

  if (effect_ == nullptr) {
    header_label_->setText(tr("Subtitles"));
    stack_->setCurrentWidget(hint_label_);
    if (table_->rowCount() > 0) {
      table_->setRowCount(0);
      cues_.clear();
    }
    revision_ = -1;
    update_buttons();
    update_status();
    return;
  }

  stack_->setCurrentWidget(table_);
  header_label_->setText(tr("Subtitles \u2014 %1: %2").arg(amber::track_label(target->track()), target->name()));

  const bool stale = target_changed || effect_->cues_revision() != revision_ || target->timeline_in() != timeline_in_ ||
                     target->timeline_out() != timeline_out_ || target->clip_in() != clip_in_ ||
                     seq->frame_rate != frame_rate_ || amber::CurrentConfig.timecode_view != timecode_view_;
  // A rebuild would close the open editor; it waits for the next refresh
  if (stale && !delegate_->editing()) {
    rebuild_table(!target_changed);
  }

  update_playhead_rows();
  update_buttons();
  update_status();
}

void SubtitleEditor::rebuild_table(bool keep_selection) {
  ClipPtr clip = clip_.lock();
  if (clip == nullptr || effect_ == nullptr) return;

  // Rows are kept selected by index when the same clip's cues changed (an edit, an undo)
  const QVector<int> keep = keep_selection ? selected_rows() : QVector<int>();

  revision_ = effect_->cues_revision();
  cues_ = effect_->cues();
  timeline_in_ = clip->timeline_in();
  timeline_out_ = clip->timeline_out();
  clip_in_ = clip->clip_in();
  frame_rate_ = clip->sequence->frame_rate;
  timecode_view_ = amber::CurrentConfig.timecode_view;

  const QColor hidden_color = palette().color(QPalette::Disabled, QPalette::Text);

  QSignalBlocker blocker(table_);
  table_->clearSelection();
  table_->setRowCount(cues_.size());
  for (int i = 0; i < cues_.size(); i++) {
    const SubtitleCue& cue = cues_.at(i);
    const long start = cue_ms_to_frame(cue.start_ms);
    const long end = cue_ms_to_frame(cue.end_ms);

    auto* index_item = new QTableWidgetItem(QString::number(i + 1));
    index_item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    index_item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    table_->setItem(i, kColIndex, index_item);

    const QString texts[] = {timecode(start), timecode(end), timecode(end - start)};
    for (int c = kColStart; c <= kColDuration; c++) {
      auto* item = new QTableWidgetItem(texts[c - kColStart]);
      item->setData(kEditRole, texts[c - kColStart]);
      table_->setItem(i, c, item);
    }

    auto* text_item = new QTableWidgetItem(one_line(cue.text));
    text_item->setData(kEditRole, cue_text_to_lines(cue.text));
    text_item->setToolTip(cue_text_to_lines(cue.text));
    table_->setItem(i, kColText, text_item);

    // Outside the clip's span on the timeline: kept, but never shown in the viewer
    if (end <= timeline_in_ || start >= timeline_out_) {
      for (int c = 0; c < kColCount; c++) table_->item(i, c)->setForeground(hidden_color);
    }
  }

  for (int row : keep) {
    if (row < table_->rowCount()) {
      table_->selectionModel()->select(table_->model()->index(row, 0),
                                       QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }
  }

  playhead_rows_.clear();
  last_playhead_ = -1;
  apply_filter();
}

void SubtitleEditor::update_playhead_rows() {
  Sequence* seq = amber::ActiveSequence.get();
  if (seq == nullptr || effect_ == nullptr) return;
  if (seq->playhead == last_playhead_) return;
  last_playhead_ = seq->playhead;

  // The cues the viewer draws at the playhead: the effect's time there, as compose_sequence computes it
  QVector<int> rows;
  if (seq->playhead >= timeline_in_ && seq->playhead < timeline_out_) {
    const qint64 ms = cue_ms_at_frame(seq->playhead - timeline_in_ + clip_in_, frame_rate_);
    for (int i = 0; i < cues_.size(); i++) {
      if (cues_.at(i).start_ms <= ms && ms < cues_.at(i).end_ms) rows.append(i);
    }
  }
  if (rows == playhead_rows_) return;

  QColor accent = palette().color(QPalette::Highlight);
  accent.setAlpha(70);
  for (int row : playhead_rows_) {
    if (row >= table_->rowCount()) continue;
    for (int c = 0; c < kColCount; c++) {
      QTableWidgetItem* item = table_->item(row, c);
      QFont f = item->font();
      f.setBold(false);
      item->setFont(f);
      item->setBackground(QBrush());
    }
  }
  for (int row : rows) {
    for (int c = 0; c < kColCount; c++) {
      QTableWidgetItem* item = table_->item(row, c);
      QFont f = item->font();
      f.setBold(true);
      item->setFont(f);
      item->setBackground(accent);
    }
  }
  playhead_rows_ = rows;

  // Follow playback and scrubbing, but leave the view alone while a cell is being edited
  if (!rows.isEmpty() && !delegate_->editing() && !table_->isRowHidden(rows.first())) {
    table_->scrollToItem(table_->item(rows.first(), kColText), QAbstractItemView::EnsureVisible);
  }
}

void SubtitleEditor::update_status() {
  if (!message_.isEmpty() && message_age_.isValid() && message_age_.elapsed() < 6000) {
    status_label_->setText(message_);
    return;
  }
  message_.clear();

  if (effect_ == nullptr) {
    status_label_->clear();
    return;
  }
  const int count = cues_.size();
  QString text = count == 1 ? tr("1 subtitle") : tr("%1 subtitles").arg(count);
  if (!filter_edit_->text().trimmed().isEmpty()) {
    int shown = 0;
    for (int row = 0; row < table_->rowCount(); row++) {
      if (!table_->isRowHidden(row)) shown++;
    }
    text += QStringLiteral(" \u00B7 ") + tr("%1 shown").arg(shown);
  }
  const int selected = selected_rows().size();
  if (selected > 0) text += QStringLiteral(" \u00B7 ") + tr("%1 selected").arg(selected);
  status_label_->setText(text);
}

void SubtitleEditor::update_buttons() {
  const bool has_target = effect_ != nullptr;
  add_button_->setEnabled(has_target);
  delete_button_->setEnabled(has_target && !selected_rows().isEmpty());
  shift_button_->setEnabled(has_target && !cues_.isEmpty());
  export_button_->setEnabled(has_target && !cues_.isEmpty());
  import_button_->setEnabled(amber::ActiveSequence != nullptr);
}

void SubtitleEditor::show_message(const QString& message) {
  message_ = message;
  message_age_.start();
  update_status();
}

void SubtitleEditor::apply_filter() {
  const QString filter = filter_edit_->text().trimmed();
  for (int row = 0; row < table_->rowCount(); row++) {
    const bool hide = !filter.isEmpty() &&
                      !table_->item(row, kColText)->data(kEditRole).toString().contains(filter, Qt::CaseInsensitive);
    table_->setRowHidden(row, hide);
    // A hidden row must not take part in Delete or Shift
    if (hide) {
      for (int c = 0; c < kColCount; c++) table_->item(row, c)->setSelected(false);
    }
  }
  update_status();
}

QVector<int> SubtitleEditor::selected_rows() const {
  QVector<int> rows;
  for (const QModelIndex& index : table_->selectionModel()->selectedRows()) {
    if (!table_->isRowHidden(index.row())) rows.append(index.row());
  }
  std::sort(rows.begin(), rows.end());
  return rows;
}

long SubtitleEditor::cue_ms_to_frame(qint64 ms) const {
  return timeline_in_ - clip_in_ + first_frame_at_or_after_ms(ms, frame_rate_);
}

qint64 SubtitleEditor::frame_to_cue_ms(long frame) const {
  return cue_ms_at_frame(frame - timeline_in_ + clip_in_, frame_rate_);
}

QString SubtitleEditor::timecode(long frames) const {
  // Cues before the clip's source start fall before frame 0 of a clip placed early: frame_to_timecode() takes no sign
  if (frames < 0) return QStringLiteral("-") + frame_to_timecode(-frames, timecode_view_, frame_rate_);
  return frame_to_timecode(frames, timecode_view_, frame_rate_);
}

bool SubtitleEditor::parse_timecode(const QString& text, bool allow_negative, long* frames) const {
  QString s = text.trimmed();
  bool negative = false;
  if (allow_negative && (s.startsWith(QLatin1Char('-')) || s.startsWith(QLatin1Char('+')))) {
    negative = s.startsWith(QLatin1Char('-'));
    s = s.mid(1).trimmed();
  }

  // timecode_to_frame() takes anything (a typo reads as frame 0): check the shape first
  static const QRegularExpression number_re(QStringLiteral("^\\d+$"));
  static const QRegularExpression timecode_re(QStringLiteral("^\\d+([:;]\\d+){0,3}$"));
  const int view = amber::CurrentConfig.timecode_view;
  const bool plain_number = view == amber::kTimecodeFrames || view == amber::kTimecodeMilliseconds;
  if (!(plain_number ? number_re : timecode_re).match(s).hasMatch()) return false;

  const long f = timecode_to_frame(s, view, frame_rate_);
  *frames = negative ? -f : f;
  return true;
}

void SubtitleEditor::commit(QVector<SubtitleCue> cues, const QString& name, int select_index) {
  commit_to(clip_, effect_, std::move(cues), name, select_index);
}

void SubtitleEditor::commit_to(const std::weak_ptr<Clip>& clip, SubtitleEffect* effect, QVector<SubtitleCue> cues,
                               const QString& name, int select_index) {
  // The clip and its effect must still be in the sequence (a queued edit can outlive them)
  ClipPtr c = clip.lock();
  if (c == nullptr || effect == nullptr || amber::ActiveSequence == nullptr ||
      find_clip(amber::ActiveSequence.get(), c.get()) == nullptr || subtitle_effect_of(c.get()) != effect) {
    return;
  }

  // Rows stay in start order: find where the cue to select lands
  SubtitleCue selected;
  if (select_index >= 0 && select_index < cues.size()) selected = cues.at(select_index);
  sort_cues(cues);
  int new_row = -1;
  if (select_index >= 0) {
    for (int i = 0; i < cues.size(); i++) {
      if (cues.at(i).start_ms == selected.start_ms && cues.at(i).end_ms == selected.end_ms &&
          cues.at(i).text == selected.text) {
        new_row = i;
        break;
      }
    }
  }

  amber::UndoStack.push(new SetSubtitleCues(effect, cues, name));
  refresh();

  if (effect == effect_ && new_row >= 0 && new_row < table_->rowCount()) {
    table_->setCurrentCell(new_row, kColText, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    table_->scrollToItem(table_->item(new_row, kColText));
  }
}

void SubtitleEditor::queue_commit(const QVector<SubtitleCue>& cues, const QString& name, int select_index) {
  // After the view has closed the editor: committing rebuilds every item. Bound to this clip, whatever the target is by
  // then (focus leaving the editor for the timeline commits it, then the click selects another clip)
  std::weak_ptr<Clip> clip = clip_;
  SubtitleEffect* effect = effect_;
  QTimer::singleShot(
      0, this, [this, clip, effect, cues, name, select_index]() { commit_to(clip, effect, cues, name, select_index); });
}

void SubtitleEditor::row_clicked(int row) {
  if (row < 0 || row >= cues_.size() || panel_sequence_viewer == nullptr) return;
  // Shift/Ctrl+click extends the selection (to shift or delete several cues): no seek
  if (QApplication::keyboardModifiers() & (Qt::ShiftModifier | Qt::ControlModifier)) return;
  // The first frame the cue shows on. No audio scrub: a click in a list isn't a scrub gesture
  panel_sequence_viewer->seek(cue_ms_to_frame(cues_.at(row).start_ms), false);
}

void SubtitleEditor::apply_edit(int row, int column, const QString& value, int epoch) {
  // Called from the delegate's setModelData(), while the editor is still open: the table was not rebuilt since it
  // opened (refresh() defers that), so cues_ and the row are the edited clip's, unless the target or its cues changed
  // meanwhile, in which case the edit is refused rather than applied to the wrong cue
  if (epoch != target_epoch_ || effect_ == nullptr || effect_->cues_revision() != revision_) {
    show_message(tr("Edit dropped: the subtitles changed while it was open."));
    return;
  }
  if (row < 0 || row >= cues_.size()) return;

  QVector<SubtitleCue> cues = cues_;
  SubtitleCue& cue = cues[row];

  if (column == kColText) {
    QString lines = value;
    while (lines.endsWith(QLatin1Char('\n'))) lines.chop(1);
    if (lines.trimmed().isEmpty()) {
      show_message(tr("A subtitle needs some text: use Delete to remove it."));
      return;
    }
    const QString text = cue_lines_to_text(lines);
    if (text == cue.text) return;
    cue.text = text;
    queue_commit(cues, tr("Edit Subtitle"), row);
    return;
  }

  // The frames the cell showed: retyping them leaves a cue that starts between frames where it is (no snap, no undo
  // step)
  const long shown_start = cue_ms_to_frame(cue.start_ms);
  const long shown_end = cue_ms_to_frame(cue.end_ms);
  // Text first: in the Milliseconds view at a non-integer rate timecode_to_frame() doesn't invert frame_to_timecode(),
  // so Enter on an untouched cell would parse to another frame and move the cue
  const long shown = column == kColStart ? shown_start : column == kColEnd ? shown_end : shown_end - shown_start;
  if (value.trimmed() == timecode(shown)) return;

  long frames;
  if (!parse_timecode(value, false, &frames)) {
    show_message(tr("\"%1\" is not a timecode.").arg(value.trimmed()));
    return;
  }

  if ((column == kColStart && frames == shown_start) || (column == kColEnd && frames == shown_end) ||
      (column == kColDuration && frames == shown_end - shown_start)) {
    return;
  }

  qint64 start = cue.start_ms;
  qint64 end = cue.end_ms;
  if (column == kColStart) {
    start = frame_to_cue_ms(frames);
  } else if (column == kColEnd) {
    end = frame_to_cue_ms(frames);
  } else if (column == kColDuration) {
    end = frame_to_cue_ms(cue_ms_to_frame(cue.start_ms) + frames);
  } else {
    return;
  }

  if (start < 0) {
    show_message(tr("A subtitle can't start before the start of the clip's source."));
    return;
  }
  if (!cue_times_valid(start, end)) {
    show_message(tr("The end must come after the start."));
    return;
  }
  if (start == cue.start_ms && end == cue.end_ms) return;

  cue.start_ms = start;
  cue.end_ms = end;
  queue_commit(cues, tr("Edit Subtitle"), row);
}

void SubtitleEditor::add_cue() {
  refresh();
  Sequence* seq = amber::ActiveSequence.get();
  if (effect_ == nullptr || seq == nullptr) return;

  const long playhead = seq->playhead;
  if (playhead < timeline_in_ || playhead >= timeline_out_) {
    show_message(tr("Move the playhead over the subtitle clip to add a subtitle."));
    return;
  }

  // 2 s, or up to the next cue, but at least the playhead's frame
  const long local = playhead - timeline_in_ + clip_in_;
  const qint64 start = cue_ms_at_frame(local, frame_rate_);
  qint64 end = start + 2000;
  for (const SubtitleCue& c : cues_) {
    if (c.start_ms > start) end = qMin(end, c.start_ms);
  }
  end = qMax(end, cue_ms_at_frame(local + 1, frame_rate_));

  // The new row must be visible for its text to open for editing, whatever the filter
  filter_edit_->clear();

  QVector<SubtitleCue> cues = cues_;
  cues.append({start, end, tr("Subtitle")});
  commit(cues, tr("Add Subtitle"), cues.size() - 1);

  // Straight into typing the text
  const QVector<int> rows = selected_rows();
  if (rows.size() == 1) {
    QTableWidgetItem* item = table_->item(rows.first(), kColText);
    table_->setCurrentItem(item);
    table_->editItem(item);
  }
}

void SubtitleEditor::delete_cues() {
  refresh();
  const QVector<int> rows = selected_rows();
  if (effect_ == nullptr || rows.isEmpty()) return;

  QVector<SubtitleCue> cues;
  for (int i = 0; i < cues_.size(); i++) {
    if (!rows.contains(i)) cues.append(cues_.at(i));
  }
  table_->clearSelection();
  commit(cues, rows.size() == 1 ? tr("Delete Subtitle") : tr("Delete Subtitles"));
  show_message(rows.size() == 1 ? tr("Deleted 1 subtitle.") : tr("Deleted %1 subtitles.").arg(rows.size()));
}

void SubtitleEditor::shift_selected() {
  refresh();
  if (effect_ == nullptr || cues_.isEmpty()) return;

  QVector<int> rows = selected_rows();
  if (rows.isEmpty()) {
    for (int i = 0; i < cues_.size(); i++) rows.append(i);
  }

  bool ok = false;
  const QString label = rows.size() == cues_.size()
                            ? tr("Move all %1 subtitles by (a timecode, '-' for earlier):").arg(rows.size())
                            : tr("Move the %1 selected subtitles by (a timecode, '-' for earlier):").arg(rows.size());
  const QString input = QInputDialog::getText(this, tr("Shift Subtitles"), label, QLineEdit::Normal, timecode(0), &ok);
  if (!ok || input.trimmed().isEmpty()) return;

  long frames;
  if (!parse_timecode(input, true, &frames)) {
    show_message(tr("\"%1\" is not a timecode.").arg(input.trimmed()));
    return;
  }
  if (frames == 0) return;

  QVector<SubtitleCue> cues = cues_;
  const qint64 wanted = qRound64(double(frames) * 1000.0 / frame_rate_);
  const qint64 applied = shift_cues(cues, rows, wanted);
  if (applied == 0) {
    show_message(tr("These subtitles already start at the start of the clip's source."));
    return;
  }
  commit(cues, tr("Shift Subtitles"));
  show_message(applied != wanted ? tr("Shift shortened so that no subtitle starts before the clip's source.")
                                 : tr("Shifted %1 subtitles.").arg(rows.size()));
}

void SubtitleEditor::import_srt() {
  refresh();
  if (effect_ == nullptr) {
    // No subtitle clip yet: the File menu's import makes one (at the playhead, which then targets it here)
    if (amber::MainWindow != nullptr) amber::MainWindow->import_subtitle();
    refresh();
    return;
  }

  const QString path = QFileDialog::getOpenFileName(this, tr("Import Subtitles"), QString(),
                                                    tr("SRT Subtitle Files (*.srt);;All Files (*)"));
  if (path.isEmpty()) return;

  SrtParseResult result = parse_srt(path);
  if (result.cues.isEmpty()) {
    QMessageBox::warning(this, tr("Import Failed"), tr("No valid subtitle cues found in the file."));
    return;
  }
  if (!cues_.isEmpty() && QMessageBox::question(this, tr("Import Subtitles"),
                                                tr("Replace this clip's subtitles (%1) with the %2 from \"%3\"?")
                                                    .arg(cues_.size())
                                                    .arg(result.cues.size())
                                                    .arg(QFileInfo(path).fileName())) != QMessageBox::Yes) {
    return;
  }

  // File times count from the clip's start on the timeline, where the effect's time is clip_in
  const qint64 offset = cue_ms_at_frame(clip_in_, frame_rate_);
  for (SubtitleCue& c : result.cues) {
    c.start_ms += offset;
    c.end_ms += offset;
  }
  commit(result.cues, tr("Import Subtitles"));

  QString message = tr("Imported %1 subtitles.").arg(result.cues.size());
  if (result.skipped > 0) message += QLatin1Char(' ') + tr("%1 malformed entries skipped.").arg(result.skipped);
  show_message(message);
}

void SubtitleEditor::export_srt() {
  refresh();
  ClipPtr clip = clip_.lock();
  if (effect_ == nullptr || clip == nullptr || cues_.isEmpty()) return;

  const QString path = QFileDialog::getSaveFileName(this, tr("Export Subtitles"), clip->name() + QStringLiteral(".srt"),
                                                    tr("SRT Subtitle Files (*.srt)"));
  if (path.isEmpty()) return;

  // What the sequence shows: each cue cut to the clip's span, in sequence time
  const qint64 visible_start = frame_to_cue_ms(timeline_in_);
  const qint64 visible_end = frame_to_cue_ms(timeline_out_);
  QVector<SubtitleCue> visible;
  for (SubtitleCue c : cues_) {
    c.start_ms = qMax(c.start_ms, visible_start);
    c.end_ms = qMin(c.end_ms, visible_end);
    if (c.end_ms > c.start_ms) visible.append(c);
  }
  const qint64 offset = qRound64(double(timeline_in_ - clip_in_) * 1000.0 / frame_rate_);

  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(write_srt(visible, offset).toUtf8()) < 0 || !file.commit()) {
    QMessageBox::critical(this, tr("Export Failed"), tr("Could not write \"%1\".").arg(path));
    return;
  }
  show_message(tr("Exported %1 subtitles to %2.").arg(visible.size()).arg(QFileInfo(path).fileName()));
}
