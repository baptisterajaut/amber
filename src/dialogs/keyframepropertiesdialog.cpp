#include "keyframepropertiesdialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QtMath>

#include "effects/effect.h"
#include "effects/effectfield.h"
#include "engine/undo/undo.h"
#include "engine/undo/undostack.h"
#include "global/config.h"
#include "panels/panels.h"
#include "ui/labelslider.h"

KeyframePropertiesDialog::KeyframePropertiesDialog(QWidget* parent,
                                                   const QVector<EffectField*>& fields,
                                                   const QVector<int>& keyframe_indices,
                                                   double frame_rate)
    : QDialog(parent), fields_(fields), keyframe_indices_(keyframe_indices) {
  setWindowTitle((fields.size() == 1) ? tr("Keyframe Properties") : tr("Multiple Keyframe Properties"));

  // Capture original values for undo
  originals_.resize(fields_.size());
  for (int i = 0; i < fields_.size(); i++) {
    const EffectKeyframe& kf = fields_[i]->keyframes.at(keyframe_indices_[i]);
    originals_[i].time = kf.time;
    originals_[i].type = kf.type;
    originals_[i].pre_handle_x = kf.pre_handle_x;
    originals_[i].pre_handle_y = kf.pre_handle_y;
    originals_[i].post_handle_x = kf.post_handle_x;
    originals_[i].post_handle_y = kf.post_handle_y;
  }

  QGridLayout* layout = new QGridLayout(this);
  int row = 0;

  // Time field
  layout->addWidget(new QLabel(tr("Time:")), row, 0);
  time_slider_ = new LabelSlider(this);
  time_slider_->SetDisplayType(LabelSlider::FrameNumber);
  time_slider_->SetFrameRate(frame_rate);
  time_slider_->SetMinimum(0);

  bool all_same_time = true;
  for (int i = 1; i < fields_.size(); i++) {
    if (originals_[i].time != originals_[0].time) {
      all_same_time = false;
      break;
    }
  }
  if (all_same_time) {
    time_slider_->SetDefault(originals_[0].time);
    time_slider_->SetValue(originals_[0].time);
  } else {
    time_slider_->SetDefault(qSNaN());
    time_slider_->SetValue(qSNaN());
  }
  connect(time_slider_, &LabelSlider::valueChanged, this, [this]() { time_modified_ = true; });
  layout->addWidget(time_slider_, row, 1, 1, 3);
  row++;

  // Type field
  layout->addWidget(new QLabel(tr("Type:")), row, 0);
  type_combo_ = new QComboBox(this);
  type_combo_->addItem(tr("Linear"), EFFECT_KEYFRAME_LINEAR);
  type_combo_->addItem(tr("Bezier"), EFFECT_KEYFRAME_BEZIER);
  type_combo_->addItem(tr("Hold"), EFFECT_KEYFRAME_HOLD);

  bool all_same_type = true;
  for (int i = 1; i < fields_.size(); i++) {
    if (originals_[i].type != originals_[0].type) {
      all_same_type = false;
      break;
    }
  }
  if (all_same_type) {
    int idx = type_combo_->findData(originals_[0].type);
    if (idx >= 0) type_combo_->setCurrentIndex(idx);
  } else {
    type_combo_->insertItem(0, tr("(multiple)"), -1);
    type_combo_->setCurrentIndex(0);
  }
  connect(type_combo_, &QComboBox::currentIndexChanged, this, [this]() {
    type_modified_ = true;
    UpdateBezierEnabled();
  });
  layout->addWidget(type_combo_, row, 1, 1, 3);
  row++;

  // Bezier handles
  layout->addWidget(new QLabel(tr("Bezier In Handle:")), row, 0);

  pre_handle_x_ = new LabelSlider(this);
  pre_handle_x_->SetDecimalPlaces(2);
  pre_handle_y_ = new LabelSlider(this);
  pre_handle_y_->SetDecimalPlaces(2);

  bool all_same_pre_x = true;
  bool all_same_pre_y = true;
  for (int i = 1; i < fields_.size(); i++) {
    if (!qFuzzyCompare(originals_[i].pre_handle_x, originals_[0].pre_handle_x)) all_same_pre_x = false;
    if (!qFuzzyCompare(originals_[i].pre_handle_y, originals_[0].pre_handle_y)) all_same_pre_y = false;
  }

  if (all_same_pre_x) {
    pre_handle_x_->SetDefault(originals_[0].pre_handle_x);
    pre_handle_x_->SetValue(originals_[0].pre_handle_x);
  } else {
    pre_handle_x_->SetDefault(qSNaN());
    pre_handle_x_->SetValue(qSNaN());
  }
  if (all_same_pre_y) {
    pre_handle_y_->SetDefault(originals_[0].pre_handle_y);
    pre_handle_y_->SetValue(originals_[0].pre_handle_y);
  } else {
    pre_handle_y_->SetDefault(qSNaN());
    pre_handle_y_->SetValue(qSNaN());
  }

  connect(pre_handle_x_, &LabelSlider::valueChanged, this, [this]() { pre_handle_x_modified_ = true; });
  connect(pre_handle_y_, &LabelSlider::valueChanged, this, [this]() { pre_handle_y_modified_ = true; });

  layout->addWidget(new QLabel(tr("X:")), row, 1);
  layout->addWidget(pre_handle_x_, row, 2);
  layout->addWidget(new QLabel(tr("Y:")), row, 3);
  layout->addWidget(pre_handle_y_, row, 4);
  row++;

  layout->addWidget(new QLabel(tr("Bezier Out Handle:")), row, 0);

  post_handle_x_ = new LabelSlider(this);
  post_handle_x_->SetDecimalPlaces(2);
  post_handle_y_ = new LabelSlider(this);
  post_handle_y_->SetDecimalPlaces(2);

  bool all_same_post_x = true;
  bool all_same_post_y = true;
  for (int i = 1; i < fields_.size(); i++) {
    if (!qFuzzyCompare(originals_[i].post_handle_x, originals_[0].post_handle_x)) all_same_post_x = false;
    if (!qFuzzyCompare(originals_[i].post_handle_y, originals_[0].post_handle_y)) all_same_post_y = false;
  }

  if (all_same_post_x) {
    post_handle_x_->SetDefault(originals_[0].post_handle_x);
    post_handle_x_->SetValue(originals_[0].post_handle_x);
  } else {
    post_handle_x_->SetDefault(qSNaN());
    post_handle_x_->SetValue(qSNaN());
  }
  if (all_same_post_y) {
    post_handle_y_->SetDefault(originals_[0].post_handle_y);
    post_handle_y_->SetValue(originals_[0].post_handle_y);
  } else {
    post_handle_y_->SetDefault(qSNaN());
    post_handle_y_->SetValue(qSNaN());
  }

  connect(post_handle_x_, &LabelSlider::valueChanged, this, [this]() { post_handle_x_modified_ = true; });
  connect(post_handle_y_, &LabelSlider::valueChanged, this, [this]() { post_handle_y_modified_ = true; });

  layout->addWidget(new QLabel(tr("X:")), row, 1);
  layout->addWidget(post_handle_x_, row, 2);
  layout->addWidget(new QLabel(tr("Y:")), row, 3);
  layout->addWidget(post_handle_y_, row, 4);
  row++;

  // Dialog buttons
  QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  buttons->setCenterButtons(true);
  layout->addWidget(buttons, row, 0, 1, 5);

  connect(buttons, &QDialogButtonBox::accepted, this, &KeyframePropertiesDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  // Set initial bezier handle enable state
  UpdateBezierEnabled();
}

void KeyframePropertiesDialog::UpdateBezierEnabled() {
  int current_type = type_combo_->currentData().toInt();
  bool is_bezier = (current_type == EFFECT_KEYFRAME_BEZIER);

  // If type is "(multiple)" (data == -1), check if any selected keyframe is bezier
  if (current_type == -1) {
    is_bezier = false;
    for (int i = 0; i < originals_.size(); i++) {
      if (originals_[i].type == EFFECT_KEYFRAME_BEZIER) {
        is_bezier = true;
        break;
      }
    }
  }

  pre_handle_x_->setEnabled(is_bezier);
  pre_handle_y_->setEnabled(is_bezier);
  post_handle_x_->setEnabled(is_bezier);
  post_handle_y_->setEnabled(is_bezier);
}

void KeyframePropertiesDialog::accept() {
  ComboAction* ca = new ComboAction(tr("Edit Keyframe"));
  bool any_change = false;

  // Track the new type for sticky update (use the first change we find)
  int new_type_for_sticky = -1;

  for (int i = 0; i < fields_.size(); i++) {
    EffectField* f = fields_[i];
    int ki = keyframe_indices_[i];
    EffectKeyframe& kf = f->keyframes[ki];

    // Time
    if (time_modified_ && !qIsNaN(time_slider_->value())) {
      long new_time = qRound(time_slider_->value());
      if (new_time != kf.time) {
        ca->append(new SetLong(&kf.time, originals_[i].time, new_time));
        any_change = true;
      }
    }

    // Type
    if (type_modified_) {
      int new_type = type_combo_->currentData().toInt();
      if (new_type >= 0 && new_type != kf.type) {
        ca->append(new SetInt(&kf.type, new_type));
        any_change = true;
        if (new_type_for_sticky < 0) {
          new_type_for_sticky = new_type;
        }
      }
    }

    // Bezier handles (pre)
    if (pre_handle_x_modified_ && !qIsNaN(pre_handle_x_->value())) {
      double new_val = pre_handle_x_->value();
      if (!qFuzzyCompare(new_val, kf.pre_handle_x)) {
        ca->append(new SetDouble(&kf.pre_handle_x, originals_[i].pre_handle_x, new_val));
        any_change = true;
      }
    }
    if (pre_handle_y_modified_ && !qIsNaN(pre_handle_y_->value())) {
      double new_val = pre_handle_y_->value();
      if (!qFuzzyCompare(new_val, kf.pre_handle_y)) {
        ca->append(new SetDouble(&kf.pre_handle_y, originals_[i].pre_handle_y, new_val));
        any_change = true;
      }
    }

    // Bezier handles (post)
    if (post_handle_x_modified_ && !qIsNaN(post_handle_x_->value())) {
      double new_val = post_handle_x_->value();
      if (!qFuzzyCompare(new_val, kf.post_handle_x)) {
        ca->append(new SetDouble(&kf.post_handle_x, originals_[i].post_handle_x, new_val));
        any_change = true;
      }
    }
    if (post_handle_y_modified_ && !qIsNaN(post_handle_y_->value())) {
      double new_val = post_handle_y_->value();
      if (!qFuzzyCompare(new_val, kf.post_handle_y)) {
        ca->append(new SetDouble(&kf.post_handle_y, originals_[i].post_handle_y, new_val));
        any_change = true;
      }
    }
  }

  if (any_change) {
    amber::UndoStack.push(ca);
    update_ui(false);
  } else {
    delete ca;
  }

  // Sticky keyframe type: update config default when type changes
  if (new_type_for_sticky >= 0 && amber::CurrentConfig.sticky_keyframe_type) {
    amber::CurrentConfig.default_keyframe_type = new_type_for_sticky;
  }

  QDialog::accept();
}
