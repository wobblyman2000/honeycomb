#pragma once

#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <cmath>

namespace honeycomb {

inline QIcon createHoneycombIcon() {
    QIcon icon;
    const int sizes[] = {16, 24, 32, 48, 64, 128};

    for (int sz : sizes) {
        QPixmap pm(sz, sz);
        pm.fill(Qt::transparent);

        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);

        const qreal s = sz;
        // Dark rounded background
        QPainterPath bg;
        bg.addRoundedRect(QRectF(s * 0.04, s * 0.04, s * 0.92, s * 0.92), s * 0.22, s * 0.22);
        p.fillPath(bg, QColor("#181920"));
        p.setPen(QPen(QColor("#f5a623"), qMax(1.0, s * 0.045)));
        p.drawPath(bg);

        // Draw 3 interlocking honeycomb hexagons in warm gold
        auto drawHex = [&](qreal cx, qreal cy, qreal r, const QColor& fill, const QColor& stroke) {
            QPainterPath hex;
            for (int i = 0; i < 6; ++i) {
                const qreal angle = (M_PI / 3.0) * i - M_PI / 6.0;
                const QPointF pt(cx + r * std::cos(angle), cy + r * std::sin(angle));
                if (i == 0) hex.moveTo(pt);
                else hex.lineTo(pt);
            }
            hex.closeSubpath();
            p.fillPath(hex, fill);
            p.setPen(QPen(stroke, qMax(1.0, s * 0.03)));
            p.drawPath(hex);
        };

        const qreal r = s * 0.17;
        drawHex(s * 0.36, s * 0.37, r, QColor("#f5a623"), QColor("#ffd07b"));
        drawHex(s * 0.64, s * 0.37, r, QColor("#d98b14"), QColor("#f5a623"));
        drawHex(s * 0.50, s * 0.63, r, QColor("#ffb84d"), QColor("#ffe0a3"));

        // Crisp dark play triangle inside the bottom-center hex
        QPainterPath play;
        const qreal pr = r * 0.52;
        const qreal pcx = s * 0.50;
        const qreal pcy = s * 0.63;
        play.moveTo(pcx - pr * 0.40, pcy - pr * 0.65);
        play.lineTo(pcx + pr * 0.70, pcy);
        play.lineTo(pcx - pr * 0.40, pcy + pr * 0.65);
        play.closeSubpath();
        p.fillPath(play, QColor("#14151a"));

        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

} // namespace honeycomb
