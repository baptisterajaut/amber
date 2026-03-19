#include "checkbox_command.h"

CheckboxCommand::CheckboxCommand(QCheckBox* b) {
  box = b;
  checked = box->isChecked();
  done = true;
}

CheckboxCommand::~CheckboxCommand() = default;

void CheckboxCommand::doUndo() {
  box->setChecked(!checked);
  done = false;
}

void CheckboxCommand::doRedo() {
  if (!done) {
    box->setChecked(checked);
  }
}
