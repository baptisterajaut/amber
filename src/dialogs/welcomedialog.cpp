#include "welcomedialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QStyledItemDelegate>
#include <QSvgRenderer>
#include <QTimer>
#include <QVBoxLayout>

#include "dialogs/newsequencedialog.h"
#include "global/config.h"
#include "global/global.h"
#include "global/projectio.h"
#include "panels/panels.h"
#include "panels/project.h"

namespace {

struct SequenceTemplate {
  const char* name;
  int width;
  int height;
  double frame_rate;
};

// Square pixels, progressive, stereo: what NewSequenceDialog offers by default
constexpr SequenceTemplate kTemplates[] = {
    {QT_TRANSLATE_NOOP("WelcomeDialog", "1080p 30 fps"), 1920, 1080, 30.0},
    {QT_TRANSLATE_NOOP("WelcomeDialog", "1080p 25 fps"), 1920, 1080, 25.0},
    {QT_TRANSLATE_NOOP("WelcomeDialog", "1080p 60 fps"), 1920, 1080, 60.0},
    {QT_TRANSLATE_NOOP("WelcomeDialog", "4K UHD 30 fps"), 3840, 2160, 30.0},
    {QT_TRANSLATE_NOOP("WelcomeDialog", "720p 30 fps"), 1280, 720, 30.0},
    {QT_TRANSLATE_NOOP("WelcomeDialog", "Vertical 30 fps (Shorts, Reels)"), 1080, 1920, 30.0},
};
constexpr int kTemplateAudioFrequency = 48000;

enum RecentRole { kPathRole = Qt::UserRole, kDirRole, kDateRole, kMissingRole };

constexpr int kDimAlpha = 150;
constexpr int kMissingAlpha = 110;

QColor dimmed(QColor c, int alpha = kDimAlpha) {
  c.setAlpha(alpha);
  return c;
}

// Two lines per recent project: the bold file name with the date right-aligned, the directory dimmed below
class RecentProjectDelegate : public QStyledItemDelegate {
 public:
  using QStyledItemDelegate::QStyledItemDelegate;

  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const override {
    return QSize(option.rect.width(), 2 * option.fontMetrics.height() + 3 * kPadding);
  }

  void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);
    opt.text.clear();
    const QWidget* w = opt.widget;
    QStyle* style = w ? w->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, p, w);  // background and selection only

    const bool missing = index.data(kMissingRole).toBool();
    QColor text = opt.palette.color((opt.state & QStyle::State_Selected) ? QPalette::HighlightedText : QPalette::Text);
    QColor dim = dimmed(text, missing ? kMissingAlpha : kDimAlpha);
    if (missing) text = dim;

    const QRect r = opt.rect.adjusted(2 * kPadding, kPadding, -2 * kPadding, -kPadding);
    const int line = opt.fontMetrics.height();
    const QRect top(r.left(), r.top(), r.width(), line);
    const QRect bottom(r.left(), r.top() + line + kPadding, r.width(), line);

    p->save();
    const QString date = index.data(kDateRole).toString();
    p->setFont(opt.font);
    p->setPen(dim);
    p->drawText(top, Qt::AlignRight | Qt::AlignVCenter, date);
    const int date_width = date.isEmpty() ? 0 : opt.fontMetrics.horizontalAdvance(date) + 4 * kPadding;

    QFont bold = opt.font;
    bold.setBold(true);
    p->setFont(bold);
    p->setPen(text);
    p->drawText(top, Qt::AlignLeft | Qt::AlignVCenter,
                QFontMetrics(bold).elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight,
                                              top.width() - date_width));

    p->setFont(opt.font);
    p->setPen(dim);
    p->drawText(bottom, Qt::AlignLeft | Qt::AlignVCenter,
                opt.fontMetrics.elidedText(index.data(kDirRole).toString(), Qt::ElideMiddle, bottom.width()));
    p->restore();
  }

 private:
  static constexpr int kPadding = 4;
};

}  // namespace

WelcomeDialog::WelcomeDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("Welcome to Amber"));
  setMinimumSize(760, 460);
  setSizeGripEnabled(true);

  QVBoxLayout* layout = new QVBoxLayout(this);
  layout->setSpacing(12);
  layout->addWidget(create_header());

  QHBoxLayout* columns = new QHBoxLayout();
  columns->setSpacing(24);
  columns->addWidget(create_new_project_column());
  columns->addWidget(create_recent_column(), 1);
  layout->addLayout(columns, 1);

  QHBoxLayout* bottom = new QHBoxLayout();
  QCheckBox* show_at_startup = new QCheckBox(tr("Show this window at startup"), this);
  show_at_startup->setChecked(amber::CurrentConfig.show_welcome_screen);
  connect(show_at_startup, &QCheckBox::toggled, this, [](bool on) { amber::CurrentConfig.show_welcome_screen = on; });
  bottom->addWidget(show_at_startup);
  bottom->addStretch();
  QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  bottom->addWidget(buttons);
  layout->addLayout(bottom);

  // The most recent project is selected, so Enter resumes it
  if (open_button_->isEnabled()) {
    recent_list_->setFocus();
  } else {
    first_template_->setFocus();
  }
}

QWidget* WelcomeDialog::create_header() {
  QWidget* header = new QWidget(this);
  QVBoxLayout* layout = new QVBoxLayout(header);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(10);

  QHBoxLayout* title_row = new QHBoxLayout();
  title_row->setSpacing(14);

  // Same render as DemoNotice/AboutDialog, smaller
  QLabel* logo = new QLabel(header);
  int logo_size = 64;
  qreal dpr = 1.0;
  if (QScreen* screen = QGuiApplication::primaryScreen()) {
    logo_size = qMax(logo_size, qMin(screen->availableSize().width(), screen->availableSize().height()) / 16);
    dpr = screen->devicePixelRatio();
  }
  QSvgRenderer renderer(QStringLiteral(":/icons/amber-logo.svg"));
  QPixmap pix(logo_size * dpr, logo_size * dpr);
  pix.setDevicePixelRatio(dpr);
  pix.fill(Qt::transparent);
  QPainter painter(&pix);
  renderer.render(&painter, QRectF(0, 0, logo_size, logo_size));
  logo->setPixmap(pix);
  title_row->addWidget(logo);

  QVBoxLayout* title_text = new QVBoxLayout();
  title_text->setSpacing(2);
  title_text->addStretch();
  QLabel* name = new QLabel(QStringLiteral("Amber"), header);
  QFont name_font = name->font();
  name_font.setPointSizeF(name_font.pointSizeF() * 2);
  name_font.setBold(true);
  name->setFont(name_font);
  title_text->addWidget(name);
  // What the About dialog shows, without the "Amber" the title already says
  QString version = QStringLiteral(APPVERSION);
#ifdef GITHASH
  version += QStringLiteral(" | ") + QStringLiteral(GITHASH);
#endif
  QLabel* version_label = new QLabel(tr("Version %1").arg(version), header);
  version_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  QPalette version_palette = version_label->palette();
  version_palette.setColor(QPalette::WindowText, dimmed(version_palette.color(QPalette::WindowText)));
  version_label->setPalette(version_palette);
  title_text->addWidget(version_label);
  title_text->addStretch();
  title_row->addLayout(title_text);
  title_row->addStretch();
  layout->addLayout(title_row);

#ifndef QT_DEBUG
  // DemoNotice's warning, which this dialog replaces at startup. Background: the window colour tinted towards amber,
  // so it reads as a warning on both the dark and the light theme.
  QLabel* banner = new QLabel(
      tr("This is a pre-alpha build of Amber 2.0. Features are incomplete, may change without notice, or may not "
         "work at all.") +
          " " + tr("<b>For production work, use Amber 1.x (stable).</b>") + " " +
          tr("Bug reports and feedback are welcome on GitHub: %1")
              .arg("<a href=\"https://github.com/baptisterajaut/amber\">github.com/baptisterajaut/amber</a>"),
      header);
  banner->setWordWrap(true);
  banner->setOpenExternalLinks(true);
  banner->setMargin(10);
  banner->setAutoFillBackground(true);
  QPalette banner_palette = banner->palette();
  const QColor window = banner_palette.color(QPalette::Window);
  const QColor warning(255, 170, 0);
  constexpr double kTint = 0.22;
  banner_palette.setColor(QPalette::Window,
                          QColor::fromRgbF(window.redF() + (warning.redF() - window.redF()) * kTint,
                                           window.greenF() + (warning.greenF() - window.greenF()) * kTint,
                                           window.blueF() + (warning.blueF() - window.blueF()) * kTint));
  banner->setPalette(banner_palette);
  layout->addWidget(banner);
#endif

  return header;
}

QPushButton* WelcomeDialog::create_template_button(const QString& title, const QString& detail) {
  // A QPushButton has no second line: two labels laid over an empty button, clicks go through to it
  QPushButton* button = new QPushButton(this);
  button->setAccessibleName(title + ", " + detail);
  QVBoxLayout* layout = new QVBoxLayout(button);
  layout->setContentsMargins(10, 4, 10, 4);
  layout->setSpacing(0);

  QLabel* title_label = new QLabel(title, button);
  QFont bold = title_label->font();
  bold.setBold(true);
  title_label->setFont(bold);
  QLabel* detail_label = new QLabel(detail, button);
  const QColor text = palette().color(QPalette::ButtonText);
  for (QLabel* label : {title_label, detail_label}) {
    label->setAttribute(Qt::WA_TransparentForMouseEvents);
    QPalette p = label->palette();
    p.setColor(QPalette::WindowText, label == detail_label ? dimmed(text) : text);
    label->setPalette(p);
    layout->addWidget(label);
  }
  // The button's own size hint only knows its (empty) text, and its Fixed vertical policy ignores the layout's
  button->setMinimumSize(layout->sizeHint());
  return button;
}

QWidget* WelcomeDialog::create_new_project_column() {
  QWidget* column = new QWidget(this);
  QVBoxLayout* layout = new QVBoxLayout(column);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(6);

  QLabel* heading = new QLabel(tr("New Project"), column);
  QFont heading_font = heading->font();
  heading_font.setBold(true);
  heading_font.setPointSizeF(heading_font.pointSizeF() * 1.2);
  heading->setFont(heading_font);
  layout->addWidget(heading);

  for (const SequenceTemplate& t : kTemplates) {
    const QString detail = tr("%1 × %2 · %3 fps").arg(t.width).arg(t.height).arg(QLocale().toString(t.frame_rate));
    QPushButton* button = create_template_button(tr(t.name), detail);
    if (first_template_ == nullptr) first_template_ = button;
    connect(button, &QPushButton::clicked, this, [this, t]() {
      if (!amber::Global->start_new_project()) return;
      NewSequenceDialog::create_sequence(panel_project->get_next_sequence_name(), t.width, t.height, t.frame_rate,
                                         kTemplateAudioFrequency);
      accept();
    });
    layout->addWidget(button);
  }

  QPushButton* custom = create_template_button(tr("Custom…"), tr("Choose size, frame rate and audio"));
  connect(custom, &QPushButton::clicked, this, [this]() {
    if (!amber::Global->start_new_project()) return;
    accept();
    // Once this dialog is gone: File > New > Sequence, its own exec() included
    QTimer::singleShot(0, panel_project, &Project::new_sequence);
  });
  layout->addWidget(custom);

  QPushButton* empty = new QPushButton(tr("Empty Project"), column);
  empty->setFlat(true);
  empty->setCursor(Qt::PointingHandCursor);
  QPalette empty_palette = empty->palette();
  empty_palette.setColor(QPalette::ButtonText, empty_palette.color(QPalette::Link));
  empty->setPalette(empty_palette);
  connect(empty, &QPushButton::clicked, this, [this]() {
    if (amber::Global->start_new_project()) accept();
  });
  layout->addWidget(empty, 0, Qt::AlignLeft);

  layout->addStretch();
  return column;
}

QWidget* WelcomeDialog::create_recent_column() {
  QWidget* column = new QWidget(this);
  QVBoxLayout* layout = new QVBoxLayout(column);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(6);

  QLabel* heading = new QLabel(tr("Recent Projects"), column);
  QFont heading_font = heading->font();
  heading_font.setBold(true);
  heading_font.setPointSizeF(heading_font.pointSizeF() * 1.2);
  heading->setFont(heading_font);
  layout->addWidget(heading);

  recent_list_ = new QListWidget(column);
  recent_list_->setItemDelegate(new RecentProjectDelegate(recent_list_));
  recent_list_->setUniformItemSizes(true);
  QListWidgetItem* first_openable = nullptr;
  for (const QString& path : amber::project_io->recentProjects()) {
    const QFileInfo info(path);
    QListWidgetItem* item = new QListWidgetItem(info.fileName(), recent_list_);
    item->setData(kPathRole, path);
    item->setData(kDirRole, QDir::toNativeSeparators(info.absolutePath()));
    if (info.exists()) {
      item->setData(kDateRole, QLocale().toString(info.lastModified(), QLocale::ShortFormat));
      item->setToolTip(QDir::toNativeSeparators(info.absoluteFilePath()));
      if (first_openable == nullptr) first_openable = item;
    } else {
      // Not openable: File > Open Recent would only offer to drop it from the list
      item->setText(tr("%1 (missing)").arg(info.fileName()));
      item->setData(kMissingRole, true);
      item->setFlags(Qt::NoItemFlags);
      item->setToolTip(tr("File not found"));
    }
  }
  connect(recent_list_, &QListWidget::itemSelectionChanged, this, &WelcomeDialog::update_open_button);
  connect(recent_list_, &QListWidget::itemDoubleClicked, this, &WelcomeDialog::open_selected_recent);
  layout->addWidget(recent_list_, 1);

  QLabel* none = new QLabel(tr("No recent projects"), column);
  none->setAlignment(Qt::AlignCenter);
  QPalette none_palette = none->palette();
  none_palette.setColor(QPalette::WindowText, dimmed(none_palette.color(QPalette::WindowText)));
  none->setPalette(none_palette);
  layout->addWidget(none, 1);
  recent_list_->setVisible(recent_list_->count() > 0);
  none->setVisible(recent_list_->count() == 0);

  QHBoxLayout* buttons = new QHBoxLayout();
  buttons->addStretch();
  QPushButton* open_other = new QPushButton(tr("Open Other…"), column);
  connect(open_other, &QPushButton::clicked, this, [this]() {
    if (amber::Global->OpenProject()) accept();
  });
  buttons->addWidget(open_other);
  // Enter from the list lands here (QListWidget passes Enter on to the dialog)
  open_button_ = new QPushButton(tr("Open"), column);
  open_button_->setDefault(true);
  connect(open_button_, &QPushButton::clicked, this, &WelcomeDialog::open_selected_recent);
  buttons->addWidget(open_button_);
  layout->addLayout(buttons);

  if (first_openable != nullptr) recent_list_->setCurrentItem(first_openable);
  update_open_button();
  return column;
}

void WelcomeDialog::update_open_button() {
  const QList<QListWidgetItem*> selected = recent_list_->selectedItems();
  open_button_->setEnabled(!selected.isEmpty() && (selected.first()->flags() & Qt::ItemIsEnabled));
}

void WelcomeDialog::open_selected_recent() {
  const QList<QListWidgetItem*> selected = recent_list_->selectedItems();
  if (selected.isEmpty() || !(selected.first()->flags() & Qt::ItemIsEnabled)) return;
  // By index, as File > Open Recent does: same missing-file handling, save prompt and recents reordering
  const int index = amber::project_io->recentProjects().indexOf(selected.first()->data(kPathRole).toString());
  if (index >= 0 && amber::Global->open_recent(index)) accept();
}
