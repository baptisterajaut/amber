#ifndef SHADERTOYIMPORTDIALOG_H
#define SHADERTOYIMPORTDIALOG_H

#include <QDialog>

class QLineEdit;
class QPlainTextEdit;
struct EffectMeta;

// "Import ShaderToy Shader…" (end of the Add Video Effect menu): a name and a ShaderToy "Image" tab source. OK
// preprocesses and bakes it (an error keeps the dialog open, so nothing broken is saved), writes
// <data dir>/effects/<name>.shadertoy and registers "ShaderToy/<name>". The caller adds imported_meta() to the
// selected clips.
class ShaderToyImportDialog : public QDialog {
  Q_OBJECT
 public:
  explicit ShaderToyImportDialog(QWidget* parent = nullptr);

  // The registered effect, once the dialog was accepted
  const EffectMeta* imported_meta() const { return imported_meta_; }

 public slots:
  void accept() override;

 private slots:
  void open_file();

 private:
  void show_error(const QString& message);

  QLineEdit* name_edit_;
  QPlainTextEdit* source_edit_;
  QPlainTextEdit* error_box_;
  const EffectMeta* imported_meta_{nullptr};
};

#endif  // SHADERTOYIMPORTDIALOG_H
