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

#include "grapheditor.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QVariant>
#include <climits>

#include "effects/effect.h"
#include "effects/effectfields.h"
#include "effects/effectrow.h"
#include "effects/ui/effectfieldwidget.h"
#include "engine/clip.h"
#include "engine/sequence.h"
#include "global/debug.h"
#include "panels.h"
#include "rendering/renderfunctions.h"
#include "ui/graphview.h"
#include "ui/icons.h"
#include "ui/keyframenavigator.h"
#include "ui/labelslider.h"
#include "ui/timelineheader.h"
#include "ui/timelinetools.h"

GraphEditor::GraphEditor(QWidget* parent) : Panel(parent) {
  resize(720, 480);

  QWidget* main_widget = new QWidget(this);
  QVBoxLayout* layout = new QVBoxLayout(main_widget);
  setWidget(main_widget);

  QWidget* tool_widget = new QWidget();
  tool_widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
  QHBoxLayout* tools = new QHBoxLayout(tool_widget);

  QWidget* left_tool_widget = new QWidget();
  QHBoxLayout* left_tool_layout = new QHBoxLayout(left_tool_widget);
  left_tool_layout->setSpacing(0);
  left_tool_layout->setContentsMargins(0, 0, 0, 0);
  tools->addWidget(left_tool_widget);
  QWidget* center_tool_widget = new QWidget();
  QHBoxLayout* center_tool_layout = new QHBoxLayout(center_tool_widget);
  center_tool_layout->setSpacing(0);
  center_tool_layout->setContentsMargins(0, 0, 0, 0);
  tools->addWidget(center_tool_widget);
  QWidget* right_tool_widget = new QWidget();
  QHBoxLayout* right_tool_layout = new QHBoxLayout(right_tool_widget);
  right_tool_layout->setSpacing(0);
  right_tool_layout->setContentsMargins(0, 0, 0, 0);
  tools->addWidget(right_tool_widget);

  keyframe_nav = new KeyframeNavigator(nullptr, false);
  keyframe_nav->enable_keyframes(true);
  keyframe_nav->enable_keyframe_toggle(false);
  left_tool_layout->addWidget(keyframe_nav);
  left_tool_layout->addStretch();

  linear_button = new QPushButton();
  linear_button->setProperty("type", EFFECT_KEYFRAME_LINEAR);
  linear_button->setCheckable(true);
  bezier_button = new QPushButton();
  bezier_button->setProperty("type", EFFECT_KEYFRAME_BEZIER);
  bezier_button->setCheckable(true);
  hold_button = new QPushButton();
  hold_button->setProperty("type", EFFECT_KEYFRAME_HOLD);
  hold_button->setCheckable(true);

  center_tool_layout->addStretch();
  center_tool_layout->addWidget(linear_button);
  center_tool_layout->addWidget(bezier_button);
  center_tool_layout->addWidget(hold_button);

  // Value and time of the selected key(s), typed or dragged
  time_label_ = new QLabel();
  time_slider_ = new LabelSlider();
  time_slider_->SetDisplayType(LabelSlider::FrameNumber);
  value_label_ = new QLabel();
  value_slider_ = new LabelSlider();
  right_tool_layout->addStretch();
  right_tool_layout->addWidget(time_label_);
  right_tool_layout->addSpacing(4);
  right_tool_layout->addWidget(time_slider_);
  right_tool_layout->addSpacing(12);
  right_tool_layout->addWidget(value_label_);
  right_tool_layout->addSpacing(4);
  right_tool_layout->addWidget(value_slider_);

  layout->addWidget(tool_widget);

  QWidget* central_widget = new QWidget();
  QVBoxLayout* central_layout = new QVBoxLayout(central_widget);
  central_layout->setSpacing(0);
  central_layout->setContentsMargins(0, 0, 0, 0);
  header = new TimelineHeader();
  header->viewer = panel_sequence_viewer;
  central_layout->addWidget(header);
  view = new GraphView();
  central_layout->addWidget(view);

  layout->addWidget(central_widget);

  QWidget* value_widget = new QWidget();
  value_widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
  QHBoxLayout* values = new QHBoxLayout(value_widget);
  values->addStretch();

  QWidget* central_value_widget = new QWidget();
  value_layout = new QHBoxLayout(central_value_widget);
  value_layout->setContentsMargins(0, 0, 0, 0);
  value_layout->addWidget(new QLabel(""));  // a spacer so the layout doesn't jump
  values->addWidget(central_value_widget);

  values->addStretch();
  layout->addWidget(value_widget);

  current_row_desc = new QLabel();
  current_row_desc->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
  current_row_desc->setAlignment(Qt::AlignCenter);
  layout->addWidget(current_row_desc);

  connect(view, &GraphView::zoom_changed, header, [this](double z, double) { header->update_zoom(z); });
  connect(view, &GraphView::x_scroll_changed, header, &TimelineHeader::set_scroll);
  connect(view, &GraphView::selection_changed, this, &GraphEditor::set_key_button_enabled);
  connect(view, &GraphView::selection_changed, this, [this]() { refresh_key_entry(); });
  // The slider is still dragging until its last valueChanged: that one is the undo step
  connect(time_slider_, &LabelSlider::valueChanged, this,
          [this](double d) { view->set_selected_time(qRound64(d), !time_slider_->IsDragging()); });
  connect(value_slider_, &LabelSlider::valueChanged, this,
          [this](double d) { view->set_selected_value(d, !value_slider_->IsDragging()); });

  connect(linear_button, &QPushButton::clicked, this, &GraphEditor::set_keyframe_type);
  connect(bezier_button, &QPushButton::clicked, this, &GraphEditor::set_keyframe_type);
  connect(hold_button, &QPushButton::clicked, this, &GraphEditor::set_keyframe_type);

  Retranslate();
  refresh_key_entry();
}

EffectRow* GraphEditor::get_row() { return row; }

void GraphEditor::Retranslate() {
  setWindowTitle(tr("Graph Editor"));
  linear_button->setText(tr("Linear"));
  bezier_button->setText(tr("Bezier"));
  hold_button->setText(tr("Hold"));
  time_label_->setText(tr("Time:"));
  value_label_->setText(tr("Value:"));
}

void GraphEditor::update_panel() {
  if (isVisible()) {
    if (row != nullptr) {
      // Refresh the clip-relative offset every tick so that ripple/timeline edits
      // moving the parent clip while the same row stays selected keep the Graph
      // Editor playhead aligned with the main timeline playhead.
      const long visible_in = row->GetParentEffect()->parent_clip->timeline_in();
      header->set_visible_in(visible_in);
      view->set_visible_in(visible_in);

      int slider_index = 0;
      for (int i = 0; i < row->FieldCount(); i++) {
        EffectField* field = row->Field(i);
        if (field->type() == EffectField::EFFECT_FIELD_DOUBLE) {
          DoubleField* df = static_cast<DoubleField*>(field);
          field_sliders_.at(slider_index)->SetValue(df->GetDoubleAt(df->Now()));
          slider_index++;
        }
      }
    }
    refresh_key_entry();

    header->update();
    view->update();
  }
}

void GraphEditor::repaint_only() {
  if (!isVisible()) return;
  if (row != nullptr) {
    // Keep the clip-relative offset in sync (cheap: one long copy each).
    const long visible_in = row->GetParentEffect()->parent_clip->timeline_in();
    header->set_visible_in(visible_in);
    view->set_visible_in(visible_in);
  }
  header->update();
  view->update();
}

void GraphEditor::set_row(EffectRow* r) {
  if (r == row) return;

  for (int i = 0; i < field_sliders_.size(); i++) {
    delete field_sliders_.at(i);
    delete field_enable_buttons.at(i);
  }
  field_sliders_.clear();
  field_enable_buttons.clear();

  if (row != nullptr) {
    // clear old row connections
    disconnect(keyframe_nav, &KeyframeNavigator::goto_previous_key, row, &EffectRow::GoToPreviousKeyframe);
    disconnect(keyframe_nav, &KeyframeNavigator::toggle_key, row, &EffectRow::ToggleKeyframe);
    disconnect(keyframe_nav, &KeyframeNavigator::goto_next_key, row, &EffectRow::GoToNextKeyframe);
  }

  bool found_vals = false;

  if (r != nullptr && r->IsKeyframing()) {
    for (int i = 0; i < r->FieldCount(); i++) {
      EffectField* field = r->Field(i);
      if (field->type() == EffectField::EFFECT_FIELD_DOUBLE) {
        QPushButton* slider_button = new QPushButton();
        slider_button->setCheckable(true);
        slider_button->setChecked(field->IsEnabled());
        slider_button->setIcon(amber::icon::CreateIconFromSVG(":/icons/record.svg", false));
        slider_button->setProperty("field", i);
        slider_button->setIconSize(slider_button->iconSize() * 0.5);
        connect(slider_button, &QPushButton::toggled, this, &GraphEditor::set_field_visibility);
        field_enable_buttons.append(slider_button);
        value_layout->addWidget(slider_button);

        EffectFieldWidget* fw = EffectFieldWidget::Create(field, this);
        LabelSlider* slider = static_cast<LabelSlider*>(fw->CreateWidget());
        slider->SetColor(get_curve_color(i, r->FieldCount()).name());
        field_sliders_.append(slider);
        value_layout->addWidget(slider);

        found_vals = true;
      }
    }
  }

  if (found_vals) {
    row = r;
    current_row_desc->setText(row->GetParentEffect()->parent_clip->name() +
                              " :: " + row->GetParentEffect()->meta->name + " :: " + row->name());
    const long visible_in = r->GetParentEffect()->parent_clip->timeline_in();
    header->set_visible_in(visible_in);
    // view->set_row() below also updates its own cached visible_in, but set it
    // here too so both header and view stay consistent regardless of order.
    view->set_visible_in(visible_in);

    connect(keyframe_nav, &KeyframeNavigator::goto_previous_key, row, &EffectRow::GoToPreviousKeyframe);
    connect(keyframe_nav, &KeyframeNavigator::toggle_key, row, &EffectRow::ToggleKeyframe);
    connect(keyframe_nav, &KeyframeNavigator::goto_next_key, row, &EffectRow::GoToNextKeyframe);
  } else {
    row = nullptr;
    current_row_desc->setText(nullptr);
    // Clear stale offsets so the header ruler and (defensive) the view do not
    // keep the previously selected clip's timeline_in cached.
    header->set_visible_in(0);
    view->set_visible_in(0);
  }
  view->set_row(row);
  update_panel();
}

bool GraphEditor::view_is_focused() { return view->hasFocus() || header->hasFocus(); }

bool GraphEditor::view_is_under_mouse() { return view->underMouse() || header->underMouse(); }

void GraphEditor::delete_selected_keys() { view->delete_selected_keys(); }

void GraphEditor::select_all() { view->select_all(); }

void GraphEditor::set_key_button_enabled(bool e, int type) {
  linear_button->setEnabled(e);
  linear_button->setChecked(type == EFFECT_KEYFRAME_LINEAR);
  bezier_button->setEnabled(e);
  bezier_button->setChecked(type == EFFECT_KEYFRAME_BEZIER);
  hold_button->setEnabled(e);
  hold_button->setChecked(type == EFFECT_KEYFRAME_HOLD);
}

void GraphEditor::refresh_key_entry() {
  EffectField* field = nullptr;
  long time = 0;
  double value = 0;
  const int count = view->selected_key_info(&field, &time, &value);

  time_slider_->setEnabled(count == 1);
  value_slider_->setEnabled(count > 0);

  if (count == 1) {
    time_slider_->SetFrameRate(row->GetParentEffect()->parent_clip->sequence->frame_rate);
    time_slider_->SetValue(time);
  } else {
    time_slider_->SetValue(qSNaN());
    time_slider_->setText(QString());
  }

  if (count > 0 && field->type() == EffectField::EFFECT_FIELD_DOUBLE) {
    // Shown like the field's own slider (percent, dB...); several keys show the first one's value
    DoubleField* df = static_cast<DoubleField*>(field);
    const auto type = static_cast<amber::DisplayType>(df->GetDisplayType());
    value_slider_->SetMinimum(qIsNaN(df->GetMinimum()) ? INT_MIN : df->GetMinimum());
    value_slider_->SetMaximum(qIsNaN(df->GetMaximum()) ? INT_MAX : df->GetMaximum());
    value_slider_->SetFrameRate(df->GetFrameRate());
    value_slider_->SetDecimalPlaces(type == LabelSlider::Normal ? 3 : 1);
    value_slider_->SetDisplayType(type);
    value_slider_->SetValue(value);
  } else {
    value_slider_->SetValue(qSNaN());
    value_slider_->setText(QString());
  }
}

void GraphEditor::set_keyframe_type() {
  linear_button->setChecked(linear_button == sender());
  bezier_button->setChecked(bezier_button == sender());
  hold_button->setChecked(hold_button == sender());
  view->set_selected_keyframe_type(sender()->property("type").toInt());
}

void GraphEditor::set_field_visibility(bool b) { view->set_field_visibility(sender()->property("field").toInt(), b); }
