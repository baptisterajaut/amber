/***

    Amber - Non-Linear Video Editor
    Copyright (C) 2026  Amber Team

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#include "drawlineeffect.h"
#include "engine/sequence.h"
#include "engine/clip.h"
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QtMath>

DrawLineEffect::DrawLineEffect(Clip* c, const EffectMeta *em) : Effect(c, em) {

  EffectRow* x_row = new EffectRow(this, tr("Line X (%)"));
  route_x = new DoubleField(x_row, "route_x");

  EffectRow* y_row = new EffectRow(this, tr("Line Y (%)"));
  route_y = new DoubleField(y_row, "route_y");

  EffectRow* style_row = new EffectRow(this, tr("Line Thickness"));
  thickness_val = new DoubleField(style_row, "thickness"); 
  route_x->SetDefault(1); 
  route_x->SetMaximum(100);
  route_x->SetMinimum(0);
  
  EffectRow* smooth_sz = new EffectRow(this, tr("Smoothing number of points"));
  smooth_fld = new DoubleField(smooth_sz, "smoothsz"); 
  smooth_fld->SetDefault(1);              
  smooth_fld->SetMaximum(31);
  smooth_fld->SetMinimum(1);
                   
  route_y->SetDefault(50); 
  route_y->SetMaximum(100);
  route_y->SetMinimum(0);
  
  thickness_val->SetDefault(6);  
  thickness_val->SetMinimum(0);  
  thickness_val->SetMaximum(15);  

  SetFlags(Effect::SuperimposeFlag);
  
}

DrawLineEffect::~DrawLineEffect() {}

bool DrawLineEffect::AlwaysUpdate() { return true; }

void DrawLineEffect::draw_route_line(const QVector<QPointF>& points, int width, int height, double thick) {
  QPainterPath drawing_path;
  drawing_path.moveTo(points.first());
  for (int i = 1; i < points.size(); ++i) {
      drawing_path.lineTo(points[i]);
  }

  QPainter painter(&img);
  painter.setRenderHint(QPainter::Antialiasing);

  QPen line_pen(Qt::red, thick, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  painter.setPen(line_pen);
  painter.drawPath(drawing_path);

  if (points.size() > 1) {
      QPointF tip_point = points.last();
      double heading_degrees = -drawing_path.angleAtPercent(1.0); 
      double heading_radians = heading_degrees * (M_PI / 180.0);
      double arrow_len = thick * 3.0; 

      QPointF tip_front = tip_point;
      QPointF tip_back_left(tip_point.x() - arrow_len * qCos(heading_radians - M_PI/6), tip_point.y() - arrow_len * qSin(heading_radians - M_PI/6));
      QPointF tip_back_right(tip_point.x() - arrow_len * qCos(heading_radians + M_PI/6), tip_point.y() - arrow_len * qSin(heading_radians + M_PI/6));

      QPainterPath arrowhead_path;
      arrowhead_path.moveTo(tip_front); arrowhead_path.lineTo(tip_back_left); arrowhead_path.lineTo(tip_back_right); arrowhead_path.closeSubpath();
      painter.setPen(Qt::NoPen); painter.setBrush(QBrush(Qt::red)); painter.drawPath(arrowhead_path);
  }
}


//======================================================
void DrawLineEffect::redraw(double timecode) {
  int width  = img.width();
  int height = img.height();
  if (width <= 0 || height <= 0) return;
  img.fill(Qt::transparent);

  double current_frame = amber::ActiveSequence->playhead;
  double clip_start    = parent_clip->timeline_in();  
  
  if (current_frame < clip_start) return;

  double fps = amber::ActiveSequence->frame_rate;
  if (fps <= 0.0) fps = 29.97; 
  double frame_duration = 1.0 / fps;

  int total_history_steps = int(current_frame - clip_start);
  QVector<QPointF> drawing_points;

  for (int i = 0; i <= total_history_steps; i++) {
      // March forward step-by-step using your verified time calculation formula
      double target_t = timecode - (total_history_steps * frame_duration) + 
      (i * frame_duration);
      
      double kf_x = route_x->GetDoubleAt(target_t) * 0.01 * width;
      double kf_y = route_y->GetDoubleAt(target_t) * 0.01 * height;
      
      QPointF pt(kf_x, kf_y);
      drawing_points.append(pt);
  }
  if (drawing_points.isEmpty()) return;
  if (drawing_points.size() == 1) {
      drawing_points.append(drawing_points.first() + QPointF(1.0, 0.0));
  }

  int nsmooth=smooth_fld->GetDoubleAt(timecode);
  QVector<QPointF> smoothed = this->smoothTrajectoryFilter(drawing_points,nsmooth,0.5);
 
  double thick = thickness_val->GetDoubleAt(timecode);
  this->draw_route_line(smoothed, width, height, thick);
}


/**
 * @brief Smooths corners of a time-series trajectory while keeping straight lines intact.
 * @param inputPoints 30-60 Hz sampled curve (up to ~300 points).
 * @param windowSize Number of points to smooth over (e.g., 15 for ~0.25-0.5s of motion).
 * @param curvatureThreshold Threshold for corner detection.
 */
QVector<QPointF> DrawLineEffect::smoothTrajectoryFilter(const QVector<QPointF>& inputPoints, 
                                        int windowSize, double curvatureThreshold) 
{
 int n = inputPoints.size();
 if (n < 3 || windowSize < 3) {
     return inputPoints;
 }
 
 if (windowSize % 2 == 0) {
     windowSize++; // Force odd window size
 }
 int halfWindow = windowSize / 2;
 QVector<QPointF> outputPoints = inputPoints;

 // Precompute Gaussian weights for the smoothing window to ensure perfect blending
 QVector<double> weights(windowSize);
 double sigma = halfWindow / 2.0;
 double weightSum = 0.0;
 for (int w = -halfWindow; w <= halfWindow; ++w) {
     double wIdx = w;
     double g = std::exp(-(wIdx * wIdx) / (2.0 * sigma * sigma));
     weights[w + halfWindow] = g;
     weightSum += g;
 }

 // Step 1: Compute second derivative magnitude (acceleration proxy / curvature)
  QVector<double> curvatures(n, 0.0);
  for (int i = 1; i < n - 1; ++i) {
      double d2x = inputPoints[i-1].x() - 2.0 * inputPoints[i].x() + inputPoints[i+1].x();
      double d2y = inputPoints[i-1].y() - 2.0 * inputPoints[i].y() + inputPoints[i+1].y();
      curvatures[i] = std::sqrt(d2x * d2x + d2y * d2y);
  }
 
  // Step 2: Smooth only regions close to sharp direction changes
  for (int i = halfWindow; i < n - halfWindow; ++i) {
      bool isNearCorner = false;
      
      // Look ahead/behind within the window radius to find if a corner is active
      for (int w = -halfWindow; w <= halfWindow; ++w) {
          if (curvatures[i + w] > curvatureThreshold) {
              isNearCorner = true;
              break;
          }
      }
      
      if (isNearCorner) {
          double sumX = 0.0;
          double sumY = 0.0;
          double currentWindowWeightSum = 0.0;
          
          for (int w = -halfWindow; w <= halfWindow; ++w) {
              const QPointF& p = inputPoints[i + w];
              double wFactor = weights[w + halfWindow];
              
              sumX += p.x() * wFactor;
              sumY += p.y() * wFactor;
              currentWindowWeightSum += wFactor;
          }
          
          outputPoints[i] = QPointF(sumX / currentWindowWeightSum, sumY / currentWindowWeightSum);
      }
  }
 
  return outputPoints;
}