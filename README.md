# Honeycomb

A native Linux music player inspired by MusicBee—built in **C++20 + Qt 6 + SQLite + TagLib + libmpv**.

## Features
- **Saved Collections & Smart Playlists:** Filter on any tag, file property, play stat, or `Various Artists / Compilation` flag with nested `AND / OR` sub-rules and track limits (`Random`, `Highest Rated`, `Most Played`, `Least Played`, `Recently Added`).
- **Per-Collection Views:** Each Collection remembers its own view mode (`Album Covers`, `Album & Tracks`, `Tracks Table`), sort order, artwork size, and Column Browser state.
- **Artwork-First Browsing:** Async-cached Album Covers grid (`Ctrl+Wheel` zoom) with an inline Album Track Drawer and multi-CD disc grouping (`Disc 1`, `Disc 2`, ...).
- **Daily Listening & Queue Polish:** `Play Next`, `Auto-DJ`, clickable 5-star ratings, `Find Artist` / `Find Track` right-click filtering, **MPRIS2** Linux media keys, and a custom **System Tray** menu.

## Build Requirements
- CMake >= 3.20, C++20 compiler
- Qt 6 (`Widgets`, `Concurrent`, `DBus`)
- `taglib`, `sqlite3`, `mpv`

## Build & Run
```bash
cmake -S . -B build
cmake --build build -j8
./build/honeycomb
```
