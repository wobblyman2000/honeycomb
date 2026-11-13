#pragma once

#include <QString>
#include <cstdint>

namespace honeycomb {

struct Track {
    int64_t id = 0;
    QString path;
    QString title;
    QString artist;
    QString albumArtist;
    QString album;
    QString genre;
    int year = 0;
    int trackNumber = 0;
    int discNumber = 1;
    int64_t durationMs = 0;
    int bitrate = 0;
    int sampleRate = 0;
    QString format;
    int rating = 0; // 0..5
    int playCount = 0;
    int64_t dateAdded = 0; // Unix timestamp (seconds)
    int64_t lastPlayed = 0;
    bool isCompilation = false;
    QString artHash;
};

struct Album {
    QString album;
    QString albumArtist;
    int year = 0;
    QString genre;
    bool isCompilation = false;
    int trackCount = 0;
    int discCount = 1;
    int64_t totalDurationMs = 0;
    QString artHash;
    QString firstTrackPath;
};

} // namespace honeycomb
