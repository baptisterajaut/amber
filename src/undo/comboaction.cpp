#include "comboaction.h"

ComboAction::ComboAction() = default;

ComboAction::~ComboAction() {
    for (auto command : commands) {
        delete command;
    }
    for (auto post_command : post_commands) {
        delete post_command;
    }
}

void ComboAction::undo() {
    for (int i=commands.size()-1;i>=0;i--) {
        commands.at(i)->undo();
    }
    for (auto post_command : post_commands) {
        post_command->undo();
    }
}

void ComboAction::redo() {
    for (auto command : commands) {
        command->redo();
    }
    for (auto post_command : post_commands) {
        post_command->redo();
    }
}

void ComboAction::append(QUndoCommand* u) {
    commands.append(u);
}

void ComboAction::appendPost(QUndoCommand* u) {
  post_commands.append(u);
}

bool ComboAction::hasActions()
{
  return commands.size() > 0;
}
