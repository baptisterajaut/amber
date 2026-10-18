#ifndef RENDERQUEUEDIALOG_H
#define RENDERQUEUEDIALOG_H

#include <QDialog>

class QProgressBar;
class QPushButton;
class QTableWidget;

// File > Render Queue: amber::render_queue()'s rows (sequence, output, settings, status) with Remove, Clear Finished
// and Render, which renders them one after the other while the editor waits, as an export does. Modal: exec() it.
class RenderQueueDialog : public QDialog {
  Q_OBJECT
 public:
  explicit RenderQueueDialog(QWidget* parent);

  // Esc / title-bar close while rendering cancels the render instead of closing, as in the export dialog
  void reject() override;

 private:
  // The rows' texts from the queue
  void refresh();
  void update_buttons();
  QString status_text(int row) const;
  void remove_selected();
  // Render, or Cancel while rendering
  void render_clicked();
  void job_started(int row, int number, int count);
  void progress_changed(int value, qint64 remaining_ms);
  void pass_changed(int pass, int passes);
  void render_finished();

  QTableWidget* table_;
  QProgressBar* progress_bar_;
  QPushButton* remove_button_;
  QPushButton* clear_finished_button_;
  QPushButton* render_button_;
  QPushButton* close_button_;

  int percent_{0};          // the rendering row's percentage
  QString pass_;            // "Pass 1 of 2" during a two-pass job, empty otherwise
  qint64 job_start_ms_{0};  // when the rendering row started, for its total time
};

#endif  // RENDERQUEUEDIALOG_H
