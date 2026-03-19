#ifndef KEYFRAMEPROPERTIESDIALOG_H
#define KEYFRAMEPROPERTIESDIALOG_H

#include <QDialog>
#include <QVector>

class EffectField;
class LabelSlider;
class QComboBox;

class KeyframePropertiesDialog : public QDialog {
  Q_OBJECT
 public:
  KeyframePropertiesDialog(QWidget* parent,
                           const QVector<EffectField*>& fields,
                           const QVector<int>& keyframe_indices,
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
};

#endif  // KEYFRAMEPROPERTIESDIALOG_H
