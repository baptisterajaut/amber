#include "dialogs/shadertoyimportdialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

#include "core/path.h"
#include "core/shadertoy.h"
#include "effects/effect.h"
#include "effects/effectloaders.h"

ShaderToyImportDialog::ShaderToyImportDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("Import ShaderToy Shader"));
  resize(680, 560);

  QVBoxLayout* layout = new QVBoxLayout(this);

  QFormLayout* form = new QFormLayout();
  name_edit_ = new QLineEdit(this);
  name_edit_->setObjectName(QStringLiteral("name"));
  form->addRow(tr("Name:"), name_edit_);
  layout->addLayout(form);

  QHBoxLayout* source_header = new QHBoxLayout();
  source_header->addWidget(new QLabel(tr("Source (ShaderToy \"Image\" tab):"), this));
  source_header->addStretch();
  QPushButton* open_button = new QPushButton(tr("Open File..."), this);
  connect(open_button, &QPushButton::clicked, this, &ShaderToyImportDialog::open_file);
  source_header->addWidget(open_button);
  layout->addLayout(source_header);

  const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  source_edit_ = new QPlainTextEdit(this);
  source_edit_->setObjectName(QStringLiteral("source"));
  source_edit_->setFont(mono);
  source_edit_->setLineWrapMode(QPlainTextEdit::NoWrap);
  source_edit_->setPlaceholderText(
      QStringLiteral("void mainImage(out vec4 fragColor, in vec2 fragCoord) {\n"
                     "  vec2 uv = fragCoord / iResolution.xy;\n"
                     "  fragColor = texture(iChannel0, uv);\n"
                     "}"));
  layout->addWidget(source_edit_, 1);

  error_box_ = new QPlainTextEdit(this);
  error_box_->setObjectName(QStringLiteral("error"));
  error_box_->setFont(mono);
  error_box_->setReadOnly(true);
  error_box_->setMaximumHeight(140);
  error_box_->setVisible(false);
  layout->addWidget(error_box_);

  QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  buttons->setObjectName(QStringLiteral("buttons"));
  connect(buttons, &QDialogButtonBox::accepted, this, &ShaderToyImportDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &ShaderToyImportDialog::reject);
  layout->addWidget(buttons);
}

void ShaderToyImportDialog::show_error(const QString& message) {
  error_box_->setPlainText(message);
  error_box_->setVisible(true);
}

void ShaderToyImportDialog::open_file() {
  const QString file = QFileDialog::getOpenFileName(this, tr("Open ShaderToy Shader"), QString(),
                                                    tr("ShaderToy shaders (*.shadertoy *.glsl *.txt);;All files (*)"));
  if (file.isEmpty()) return;
  QFile f(file);
  if (!f.open(QIODevice::ReadOnly)) {
    show_error(tr("Couldn't open %1.").arg(file));
    return;
  }
  source_edit_->setPlainText(QString::fromUtf8(f.readAll()));
  name_edit_->setText(QFileInfo(file).completeBaseName());
}

void ShaderToyImportDialog::accept() {
  // 1. A name that is a valid file name on every platform
  const QString name = name_edit_->text().trimmed();
  if (name.isEmpty()) {
    show_error(tr("Enter a name for the effect."));
    return;
  }
  static const QRegularExpression kForbidden(QStringLiteral("[/\\\\:*?\"<>|]"));
  if (name.contains(kForbidden) || name.startsWith('.')) {
    show_error(tr("The name can't contain / \\ : * ? \" < > | or start with a dot."));
    return;
  }
  const QDir dir(get_data_dir().filePath(QStringLiteral("effects")));
  const QString file = dir.filePath(name + QStringLiteral(".shadertoy"));
  effects_loaded_mutex.lock();
  const bool registered = get_meta_from_name(QStringLiteral("ShaderToy/") + name) != nullptr;
  effects_loaded_mutex.unlock();
  // CBA: no replace flow, clips holding the old code would only update after reopening
  if (registered || QFileInfo::exists(file) || QFileInfo::exists(dir.filePath(name + QStringLiteral(".xml")))) {
    show_error(tr("An effect with this name already exists. Pick another name."));
    return;
  }

  // 2. Preprocess and bake: an error keeps the dialog open, a success leaves the shader in the disk cache for the
  // first render. CBA: bakes on the UI thread (tens of ms for a typical shader), a worker if big shaders stall it.
  QString source = source_edit_->toPlainText();
  source.replace(QLatin1String("\r\n"), QLatin1String("\n"));
  QString error;
  const QString glsl = amber::shadertoy::to_amber_glsl(source, {}, &error);
  if (glsl.isEmpty() || !Effect::bakeSource(glsl.toUtf8(), QShader::FragmentStage, &error).isValid()) {
    show_error(error);
    return;
  }

  // 3. Write the file where get_effects_paths() looks at the next launch
  if (!dir.mkpath(QStringLiteral("."))) {
    show_error(tr("Couldn't create the folder %1.").arg(dir.absolutePath()));
    return;
  }
  QFile f(file);
  if (!f.open(QIODevice::WriteOnly) || f.write(source.toUtf8()) < 0) {
    show_error(tr("Couldn't write %1.").arg(file));
    return;
  }
  f.close();

  // 4. Register it for this session (the menus re-read `effects` on every open)
  effects_loaded_mutex.lock();
  imported_meta_ = register_shadertoy_effect(file);
  effects_loaded_mutex.unlock();

  QDialog::accept();
}
