#ifndef UI_CHECKBOX_COMMAND_H
#define UI_CHECKBOX_COMMAND_H

#include "engine/undo/undoactions.h"
#include <QCheckBox>

class CheckboxCommand : public AmberAction {
 public:
  explicit CheckboxCommand(QCheckBox* b);
  ~CheckboxCommand() override;
  void doUndo() override;
  void doRedo() override;

 private:
  QCheckBox* box;
  bool checked;
  bool done;
};

#endif  // UI_CHECKBOX_COMMAND_H
