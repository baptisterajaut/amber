#ifndef KEYFRAMEPROPERTIESDIALOG_H
#define KEYFRAMEPROPERTIESDIALOG_H

#include <QDialog>
#include <QVector>

class ComboAction;
class EffectField;
class LabelSlider;
class QComboBox;

class KeyframePropertiesDialog : public QDialog {
  Q_OBJECT
 public:
  KeyframePropertiesDialog(QWidget* parent, const QVector<EffectField*>& fields, const QVector<int>& keyframe_indices,
                           double frame_rate);

 protected:
  void accept() override;

 private:
  QVector<EffectField*> fields_;
  QVector<int> keyframe_indices_;

  LabelSlider* time_slider_;
  QComboBox* type_combo_;
  LabelSlider* pre_handle_x_;
  LabelSlider* pre_handle_y_;
  LabelSlider* post_handle_x_;
  LabelSlider* post_handle_y_;

  bool time_modified_{false};
  bool type_modified_{false};
  bool pre_handle_x_modified_{false};
  bool pre_handle_y_modified_{false};
  bool post_handle_x_modified_{false};
  bool post_handle_y_modified_{false};

  struct OriginalValues {
    long time;
    int type;
    double pre_handle_x;
    double pre_handle_y;
    double post_handle_x;
    double post_handle_y;
  };
  QVector<OriginalValues> originals_;

  void UpdateBezierEnabled();

  /**
   * @brief Apply all pending keyframe changes for one field/keyframe pair.
   *
   * @param ca              ComboAction to append undo commands to
   * @param i               Index into fields_/keyframe_indices_/originals_
   * @param new_type_for_sticky  Out: set to the new keyframe type if type changed (first change wins)
   * @return true if any change was applied
   */
  bool apply_keyframe_changes_for_one(ComboAction* ca, int i, int& new_type_for_sticky);
};

#endif  // KEYFRAMEPROPERTIESDIALOG_H
