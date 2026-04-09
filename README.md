# NodeLib_ANCS

ASTROLAB / node0 — iOS Media Control Service Library (UNTESTED)

> [English](README_en.md) | [中文](README.md)

> UNTESTED — 当前为概念实现，建议仅用于开发参考，不建议在生产环境使用

---

## 概述

NodeLib_ANCS 为 ESP32 提供 Apple CarPlay 音频通知服务（ANCS）支持，允许设备与 iOS 设备共享媒体播放状态和控制能力。

### 支持功能

- 媒体播放状态监听（播放/暂停）
- 曲目/艺术家/专辑信息获取
- 播放控制（play/pause/next/prev）
- 音量控制
- 错误处理与连接状态管理

### 协议说明

ANCS 基于 iOS 的 BLE 音频通知服务，使用以下服务 UUID：

| UUID | 名称 | 说明 |
|------|------|------|
| `0x5B41` | `Service Media Service` | ANCS 服务 |
| `0x5B43` | `Media Playback State` | 播放状态 |
| `0x5B44` | `Media Playback Title` | 曲目标题 |
| `0x5B46` | `Media Playback Artist` | 艺术家 |
| `0x5B47` | `Media Playback Album` | 专辑 |

---

## 快速开始

### 安装依赖

```bash
# Arduino IDE
# 工具 → 库管理器 → 安装 ESP32 by Espressif
# 工具 → 库管理器 → 安装 BluetoothFy BLE
```

### 基本使用

```cpp
#include "NodeLib_ANCS.h"

NodeLib_ANCS ancs;

void setup() {
  Serial.begin(115200);
  ancs.begin("NodeLib");

  // 监听媒体更新
  ancs.onMediaUpdate([](const char* title, const char* artist, bool isPlaying, uint8_t volume) {
    Serial.printf("Title: %s, Artist: %s, Playing: %s, Volume: %d%%\n",
      title ? title : "", artist ? artist : "", isPlaying ? "YES" : "NO", volume);
  });

  // 错误处理
  ancs.onError([](const char* msg) {
    Serial.printf("Error: %s\n", msg);
  });
}

void loop() {
  ancs.loop();

  // 控制播放
  // ancs.cmdPlayPause();
  // ancs.cmdNext();
  // ancs.cmdSetVolume(80);

  delay(1);
}
```

---

## API 参考

### 生命周期

| 方法 | 说明 |
|------|------|
| `begin(const char* name)` | 初始化并启动广播 |
| `loop()` | 处理 BLE 事件（需在 `loop()` 中调用） |
| `restart()` | 重新初始化 |
| `isConnected()` | 检查连接状态 |

### 媒体控制

| 方法 | 说明 |
|------|------|
| `cmdPlayPause()` | 切换播放/暂停 |
| `cmdNext()` | 下一首 |
| `cmdPrev()` | 上一首 |
| `cmdSetVolume(uint8_t)` | 设置音量（0-100） |

### 事件回调

```cpp
// 媒体更新
void onMediaUpdate(
    const char* title,
    const char* artist,
    bool        isPlaying,
    uint8_t     volume
);

// 错误回调
void onError(const char* msg);
```

---

## 状态说明

| 状态值 | 说明 |
|--------|-------|
| `Idle` | 未初始化 |
| `Advertising` | 广播中，等待 iOS 连接 |
| `Connecting` | iOS 已连接，建立配对 |
| `Securing` | 加密配对中 |
| `Ready` | ANCS 服务就绪 |
| `Error` | 错误/超时 |

---

## 架构

```
NodeLib_ANCS/
  src/
    NodeLib_ANCS.h
    NodeLib_ANCS.cpp
  library.json
  README.md
  examples/
    BriefCase/
      BriefCase.ino
```

---

## 注意事项


- **未测试**：当前为概念实现，建议仅用于开发参考
- **兼容性**：仅支持 iOS 15+ 的 ANCS 协议
- **安全性**：需 iOS 允许 BLE 访问（CarPlay 设置）
- **生产环境**：不建议在生产环境使用，仅用于原型开发

---

## 贡献

欢迎提交 Issue 和 PR。
