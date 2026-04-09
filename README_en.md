# NodeLib_ANCS

ASTROLAB / node0 — iOS Media Control Service Library (UNTESTED)

> UNTESTED - Currently a conceptual implementation. Recommended for development reference only, not for production use.

---

## Overview

NodeLib_ANCS provides Apple CarPlay Audio Notification Service (ANCS) support for ESP32, allowing the device to share media playback status and control capabilities with iOS devices.

### Supported Features

- Media playback status monitoring (play/pause)
- Track/artist/album information retrieval
- Playback control (play/pause/next/prev)
- Volume control
- Error handling and connection state management

### Protocol Details

ANCS is based on iOS Bluetooth Low Energy (BLE) audio notification service, using the following service UUIDs:

| UUID | Name | Description |
|------|------|-------------|
| `0x5B41` | Service Media Service | ANCS Service |
| `0x5B43` | Media Playback State | Playback Status |
| `0x5B44` | Media Playback Title | Track Title |
| `0x5B46` | Media Playback Artist | Artist Name |
| `0x5B47` | Media Playback Album | Album Name |

---

## Quick Start

### Installation

```bash
# Arduino IDE
# Tools → Library Manager → Install "ESP32 by Espressif"
# Tools → Library Manager → Install "BluetoothFy BLE"
```

### Basic Usage

```cpp
#include "NodeLib_ANCS.h"

NodeLib_ANCS ancs;

void setup() {
  Serial.begin(115200);
  ancs.begin("NodeLib");

  // Listen for media updates
  ancs.onMediaUpdate([](const char* title, const char* artist, bool isPlaying, uint8_t volume) {
    Serial.printf("Title: %s, Artist: %s, Playing: %s, Volume: %d%%\n",
      title ? title : "", artist ? artist : "", isPlaying ? "YES" : "NO", volume);
  });

  // Error handling
  ancs.onError([](const char* msg) {
    Serial.printf("Error: %s\n", msg);
  });
}

void loop() {
  ancs.loop();

  // Control playback
  // ancs.cmdPlayPause();
  // ancs.cmdNext();
  // ancs.cmdPrev();
  // ancs.cmdSetVolume(80);

  delay(1);
}
```

---

## API Reference

### Lifecycle

| Method | Description |
|--------|-------------|
| `begin(const char* name)` | Initialize and start broadcasting |
| `loop()` | Process BLE events (must be called in `loop()`) |
| `restart()` | Re-initialize |
| `isConnected()` | Check connection status |

### Media Control

| Method | Description |
|--------|-------------|
| `cmdPlayPause()` | Toggle play/pause |
| `cmdNext()` | Next track |
| `cmdPrev()` | Previous track |
| `cmdSetVolume(uint8_t)` | Set volume (0-100) |

### Event Callbacks

```cpp
// Media update callback
void onMediaUpdate(
    const char* title,
    const char* artist,
    bool        isPlaying,
    uint8_t     volume
);

// Error callback
void onError(const char* msg);
```

---

## Status States

| State | Description |
|-------|-------------|
| `Idle` | Not initialized |
| `Advertising` | Broadcasting, waiting for iOS connection |
| `Connecting` | iOS connected, pairing in progress |
| `Securing` | Encryption pairing in progress |
| `Ready` | ANCS service ready |
| `Error` | Error or timeout |

---

## Architecture

```
NodeLib_ANCS/
  src/
    NodeLib_ANCS.h
    NodeLib_ANCS.cpp
  library.json
  README.md
  README_en.md
  examples/
    BriefCase/
      BriefCase.ino
```

---

## Warnings

- **UNTESTED**: Currently a conceptual implementation, recommended for development reference only
- **Compatibility**: Only supports iOS 15+ ANCS protocol
- **Security**: Requires iOS BLE access enabled (CarPlay settings)
- **Production**: Not recommended for production use, intended for prototype development only

---

## Related

- [NodeLib_GMCS](../NodeLib_GMCS/README_en.md) — Android Media Control Service Library

---

## Contributing

Welcome to submit issues and PRs.
