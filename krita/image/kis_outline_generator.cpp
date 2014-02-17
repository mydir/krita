/*
 *  Copyright (c) 2004 Boudewijn Rempt <boud@valdyas.org>
 *  Copyright (c) 2007,2010 Sven Langkamp <sven.langkamp@gmail.com>
 *
 *  Outline algorithm based of the limn of fontutils
 *  Copyright (c) 1992 Karl Berry <karl@cs.umb.edu>
 *  Copyright (c) 1992 Kathryn Hargreaves <letters@cs.umb.edu>
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

#include "kis_outline_generator.h"
#include "kdebug.h"
#include <KoColorSpace.h>
#include <KoColorSpaceRegistry.h>

#include "kis_paint_device.h"
#include <kis_iterator_ng.h>
#include <kis_random_accessor_ng.h>

KisOutlineGenerator::KisOutlineGenerator(const KoColorSpace* cs, quint8 defaultOpacity)
    : m_cs(cs), m_defaultOpacity(defaultOpacity), m_simple(false)
{
}

QVector<QPolygon> KisOutlineGenerator::outline(quint8* buffer, qint32 xOffset, qint32 yOffset, qint32 width, qint32 height)
{
    QVector<QPolygon> paths;

    try {
        quint8* marks = new quint8[width*height];
        memset(marks, 0, width *height);


        int nodes = 0;
        for (qint32 y = 0; y < height; y++) {
            for (qint32 x = 0; x < width; x++) {

                if (m_cs->opacityU8(buffer + (y*width+x)*m_cs->pixelSize()) == m_defaultOpacity)
                    continue;

                EdgeType startEdge = TopEdge;

                EdgeType edge = startEdge;
                while (edge != NoEdge && (marks[y*width+x] & (1 << edge) || !isOutlineEdge(edge, x, y, buffer, width, height))) {
                    edge = nextEdge(edge);
                    if (edge == startEdge)
                        edge = NoEdge;
                }

                if (edge != NoEdge) {
                    QPolygon path;
                    path << QPoint(x + xOffset, y + yOffset);

                    bool clockwise = edge == BottomEdge;

                    qint32 row = y, col = x;
                    EdgeType currentEdge = edge;
                    EdgeType lastEdge = currentEdge;
                    do {
                        //While following a straight line no points need to be added
                        if (lastEdge != currentEdge) {
                            appendCoordinate(&path, col + xOffset, row + yOffset, currentEdge);
                            nodes++;
                            lastEdge = currentEdge;
                        }

                        marks[row*width+col] |= 1 << currentEdge;
                        nextOutlineEdge(&currentEdge, &row, &col, buffer, width, height);
                    } while (row != y || col != x || currentEdge != edge);

                    if(!m_simple || !clockwise)
                        paths.push_back(path);
                }
            }
        }
        delete[] marks;
    }
    catch(std::bad_alloc) {
        warnKrita << "KisOutlineGenerator::outline ran out of memory allocating " <<  width << "*" << height << "marks";
    }

    return paths;
}

QVector<QPolygon> KisOutlineGenerator::outline(const KisPaintDevice *buffer, qint32 xOffset, qint32 yOffset, qint32 width, qint32 height)
{
    m_xOffset = xOffset;
    m_yOffset = yOffset;

    m_accessor = buffer->createRandomConstAccessorNG(xOffset, yOffset);

    const KoColorSpace *alphaCs = KoColorSpaceRegistry::instance()->alpha8();
    KisPaintDeviceSP marks = new KisPaintDevice(alphaCs);
    quint8 defaultPixel = 0;
    marks->setDefaultPixel(&defaultPixel);
    KisRandomAccessorSP marksAccessor = marks->createRandomAccessorNG(0, 0);

    QVector<QPolygon> paths;

    int nodes = 0;
    for (qint32 y = 0; y < height; y++) {
        for (qint32 x = 0; x < width; x++) {

            m_accessor->moveTo(x + xOffset, y + yOffset);

            if (m_cs->opacityU8(m_accessor->oldRawData()) == m_defaultOpacity)
                continue;

            EdgeType startEdge = TopEdge;

            EdgeType edge = startEdge;
            marksAccessor->moveTo(x, y);
            while (edge != NoEdge && (alphaCs->opacityU8(marksAccessor->rawData()) & (1 << edge) || !isOutlineEdge(edge, x, y, buffer, width, height))) {
                edge = nextEdge(edge);
                if (edge == startEdge)
                    edge = NoEdge;
            }

            if (edge != NoEdge) {
                QPolygon path;
                path << QPoint(x + xOffset, y + yOffset);

                bool clockwise = edge == BottomEdge;

                qint32 row = y, col = x;
                EdgeType currentEdge = edge;
                EdgeType lastEdge = currentEdge;
                do {
                    //While following a straight line no points need to be added
                    if (lastEdge != currentEdge) {
                        appendCoordinate(&path, col + xOffset, row + yOffset, currentEdge);
                        nodes++;
                        lastEdge = currentEdge;
                    }
                    marksAccessor->moveTo(col, row);
                    quint8 mark = alphaCs->opacityU8(marksAccessor->rawData());
                    mark |= 1 << currentEdge;
                    alphaCs->setOpacity(marksAccessor->rawData(), mark, 1);
                    nextOutlineEdge(&currentEdge, &row, &col, buffer, width, height);
                } while (row != y || col != x || currentEdge != edge);

                if(!m_simple || !clockwise)
                    paths.push_back(path);
            }
        }
    }

    return paths;
}


bool KisOutlineGenerator::isOutlineEdge(EdgeType edge, qint32 x, qint32 y, quint8* buffer, qint32 bufWidth, qint32 bufHeight)
{
    if (m_cs->opacityU8(buffer + (y*bufWidth+x)*m_cs->pixelSize()) == m_defaultOpacity)
        return false;

    switch (edge) {
    case LeftEdge:
        return x == 0 || m_cs->opacityU8(buffer + (y*bufWidth+(x - 1))*m_cs->pixelSize()) == m_defaultOpacity;
    case TopEdge:
        return y == 0 || m_cs->opacityU8(buffer + ((y - 1)*bufWidth+x)*m_cs->pixelSize()) == m_defaultOpacity;
    case RightEdge:
        return x == bufWidth - 1 || m_cs->opacityU8(buffer + (y*bufWidth+(x + 1))*m_cs->pixelSize()) == m_defaultOpacity;
    case BottomEdge:
        return y == bufHeight - 1 || m_cs->opacityU8(buffer + ((y + 1)*bufWidth+x)*m_cs->pixelSize()) == m_defaultOpacity;
    case NoEdge:
        return false;
    }
    return false;
}


bool KisOutlineGenerator::isOutlineEdge(EdgeType edge, qint32 x, qint32 y, const KisPaintDevice * /*buffer*/, qint32 bufWidth, qint32 bufHeight)
{

    m_accessor->moveTo(x + m_xOffset, y + m_yOffset);

    if (m_cs->opacityU8(m_accessor->oldRawData()) == m_defaultOpacity)
        return false;

    switch (edge) {
    case LeftEdge:
    {
        m_accessor->moveTo(x + m_xOffset - 1, y + m_yOffset);
        return x == 0 || m_cs->opacityU8(m_accessor->oldRawData()) == m_defaultOpacity;
    }
    case TopEdge:
    {
        m_accessor->moveTo(x + m_xOffset, y + m_yOffset - 1);
        return y == 0 || m_cs->opacityU8(m_accessor->oldRawData()) == m_defaultOpacity;
    }
    case RightEdge:
    {
        m_accessor->moveTo(x + m_xOffset + 1, y + m_yOffset);
        return x == bufWidth - 1 || m_cs->opacityU8(m_accessor->oldRawData()) == m_defaultOpacity;
    }
    case BottomEdge:
    {
        m_accessor->moveTo(x + m_xOffset, y + m_yOffset + 1);
        return y == bufHeight - 1 || m_cs->opacityU8(m_accessor->oldRawData()) == m_defaultOpacity;
    }
    case NoEdge:
        return false;
    }
    return false;
}


#define TRY_PIXEL(deltaRow, deltaCol, test_edge)                                                \
    {                                                                                               \
        int test_row = *row + deltaRow;                                                             \
        int test_col = *col + deltaCol;                                                             \
        if ( (0 <= (test_row) && (test_row) < height && 0 <= (test_col) && (test_col) < width) &&   \
                isOutlineEdge (test_edge, test_col, test_row, buffer, width, height))                  \
        {                                                                                           \
            *row = test_row;                                                                        \
            *col = test_col;                                                                        \
            *edge = test_edge;                                                                      \
            break;                                                                                  \
        }                                                                                       \
    }

void KisOutlineGenerator::nextOutlineEdge(EdgeType *edge, qint32 *row, qint32 *col, quint8* buffer, qint32 width, qint32 height)
{
    int original_row = *row;
    int original_col = *col;

    switch (*edge) {
    case RightEdge:
        TRY_PIXEL(-1, 0, RightEdge);
        TRY_PIXEL(-1, 1, BottomEdge);
        break;

    case TopEdge:
        TRY_PIXEL(0, -1, TopEdge);
        TRY_PIXEL(-1, -1, RightEdge);
        break;

    case LeftEdge:
        TRY_PIXEL(1, 0, LeftEdge);
        TRY_PIXEL(1, -1, TopEdge);
        break;

    case BottomEdge:
        TRY_PIXEL(0, 1, BottomEdge);
        TRY_PIXEL(1, 1, LeftEdge);
        break;

    default:
        break;

    }

    if (*row == original_row && *col == original_col)
        *edge = nextEdge(*edge);
}

void KisOutlineGenerator::nextOutlineEdge(EdgeType *edge, qint32 *row, qint32 *col, const KisPaintDevice *buffer, qint32 width, qint32 height)
{
    int original_row = *row;
    int original_col = *col;

    switch (*edge) {
    case RightEdge:
        TRY_PIXEL(-1, 0, RightEdge);
        TRY_PIXEL(-1, 1, BottomEdge);
        break;

    case TopEdge:
        TRY_PIXEL(0, -1, TopEdge);
        TRY_PIXEL(-1, -1, RightEdge);
        break;

    case LeftEdge:
        TRY_PIXEL(1, 0, LeftEdge);
        TRY_PIXEL(1, -1, TopEdge);
        break;

    case BottomEdge:
        TRY_PIXEL(0, 1, BottomEdge);
        TRY_PIXEL(1, 1, LeftEdge);
        break;

    default:
        break;

    }

    if (*row == original_row && *col == original_col)
        *edge = nextEdge(*edge);
}


void KisOutlineGenerator::appendCoordinate(QPolygon * path, int x, int y, EdgeType edge)
{
    switch (edge) {
    case TopEdge:
        x++;
        break;
    case RightEdge:
        x++;
        y++;
        break;
    case BottomEdge:
        y++;
        break;
    case LeftEdge:
    case NoEdge:
        break;

    }
    *path << QPoint(x, y);
}

void KisOutlineGenerator::setSimpleOutline(bool simple)
{
    m_simple = simple;
}
