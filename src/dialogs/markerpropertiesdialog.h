#ifndef MARKERPROPERTIESDIALOG_H
#define MARKERPROPERTIESDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QVector>

#include "core/marker.h"
#include "ui/labelslider.h"

class QComboBox;

class MarkerPropertiesDialog : public QDialog {
  Q_OBJECT
 public:
  // `indices` address markers in `array` (sequence, clip or footage markers)
  MarkerPropertiesDialog(QWidget* parent, QVector<Marker>* array, const QVector<int>& indices, double frame_rate);

 protected:
  void accept() override;

 private:
  QVector<Marker>* array_;
  QVector<int> indices_;
  QVector<Marker*> markers_;
  QLineEdit* name_edit_;
  LabelSlider* frame_slider_;
  QComboBox* color_combo_;
  bool name_modified_{false};
  bool frame_modified_{false};
  bool color_modified_{false};
};

#endif  // MARKERPROPERTIESDIALOG_H
