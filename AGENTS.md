# NodeLib_ANCS 开发指南

## 概览

ANCS 库为 ESP32 提供 Apple CarPlay 音频通知服务支持。由于 ANCS 是 Apple 专有协议，此库主要作为概念验证和开发参考。

## 协议实现

ANCS 使用以下 GATT 特性：

| UUID | 名称 | 描述 |
|------|------|------|
| 0x1B00 | ANCS Service | 主服务 |
| 0x1B01 | Media Notification | 媒体信息通知 |
| 0x1B02 | Playback Status | 播放状态通知 |
| 0x1B03 | Volume Control | 音量控制 |
| 0x1B04 | Playback Rate | 播放速度控制 |
| 0x1B05 | Playback Queue | 播放队列通知 |

### 媒体通知格式

```json
{
  "bundleID": "com.apple Music",
  "media": {
    "artworkData64x64": "base64...",
    "artworkData120x120": "base64...",
    "artworkData600x600": "base64...",
    "title": "Song Title",
    "artist": "Artist Name",
    "album": "Album Name",
    "genre": "Genre",
    "trackNumber": 1,
    "trackCount": 10,
    "discNumber": 1,
    "discCount": 1,
    "copyright": "© 2024",
    "year": 2024,
    "duration": 180,
    "playbackPosition": 45
  },
  "playbackState": "nowPlaying",  // nowPlaying, paused, stopped
  "isShuffled": false,
  "isRepeatOn": false
}
```

### 播放状态

```json
{
  "playbackState": "nowPlaying",  // nowPlaying, paused, stopped
  "isPlaying": true,
  "volume": 50,
  "playbackRate": "normal"  // slow, normal, fast
}
```

## 安全考虑

ANCS 服务通常**不需要加密**（iOS 侧不要求），但建议在生产环境中启用：

```cpp
BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);
```

## 常见问题

### Q: 为什么 ANCS 连接不稳定？

A: 可能是以下原因：
1. 距离过远导致信号弱
2. iOS 设备未授权 ANCS 访问
3. GATT 服务未正确注册
4. 干扰过多（2.4GHz 频段）

### Q: 如何获取媒体封面？

A: ANCS 发送 base64 编码的图片数据，需要解码保存为文件：

```cpp
if (artworkData64x64) {
  // 解码 base64 并保存为文件
}
```

## 开发建议

1. **不要在生产环境使用** — ANCS 是专有协议，Apple 可能随时更改格式
2. **优先使用 GMCS** — 如果设备是 Android，GMCS 是更好的选择
3. **保持固件更新** — 确保 ESP32 Arduino 库支持最新的 GATT API

## 参考

- [Apple ANCS 文档](https://developer.apple.com/documentation/cpuservices/overview_of_audio_notifications_on_carplay)
- [Nordic SDK ANCS 示例](https://github.com/NordicSemiconductor/nn_ancs)

---

*由 ASTROLAB / node0 开发*
