#include "icons.h"

#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QHash>
#include <QPixmap>
#include <QFont>
#include <cmath>

namespace {

const QColor kOutline(40, 50, 70);
const QColor kBlue(60, 120, 200);
const QColor kLightBlue(150, 195, 240);

QLinearGradient vgrad(qreal y0, qreal y1, const QColor &a, const QColor &b)
{
    QLinearGradient g(0, y0, 0, y1);
    g.setColorAt(0, a);
    g.setColorAt(1, b);
    return g;
}

void dashedRect(QPainter &p, const QRectF &r)
{
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QPen(Qt::white, 1));
    p.drawRect(r);
    QPen dash(Qt::black, 1, Qt::CustomDashLine);
    dash.setDashPattern({2, 2});
    p.setPen(dash);
    p.drawRect(r);
    p.setRenderHint(QPainter::Antialiasing, true);
}

void dashedPath(QPainter &p, const QPainterPath &path)
{
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(Qt::white, 1.2));
    p.drawPath(path);
    QPen dash(Qt::black, 1.2, Qt::CustomDashLine);
    dash.setDashPattern({1.6, 1.6});
    p.setPen(dash);
    p.drawPath(path);
}

void arrowCursor(QPainter &p, QPointF o, qreal s, const QColor &fill)
{
    QPainterPath a;
    a.moveTo(o);
    a.lineTo(o + QPointF(0, 11 * s));
    a.lineTo(o + QPointF(2.8 * s, 8.4 * s));
    a.lineTo(o + QPointF(4.8 * s, 12.6 * s));
    a.lineTo(o + QPointF(6.4 * s, 11.8 * s));
    a.lineTo(o + QPointF(4.4 * s, 7.8 * s));
    a.lineTo(o + QPointF(8 * s, 7.8 * s));
    a.closeSubpath();
    p.setPen(QPen(fill == Qt::white ? Qt::black : Qt::white, 1));
    p.setBrush(fill);
    p.drawPath(a);
}

void fourWay(QPainter &p, QPointF c, qreal r, const QColor &col)
{
    p.setPen(QPen(col, 1.3, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(c - QPointF(r, 0), c + QPointF(r, 0));
    p.drawLine(c - QPointF(0, r), c + QPointF(0, r));
    p.setPen(Qt::NoPen);
    p.setBrush(col);
    qreal h = 2.0;
    QPolygonF t;
    t << c + QPointF(r + 1, 0) << c + QPointF(r - 1, -h) << c + QPointF(r - 1, h);
    p.drawPolygon(t);
    t.clear();
    t << c - QPointF(r + 1, 0) << c - QPointF(r - 1, -h) << c - QPointF(r - 1, h);
    p.drawPolygon(t);
    t.clear();
    t << c + QPointF(0, r + 1) << c + QPointF(-h, r - 1) << c + QPointF(h, r - 1);
    p.drawPolygon(t);
    t.clear();
    t << c - QPointF(0, r + 1) << c - QPointF(-h, r - 1) << c - QPointF(h, r - 1);
    p.drawPolygon(t);
}

void magnifier(QPainter &p, int sign)
{
    p.setPen(QPen(QColor(120, 80, 40), 3, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(10, 10), QPointF(14.2, 14.2));
    QRadialGradient g(QPointF(5, 5), 6);
    g.setColorAt(0, QColor(240, 250, 255));
    g.setColorAt(1, QColor(130, 190, 240));
    p.setBrush(g);
    p.setPen(QPen(QColor(60, 70, 90), 1.4));
    p.drawEllipse(QPointF(6.5, 6.5), 5, 5);
    if (sign) {
        p.setPen(QPen(QColor(20, 40, 90), 1.6));
        p.drawLine(QPointF(4, 6.5), QPointF(9, 6.5));
        if (sign > 0)
            p.drawLine(QPointF(6.5, 4), QPointF(6.5, 9));
    }
}

void page(QPainter &p, const QRectF &r, const QColor &fill = Qt::white)
{
    QPainterPath pg;
    pg.moveTo(r.topLeft());
    pg.lineTo(r.right() - 3, r.top());
    pg.lineTo(r.right(), r.top() + 3);
    pg.lineTo(r.bottomRight());
    pg.lineTo(r.bottomLeft());
    pg.closeSubpath();
    p.setBrush(vgrad(r.top(), r.bottom(), fill, fill.darker(108)));
    p.setPen(QPen(QColor(110, 120, 140), 1));
    p.drawPath(pg);
    p.drawLine(QPointF(r.right() - 3, r.top()), QPointF(r.right() - 3, r.top() + 3));
    p.drawLine(QPointF(r.right() - 3, r.top() + 3), QPointF(r.right(), r.top() + 3));
}

void star(QPainter &p, QPointF c, qreal r, const QColor &col)
{
    QPainterPath s;
    for (int i = 0; i < 8; ++i) {
        qreal a = i * M_PI / 4;
        qreal rr = (i % 2) ? r * 0.4 : r;
        QPointF pt = c + QPointF(std::cos(a) * rr, std::sin(a) * rr);
        if (i == 0) s.moveTo(pt); else s.lineTo(pt);
    }
    s.closeSubpath();
    p.setPen(QPen(col.darker(150), 0.8));
    p.setBrush(col);
    p.drawPath(s);
}

void bigArrow(QPainter &p, bool up, const QColor &col)
{
    QPolygonF a;
    if (up)
        a << QPointF(8, 2) << QPointF(14, 8.5) << QPointF(10.5, 8.5) << QPointF(10.5, 14)
          << QPointF(5.5, 14) << QPointF(5.5, 8.5) << QPointF(2, 8.5);
    else
        a << QPointF(8, 14) << QPointF(14, 7.5) << QPointF(10.5, 7.5) << QPointF(10.5, 2)
          << QPointF(5.5, 2) << QPointF(5.5, 7.5) << QPointF(2, 7.5);
    p.setPen(QPen(col.darker(160), 1));
    p.setBrush(vgrad(2, 14, col.lighter(140), col));
    p.drawPolygon(a);
}

void curvedArrow(QPainter &p, bool left, const QColor &col)
{
    p.save();
    if (!left) {
        p.translate(16, 0);
        p.scale(-1, 1);
    }
    QPainterPath path;
    path.moveTo(4, 7);
    path.cubicTo(6, 2.5, 13, 3, 13.5, 9);
    path.cubicTo(13.5, 11, 12.5, 12.5, 11, 13.5);
    p.setPen(QPen(col, 2.2, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
    QPolygonF h;
    h << QPointF(1.5, 4) << QPointF(7.5, 5.2) << QPointF(3.2, 10);
    p.setPen(Qt::NoPen);
    p.setBrush(col);
    p.drawPolygon(h);
    p.restore();
}

void layerStack(QPainter &p, QPointF o, const QColor &col)
{
    QPolygonF d;
    d << o + QPointF(0, 3) << o + QPointF(6, 0) << o + QPointF(12, 3) << o + QPointF(6, 6);
    p.setPen(QPen(col.darker(170), 0.9));
    p.setBrush(col.lighter(130));
    p.translate(0, 3);
    p.drawPolygon(d);
    p.translate(0, -3);
    p.setBrush(col);
    p.drawPolygon(d);
}

void twoShapes(QPainter &p, int mode)
{
    QRectF a(1.5, 2.5, 8, 8), b(6.5, 5.5, 8, 8);
    QPainterPath pa, pb;
    pa.addRect(a);
    pb.addRect(b);
    QPainterPath res;
    switch (mode) {
    case 0: res = pb; break;
    case 1: res = pa.united(pb); break;
    case 2: res = pa.subtracted(pb); break;
    case 3: res = pa.intersected(pb); break;
    case 4: res = pa.subtracted(pb).united(pb.subtracted(pa)); break;
    }
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(100, 160, 230));
    p.drawPath(res);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(30, 50, 90), 1));
    if (mode != 0)
        p.drawRect(a);
    p.drawRect(b);
}

void gradientIcon(QPainter &p, int type)
{
    QRectF r(1.5, 1.5, 13, 13);
    QGradient *g = nullptr;
    QLinearGradient lin(r.topLeft(), r.bottomRight());
    QRadialGradient rad(r.center(), 7);
    QConicalGradient con(r.center(), 90);
    switch (type) {
    case 0:
        lin.setColorAt(0, QColor(20, 30, 60));
        lin.setColorAt(1, Qt::white);
        g = &lin;
        break;
    case 1:
        lin.setColorAt(0, Qt::white);
        lin.setColorAt(0.5, QColor(20, 30, 60));
        lin.setColorAt(1, Qt::white);
        g = &lin;
        break;
    case 2: {
        p.setPen(QPen(QColor(80, 80, 90), 1));
        p.setBrush(Qt::white);
        p.drawRect(r);
        QPolygonF d;
        d << QPointF(8, 2) << QPointF(14, 8) << QPointF(8, 14) << QPointF(2, 8);
        QRadialGradient rg(QPointF(8, 8), 6);
        rg.setColorAt(0, QColor(20, 30, 60));
        rg.setColorAt(1, QColor(200, 210, 230));
        p.setBrush(rg);
        p.setPen(Qt::NoPen);
        p.drawPolygon(d);
        return;
    }
    case 3:
        rad.setColorAt(0, QColor(20, 30, 60));
        rad.setColorAt(1, Qt::white);
        g = &rad;
        break;
    case 4:
        con.setColorAt(0, QColor(20, 30, 60));
        con.setColorAt(0.5, Qt::white);
        con.setColorAt(1, QColor(20, 30, 60));
        g = &con;
        break;
    }
    p.setPen(QPen(QColor(80, 80, 90), 1));
    p.setBrush(*g);
    p.drawRect(r);
}

void letter(QPainter &p, const QString &s, bool bold, bool italic, bool underline, bool strike,
            const QString &family = "Serif")
{
    QFont f(family);
    f.setPixelSize(13);
    f.setBold(bold);
    f.setItalic(italic);
    f.setUnderline(underline);
    f.setStrikeOut(strike);
    p.setFont(f);
    p.setPen(QColor(20, 30, 60));
    p.drawText(QRectF(0, 0, 16, 16), Qt::AlignCenter, s);
}

void alignLines(QPainter &p, int align)
{
    p.setPen(QPen(QColor(30, 40, 60), 1.2));
    p.setRenderHint(QPainter::Antialiasing, false);
    int widths[] = {12, 8, 12, 6};
    for (int i = 0; i < 4; ++i) {
        int w = widths[i];
        int x = align == 0 ? 2 : align == 1 ? 8 - w / 2 : 14 - w;
        p.drawLine(x, 3 + i * 3, x + w, 3 + i * 3);
    }
}

void drawIcon(QPainter &p, const QString &n)
{
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    if (n == "rectselect") {
        dashedRect(p, QRectF(2.5, 3.5, 11, 9));
    } else if (n == "ellipseselect") {
        QPainterPath e;
        e.addEllipse(QRectF(2, 2.5, 12, 11));
        dashedPath(p, e);
    } else if (n == "lasso") {
        QPainterPath e;
        e.moveTo(4, 12);
        e.cubicTo(-1, 9, 2, 2, 8.5, 2.5);
        e.cubicTo(15, 3, 15, 9, 9, 10);
        e.cubicTo(6, 10.5, 5, 9.5, 4, 12);
        dashedPath(p, e);
        p.setPen(QPen(QColor(150, 100, 50), 1.4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(4, 12), QPointF(3, 15));
    } else if (n == "move") {
        arrowCursor(p, QPointF(1.5, 1), 0.95, QColor(20, 20, 30));
        fourWay(p, QPointF(11.5, 11.5), 3, QColor(30, 90, 190));
    } else if (n == "moveselection") {
        arrowCursor(p, QPointF(1.5, 1), 0.95, Qt::white);
        fourWay(p, QPointF(11.5, 11.5), 3, QColor(30, 90, 190));
    } else if (n == "zoom") {
        magnifier(p, 0);
    } else if (n == "zoomin") {
        magnifier(p, 1);
    } else if (n == "zoomout") {
        magnifier(p, -1);
    } else if (n == "magicwand") {
        p.setPen(QPen(QColor(30, 30, 30), 2.4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(5.5, 5.5), QPointF(14, 14));
        p.setPen(QPen(Qt::white, 2.4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(4.5, 4.5), QPointF(6, 6));
        star(p, QPointF(4, 4), 3.4, QColor(255, 220, 40));
        star(p, QPointF(11, 3), 1.8, QColor(255, 200, 60));
        star(p, QPointF(3, 11), 1.6, QColor(255, 200, 60));
    } else if (n == "pan") {
        QColor skin(250, 222, 190), line(140, 100, 60);
        p.setPen(QPen(line, 0.9));
        p.setBrush(skin);
        QPainterPath hand;
        hand.addRoundedRect(QRectF(3.5, 7, 9, 7.5), 3, 3);
        for (int i = 0; i < 4; ++i)
            hand.addRoundedRect(QRectF(3.5 + i * 2.3, 2 + (i == 0 || i == 3 ? 1.5 : 0), 2.2, 8), 1.1, 1.1);
        hand.addRoundedRect(QRectF(0.8, 7.5, 5, 2.4), 1.2, 1.2);
        p.drawPath(hand.simplified());
    } else if (n == "bucket") {
        p.save();
        p.translate(7, 9);
        p.rotate(-35);
        p.setPen(QPen(QColor(60, 60, 70), 1));
        p.setBrush(vgrad(-5, 5, QColor(235, 235, 240), QColor(150, 150, 165)));
        p.drawRoundedRect(QRectF(-4.5, -4, 9, 9), 1.5, 1.5);
        p.setBrush(QColor(90, 90, 100));
        p.drawEllipse(QRectF(-4.5, -5.5, 9, 3));
        p.restore();
        QPainterPath drip;
        drip.moveTo(11.5, 5);
        drip.cubicTo(14, 6, 15, 9, 14.5, 12);
        drip.cubicTo(14, 14.5, 12, 14.5, 12, 12);
        drip.cubicTo(12, 10, 13, 8, 11.5, 5);
        p.setPen(QPen(QColor(20, 70, 160), 0.8));
        p.setBrush(QColor(40, 120, 230));
        p.drawPath(drip);
    } else if (n == "gradient") {
        QLinearGradient g(0, 0, 16, 16);
        g.setColorAt(0, QColor(10, 20, 120));
        g.setColorAt(1, QColor(80, 200, 255));
        p.setPen(QPen(QColor(30, 40, 70), 1));
        p.setBrush(g);
        p.drawRect(QRectF(1.5, 2.5, 13, 11));
    } else if (n == "brush") {
        p.setPen(QPen(QColor(110, 60, 20), 0.8));
        QPolygonF handle;
        handle << QPointF(14.5, 0.8) << QPointF(15.5, 1.8) << QPointF(8.5, 9.5) << QPointF(6.8, 7.8);
        p.setBrush(QColor(200, 130, 60));
        p.drawPolygon(handle);
        QPolygonF ferrule;
        ferrule << QPointF(6.8, 7.8) << QPointF(8.5, 9.5) << QPointF(7, 11) << QPointF(5.2, 9.3);
        p.setBrush(QColor(200, 200, 210));
        p.setPen(QPen(QColor(90, 90, 100), 0.8));
        p.drawPolygon(ferrule);
        QPainterPath tip;
        tip.moveTo(5.2, 9.3);
        tip.lineTo(7, 11);
        tip.cubicTo(6, 14, 3, 15, 0.8, 15.2);
        tip.cubicTo(1.5, 13, 2.5, 10.5, 5.2, 9.3);
        p.setBrush(QColor(30, 60, 140));
        p.setPen(QPen(QColor(10, 20, 60), 0.8));
        p.drawPath(tip);
    } else if (n == "eraser") {
        p.save();
        p.translate(8, 8);
        p.rotate(-40);
        p.setPen(QPen(QColor(120, 60, 70), 0.9));
        p.setBrush(QColor(245, 245, 250));
        p.drawRect(QRectF(-6.5, -3, 6, 6));
        p.setBrush(vgrad(-3, 3, QColor(255, 170, 180), QColor(220, 100, 120)));
        p.drawRect(QRectF(-0.5, -3, 7, 6));
        p.restore();
    } else if (n == "pencil") {
        p.save();
        p.translate(8, 8);
        p.rotate(-45);
        p.setPen(QPen(QColor(110, 80, 10), 0.8));
        p.setBrush(vgrad(-2, 2, QColor(255, 230, 90), QColor(230, 170, 20)));
        p.drawRect(QRectF(-4, -2, 11, 4));
        p.setBrush(QColor(240, 150, 160));
        p.drawRect(QRectF(7, -2, 2, 4));
        QPolygonF tip;
        tip << QPointF(-4, -2) << QPointF(-8.5, 0) << QPointF(-4, 2);
        p.setBrush(QColor(245, 215, 170));
        p.drawPolygon(tip);
        p.setBrush(QColor(40, 40, 40));
        p.setPen(Qt::NoPen);
        QPolygonF lead;
        lead << QPointF(-6.8, -0.75) << QPointF(-8.5, 0) << QPointF(-6.8, 0.75);
        p.drawPolygon(lead);
        p.restore();
    } else if (n == "colorpicker") {
        p.save();
        p.translate(8, 8);
        p.rotate(-45);
        p.setPen(QPen(QColor(20, 40, 90), 0.8));
        p.setBrush(QColor(40, 90, 200));
        p.drawRoundedRect(QRectF(3, -2.6, 5.5, 5.2), 2, 2);
        p.setBrush(QColor(30, 30, 40));
        p.drawRect(QRectF(2, -1.8, 1.4, 3.6));
        p.setBrush(QColor(210, 230, 250));
        p.setPen(QPen(QColor(80, 100, 130), 0.8));
        QPolygonF tube;
        tube << QPointF(2, -1.1) << QPointF(-6, -0.7) << QPointF(-8, 0) << QPointF(-6, 0.7) << QPointF(2, 1.1);
        p.drawPolygon(tube);
        p.restore();
    } else if (n == "clonestamp") {
        p.setPen(QPen(QColor(110, 60, 10), 0.8));
        p.setBrush(vgrad(1, 8, QColor(250, 190, 90), QColor(210, 120, 30)));
        p.drawEllipse(QRectF(5, 1, 6, 4.5));
        p.drawRect(QRectF(6.8, 5, 2.4, 4));
        p.setBrush(vgrad(9, 13, QColor(250, 190, 90), QColor(200, 110, 20)));
        p.drawRoundedRect(QRectF(2, 9, 12, 3.5), 1, 1);
        p.setBrush(QColor(60, 60, 70));
        p.drawRect(QRectF(2.5, 12.5, 11, 2));
    } else if (n == "recolor") {
        p.setPen(QPen(QColor(20, 40, 100), 0.8));
        p.setBrush(QColor(50, 110, 230));
        p.drawEllipse(QRectF(1.5, 6.5, 8, 8));
        p.setPen(QPen(QColor(120, 10, 10), 0.8));
        p.setBrush(QColor(230, 40, 40));
        p.drawEllipse(QRectF(6.5, 1.5, 8, 8));
        p.setPen(QPen(Qt::white, 1.2));
        p.drawArc(QRectF(3, 3, 10, 10), 30 * 16, 120 * 16);
    } else if (n == "text") {
        QFont f("Serif");
        f.setPixelSize(15);
        f.setBold(true);
        p.setFont(f);
        p.setPen(QColor(20, 30, 60));
        p.drawText(QRectF(0, 0, 16, 16), Qt::AlignCenter, "A");
    } else if (n == "line") {
        QPainterPath c;
        c.moveTo(3, 13);
        c.cubicTo(3, 4, 13, 12, 13, 3);
        p.setPen(QPen(QColor(20, 30, 60), 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawPath(c);
        p.setPen(QPen(QColor(20, 30, 60), 0.8));
        p.setBrush(Qt::white);
        p.drawRect(QRectF(1.5, 11.5, 3, 3));
        p.drawRect(QRectF(11.5, 1.5, 3, 3));
    } else if (n == "rectangle") {
        p.setPen(QPen(QColor(20, 40, 90), 1));
        p.setBrush(vgrad(3, 13, QColor(120, 170, 240), QColor(50, 100, 200)));
        p.drawRect(QRectF(1.5, 3.5, 13, 9));
    } else if (n == "roundrect") {
        p.setPen(QPen(QColor(20, 40, 90), 1));
        p.setBrush(vgrad(3, 13, QColor(120, 170, 240), QColor(50, 100, 200)));
        p.drawRoundedRect(QRectF(1.5, 3.5, 13, 9), 3.5, 3.5);
    } else if (n == "ellipse") {
        p.setPen(QPen(QColor(20, 40, 90), 1));
        p.setBrush(vgrad(3, 13, QColor(120, 170, 240), QColor(50, 100, 200)));
        p.drawEllipse(QRectF(1.5, 3.5, 13, 9));
    } else if (n == "freeform") {
        QPainterPath f;
        f.moveTo(2, 13);
        f.cubicTo(1, 6, 4, 2, 8, 3);
        f.cubicTo(11, 4, 9, 7, 12, 7);
        f.cubicTo(15, 7, 15, 13, 12, 13.5);
        f.closeSubpath();
        p.setPen(QPen(QColor(20, 40, 90), 1));
        p.setBrush(vgrad(3, 13, QColor(120, 170, 240), QColor(50, 100, 200)));
        p.drawPath(f);
    } else if (n == "new") {
        page(p, QRectF(3.5, 1.5, 10, 13));
        star(p, QPointF(4, 4), 3.5, QColor(255, 210, 40));
    } else if (n == "open") {
        p.setPen(QPen(QColor(150, 110, 20), 0.9));
        p.setBrush(QColor(240, 200, 90));
        p.drawRect(QRectF(1.5, 3.5, 6, 2));
        p.drawRect(QRectF(1.5, 4.5, 12, 9.5));
        QPolygonF front;
        front << QPointF(3.5, 7.5) << QPointF(15.5, 7.5) << QPointF(13.5, 14) << QPointF(1.5, 14);
        p.setBrush(vgrad(7, 14, QColor(255, 230, 140), QColor(240, 190, 70)));
        p.drawPolygon(front);
    } else if (n == "save") {
        p.setPen(QPen(QColor(20, 40, 100), 0.9));
        p.setBrush(vgrad(1, 15, QColor(90, 120, 210), QColor(40, 60, 150)));
        p.drawRoundedRect(QRectF(1.5, 1.5, 13, 13), 1.5, 1.5);
        p.setBrush(Qt::white);
        p.setPen(Qt::NoPen);
        p.drawRect(QRectF(3.5, 2, 9, 5.5));
        p.setBrush(QColor(200, 205, 215));
        p.drawRect(QRectF(4.5, 10, 7, 4.5));
        p.setBrush(QColor(40, 60, 150));
        p.drawRect(QRectF(5.5, 11, 1.8, 3));
    } else if (n == "print") {
        p.setPen(QPen(QColor(70, 70, 80), 0.9));
        p.setBrush(Qt::white);
        p.drawRect(QRectF(4.5, 1.5, 7, 5));
        p.setBrush(vgrad(6, 12, QColor(225, 225, 230), QColor(150, 150, 160)));
        p.drawRoundedRect(QRectF(1.5, 6.5, 13, 6), 1.5, 1.5);
        p.setBrush(Qt::white);
        p.drawRect(QRectF(4.5, 10.5, 7, 4));
        p.setPen(QPen(QColor(60, 200, 60), 1.2));
        p.drawPoint(QPointF(12.5, 8.5));
    } else if (n == "cut") {
        p.setPen(QPen(QColor(90, 90, 100), 1.3));
        p.drawLine(QPointF(4, 1.5), QPointF(10.5, 10));
        p.drawLine(QPointF(12, 1.5), QPointF(5.5, 10));
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(30, 30, 30), 1.4));
        p.drawEllipse(QPointF(4.5, 12), 2.5, 2.5);
        p.drawEllipse(QPointF(11.5, 12), 2.5, 2.5);
    } else if (n == "copy") {
        page(p, QRectF(1.5, 1.5, 8.5, 10.5));
        page(p, QRectF(6, 4.5, 8.5, 10.5));
    } else if (n == "paste") {
        p.setPen(QPen(QColor(110, 70, 20), 0.9));
        p.setBrush(vgrad(2, 15, QColor(220, 160, 90), QColor(170, 110, 40)));
        p.drawRoundedRect(QRectF(1.5, 2.5, 10, 12), 1, 1);
        p.setBrush(QColor(190, 190, 200));
        p.setPen(QPen(QColor(80, 80, 90), 0.8));
        p.drawRoundedRect(QRectF(4, 1, 5, 3), 1, 1);
        page(p, QRectF(6.5, 5.5, 8, 9.5));
    } else if (n == "crop") {
        p.setPen(QPen(QColor(30, 30, 40), 1.6));
        p.drawLine(QPointF(4, 1), QPointF(4, 12));
        p.drawLine(QPointF(4, 12), QPointF(15, 12));
        p.drawLine(QPointF(1, 4), QPointF(12, 4));
        p.drawLine(QPointF(12, 4), QPointF(12, 15));
        p.setPen(QPen(QColor(200, 30, 30), 1));
        p.drawLine(QPointF(5.5, 10.5), QPointF(10.5, 5.5));
    } else if (n == "deselect") {
        dashedRect(p, QRectF(1.5, 1.5, 10, 9));
        p.setPen(QPen(QColor(210, 30, 30), 2, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(9, 9), QPointF(14, 14));
        p.drawLine(QPointF(14, 9), QPointF(9, 14));
    } else if (n == "selectall") {
        dashedRect(p, QRectF(1.5, 1.5, 13, 13));
        p.fillRect(QRectF(4, 4, 8, 8), QColor(110, 170, 240, 160));
    } else if (n == "invertselection") {
        p.fillRect(QRectF(1.5, 1.5, 13, 13), QColor(110, 170, 240, 160));
        p.fillRect(QRectF(5, 5, 6, 6), Qt::white);
        dashedRect(p, QRectF(4.5, 4.5, 7, 7));
    } else if (n == "erase") {
        dashedRect(p, QRectF(1.5, 1.5, 13, 13));
        p.setPen(QPen(QColor(210, 30, 30), 2, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(5, 5), QPointF(11, 11));
        p.drawLine(QPointF(11, 5), QPointF(5, 11));
    } else if (n == "fill") {
        p.fillRect(QRectF(2, 2, 12, 12), QColor(60, 120, 220));
        dashedRect(p, QRectF(1.5, 1.5, 13, 13));
    } else if (n == "undo") {
        curvedArrow(p, true, QColor(30, 90, 200));
    } else if (n == "redo") {
        curvedArrow(p, false, QColor(30, 90, 200));
    } else if (n == "grid") {
        p.setRenderHint(QPainter::Antialiasing, false);
        p.fillRect(QRect(1, 1, 14, 14), Qt::white);
        p.setPen(QColor(60, 70, 90));
        for (int i = 0; i <= 3; ++i) {
            int v = 1 + i * 14 / 3;
            p.drawLine(v, 1, v, 15);
            p.drawLine(1, v, 15, v);
        }
    } else if (n == "tile") {
        p.setRenderHint(QPainter::Antialiasing, false);
        for (int i = 0; i < 4; ++i) {
            QRect r(1 + (i % 2) * 7, 1 + (i / 2) * 7, 7, 7);
            p.fillRect(r, i == 0 ? QColor(90, 150, 230) : QColor(170, 200, 240));
            p.setPen(QColor(40, 60, 100));
            p.drawRect(r.adjusted(0, 0, -1, -1));
        }
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(QColor(220, 60, 40), 1.4));
        p.drawLine(QPointF(4, 4.5), QPointF(11, 4.5));
        p.drawLine(QPointF(4, 11.5), QPointF(11, 11.5));
    } else if (n == "animation") {
        p.setPen(QPen(QColor(40, 40, 50), 0.9));
        p.setBrush(QColor(60, 60, 70));
        p.drawRect(QRectF(1.5, 2.5, 13, 11));
        p.setBrush(QColor(150, 200, 250));
        p.drawRect(QRectF(4.5, 4.5, 7, 7));
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        for (int i = 0; i < 4; ++i) {
            p.drawRect(QRectF(2.3, 3.3 + i * 2.6, 1.4, 1.4));
            p.drawRect(QRectF(12.3, 3.3 + i * 2.6, 1.4, 1.4));
        }
        p.setBrush(QColor(30, 90, 200));
        QPolygonF t;
        t << QPointF(6.5, 5.8) << QPointF(10, 8) << QPointF(6.5, 10.2);
        p.drawPolygon(t);
    } else if (n == "play" || n == "pause" || n == "stepback" || n == "stepfwd") {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(30, 90, 200));
        if (n == "play") {
            QPolygonF t;
            t << QPointF(4, 2.5) << QPointF(13.5, 8) << QPointF(4, 13.5);
            p.drawPolygon(t);
        } else if (n == "pause") {
            p.drawRect(QRectF(4, 3, 3, 10));
            p.drawRect(QRectF(9, 3, 3, 10));
        } else {
            p.save();
            if (n == "stepback") {
                p.translate(16, 0);
                p.scale(-1, 1);
            }
            QPolygonF t;
            t << QPointF(3, 3) << QPointF(10, 8) << QPointF(3, 13);
            p.drawPolygon(t);
            p.drawRect(QRectF(11, 3, 2.2, 10));
            p.restore();
        }
    } else if (n == "cube") {
        QPolygonF top, left, right;
        top << QPointF(8, 1.5) << QPointF(14, 4.5) << QPointF(8, 7.5) << QPointF(2, 4.5);
        left << QPointF(2, 4.5) << QPointF(8, 7.5) << QPointF(8, 14.5) << QPointF(2, 11.5);
        right << QPointF(8, 7.5) << QPointF(14, 4.5) << QPointF(14, 11.5) << QPointF(8, 14.5);
        p.setPen(QPen(QColor(30, 50, 30), 0.8));
        p.setBrush(QColor(120, 200, 90));
        p.drawPolygon(top);
        p.setBrush(QColor(150, 105, 60));
        p.drawPolygon(left);
        p.setBrush(QColor(110, 75, 40));
        p.drawPolygon(right);
    } else if (n == "normalmap" || n == "fx_maps") {
        QLinearGradient g(0, 0, 16, 16);
        g.setColorAt(0, QColor(90, 120, 255));
        g.setColorAt(0.5, QColor(128, 128, 255));
        g.setColorAt(1, QColor(190, 140, 230));
        p.setPen(QPen(QColor(40, 40, 110), 0.9));
        p.setBrush(g);
        p.drawRoundedRect(QRectF(1.5, 1.5, 13, 13), 2, 2);
        p.setPen(QPen(QColor(255, 255, 255, 200), 1.2));
        p.drawLine(QPointF(8, 11), QPointF(8, 4));
        p.drawLine(QPointF(8, 4), QPointF(6, 6));
        p.drawLine(QPointF(8, 4), QPointF(10, 6));
    } else if (n == "plugin") {
        p.setPen(QPen(QColor(40, 90, 40), 0.9));
        p.setBrush(QColor(110, 190, 90));
        QPainterPath pc;
        pc.addRoundedRect(QRectF(2, 5, 9, 9), 1.5, 1.5);
        pc.addEllipse(QRectF(4.5, 1.5, 4, 4));
        pc.addEllipse(QRectF(9.5, 7.5, 4, 4));
        p.drawPath(pc.simplified());
    } else if (n == "rulers") {
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(QColor(120, 90, 30));
        p.setBrush(QColor(250, 225, 140));
        p.drawRect(1, 1, 14, 4);
        p.drawRect(1, 1, 4, 14);
        for (int i = 6; i < 15; i += 2) {
            p.drawLine(i, 1, i, 3);
            p.drawLine(1, i, 3, i);
        }
    } else if (n == "close") {
        p.setPen(QPen(QColor(60, 60, 60), 1.6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(4, 4), QPointF(12, 12));
        p.drawLine(QPointF(12, 4), QPointF(4, 12));
    } else if (n == "first" || n == "last") {
        p.save();
        if (n == "last") {
            p.translate(16, 0);
            p.scale(-1, 1);
        }
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(30, 90, 200));
        p.drawRect(QRectF(2, 3, 2, 10));
        QPolygonF t1, t2;
        t1 << QPointF(4, 8) << QPointF(9, 3) << QPointF(9, 13);
        t2 << QPointF(9, 8) << QPointF(14, 3) << QPointF(14, 13);
        p.drawPolygon(t1);
        p.drawPolygon(t2);
        p.restore();
    } else if (n == "layeradd") {
        page(p, QRectF(1.5, 1.5, 10, 12.5));
        p.setPen(QPen(QColor(20, 140, 30), 2.4));
        p.drawLine(QPointF(11.5, 8), QPointF(11.5, 15));
        p.drawLine(QPointF(8, 11.5), QPointF(15, 11.5));
    } else if (n == "layerdelete") {
        p.setPen(QPen(QColor(200, 30, 30), 2.6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(3.5, 3.5), QPointF(12.5, 12.5));
        p.drawLine(QPointF(12.5, 3.5), QPointF(3.5, 12.5));
    } else if (n == "layerduplicate") {
        page(p, QRectF(1.5, 1.5, 9, 11));
        page(p, QRectF(5.5, 4.5, 9, 11), QColor(220, 235, 255));
    } else if (n == "layermerge") {
        page(p, QRectF(2.5, 7.5, 11, 7));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(30, 90, 200));
        QPolygonF a;
        a << QPointF(8, 8) << QPointF(12, 4) << QPointF(9.5, 4) << QPointF(9.5, 1) << QPointF(6.5, 1)
          << QPointF(6.5, 4) << QPointF(4, 4);
        p.drawPolygon(a);
    } else if (n == "layerup") {
        bigArrow(p, true, QColor(40, 110, 220));
    } else if (n == "layerdown") {
        bigArrow(p, false, QColor(40, 110, 220));
    } else if (n == "layerprops") {
        page(p, QRectF(1.5, 1.5, 10, 12.5));
        p.save();
        p.translate(11, 11);
        p.rotate(45);
        p.setPen(QPen(QColor(60, 60, 70), 0.8));
        p.setBrush(QColor(180, 180, 190));
        p.drawRect(QRectF(-1.2, -5, 2.4, 9));
        p.drawEllipse(QRectF(-2.5, -6.5, 5, 3.5));
        p.restore();
    } else if (n == "layers") {
        layerStack(p, QPointF(2, 3), QColor(80, 140, 230));
    } else if (n == "history") {
        p.setPen(QPen(QColor(40, 50, 80), 1.2));
        p.setBrush(Qt::white);
        p.drawEllipse(QRectF(1.5, 1.5, 13, 13));
        p.drawLine(QPointF(8, 8), QPointF(8, 4));
        p.drawLine(QPointF(8, 8), QPointF(11, 9.5));
    } else if (n == "colors") {
        QConicalGradient g(QPointF(8, 8), 0);
        for (int i = 0; i <= 6; ++i)
            g.setColorAt(i / 6.0, QColor::fromHsv((i * 60) % 360, 255, 255));
        p.setPen(QPen(QColor(60, 60, 60), 0.8));
        p.setBrush(g);
        p.drawEllipse(QRectF(1.5, 1.5, 13, 13));
    } else if (n == "tools") {
        p.save();
        drawIcon(p, "brush");
        p.restore();
    } else if (n == "swap") {
        p.setPen(QPen(QColor(30, 90, 200), 1.3));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(2, 3, 11, 11), 0, 90 * 16);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(30, 90, 200));
        QPolygonF a, b;
        a << QPointF(5, 1) << QPointF(5, 6.5) << QPointF(2, 3.5);
        b << QPointF(10.5, 9) << QPointF(16, 9) << QPointF(13, 12);
        p.drawPolygon(a);
        p.drawPolygon(b);
    } else if (n == "resetcolors") {
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(QColor(90, 90, 90));
        p.setBrush(Qt::white);
        p.drawRect(5, 5, 8, 8);
        p.setBrush(Qt::black);
        p.drawRect(2, 2, 8, 8);
    } else if (n == "addpalette") {
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(QColor(60, 60, 60));
        p.setBrush(QColor(40, 40, 40));
        p.drawRect(2, 2, 10, 10);
        p.setPen(QPen(QColor(30, 160, 40), 2));
        p.drawLine(12, 9, 12, 15);
        p.drawLine(9, 12, 15, 12);
    } else if (n == "palette") {
        QPainterPath pal;
        pal.addEllipse(QRectF(1, 2, 14, 12));
        QPainterPath hole;
        hole.addEllipse(QRectF(9, 9, 3, 3));
        p.setPen(QPen(QColor(130, 90, 40), 0.9));
        p.setBrush(QColor(240, 210, 150));
        p.drawPath(pal.subtracted(hole));
        QColor cs[] = {Qt::red, QColor(30, 160, 40), Qt::blue, QColor(250, 200, 0)};
        QPointF pts[] = {{4.5, 6}, {7.5, 4.5}, {11, 5.5}, {4.5, 10}};
        p.setPen(Qt::NoPen);
        for (int i = 0; i < 4; ++i) {
            p.setBrush(cs[i]);
            p.drawEllipse(pts[i], 1.5, 1.5);
        }
    } else if (n == "selreplace") {
        twoShapes(p, 0);
    } else if (n == "selunion") {
        twoShapes(p, 1);
    } else if (n == "selexclude") {
        twoShapes(p, 2);
    } else if (n == "selintersect") {
        twoShapes(p, 3);
    } else if (n == "selxor") {
        twoShapes(p, 4);
    } else if (n == "shapeoutline") {
        p.setPen(QPen(QColor(30, 50, 110), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(2, 3, 12, 10));
    } else if (n == "shapefill") {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(60, 120, 220));
        p.drawRect(QRectF(2, 3, 12, 10));
    } else if (n == "shapeboth") {
        p.setPen(QPen(QColor(30, 50, 110), 2));
        p.setBrush(QColor(140, 190, 250));
        p.drawRect(QRectF(2, 3, 12, 10));
    } else if (n.startsWith("grad") && n.size() > 4 && n != "gradient") {
        static const QStringList types = {"gradlinear", "gradreflected", "graddiamond", "gradradial", "gradconical"};
        int t = types.indexOf(n);
        if (t >= 0) {
            gradientIcon(p, t);
        } else if (n == "gradcolor") {
            QLinearGradient g(0, 0, 16, 0);
            g.setColorAt(0, QColor(220, 30, 30));
            g.setColorAt(1, QColor(30, 60, 220));
            p.setPen(QPen(QColor(60, 60, 70), 1));
            p.setBrush(g);
            p.drawRect(QRectF(1.5, 3.5, 13, 9));
        } else if (n == "gradtransparency") {
            p.drawPixmap(QRect(2, 4, 12, 8), Icons::checkerboard(2));
            QLinearGradient g(0, 0, 16, 0);
            g.setColorAt(0, QColor(30, 30, 40, 255));
            g.setColorAt(1, QColor(30, 30, 40, 0));
            p.setPen(QPen(QColor(60, 60, 70), 1));
            p.setBrush(g);
            p.drawRect(QRectF(1.5, 3.5, 13, 9));
        }
    } else if (n == "aa_on" || n == "aa_off") {
        p.setRenderHint(QPainter::Antialiasing, n == "aa_on");
        p.setPen(QPen(QColor(20, 30, 60), 1.6));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QRectF(2.5, 2.5, 11, 11));
    } else if (n == "blend_on" || n == "blend_off") {
        p.drawPixmap(QRect(1, 1, 14, 14), Icons::checkerboard(2));
        QColor c(40, 100, 220, n == "blend_on" ? 140 : 255);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawEllipse(QRectF(3.5, 3.5, 9, 9));
    } else if (n == "bold") {
        letter(p, "B", true, false, false, false, "Sans");
    } else if (n == "italic") {
        letter(p, "I", false, true, false, false, "Serif");
    } else if (n == "underline") {
        letter(p, "U", false, false, true, false, "Sans");
    } else if (n == "strike") {
        letter(p, "S", false, false, false, true, "Sans");
    } else if (n == "alignleft") {
        alignLines(p, 0);
    } else if (n == "aligncenter") {
        alignLines(p, 1);
    } else if (n == "alignright") {
        alignLines(p, 2);
    } else if (n == "fliph" || n == "flipv") {
        p.save();
        if (n == "flipv") {
            p.translate(16, 0);
            p.rotate(90);
        }
        p.setPen(QPen(QColor(40, 40, 50), 0.8, Qt::DashLine));
        p.drawLine(QPointF(8, 1), QPointF(8, 15));
        p.setPen(QPen(QColor(20, 40, 90), 0.9));
        QPolygonF l, r;
        l << QPointF(6.5, 3) << QPointF(6.5, 13) << QPointF(1.5, 13);
        r << QPointF(9.5, 3) << QPointF(9.5, 13) << QPointF(14.5, 13);
        p.setBrush(QColor(60, 120, 220));
        p.drawPolygon(l);
        p.setBrush(Qt::white);
        p.drawPolygon(r);
        p.restore();
    } else if (n == "rotatecw" || n == "rotateccw" || n == "rotate180") {
        p.save();
        if (n == "rotateccw") {
            p.translate(16, 0);
            p.scale(-1, 1);
        }
        p.setPen(QPen(QColor(20, 40, 90), 0.9));
        p.setBrush(QColor(200, 220, 250));
        p.drawRect(QRectF(1.5, 6.5, 8, 8));
        p.setPen(QPen(QColor(30, 90, 200), 1.5));
        p.setBrush(Qt::NoBrush);
        int span = n == "rotate180" ? 180 : 90;
        p.drawArc(QRectF(4, 2, 9, 9), (90 - span + 90) * 16 - 90 * 16, span * 16);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(30, 90, 200));
        QPolygonF h;
        h << QPointF(14.5, 5.5) << QPointF(10.5, 5.5) << QPointF(12.5, 9);
        p.drawPolygon(h);
        p.restore();
    } else if (n == "resize") {
        p.setPen(QPen(QColor(20, 40, 90), 0.9));
        p.setBrush(QColor(200, 220, 250));
        p.drawRect(QRectF(1.5, 6.5, 8, 8));
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(20, 40, 90), 0.9, Qt::DashLine));
        p.drawRect(QRectF(1.5, 1.5, 13, 13));
        p.setPen(QPen(QColor(30, 90, 200), 1.4));
        p.drawLine(QPointF(8, 8), QPointF(13, 3));
        p.drawLine(QPointF(13, 3), QPointF(10, 3));
        p.drawLine(QPointF(13, 3), QPointF(13, 6));
    } else if (n == "canvassize") {
        p.setPen(QPen(QColor(20, 40, 90), 0.9));
        p.setBrush(QColor(255, 255, 255));
        p.drawRect(QRectF(1.5, 1.5, 13, 13));
        p.setBrush(QColor(80, 140, 230));
        p.drawRect(QRectF(5.5, 5.5, 5, 5));
    } else if (n == "flatten") {
        layerStack(p, QPointF(2, 1), QColor(80, 140, 230));
        p.setPen(QPen(QColor(30, 30, 40), 1.2));
        p.drawLine(QPointF(2, 14.5), QPointF(14, 14.5));
    } else if (n == "adjust") {
        p.setPen(QPen(QColor(40, 40, 50), 0.8));
        p.setBrush(QColor(255, 255, 255));
        p.drawEllipse(QRectF(2.5, 2.5, 11, 11));
        p.setBrush(QColor(30, 30, 30));
        QPainterPath half;
        half.moveTo(8, 2.5);
        half.arcTo(QRectF(2.5, 2.5, 11, 11), 90, 180);
        half.closeSubpath();
        p.drawPath(half);
    } else if (n == "invertcolors") {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(250, 160, 40));
        p.drawRect(QRectF(1.5, 1.5, 6.5, 13));
        p.setBrush(QColor(5, 95, 215));
        p.drawRect(QRectF(8, 1.5, 6.5, 13));
    } else if (n == "blackwhite") {
        QLinearGradient g(0, 0, 16, 0);
        g.setColorAt(0, Qt::black);
        g.setColorAt(1, Qt::white);
        p.setPen(QPen(QColor(60, 60, 60), 0.8));
        p.setBrush(g);
        p.drawRect(QRectF(1.5, 2.5, 13, 11));
    } else if (n == "sepia") {
        QLinearGradient g(0, 0, 16, 0);
        g.setColorAt(0, QColor(60, 40, 20));
        g.setColorAt(1, QColor(240, 220, 180));
        p.setPen(QPen(QColor(80, 60, 30), 0.8));
        p.setBrush(g);
        p.drawRect(QRectF(1.5, 2.5, 13, 11));
    } else if (n == "curves") {
        p.fillRect(QRectF(1.5, 1.5, 13, 13), Qt::white);
        p.setPen(QPen(QColor(120, 120, 120), 0.6));
        p.drawRect(QRectF(1.5, 1.5, 13, 13));
        QPainterPath c;
        c.moveTo(2, 14);
        c.cubicTo(6, 13, 8, 3, 14, 2);
        p.setPen(QPen(QColor(20, 30, 60), 1.3));
        p.drawPath(c);
    } else if (n == "levels") {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(60, 60, 70));
        int hs[] = {3, 6, 9, 12, 10, 7, 11, 8, 5, 3, 2};
        for (int i = 0; i < 11; ++i)
            p.drawRect(QRectF(2 + i * 1.1, 14 - hs[i], 1.1, hs[i]));
    } else if (n == "huesat") {
        QLinearGradient g(0, 0, 16, 0);
        for (int i = 0; i <= 6; ++i)
            g.setColorAt(i / 6.0, QColor::fromHsv((i * 60) % 360, 255, 255));
        p.setPen(QPen(QColor(60, 60, 60), 0.8));
        p.setBrush(g);
        p.drawRect(QRectF(1.5, 4.5, 13, 7));
    } else if (n == "brightness") {
        p.setPen(QPen(QColor(230, 170, 0), 1.2));
        for (int i = 0; i < 8; ++i) {
            qreal a = i * M_PI / 4;
            p.drawLine(QPointF(8 + std::cos(a) * 5, 8 + std::sin(a) * 5),
                       QPointF(8 + std::cos(a) * 7, 8 + std::sin(a) * 7));
        }
        p.setBrush(QColor(255, 220, 60));
        p.drawEllipse(QPointF(8, 8), 3.5, 3.5);
    } else if (n == "posterize") {
        QColor cs[] = {QColor(200, 40, 40), QColor(240, 180, 30), QColor(40, 160, 60), QColor(40, 90, 210)};
        for (int i = 0; i < 4; ++i)
            p.fillRect(QRectF(1.5 + (i % 2) * 6.5, 1.5 + (i / 2) * 6.5, 6.5, 6.5), cs[i]);
    } else if (n == "autolevel") {
        drawIcon(p, "levels");
        star(p, QPointF(12, 4), 3, QColor(255, 210, 40));
    } else if (n.startsWith("fx_")) {
        // effect category icons
        QString c = n.mid(3);
        if (c == "artistic") {
            drawIcon(p, "palette");
        } else if (c == "blurs") {
            for (int i = 0; i < 4; ++i) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(60, 120, 220, 60 + i * 40));
                p.drawEllipse(QPointF(8, 8), 7 - i * 1.5, 7 - i * 1.5);
            }
        } else if (c == "distort") {
            p.setPen(QPen(QColor(30, 60, 140), 1));
            p.setBrush(Qt::NoBrush);
            for (int i = 0; i < 4; ++i) {
                QPainterPath w;
                w.moveTo(1, 3 + i * 3.3);
                w.cubicTo(5, 0 + i * 3.3, 10, 6 + i * 3.3, 15, 3 + i * 3.3);
                p.drawPath(w);
            }
        } else if (c == "noise") {
            p.fillRect(QRectF(1.5, 1.5, 13, 13), QColor(220, 220, 225));
            unsigned s = 12345;
            for (int i = 0; i < 40; ++i) {
                s = s * 1103515245 + 12345;
                int x = 2 + (s >> 8) % 12;
                int y = 2 + (s >> 16) % 12;
                p.fillRect(QRectF(x, y, 1, 1), QColor::fromHsv((s >> 4) % 360, 200, 120));
            }
        } else if (c == "photo") {
            p.setPen(QPen(QColor(40, 40, 50), 0.9));
            p.setBrush(vgrad(4, 14, QColor(120, 120, 130), QColor(50, 50, 60)));
            p.drawRoundedRect(QRectF(1.5, 4.5, 13, 9), 1.5, 1.5);
            p.drawRect(QRectF(5, 2.5, 5, 2));
            p.setBrush(QColor(150, 200, 250));
            p.drawEllipse(QPointF(8, 9), 3, 3);
        } else if (c == "render") {
            QRadialGradient g(QPointF(6, 6), 9);
            g.setColorAt(0, Qt::white);
            g.setColorAt(1, QColor(80, 140, 230));
            p.setPen(Qt::NoPen);
            p.setBrush(g);
            p.drawEllipse(QRectF(2, 3, 12, 10));
        } else if (c == "stylize") {
            p.setPen(QPen(QColor(90, 90, 100), 1));
            p.setBrush(vgrad(1, 15, QColor(240, 240, 245), QColor(150, 150, 160)));
            p.drawRect(QRectF(1.5, 1.5, 13, 13));
            QFont f("Serif");
            f.setPixelSize(12);
            f.setBold(true);
            p.setFont(f);
            p.setPen(QColor(255, 255, 255));
            p.drawText(QRectF(1, 1, 16, 16), Qt::AlignCenter, "S");
            p.setPen(QColor(60, 60, 70));
            p.drawText(QRectF(0, 0, 16, 16), Qt::AlignCenter, "S");
        } else {
            star(p, QPointF(8, 8), 6, QColor(255, 200, 40));
        }
    } else if (n == "effects") {
        star(p, QPointF(8, 8), 6.5, QColor(255, 200, 40));
    } else if (n == "image") {
        p.setPen(QPen(QColor(60, 70, 90), 0.9));
        p.setBrush(vgrad(2, 14, QColor(150, 200, 250), QColor(220, 240, 255)));
        p.drawRect(QRectF(1.5, 2.5, 13, 11));
        QPolygonF m;
        m << QPointF(2, 13) << QPointF(6, 7) << QPointF(9, 10) << QPointF(11, 8) << QPointF(14, 13);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(60, 140, 60));
        p.drawPolygon(m);
        p.setBrush(QColor(255, 220, 60));
        p.drawEllipse(QPointF(11, 5.5), 1.5, 1.5);
    } else if (n == "help") {
        p.setPen(QPen(QColor(20, 60, 150), 0.9));
        p.setBrush(vgrad(1, 15, QColor(110, 160, 240), QColor(30, 80, 190)));
        p.drawEllipse(QRectF(1.5, 1.5, 13, 13));
        QFont f("Sans");
        f.setPixelSize(11);
        f.setBold(true);
        p.setFont(f);
        p.setPen(Qt::white);
        p.drawText(QRectF(0, 0, 16, 16), Qt::AlignCenter, "?");
    } else if (n == "finish") {
        p.setPen(QPen(QColor(30, 150, 40), 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPolyline(QPolygonF() << QPointF(3, 8.5) << QPointF(6.5, 12) << QPointF(13, 4));
    } else if (n == "rotatezoom") {
        drawIcon(p, "rotatecw");
    } else if (n == "importlayer") {
        drawIcon(p, "open");
        p.setPen(QPen(QColor(20, 140, 30), 2));
        p.drawLine(QPointF(12, 0.5), QPointF(12, 6.5));
        p.drawLine(QPointF(9, 3.5), QPointF(15, 3.5));
    } else if (n == "windows") {
        p.setPen(QPen(QColor(50, 60, 90), 0.9));
        p.setBrush(Qt::white);
        p.drawRect(QRectF(1.5, 2.5, 13, 11));
        p.fillRect(QRectF(2, 3, 12, 2.5), QColor(80, 130, 210));
    } else {
        // generic fallback
        p.setPen(QPen(QColor(90, 90, 100), 1));
        p.setBrush(QColor(230, 230, 235));
        p.drawRect(QRectF(2.5, 2.5, 11, 11));
    }
}

} // namespace

namespace Icons {

QPixmap pixmap(const QString &name, int size)
{
    static QHash<QString, QPixmap> cache;
    QString key = name + QString::number(size);
    auto it = cache.find(key);
    if (it != cache.end())
        return *it;
    const int scale = 2;
    QPixmap pm(size * scale, size * scale);
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.scale(size * scale / 16.0, size * scale / 16.0);
        drawIcon(p, name);
    }
    pm.setDevicePixelRatio(scale);
    cache.insert(key, pm);
    return pm;
}

QIcon get(const QString &name)
{
    static QHash<QString, QIcon> cache;
    auto it = cache.find(name);
    if (it != cache.end())
        return *it;
    QIcon icon;
    icon.addPixmap(pixmap(name, 16));
    icon.addPixmap(pixmap(name, 24));
    // disabled state: desaturated & faded
    QImage img = pixmap(name, 16).toImage().convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y) {
        QRgb *l = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            int g = qGray(l[x]);
            g = 128 + g / 2;
            l[x] = qRgba(g, g, g, qAlpha(l[x]) * 2 / 5);
        }
    }
    QPixmap dis = QPixmap::fromImage(img);
    dis.setDevicePixelRatio(2);
    icon.addPixmap(dis, QIcon::Disabled);
    cache.insert(name, icon);
    return icon;
}

QCursor toolCursor(const QString &iconName, bool crosshair)
{
    const int s = 2;
    QPixmap pm(32 * s, 32 * s);
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.scale(s, s);
        if (crosshair) {
            p.setRenderHint(QPainter::Antialiasing, false);
            p.setPen(QPen(Qt::white, 3));
            p.drawLine(5, 0, 5, 3);
            p.drawLine(5, 7, 5, 10);
            p.drawLine(0, 5, 3, 5);
            p.drawLine(7, 5, 10, 5);
            p.setPen(QPen(Qt::black, 1));
            p.drawLine(5, 0, 5, 3);
            p.drawLine(5, 7, 5, 10);
            p.drawLine(0, 5, 3, 5);
            p.drawLine(7, 5, 10, 5);
            p.drawPoint(5, 5);
            p.drawPixmap(QRect(10, 10, 16, 16), pixmap(iconName, 16));
        } else {
            p.drawPixmap(QRect(0, 0, 16, 16), pixmap(iconName, 16));
        }
    }
    pm.setDevicePixelRatio(s);
    return crosshair ? QCursor(pm, 5, 5) : QCursor(pm, 0, 0);
}

QPixmap checkerboard(int cell)
{
    QPixmap pm(cell * 2, cell * 2);
    pm.fill(Qt::white);
    QPainter p(&pm);
    p.fillRect(0, 0, cell, cell, QColor(204, 204, 204));
    p.fillRect(cell, cell, cell, cell, QColor(204, 204, 204));
    return pm;
}

} // namespace Icons
