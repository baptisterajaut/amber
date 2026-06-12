#ifndef UNDOSTACK_H
#define UNDOSTACK_H

#include <QDateTime>
#include <QUndoStack>
#include <QVector>

/**
 * @brief QUndoStack subclass that records a wall-clock timestamp for every
 *        pushed command.
 *
 * The timestamp vector is kept parallel to the internal command list:
 *   - push()  appends QDateTime::currentDateTime() and truncates any
 *             timestamps that belong to commands that were already
 *             discarded by a previous push() while not at the top of the
 *             stack (i.e. the "redo branch" that Qt silently drops).
 *   - clear() wipes timestamps in sync with the base-class clear().
 *
 * No existing call-site needs to be modified.
 */
class TimestampedUndoStack : public QUndoStack {
  Q_OBJECT
 public:
  explicit TimestampedUndoStack(QObject* parent = nullptr);

  /** Push a command and record the current time for it. */
  void push(QUndoCommand* cmd);

  /** Wipe all timestamps in sync with the base-class clear(). */
  void clear();

  /**
   * Return the timestamp for command at position @p idx (0 = initial state
   * placeholder, 1 = first real command, …).  Returns a null QDateTime for
   * the initial-state placeholder (idx == 0) or any out-of-range index.
   */
  QDateTime timestampAt(int idx) const;

  /** Total number of timestamp slots (== count() + 1 for the initial slot). */
  int timestampCount() const;

 private:
  // Index 0 is a null sentinel for the "Initial State" entry that QUndoView
  // always shows.  Real timestamps start at index 1.
  QVector<QDateTime> timestamps_;
};

namespace amber {
/**
 * @brief Global undo stack object
 */
extern TimestampedUndoStack UndoStack;
}  // namespace amber

#endif  // UNDOSTACK_H
