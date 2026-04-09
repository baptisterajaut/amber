#include "effectui.h"

#include <QPoint>
#include <QtMath>

#include "effects/ui/effectfieldwidget.h"

#include "engine/clip.h"
#include "ui/menuhelper.h"
#include "ui/keyframenavigator.h"
#include "ui/clickablelabel.h"
#include "ui/icons.h"
#include "ui/menu.h"
#include "panels/panels.h"
#include "engine/undo/undo_effect.h"
#include "global/global.h"

EffectUI::EffectUI(Effect* e) :
  effect_(e),
  effect_reset_button_(nullptr)
{
  Q_ASSERT(e != nullptr);

  QString effect_name;

  // If this effect is actually a transition
  if (e->meta->type == EFFECT_TYPE_TRANSITION) {

    Transition* t = static_cast<Transition*>(e);

    // Since effects can have two clip attachments, find out which one is selected
    Clip* selected_clip = t->parent_clip;
    bool both_selected = false;

    // Check if this is a shared transition
    if (t->secondary_clip != nullptr) {

      // Check which clips are selected
      if (t->secondary_clip->IsSelected()) {

        selected_clip = t->secondary_clip;

        if (t->parent_clip->IsSelected()) {
          // Both clips are selected
          both_selected = true;
        }

      } else if (!t->parent_clip->IsSelected()) {

        // Neither are selected, but the naming scheme (no "opening" or "closing" modifier) will be the same
        both_selected = true;

      }

    }

    // See if the transition is the clip's opening or closing transition and label it accordingly
    if (both_selected) {
      effect_name = t->name;
    } else if (selected_clip->opening_transition.get() == t) {
      effect_name = tr("%1 (Opening)").arg(t->name);
    } else {
      effect_name = tr("%1 (Closing)").arg(t->name);
    }

  } else {

    // Otherwise just set the title normally
    effect_name = e->name;

  }

  SetTitle(effect_name);

  QWidget* ui = new QWidget(this);
  ui->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
  SetContents(ui);

  SetExpanded(e->IsExpanded());
  connect(this, &EffectUI::visibleChanged, e, &Effect::SetExpanded);

  layout_ = new QGridLayout(ui);
  layout_->setSpacing(4);

  connect(title_bar,
          &QWidget::customContextMenuRequested,
          this,
          &EffectUI::show_context_menu);

  // Effect-level reset button in the title bar
  if (e->meta->type == EFFECT_TYPE_EFFECT) {
    QHBoxLayout* tbl = static_cast<QHBoxLayout*>(title_bar->layout());
    effect_reset_button_ = new QPushButton(title_bar);
    effect_reset_button_->setIcon(amber::icon::CreateIconFromSVG(":/icons/undo.svg", false));
    effect_reset_button_->setIconSize(QSize(10, 10));
    effect_reset_button_->setFixedSize(16, 16);
    effect_reset_button_->setFlat(true);
    effect_reset_button_->setToolTip(tr("Reset All to Defaults"));
    effect_reset_button_->setVisible(false);
    // Insert after header label (index 3 = after collapse_button, enabled_check, header)
    tbl->insertWidget(3, effect_reset_button_);
    connect(effect_reset_button_, &QPushButton::clicked, this, &EffectUI::ResetToDefaults);
  }

  int maximum_column = 0;

  widgets_.resize(e->row_count());
  field_widgets_.resize(e->row_count());

  for (int i=0;i<e->row_count();i++) {
    EffectRow* row = e->row(i);

    ClickableLabel* row_label = new ClickableLabel(row->name());
    connect(row_label, &ClickableLabel::clicked, row, &EffectRow::FocusRow);

    labels_.append(row_label);

    layout_->addWidget(row_label, i, 0);

    widgets_[i].resize(row->FieldCount());
    field_widgets_[i].resize(row->FieldCount());

    int column = 1;
    for (int j=0;j<row->FieldCount();j++) {
      EffectField* field = row->Field(j);

      EffectFieldWidget* fw = EffectFieldWidget::Create(field, this);
      QWidget* widget = fw->CreateWidget();

      field_widgets_[i][j] = fw;
      widgets_[i][j] = widget;

      layout_->addWidget(widget, i, column, 1, field->GetColumnSpan());

      column += field->GetColumnSpan();
    }

    // Find maximum column to place keyframe controls
    maximum_column = qMax(row->FieldCount(), maximum_column);
  }

  // Per-row reset buttons (between field widgets and keyframe nav)
  maximum_column++;
  int reset_column = maximum_column;

  row_reset_buttons_.resize(e->row_count());

  for (int i=0;i<e->row_count();i++) {
    EffectRow* row = e->row(i);

    // Only create reset button for rows that have fields with defaults
    bool has_defaults = false;
    for (int j=0;j<row->FieldCount();j++) {
      if (row->Field(j)->HasDefault()) {
        has_defaults = true;
        break;
      }
    }

    if (has_defaults) {
      QPushButton* btn = new QPushButton();
      btn->setIcon(amber::icon::CreateIconFromSVG(":/icons/undo.svg", false));
      btn->setIconSize(QSize(10, 10));
      btn->setFixedSize(16, 16);
      btn->setFlat(true);
      btn->setToolTip(tr("Reset to Default"));
      btn->setVisible(false);
      layout_->addWidget(btn, i, reset_column);

      int row_index = i;
      connect(btn, &QPushButton::clicked, this, [this, row_index]() { ResetRow(row_index); });

      row_reset_buttons_[i] = btn;
    } else {
      row_reset_buttons_[i] = nullptr;
    }
  }

  // Create keyframe controls
  maximum_column++;

  keyframe_navigators_.resize(e->row_count());

  for (int i=0;i<e->row_count();i++) {
    EffectRow* row = e->row(i);

    KeyframeNavigator* nav;

    if (row->IsKeyframable()) {

      nav = new KeyframeNavigator();

      nav->enable_keyframes(row->IsKeyframing());

      AttachKeyframeNavigationToRow(row, nav);

      layout_->addWidget(nav, i, maximum_column);

    } else {

      nav = nullptr;

    }

    keyframe_navigators_[i] = nav;

  }

  enabled_check->setChecked(e->IsEnabled());
  QCheckBox* chk = enabled_check;
  QWidget* cw = contents;
  connect(e, &Effect::EnabledChanged, chk, [chk, cw](bool b) {
    chk->blockSignals(true);
    chk->setChecked(b);
    chk->blockSignals(false);
    cw->setEnabled(b);
  });
  connect(chk, &QCheckBox::toggled, this, [e](bool checked) {
    auto* cmd = new SetEffectEnabled(e, checked);
    cmd->setText(checked ? QObject::tr("Enable Effect") : QObject::tr("Disable Effect"));
    amber::UndoStack.push(cmd);
    update_ui(false);
  });
}

void EffectUI::AddAdditionalEffect(Effect *e)
{
  // Ensure this is the same kind of effect and will be fully compatible
  Q_ASSERT(e->meta == effect_->meta);

  // Add 'multiple' modifier to header label (but only once)
  if (additional_effects_.isEmpty()) {
    QString new_title = tr("%1 (multiple)").arg(Title());

    SetTitle(new_title);
  }

  // Add effect to list
  additional_effects_.append(e);

  // Attach this UI's widgets to the additional effect
  for (int i=0;i<effect_->row_count();i++) {

    EffectRow* row = effect_->row(i);

    // Attach existing keyframe navigator to this effect's row
    AttachKeyframeNavigationToRow(e->row(i), keyframe_navigators_.at(i));

    for (int j=0;j<row->FieldCount();j++) {

      // Attach existing field widget to this effect's field
      QWidget* existing_widget = Widget(i, j);
      if (i < field_widgets_.size() && j < field_widgets_[i].size() && field_widgets_[i][j]) {
        field_widgets_[i][j]->CreateWidget(existing_widget);
      }

    }

  }
}

Effect *EffectUI::GetEffect()
{
  return effect_;
}

int EffectUI::GetRowY(int row, QWidget* mapToWidget) {

  // Currently to get a Y value in the context of `mapToWidget`, we use `panel_effect_controls` as the base. Mapping
  // to global doesn't work for some reason, so this is the best reference point we have.

  QLabel* row_label = labels_.at(row);

  // Get center point of label (label->rect()->center()->y() - instead of y()+height/2 - produces an inaccurate result)
  return row_label->y()
      + row_label->height() / 2
      + mapToWidget->mapFrom(panel_effect_controls, contents->mapTo(panel_effect_controls, contents->pos())).y()
      - title_bar->height();
}

bool EffectUI::IsFieldAtDefault(EffectField* field) {
  if (!field->HasDefault()) return true;
  QVariant current = field->GetValueAt(field->Now());
  QVariant def = field->GetDefaultData();
  if (field->type() == EffectField::EFFECT_FIELD_DOUBLE) {
    double cur_d = current.toDouble();
    double def_d = def.toDouble();
    return (cur_d == def_d) || qFuzzyCompare(cur_d, def_d);
  }
  return current == def;
}

void EffectUI::UpdateFromEffect()
{
  Effect* effect = GetEffect();

  bool any_row_not_default = false;

  for (int j=0;j<effect->row_count();j++) {

    EffectRow* row = effect->row(j);
    bool row_at_default = true;

    for (int k=0;k<row->FieldCount();k++) {
      EffectField* field = row->Field(k);

      // Check if this UI object is attached to one effect or many
      auto update_widget = [&](double timecode) {
        QWidget* w = Widget(j, k);
        if (j < field_widgets_.size() && k < field_widgets_[j].size() && field_widgets_[j][k]) {
          field_widgets_[j][k]->UpdateWidgetValue(w, timecode);
        }
      };

      if (additional_effects_.isEmpty()) {

        update_widget(field->Now());

      } else {

        bool same_value = true;

        for (int i=0;i<additional_effects_.size();i++) {
          EffectField* previous_field = i > 0 ? additional_effects_.at(i-1)->row(j)->Field(k) : field;
          EffectField* additional_field = additional_effects_.at(i)->row(j)->Field(k);

          if (additional_field->GetValueAt(additional_field->Now()) != previous_field->GetValueAt(previous_field->Now())) {
            same_value = false;
            break;
          }
        }

        if (same_value) {
          update_widget(field->Now());
        } else {
          update_widget(qSNaN());
        }

      }

      if (!IsFieldAtDefault(field)) {
        row_at_default = false;
      }
    }

    // Update per-row reset button visibility
    if (j < row_reset_buttons_.size() && row_reset_buttons_[j] != nullptr) {
      row_reset_buttons_[j]->setVisible(!row_at_default);
    }

    if (!row_at_default) {
      any_row_not_default = true;
    }
  }

  // Update effect-level reset button visibility
  if (effect_reset_button_ != nullptr) {
    effect_reset_button_->setVisible(any_row_not_default);
  }
}

bool EffectUI::IsAttachedToClip(Clip *c)
{
  if (GetEffect()->parent_clip == c) {
    return true;
  }

  for (auto additional_effect : additional_effects_) {
    if (additional_effect->parent_clip == c) {
      return true;
    }
  }

  return false;
}

QWidget *EffectUI::Widget(int row, int field)
{
  return widgets_.at(row).at(field);
}

void EffectUI::AttachKeyframeNavigationToRow(EffectRow *row, KeyframeNavigator *nav)
{
  if (nav == nullptr) {
    return;
  }

  connect(nav, &KeyframeNavigator::goto_previous_key, row, &EffectRow::GoToPreviousKeyframe);
  connect(nav, &KeyframeNavigator::toggle_key, row, &EffectRow::ToggleKeyframe);
  connect(nav, &KeyframeNavigator::goto_next_key, row, &EffectRow::GoToNextKeyframe);
  connect(nav, &KeyframeNavigator::keyframe_enabled_changed, row, &EffectRow::SetKeyframingEnabled);
  connect(nav, &KeyframeNavigator::clicked, row, &EffectRow::FocusRow);
  connect(row, &EffectRow::KeyframingSetChanged, nav, &KeyframeNavigator::enable_keyframes);
}

void EffectUI::ResetEffectFields(Effect* e) {
  for (int i = 0; i < e->row_count(); i++) {
    EffectRow* row = e->row(i);
    for (int j = 0; j < row->FieldCount(); j++) {
      EffectField* field = row->Field(j);
      if (field->HasDefault()) {
        field->SetValueAt(field->Now(), field->GetDefaultData());
      }
    }
  }
}

void EffectUI::ResetRow(int row_index) {
  ComboAction* ca = new ComboAction();

  QVector<Effect*> effects;
  effects.append(effect_);
  effects.append(additional_effects_);

  QVector<KeyframeDataChange*> kdcs;
  for (auto* e : effects) {
    EffectRow* row = e->row(row_index);
    for (int j = 0; j < row->FieldCount(); j++) {
      EffectField* field = row->Field(j);
      if (field->HasDefault()) {
        kdcs.append(new KeyframeDataChange(field));
        field->SetValueAt(field->Now(), field->GetDefaultData());
      }
    }
  }

  for (auto* kdc : kdcs) {
    kdc->SetNewKeyframes();
    ca->append(kdc);
  }

  ca->setText(QObject::tr("Reset to Default"));
  amber::UndoStack.push(ca);
  update_ui(false);
}

void EffectUI::ResetToDefaults() {
  ComboAction* ca = new ComboAction();

  QVector<Effect*> effects;
  effects.append(effect_);
  effects.append(additional_effects_);

  QVector<KeyframeDataChange*> kdcs;
  for (auto* e : effects) {
    for (int i = 0; i < e->row_count(); i++) {
      EffectRow* row = e->row(i);
      for (int j = 0; j < row->FieldCount(); j++) {
        EffectField* field = row->Field(j);
        if (field->HasDefault()) {
          kdcs.append(new KeyframeDataChange(field));
        }
      }
    }
  }

  for (auto* e : effects) {
    ResetEffectFields(e);
  }

  for (auto* kdc : kdcs) {
    kdc->SetNewKeyframes();
    ca->append(kdc);
  }

  ca->setText(QObject::tr("Reset to Defaults"));
  amber::UndoStack.push(ca);
  update_ui(false);
}

void EffectUI::show_context_menu(const QPoint& pos) {
  if (effect_->meta->type == EFFECT_TYPE_EFFECT) {
    Menu menu;

    Clip* c = effect_->parent_clip;

    int index = c->IndexOfEffect(effect_);

    QAction* cut_action = menu.addAction(tr("Cu&t"));
    connect(cut_action, &QAction::triggered, this, &EffectUI::CutRequested);

    QAction* copy_action = menu.addAction(tr("&Copy"));
    connect(copy_action, &QAction::triggered, this, &EffectUI::CopyRequested);

    amber::MenuHelper.create_effect_paste_action(&menu);

    menu.addSeparator();

    QAction* move_up_action = nullptr;
    QAction* move_down_action = nullptr;

    if (index > 0) {
      move_up_action = menu.addAction(tr("Move &Up"), GetEffect(), &Effect::move_up);
    }

    if (index < c->effects.size() - 1) {
      move_down_action = menu.addAction(tr("Move &Down"), GetEffect(), &Effect::move_down);
    }

    menu.addSeparator();

    QAction* delete_action = menu.addAction(tr("D&elete"), GetEffect(), &Effect::delete_self);

    // Loop through additional effects and link these too
    for (auto additional_effect : additional_effects_) {
      if (move_up_action != nullptr) {
        connect(move_up_action, &QAction::triggered, additional_effect, &Effect::move_up);
      }

      if (move_down_action != nullptr) {
        connect(move_down_action, &QAction::triggered, additional_effect, &Effect::move_down);
      }

      connect(delete_action, &QAction::triggered, additional_effect, &Effect::delete_self);
    }

    menu.addSeparator();

    menu.addAction(tr("Reset to Defaults"), this, &EffectUI::ResetToDefaults);

    menu.addSeparator();

    menu.addAction(tr("Load Settings From File"), GetEffect(), &Effect::load_from_file);

    menu.addAction(tr("Save Settings to File"), GetEffect(), &Effect::save_to_file);

    menu.exec(title_bar->mapToGlobal(pos));
  }
}
