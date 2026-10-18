#ifndef EFFECTUI_H
#define EFFECTUI_H

#include "collapsiblewidget.h"
#include "effects/effect.h"

class EffectFieldWidget;

class EffectUI : public CollapsibleWidget {
  Q_OBJECT
 public:
  EffectUI(Effect* e);
  void AddAdditionalEffect(Effect* e);
  Effect* GetEffect();
  int GetRowY(int row, QWidget* mapToWidget);
  void UpdateFromEffect();
  bool IsAttachedToClip(Clip* c);
  void SetLabelColumnWidth(int w);

  const QVector<QLabel*>& labels() const { return labels_; }

 signals:
  void CutRequested();
  void CopyRequested();
  void SavePresetRequested();

 private:
  QWidget* Widget(int row, int field);
  void AttachKeyframeNavigationToRow(EffectRow* row, KeyframeNavigator* nav);

  static bool IsFieldAtDefault(EffectField* field);
  void ResetEffectFields(Effect* e);
  void update_field_widget(int row, int col, EffectField* field);
  bool is_row_at_default(EffectRow* row);

  Effect* effect_;
  QVector<Effect*> additional_effects_;
  QGridLayout* layout_;
  QVector<QVector<QWidget*> > widgets_;
  QVector<QVector<EffectFieldWidget*> > field_widgets_;
  QVector<QLabel*> labels_;
  QVector<KeyframeNavigator*> keyframe_navigators_;
  QVector<QPushButton*> row_reset_buttons_;
  QPushButton* effect_reset_button_;
  QLabel* shader_error_icon_;  // shown while the effect's shader failed to preprocess or bake

  void UpdateShaderError();

 private slots:
  void show_context_menu(const QPoint&);
  void ResetToDefaults();
  void ResetRow(int row);
};

#endif  // EFFECTUI_H
