#pragma once

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QString>
#include <QTextStream>
#include <QVector>
#include <algorithm>

#include <taglib/fileref.h>
#include <taglib/tpropertymap.h>

namespace honeycomb {

struct LyricLine {
    int64_t timeMs = -1; // -1 for unsynced
    QString text;
};

struct LyricsData {
    bool isSynced = false;
    QVector<LyricLine> lines;
};

inline LyricsData parseLyricsText(const QString& raw) {
    LyricsData data;
    if (raw.trimmed().isEmpty()) return data;

    static const QRegularExpression lrcRx(R"(\[(\d{1,2}):(\d{2})(?:\.(\d{1,3}))?\])");
    const QStringList rawLines = raw.split('\n');

    for (const QString& line : rawLines) {
        QRegularExpressionMatchIterator it = lrcRx.globalMatch(line);
        QVector<int64_t> timestamps;
        int lastEnd = 0;
        while (it.hasNext()) {
            const auto m = it.next();
            const int mins = m.captured(1).toInt();
            const int secs = m.captured(2).toInt();
            QString fracStr = m.captured(3);
            int ms = 0;
            if (!fracStr.isEmpty()) {
                if (fracStr.length() == 1) ms = fracStr.toInt() * 100;
                else if (fracStr.length() == 2) ms = fracStr.toInt() * 10;
                else ms = fracStr.left(3).toInt();
            }
            timestamps.append(static_cast<int64_t>(mins) * 60000 + secs * 1000 + ms);
            lastEnd = m.capturedEnd();
        }

        if (!timestamps.isEmpty()) {
            data.isSynced = true;
            const QString lyricText = line.mid(lastEnd).trimmed();
            for (int64_t ts : timestamps) {
                data.lines.append({ts, lyricText});
            }
        } else {
            const QString trimmed = line.trimmed();
            // Skip metadata tags like [ar:...], [ti:...]
            if (!(trimmed.startsWith('[') && trimmed.endsWith(']') && trimmed.contains(':'))) {
                data.lines.append({-1, trimmed});
            }
        }
    }

    if (data.isSynced) {
        std::sort(data.lines.begin(), data.lines.end(), [](const LyricLine& a, const LyricLine& b) {
            return a.timeMs < b.timeMs;
        });
    }
    return data;
}

inline LyricsData loadLyricsForTrack(const QString& audioPath) {
    if (audioPath.isEmpty()) return {};

    const QFileInfo fi(audioPath);
    const QString base = fi.absolutePath() + "/" + fi.completeBaseName();

    // 1. Check external .lrc or .txt next to the track
    for (const QString& ext : {".lrc", ".txt"}) {
        QFile f(base + ext);
        if (f.exists() && f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&f);
            const QString content = in.readAll();
            if (!content.trimmed().isEmpty()) {
                return parseLyricsText(content);
            }
        }
    }

    // 2. Check embedded lyrics via TagLib PropertyMap
    const QByteArray pathUtf8 = audioPath.toUtf8();
    TagLib::FileRef ref(pathUtf8.constData());
    if (!ref.isNull() && ref.file()) {
        const TagLib::PropertyMap props = ref.file()->properties();
        for (const char* key : {"LYRICS", "UNSYNCEDLYRICS", "SYNCEDLYRICS"}) {
            if (props.contains(key) && !props[key].isEmpty()) {
                const QString raw = QString::fromStdWString(props[key].front().toWString());
                if (!raw.trimmed().isEmpty()) {
                    return parseLyricsText(raw);
                }
            }
        }
    }

    return {};
}

} // namespace honeycomb
