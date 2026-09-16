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
#ifndef DRAWLINEEFFECT_H
#define DRAWLINEEFFECT_H

#include "effects/effect.h"
#include <QPointF>
#include <QVector>

class EffectGizmo;

class DrawLineEffect : public Effect {
    Q_OBJECT 
public:
    DrawLineEffect(Clip* c, const EffectMeta *em);
    virtual ~DrawLineEffect();

    void redraw(double timecode) override;
    
	QVector<QPointF> smoothTrajectoryFilter(const QVector<QPointF>& inputPoints, 
	                                        int windowSize = 15, 
	                                        double curvatureThreshold = 0.5);
                                                   

protected:                       
    bool AlwaysUpdate() override;

public:
private:
    DoubleField* route_x;
    DoubleField* route_y;
    DoubleField* thickness_val;
    DoubleField* smooth_fld;

    void draw_route_line(const QVector<QPointF>& points, int width, int height,
    	double thick);
};

#endif // DRAWLINEEFFECT_H