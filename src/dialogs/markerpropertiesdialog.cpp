#include "markerpropertiesdialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QIcon>
#include <QLabel>
#include <QPixmap>

#include "engine/undo/undo.h"
#include "engine/undo/undostack.h"
#include "panels/panels.h"
#include "ui/colorlabel.h"

MarkerPropertiesDialog::MarkerPropertiesDialog(QWidget* parent, QVector<Marker>* array, const QVector<int>& indices,
                                               double frame_rate)
    : QDialog(parent), array_(array), indices_(indices) {
  // Pointers are only read while the modal dialog is open; undo addresses markers by index (see accept())
  for (int idx : indices_) markers_.append(&(*array_)[idx]);

  setWindowTitle((markers_.size() == 1) ? tr("\"%1\" Marker Properties").arg(markers_.at(0)->name.isEmpty()
                                                                                ? tr("Untitled")
                                                                                : markers_.at(0)->name)
                                        : tr("Multiple Marker Properties"));

  QGridLayout* layout = new QGridLayout(this);
  int row = 0;

  // Name field
  layout->addWidget(new QLabel(tr("Name:")), row, 0);
  name_edit_ = new QLineEdit(this);

  bool all_same_name = true;
  for (int i = 1; i < markers_.size(); i++) {
    if (markers_[i]->name != markers_[0]->name) {
      all_same_name = false;
      break;
    }
  }
  if (all_same_name) {
    name_edit_->setText(markers_[0]->name);
  } else {
    name_edit_->setPlaceholderText(tr("(multiple)"));
  }
  connect(name_edit_, &QLineEdit::textEdited, this, [this]() { name_modified_ = true; });
  layout->addWidget(name_edit_, row, 1);
  row++;

  // Frame field
  layout->addWidget(new QLabel(tr("Frame:")), row, 0);
  frame_slider_ = new LabelSlider(this);
  frame_slider_->SetDisplayType(LabelSlider::FrameNumber);
  frame_slider_->SetFrameRate(frame_rate);
  frame_slider_->SetMinimum(0);

  bool all_same_frame = true;
  for (int i = 1; i < markers_.size(); i++) {
    if (markers_[i]->frame != markers_[0]->frame) {
      all_same_frame = false;
      break;
    }
  }
  if (all_same_frame) {
    frame_slider_->SetDefault(markers_[0]->frame);
    frame_slider_->SetValue(markers_[0]->frame);
  } else {
    frame_slider_->SetDefault(qSNaN());
    frame_slider_->SetValue(qSNaN());
  }
  connect(frame_slider_, &LabelSlider::valueChanged, this, [this]() { frame_modified_ = true; });
  layout->addWidget(frame_slider_, row, 1);
  row++;

  // Duration field (0 = point marker)
  layout->addWidget(new QLabel(tr("Duration:")), row, 0);
  duration_slider_ = new LabelSlider(this);
  duration_slider_->SetDisplayType(LabelSlider::FrameNumber);
  duration_slider_->SetFrameRate(frame_rate);
  duration_slider_->SetMinimum(0);

  bool all_same_duration = true;
  for (int i = 1; i < markers_.size(); i++) {
    if (markers_[i]->duration != markers_[0]->duration) {
      all_same_duration = false;
      break;
    }
  }
  if (all_same_duration) {
    duration_slider_->SetDefault(markers_[0]->duration);
    duration_slider_->SetValue(markers_[0]->duration);
  } else {
    duration_slider_->SetDefault(qSNaN());
    duration_slider_->SetValue(qSNaN());
  }
  connect(duration_slider_, &LabelSlider::valueChanged, this, [this]() { duration_modified_ = true; });
  layout->addWidget(duration_slider_, row, 1);
  row++;

  // Color Label field
  layout->addWidget(new QLabel(tr("Color:")), row, 0);
  color_combo_ = new QComboBox(this);

  // Add "None" entry
  color_combo_->addItem(tr("None"), 0);

  // Add color swatch entries
  for (int i = 1; i <= amber::kColorLabelCount; i++) {
    QPixmap px(16, 16);
    px.fill(amber::GetColorLabel(i));
    color_combo_->addItem(QIcon(px), amber::GetColorLabelName(i), i);
  }

  bool all_same_color = true;
  for (int i = 1; i < markers_.size(); i++) {
    if (markers_[i]->color_label != markers_[0]->color_label) {
      all_same_color = false;
      break;
    }
  }
  if (all_same_color) {
    // Find the combo index for this color_label value
    int idx = color_combo_->findData(markers_[0]->color_label);
    if (idx >= 0) color_combo_->setCurrentIndex(idx);
  } else {
    // Insert a "(multiple)" placeholder at position 0 and select it
    color_combo_->insertItem(0, tr("(multiple)"), -1);
    color_combo_->setCurrentIndex(0);
  }
  connect(color_combo_, &QComboBox::currentIndexChanged, this, [this]() { color_modified_ = true; });
  layout->addWidget(color_combo_, row, 1);
  row++;

  // Dialog buttons
  QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  buttons->setCenterButtons(true);
  layout->addWidget(buttons, row, 0, 1, 2);

  connect(buttons, &QDialogButtonBox::accepted, this, &MarkerPropertiesDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void MarkerPropertiesDialog::accept() {
  ComboAction* ca = new ComboAction(tr("Edit Marker"));
  bool any_change = false;

  for (int i = 0; i < markers_.size(); i++) {
    const Marker& m = *markers_[i];
    Marker edited = m;

    // Name
    if (name_modified_ && !name_edit_->text().isEmpty()) edited.name = name_edit_->text();

    // Frame
    if (frame_modified_ && !qIsNaN(frame_slider_->value())) edited.frame = qRound(frame_slider_->value());

    // Duration
    if (duration_modified_ && !qIsNaN(duration_slider_->value())) {
      edited.duration = qMax(0L, long(qRound(duration_slider_->value())));
    }

    // Color label
    if (color_modified_) {
      int new_label = color_combo_->currentData().toInt();
      if (new_label >= 0) edited.color_label = new_label;
    }

    if (edited.name != m.name || edited.frame != m.frame || edited.duration != m.duration ||
        edited.color_label != m.color_label) {
      // Index, not &m: adding a marker later can reallocate the array before this is undone
      ca->append(new EditMarkerAction(array_, indices_[i], m, edited));
      any_change = true;
    }
  }

  if (any_change) {
    amber::UndoStack.push(ca);
    update_ui(false);
  } else {
    delete ca;
  }

  QDialog::accept();
}
