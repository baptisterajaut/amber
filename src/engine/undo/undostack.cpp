#include "undostack.h"

// ---------------------------------------------------------------------------
// TimestampedUndoStack
// ---------------------------------------------------------------------------

TimestampedUndoStack::TimestampedUndoStack(QObject* parent)
    : QUndoStack(parent) {
  // Slot 0 is the null sentinel for the "Initial State" placeholder row.
  timestamps_.append(QDateTime());
}

void TimestampedUndoStack::push(QUndoCommand* cmd) {
  // Qt silently discards all commands after the current index when a new
  // command is pushed while in an undone state.  Mirror that truncation so
  // our timestamps vector stays parallel.
  //
  // Before the base-class push, index() points to the last *active* command
  // (0 == initial state).  After the push the new command will sit at
  // index() + 1, so everything beyond that is gone.
  //
  // We truncate to (index + 1) entries (keeping slots 0..index), then append
  // the new timestamp for the incoming command.
  if (timestamps_.size() > index() + 1) {
    timestamps_.resize(index() + 1);
  }
  timestamps_.append(QDateTime::currentDateTime());
  QUndoStack::push(cmd);  // base class takes ownership of cmd
}

void TimestampedUndoStack::clear() {
  QUndoStack::clear();
  timestamps_.clear();
  // Re-add the null sentinel for the initial-state placeholder.
  timestamps_.append(QDateTime());
}

QDateTime TimestampedUndoStack::timestampAt(int idx) const {
  if (idx <= 0 || idx >= timestamps_.size()) return QDateTime();
  return timestamps_.at(idx);
}

int TimestampedUndoStack::timestampCount() const {
  return timestamps_.size();
}

// ---------------------------------------------------------------------------
// Global instance
// ---------------------------------------------------------------------------

TimestampedUndoStack amber::UndoStack;
