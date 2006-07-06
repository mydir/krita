/*
 *  kis_tool_star.cc -- part of Krita
 *
 *  Copyright (c) 2004 Michael Thaler <michael.thaler@physik.tu-muenchen.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

/* Just an initial commit. Emanuele Tamponi */


#include <math.h>

#include <qpainter.h>
#include <qspinbox.h>
#include <qlayout.h>

#include <kaction.h>
#include <kdebug.h>
#include <klocale.h>
#include <kdebug.h>
#include <knuminput.h>

#include "kis_doc.h"
#include "kis_painter.h"
#include "kis_point.h"
#include "kis_canvas_subject.h"
#include "kis_canvas_controller.h"
#include "kis_button_press_event.h"
#include "kis_button_release_event.h"
#include "kis_move_event.h"
#include "kis_paintop_registry.h"
#include "kis_canvas.h"
#include "kis_canvas_painter.h"
#include "kis_cursor.h"
#include "kis_vec.h"

#include "kis_curves_framework.h"

#include "kis_tool_curves.h"
#include "wdg_tool_curves.h"


class KisCurveExample : public KisCurve {

    typedef KisCurve super;
    
public:

    KisCurveExample(KisPaintDevice& dev) : super(dev) {}

    ~KisCurveExample() {}

    virtual bool calculateCurve(KisPoint, KisPoint);
    virtual bool calculateCurve(CurvePoint, CurvePoint);

};


bool KisCurveExample::calculateCurve(CurvePoint pos1, CurvePoint pos2)
{
    return calculateCurve(pos1.getPoint(),pos2.getPoint());
}

/* Brutally taken from KisPainter::paintLine, sorry :) */
bool KisCurveExample::calculateCurve(KisPoint pos1, KisPoint pos2)
{
    double savedDist = 0;
    KisVector2D end(pos2);
    KisVector2D start(pos1);

    KisVector2D dragVec = end - start;
    KisVector2D movement = dragVec;

    // XXX: The spacing should vary as the pressure changes along the line.
    // This is a quick simplification.
    double xSpacing = 1;
    double ySpacing = 1;

    if (xSpacing < 0.5) {
        xSpacing = 0.5;
    }
    if (ySpacing < 0.5) {
        ySpacing = 0.5;
    }

    double xScale = 1;
    double yScale = 1;
    double spacing;
    // Scale x or y so that we effectively have a square brush
    // and calculate distance in that coordinate space. We reverse this scaling
    // before drawing the brush. This produces the correct spacing in both
    // x and y directions, even if the brush's aspect ratio is not 1:1.
    if (xSpacing > ySpacing) {
        yScale = xSpacing / ySpacing;
        spacing = xSpacing;
    }
    else {
        xScale = ySpacing / xSpacing;
        spacing = ySpacing;
    }

    dragVec.setX(dragVec.x() * xScale);
    dragVec.setY(dragVec.y() * yScale);

    double newDist = dragVec.length();
    double dist = savedDist + newDist;
    double l_savedDist = savedDist;

    if (dist < spacing) {
        return dist;
    }

    dragVec.normalize();
    KisVector2D step(0, 0);

    while (dist >= spacing) {
        if (l_savedDist > 0) {
            step += dragVec * (spacing - l_savedDist);
            l_savedDist -= spacing;
        }
        else {
            step += dragVec * spacing;
        }

        KisPoint p(start.x() + (step.x() / xScale), start.y() + (step.y() / yScale));

        double distanceMoved = step.length();
        double t = 0;

        if (newDist > DBL_EPSILON) {
            t = distanceMoved / newDist;
        }
        kdDebug(0) << "Aggiungo Punto" << endl;
        add (p);
        dist -= spacing;
    }

    return true;
}


KisToolCurves::KisToolCurves()
    : super(i18n("Curves Initial Commit")),
      m_currentImage (0)
{
    setName("tool_curves");
    setCursor(KisCursor::load("tool_curves_cursor.png", 6, 6));

    m_dragging = false;

    m_curve = 0;
}

KisToolCurves::~KisToolCurves()
{
    delete m_curve;
}

void KisToolCurves::update (KisCanvasSubject *subject)
{
    super::update (subject);
    if (m_subject)
        m_currentImage = m_subject->currentImg ();

    m_curve = new KisCurveExample(*(m_currentImage->activeDevice()));
}

void KisToolCurves::deactivate()
{
    m_curve->clear();
    m_dragging = false;
}

void KisToolCurves::buttonPress(KisButtonPressEvent *event)
{
    if (m_currentImage && event->button() == LeftButton) {
        if (!m_dragging) {
            m_dragging = true;
            m_start = event->pos();
            m_end = m_start;
            m_curve->add(m_start,true);
        }
    }
}

void KisToolCurves::move(KisMoveEvent *event)
{
    return;
}

void KisToolCurves::buttonRelease(KisButtonReleaseEvent *event)
{
    if (!m_subject || !m_currentImage)
        return;

    m_end = event->pos();

    kdDebug(0) << "Oh ci siamo?" << endl;

    if (m_end != m_start) {
        m_curve->calculateCurve(m_start,m_end);
        m_curve->add(m_end);
        draw();
        m_curve->clear();
        m_dragging = false;
    }
}

void KisToolCurves::draw()
{
    m_dragging = false;

    KisPaintDeviceSP device = m_currentImage->activeDevice ();
    if (!device) return;
    
    KisPainter painter (device);
    if (m_currentImage->undo()) painter.beginTransaction (i18n ("Example of curve"));

    painter.setPaintColor(m_subject->fgColor());
    painter.setBrush(m_subject->currentBrush());
    painter.setOpacity(m_opacity);
    painter.setCompositeOp(m_compositeOp);
    KisPaintOp * op = KisPaintOpRegistry::instance()->paintOp(m_subject->currentPaintop(), m_subject->currentPaintopSettings(), &painter);
    painter.setPaintOp(op); // Painter takes ownership

    for( int i = 0; i < m_curve->count(); i++ )
    {
        painter.paintAt((*m_curve)[i].getPoint(), PRESSURE_DEFAULT, 0, 0);
    }

    device->setDirty( painter.dirtyRect() );
    notifyModified();

    if (m_currentImage->undo()) {
        m_currentImage->undoAdapter()->addCommand(painter.endTransaction());
    }
}

void KisToolCurves::setup(KActionCollection *collection)
{
    m_action = static_cast<KRadioAction *>(collection->action(name()));

    if (m_action == 0) {
        KShortcut shortcut(Qt::Key_Plus);
        shortcut.append(KShortcut(Qt::Key_F9));
        m_action = new KRadioAction(i18n("&Curves"),
                                    "tool_curves",
                                    shortcut,
                                    this,
                                    SLOT(activate()),
                                    collection,
                                    name());
        Q_CHECK_PTR(m_action);

        m_action->setToolTip(i18n("Draw curves"));
        m_action->setExclusiveGroup("tools");
        m_ownAction = true;
    }
}

#include "kis_tool_curves.moc"
