#include "drawlineeffect.h"
#include "engine/sequence.h"
#include "engine/clip.h"
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <algorithm>
#include <QtMath>

DrawLineEffect::DrawLineEffect(Clip* c, const EffectMeta *em) : Effect(c, em) {
//  EffectRow* pos_row = new EffectRow(this, tr("Route Position X / Y (%)"));
	EffectRow* x_row = new EffectRow(this, tr("Route Position X (%)"));
   route_x = new DoubleField(x_row, "route_x");
   route_x->SetDefault(1); 
  route_x->SetMaximum(100);
  route_x->SetMinimum(0);
  EffectRow* y_row = new EffectRow(this, tr("Route Position Y (%)"));  
  route_y = new DoubleField(y_row, "route_y"); 
  route_y->SetDefault(50); 
  route_y->SetMaximum(100);
  route_y->SetMinimum(0);

  EffectRow* style_row = new EffectRow(this, tr("Style Controls"));
  thickness_val = new DoubleField(style_row, "thickness"); thickness_val->SetDefault(6);

  route_handle_gizmo = add_gizmo(GIZMO_TYPE_DOT);
  route_handle_gizmo->set_cursor(Qt::SizeFDiagCursor); 
//  route_handle_gizmo->set_cursor(Qt::CrossCursor); 
  route_handle_gizmo->x_field1 = route_x;  
  
  route_handle_gizmo = add_gizmo(GIZMO_TYPE_DOT);
  route_handle_gizmo->set_cursor(Qt::SizeFDiagCursor); 
  route_handle_gizmo->y_field1 = route_y;         

  SetFlags(Effect::SuperimposeFlag);
  refresh();
}

DrawLineEffect::~DrawLineEffect() {}

bool DrawLineEffect::AlwaysUpdate() { 
	return true; 
}
void DrawLineEffect::refresh() {
  if (parent_clip != nullptr && parent_clip->sequence != nullptr) {
      double seq_width  = parent_clip->sequence->width;
      double seq_height = parent_clip->sequence->height;

      if (seq_width > 0 && seq_height > 0) {
          // Syncs 100% with the Transform math parameters you discovered!
          route_handle_gizmo->x_field_multi1 = 100.0 / seq_width;
          route_handle_gizmo->y_field_multi1 = -100.0 / seq_height; // Retains your critical Y inversion!
      }
  }
}
void DrawLineEffect::process_coords(double timecode, GLTextureCoords& coords, int width) {
    // Simply pass the coordinate structure straight through without altering it.
    // This provides a clean, distortion-free baseline that unfreezes mouse tracking!
    Q_UNUSED(timecode);
    Q_UNUSED(coords);
    Q_UNUSED(width);
}

void DrawLineEffect::draw_route_line(const QVector<QPointF>& points, int width, int height, double thick) {
  // Generate the travel line trajectory path blueprint blueprint
  QPainterPath drawing_path;
  drawing_path.moveTo(points.first());
  for (int i = 1; i < points.size(); ++i) {
      drawing_path.lineTo(points[i]);
  }

  // Pick up our drawing canvas art tools
  QPainter painter(&img);
  painter.setRenderHint(QPainter::Antialiasing);

  QPen line_pen(Qt::red, thick, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  painter.setPen(line_pen);
  painter.drawPath(drawing_path);

  // Paint the self-aligning tracking arrowhead at the leading tip position
  if (points.size() > 1) {
      QPointF tip_point = points.last();
      double heading_degrees = -drawing_path.angleAtPercent(1.0); 
      double heading_radians = heading_degrees * (M_PI / 180.0);

      double arrow_len = thick * 3.0; 

      QPointF tip_front = tip_point;
      QPointF tip_back_left(
          tip_point.x() - arrow_len * qCos(heading_radians - M_PI/6),
          tip_point.y() - arrow_len * qSin(heading_radians - M_PI/6)
      );
      QPointF tip_back_right(
          tip_point.x() - arrow_len * qCos(heading_radians + M_PI/6),
          tip_point.y() - arrow_len * qSin(heading_radians + M_PI/6)
      );

      QPainterPath arrowhead_path;
      arrowhead_path.moveTo(tip_front);
      arrowhead_path.lineTo(tip_back_left);
      arrowhead_path.lineTo(tip_back_right);
      arrowhead_path.closeSubpath();

      painter.setPen(Qt::NoPen);
      painter.setBrush(QBrush(Qt::red));
      painter.drawPath(arrowhead_path);
  }
}

void DrawLineEffect::redraw(double timecode) {
  int width  = img.width();
  int height = img.height();
  if (width <= 0 || height <= 0) return;

  img.fill(Qt::transparent);

  // Fetch coordinates safely from separated keyvalTracks
  double current_x = route_x->GetDoubleAt(timecode) * 0.01 * width;
  double current_y = route_y->GetDoubleAt(timecode) * 0.01 * height;
  QPointF current_point(current_x, current_y);

  int current_frame = int(amber::ActiveSequence->playhead);
  double clip_start = parent_clip->timeline_in();  

  if (current_frame < int(clip_start)) return;

  // Flush map history cache if we reset back to the opening of the clip shot
  if (current_frame <= int(clip_start) + 1) {
      route_history_map.clear();
  }

  // Lock coordinate snapshot safely into local map memory cache
  route_history_map.insert(current_frame, current_point);

  // Filter out any frames ahead of our playhead for clean backwards scrubbing
  QVector<QPointF> drawing_points;
  for (auto it = route_history_map.begin(); it != route_history_map.end(); ++it) {
      if (it.key() <= current_frame) {
          if (drawing_points.isEmpty() || drawing_points.last() != it.value()) {
              drawing_points.append(it.value());
          }
      }
  }

  if (drawing_points.isEmpty()) return;
  if (drawing_points.size() == 1) {
      drawing_points.append(drawing_points.first() + QPointF(1.0, 0.0));
  }

  double thick = thickness_val->GetDoubleAt(timecode);

  // PASS CONTROLS DIRECTLY DOWN TO THE PURE GRAPHICS HELPER METHOD Below!
  this->draw_route_line(drawing_points, width, height, thick);
}
