#ifndef WELCOMEDIALOG_H
#define WELCOMEDIALOG_H

#include <QDialog>

class QListWidget;
class QPushButton;

// Help > Welcome Screen..., and the startup dialog when no project opens (Config::show_welcome_screen). The left
// column starts a new project from a sequence template, the right one reopens a recent project. Templates, Custom and
// Empty Project go through File > New's save prompt (AmberGlobal::start_new_project(), nothing to ask on the empty
// startup project), recents through File > Open Recent (AmberGlobal::open_recent()). Escape/Close leaves the project
// as it is.
// CBA: no recent thumbnails, pinning or custom templates; add if asked
class WelcomeDialog : public QDialog {
  Q_OBJECT
 public:
  explicit WelcomeDialog(QWidget* parent = nullptr);

 private:
  QWidget* create_header();
  QWidget* create_new_project_column();
  QWidget* create_recent_column();
  QPushButton* create_template_button(const QString& title, const QString& detail);

  void open_selected_recent();
  void update_open_button();

  QListWidget* recent_list_{nullptr};
  QPushButton* open_button_{nullptr};
  QPushButton* first_template_{nullptr};
};

#endif  // WELCOMEDIALOG_H
