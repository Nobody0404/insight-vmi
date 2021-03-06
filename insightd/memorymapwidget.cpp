/*
 * memorymapwidget.cpp
 *
 *  Created on: 01.09.2010
 *      Author: chrschn
 */

#include "memorymapwidget.h"
#include <QApplication>
#include <QEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QMultiMap>
#include <QLinkedList>
#include <QTime>
#include <QTimer>
#include <QHelpEvent>
#include <QToolTip>
#include <QTextDocument>
#include <QTextCursor>
#include <QTextTable>
#include <math.h>
#include <insight/memorymaprangetree.h>
#include <insight/virtualmemory.h>
#include <insight/varsetter.h>
#include <debug.h>


static const int margin = 3;
static const int cellSize = 9;

static const unsigned char PROB_MAX = 16;
enum ColorIndicies {
    ccProbMax   = PROB_MAX,
    ccFunction,
    ccGlobalVar,
    ccValid,
    ccInvalid,
    ccUnknown,
    ccCount
};

static const QColor cellColors[ccCount] = {
    // Probabilities
    QColor(255,   0,   0),  //  0
    QColor(255,  16,   0),  //  1
    QColor(255,  32,   0),  //  2
    QColor(255,  48,   0),  //  3
    QColor(255,  64,   0),  //  4
    QColor(255,  80,   0),  //  5
    QColor(255,  96,   0),  //  6
    QColor(255, 112,   0),  //  7
    QColor(255, 128,   0),  //  8
    QColor(224, 128,   0),  //  9
    QColor(192, 128,   0),  // 10
    QColor(160, 128,   0),  // 11
    QColor(128, 128,   0),  // 12
    QColor( 96, 128,   0),  // 13
    QColor( 64, 128,   0),  // 14
    QColor( 32, 128,   0),  // 15
    QColor(  0, 128,   0),  // 16
    // Validities
    QColor(0,   128, 255), // ccFunction
    QColor(0,     0, 255), // ccGlobalVar
    QColor(0,   128,   0), // ccValid
    QColor(255,   0,   0), // ccInvalid
    QColor(160, 160, 160)  // ccUnknown
};


struct Cell
{
    Cell() : addrStart(0), addrEnd(0), index(-1)
    {
        resetColors();
    }

    void resetColors()
    {
        for (int i = 0; i < 4; ++i)
            colors[i] = -1;
    }

    quint64 addrStart, addrEnd, index;
    int colors[4];
};

//------------------------------------------------------------------------------

MemoryMapWidget::MemoryMapWidget(const MemoryMapRangeTree* map, QWidget *parent)
    : QWidget(parent), _map(map), _diff(0), _visMapValid(false), _address(-1),
      _cols(0), _rows(0), _antialiasing(false), _isPainting(false),
      _showOnlyKernelSpace(false)
{
//    setWindowTitle(tr("Difference view"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setContentsMargins(margin, margin, margin, margin);
    setMouseTracking(true);
}


MemoryMapWidget::~MemoryMapWidget()
{
}


quint64 MemoryMapWidget::visibleAddrSpaceStart() const
{
    if (!_showOnlyKernelSpace || !_map)
        return 0;
    return _specs.pageOffset;
}


quint64 MemoryMapWidget::visibleAddrSpaceEnd() const
{
    return totalAddrSpaceEnd();
}


quint64 MemoryMapWidget::visibleAddrSpaceLength() const
{
    quint64 length = visibleAddrSpaceEnd() - visibleAddrSpaceStart();
    // Correct the size, if below the 64 bit boundary
    if (length < VADDR_SPACE_X86_64)
        ++length;
    return length;
}


quint64 MemoryMapWidget::totalAddrSpaceEnd() const
{
    if (_map && _diff)
        return qMax(_map->addrSpaceEnd(), _diff->addrSpaceEnd());
    if (_map)
        return _map->addrSpaceEnd();
    if (_diff)
        return _diff->addrSpaceEnd();
    return VADDR_SPACE_X86;
}


template<class T>
void insertSorted(T& list, const typename T::value_type value,
        bool eliminateDoubles = true)
{
    typename T::iterator it = list.begin();
    while ((it != list.end()) && (*it <= value)) {
        if (eliminateDoubles && (*it == value))
            return;
        ++it;
    }
    list.insert(it, value);
}


void MemoryMapWidget::closeEvent(QCloseEvent* e)
{
    e->accept();
}


void MemoryMapWidget::resizeEvent(QResizeEvent* /*e*/)
{
    _cols = drawWidth() / cellSize;
    _rows = drawHeight() / cellSize;

    _bytesPerCell = ceil(visibleAddrSpaceLength() / (double)(_cols * _rows));
}


void MemoryMapWidget::mouseMoveEvent(QMouseEvent *e)
{
    // Find out at which address the mouse pointer is
    int x = e->x() - margin, y = e->y() - margin;
    if (x < 0 || y < 0 || x >= drawWidth() || y >= drawHeight())
        return;

    int c = x / cellSize, r = y / cellSize;

    quint64 addr = (r * _cols + c) * _bytesPerCell + visibleAddrSpaceStart();

    if (_address != addr && addr < visibleAddrSpaceEnd()) {
        _address = addr;
        emit addressChanged(_address);
    }
}


void MemoryMapWidget::paintEvent(QPaintEvent * e)
{
    // Set _isPainting to true now and to false on exit
    if (_isPainting) {
        e->ignore();
        return;
    }

    VarSetter<bool> painting(&_isPainting, true, false);

    static const QColor unusedColor(255, 255, 255);
    static const QColor gridColor(240, 240, 240);
    static const QColor bgColor(255, 255, 255);
    static const QColor userLandBgColor(220, 220, 220);
    static const QColor diffColor(0, 0, 255);

    QPainter painter(this);
    if (_antialiasing)
        painter.setRenderHint(QPainter::Antialiasing);

    // Draw in "byte"-coordinates, scale bytes to entire widget
    painter.translate(margin, margin);
    // Clear widget
    painter.setPen(Qt::NoPen);
    painter.setBrush(bgColor);
    painter.drawRect(0, 0, _cols * cellSize, _rows * cellSize);

    // Draw user-land background
    if (!_showOnlyKernelSpace) {
        qint64 userLandCells = _specs.pageOffset / _bytesPerCell;
        qint64 userLandRows = userLandCells / _cols;
        painter.setBrush(userLandBgColor);
        painter.drawRect(0, 0, _cols * cellSize, userLandRows * cellSize);
        if (userLandCells % _cols != 0) {
            quint64 bytesMissing = _specs.pageOffset - (userLandRows * _cols * _bytesPerCell);
            painter.drawRect(0, userLandRows * cellSize,
                             (bytesMissing / (float)_bytesPerCell) * cellSize, cellSize);
        }
    }

    // Draw grid lines if we have at least 3 pixel per byte
    if (cellSize > 1 && gridColor != unusedColor) {
        painter.setPen(gridColor);
        for (int i = 1; i < _cols; i++)
            painter.drawLine(i * cellSize - 1, 0, i * cellSize - 1, _rows * cellSize - 1);
        for (int i = 1; i < _rows; i++)
            painter.drawLine(0, i * cellSize - 1, _cols * cellSize - 1, i * cellSize - 1);
    }

    // Draw all used parts
    painter.setBrush(Qt::NoBrush);
    painter.setPen(Qt::NoPen);

    bool sameAddrSpaceSize =
            !_map || !_diff || _map->addrSpaceEnd() == _diff->addrSpaceEnd();

    // State of current memory cell
    Cell cell;

    cell.addrEnd = visibleAddrSpaceStart() - 1;

    const int totalCells = _cols * _rows;
    int lastColor = -1;

#define set_brush(c) \
    if (lastColor != (c)) { \
        painter.setBrush(cellColors[c]); \
        lastColor = (c); \
    }

    for (int i = 0; i < totalCells; ++i) {

        cell.index = i;
        cell.addrStart = cell.addrEnd + 1;
        cell.addrEnd += _bytesPerCell;
        // Beware of overflows
        if (cell.addrEnd < cell.addrStart ||
                cell.addrEnd > totalAddrSpaceEnd())
            cell.addrEnd = totalAddrSpaceEnd();

        MemMapProperties props = _map ?
                _map->propertiesOfRange(cell.addrStart, cell.addrEnd) :
                MemMapProperties();

        DiffProperties diffProps = _diff && sameAddrSpaceSize ?
                _diff->propertiesOfRange(cell.addrStart, cell.addrEnd) :
                DiffProperties();

        if (props.isEmpty() && diffProps.isEmpty())
            continue;

        cell.resetColors();
//        getCellProbabilityColors(&cell, props);
        getCellValidityColors(&cell, props);

        // Draw  cell
        quint64 r = cell.index / _cols;
        quint64 c = cell.index % _cols;

        // Anything to paint?
        if (cell.colors[0] < 0) {
            lastColor = -1;
            continue;
        }

        if (cellSize == 1) {
            // If we can only paint one pixel, then use only one color
            if (lastColor != cell.colors[0]) {
                painter.setPen(cellColors[cell.colors[0]]);
                lastColor = cell.colors[0];
            }
            painter.drawPoint(c, r);
        }
        else {
            // Calculate coordinates
            int x1, y1, w, h;
            if (diffProps.isEmpty()) {
                x1 = c * cellSize;
                y1 = r * cellSize;
                w = h = cellSize - 1;
            }
            else {
                x1 = c * cellSize - 1;
                y1 = r * cellSize - 1;
                w = h = cellSize;
            }

            if (cell.colors[0] >= 0) {
                set_brush(cell.colors[0]);
                painter.drawRect(x1, y1, w, h);

                if (cell.colors[1] >= 0) {
                    // We need to draw grid lines
                    painter.setPen(gridColor);
                    set_brush(cell.colors[1]);
                    // We need to enlarge the cell size now because of the pen
                    QPoint points[3] = {
                        QPoint(x1-1, y1-1),
                        QPoint(x1-1, y1 + h),
                        QPoint(x1 + w, y1 + h)
                    };
                    painter.drawConvexPolygon(points, 3);

                    if (cell.colors[2] >= 0) {
                        set_brush(cell.colors[2]);
                        painter.setBrush(cellColors[cell.colors[2]]);
                        QPoint points4[4] = {
                            QPoint(x1-1, y1 + h),
                            QPoint(x1-1 + (w>>1), y1 + ((h+1)>>1)),
                            QPoint(x1 + ((w+1)>>1), y1 + ((h+1)>>1)),
                            QPoint(x1 + w, y1 + h),
                        };

                        painter.drawConvexPolygon(points4, 4);

                        if (cell.colors[3] >= 0) {
                            set_brush(cell.colors[3]);
                            points4[0] = QPoint(x1-1, y1-1);
                            points4[1] = QPoint(x1 + (w>>1), y1 + (h>>1));
                            points4[2] = QPoint(x1 + ((w+1)>>1), y1 + (h>>1));
                            points4[3] = QPoint(x1 + w, y1 - 1);
                            painter.drawConvexPolygon(points4, 4);
                        }
                    }
                    // Disable gridlines again
                    painter.setPen(Qt::NoPen);
                }
            }
        }
    }
}


void MemoryMapWidget::getCellProbabilityColors(Cell *cell,
                                               const MemMapProperties &props)
{
    int numColors = 0;
    if (props.baseTypes & rtFunction)
        cell->colors[numColors++] = ccFunction;
    if (props.baseTypes & ~rtFunction)
        cell->colors[numColors++] = (unsigned char)(PROB_MAX * props.maxProbability);
}


void MemoryMapWidget::getCellValidityColors(struct Cell *cell,
                                            const MemMapProperties &props)
{
    static const int validMask =
            SlubObjects::ovEmbedded |
            SlubObjects::ovEmbeddedCastType |
            SlubObjects::ovEmbeddedUnion |
            SlubObjects::ovValid |
            SlubObjects::ovValidCastType;

    static const int invalidMask =
            SlubObjects::ovConflict |
//            SlubObjects::ovInvalid |
            SlubObjects::ovNotFound;

    // Which colors to draw?
    int numColors = 0;
    if (props.baseTypes & rtFunction)
        cell->colors[numColors++] = ccFunction;
//    if (props.slubValidities & SlubObjects::ovValidGlobal)
//        colors[numColors++] = vcGlobalVar;
    if (props.slubValidities & validMask)
        cell->colors[numColors++] = ccValid;
    if (props.slubValidities & invalidMask)
        cell->colors[numColors++] = ccInvalid;
    if (numColors < 4 &&
        (props.slubValidities & ~(validMask|invalidMask /*|SlubObjects::ovValidGlobal*/ )))
        cell->colors[numColors++] = ccUnknown;
}


bool MemoryMapWidget::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        QHelpEvent* helpEvent = static_cast<QHelpEvent *>(event);

        // Find out at which address the mouse pointer is
        int x = helpEvent->x() - margin, y = helpEvent->y() - margin;
        if (x < 0 || y < 0 || x >= drawWidth() || y >= drawHeight()) {
            QToolTip::hideText();
            event->ignore();
        }
        else {
            int c = x / cellSize, r = y / cellSize;
            quint64 addrStart = (r * _cols + c) * _bytesPerCell + visibleAddrSpaceStart();
            quint64 addrEnd = addrStart + _bytesPerCell - 1;
            // Limit addrEnd to acutal end of address space. There are two cases:
            // 32-bit: addrEnd is beyond totalAddrSpaceEnd()
            // 64-bit: addrEnd is close to zero, because an overflow occured
            if (addrEnd > totalAddrSpaceEnd() ||
                totalAddrSpaceEnd() - addrStart < totalAddrSpaceEnd() - addrEnd)
            {
                addrEnd = totalAddrSpaceEnd();
            }
            int width = _map && _map->addrSpaceEnd() >= (1ULL << 32) ? 16 : 8;

            QTextDocument doc;
            QTextCursor cur(&doc);

            QTextBlockFormat hdrBlkFmt;
            hdrBlkFmt.setAlignment(Qt::AlignHCenter);
//            QTextCharFormat hdrChrFmt;
//            hdrChrFmt.setFontPointSize(doc.defaultFont().pointSizeF());
//            hdrChrFmt.setFontWeight(QFont::Bold);
//            hdrChrFmt.setFontFixedPitch(true);

            QFont font; // = doc.defaultFont();
            font.setPointSizeF(8);
            doc.setDefaultFont(font);

            const QTextBlockFormat defBlkFmt = cur.blockFormat();
            const QTextCharFormat defCharFmt = cur.charFormat();
            QTextBlockFormat cellProbBlkFmt = cur.blockFormat();
            cellProbBlkFmt.setAlignment(Qt::AlignRight);
            QTextCharFormat cellAddrCharFmt = cur.charFormat();
            cellAddrCharFmt.setFontFixedPitch(true);
            QTextCharFormat cellTypeCharFmt = cur.charFormat();
            cellTypeCharFmt.setFontWeight(QFont::Bold);
            QTextCharFormat cellNameCharFmt = cur.charFormat();
//            cellNameCharFmt.setFontWeight(QFont::Bold);
            QTextCharFormat cellProbCharFmt = cur.charFormat();

            cur.insertHtml(QString("<h3><tt>0x%1</tt>&nbsp;&ndash;&nbsp;<tt>0x%2</tt></h3>")
                            .arg(addrStart, width, 16, QChar('0'))
                            .arg(addrEnd, width, 16, QChar('0')));
            cur.mergeBlockFormat(hdrBlkFmt);
//            cur.setCharFormat(hdrChrFmt);


            NodeList nodes = _map ?
                    _map->objectsInRange(addrStart, addrEnd).toList()
                    : NodeList();
            qSort(nodes.begin(), nodes.end(), NodeProbabilityGreaterThan);

            if (!nodes.isEmpty()) {
                const int maxTables = 1;
                const int maxRowsPerTable = 30;
                int rows = nodes.size() > maxTables * maxRowsPerTable ?
                        maxTables * maxRowsPerTable : nodes.size();

                QTextTableFormat tabFmt;
                tabFmt.setCellSpacing(2);
                tabFmt.setBorderStyle(QTextFrameFormat::BorderStyle_None);

                cur.insertBlock(defBlkFmt, defCharFmt);
                cur.insertHtml(QString("<b>%1 object(s)</b>").arg(nodes.size()));

                cur.insertBlock(defBlkFmt, defCharFmt);

                int rowCnt = 0;
                for (NodeList::iterator it = nodes.begin();
                        it != nodes.end() && rowCnt < rows;
                        ++it, ++rowCnt)
                {
                    if (rowCnt % maxRowsPerTable == 0) {
                        int r = rows - rowCnt >= maxRowsPerTable ?
                                maxRowsPerTable : rows - rowCnt;
                        cur.movePosition(QTextCursor::End);
                        cur.insertTable(r, 4, tabFmt);
                    }

                    const MemoryMapNode* node = *it;
                    int colorIdx = (unsigned char)(PROB_MAX * node->probability());
                    cellProbCharFmt.setForeground(QBrush(cellColors[colorIdx]));

                    cur.setBlockFormat(defBlkFmt);
                    cur.setCharFormat(cellAddrCharFmt);
                    cur.insertText(QString("0x%1")
                            .arg(node->address(), width, 16, QChar('0')));
                    cur.movePosition(QTextCursor::NextCell);

                    cur.setCharFormat(cellTypeCharFmt);
                    cur.insertText(node->type() ? node->type()->prettyName()
                                                : QString());
                    cur.movePosition(QTextCursor::NextCell);

                    cur.setCharFormat(cellNameCharFmt);
                    QString name = node->fullName();
                    if (name.length() > 100)
                        name.replace(49, name.length() - 97, "...");
                    cur.insertText(name);
                    cur.movePosition(QTextCursor::NextCell);

                    cur.setBlockFormat(cellProbBlkFmt);
                    cur.setCharFormat(cellProbCharFmt);
                    cur.insertText(QString("%1%")
                            .arg(node->probability() * 100.0, 0, 'f', 1));
                    cur.movePosition(QTextCursor::NextCell);
                }

                if (rows < nodes.size()) {
                    cur.movePosition(QTextCursor::End);
                    cur.insertBlock(defBlkFmt, defCharFmt);
                    cur.insertText(QString("(%1 more)")
                            .arg(nodes.size() - rows));
                }
            }

            QToolTip::showText(helpEvent->globalPos(), doc.toHtml());
        }

        return true;
    }
    return QWidget::event(event);
}


int MemoryMapWidget::drawWidth() const
{
    return width() - 2*margin;
}


int MemoryMapWidget::drawHeight() const
{
    return height() - 2*margin;
}


const MemoryMapRangeTree* MemoryMapWidget::map() const
{
    return _map;
}


const MemoryDiffTree* MemoryMapWidget::diff() const
{
    return _diff;
}


void MemoryMapWidget::setDiff(const MemoryDiffTree* diff)
{
    if (_diff == diff)
        return;
    _diff = diff;
    resizeEvent(0);
    update();
}


void MemoryMapWidget::setMap(const MemoryMapRangeTree* map,
                             const MemSpecs& specs)
{
    if (_map == map)
        return;
    _map = map;
    _specs = specs;
    resizeEvent(0);
    update();
}


bool MemoryMapWidget::antiAliasing() const
{
    return _antialiasing;
}


void MemoryMapWidget::setAntiAliasing(bool value)
{
    _antialiasing = value;
    update();
}


bool MemoryMapWidget::isPainting() const
{
    return _isPainting;
}


void MemoryMapWidget::setShowOnlyKernelSpace(bool value)
{
    if (_showOnlyKernelSpace == value)
        return;
    _showOnlyKernelSpace = value;
    resizeEvent(0);
    update();
}


bool MemoryMapWidget::showOnlyKernelSpace() const
{
    return _showOnlyKernelSpace;
}
