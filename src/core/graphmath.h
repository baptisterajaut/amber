#ifndef CORE_GRAPHMATH_H
#define CORE_GRAPHMATH_H

namespace amber::graph {

// Pure math behind the Graph Editor (ui/graphview.cpp), kept here so it can be tested without a window.

// Value of a key dragged in the Graph Editor: it follows the mouse unrounded (no DoubleField declares a display
// precision; the screen already quantizes to 1 px), kept inside the field's range. A NaN bound is no bound, as in
// DoubleField.
double ClampToRange(double value, double minimum, double maximum);

// ClampToRange() for a key dragged from `start`: a key already outside the range (a viewer gizmo writes unclamped, e.g.
// a negative Transform scale, and an old project can hold anything) is not pulled into it, only kept from going
// further out. A NaN start is a plain ClampToRange().
double ClampDragValue(double value, double start, double minimum, double maximum);

}  // namespace amber::graph

#endif  // CORE_GRAPHMATH_H
