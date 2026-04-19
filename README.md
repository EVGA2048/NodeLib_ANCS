# NodeLib_ANCS

**ASTROLAB / node0** — ESP32 iOS 通知与媒体控制库

> 基于 BLE ANCS（Apple Notification Center Service）和 AMS（Apple Media Service），让 ESP32 接收 iPhone 的通知推送并控制媒体播放。

---

## 支持功能

| 功能 | 说明 |
|------|------|
| 通知接收 | App ID、标题、副标题、正文 |
| 通知移除事件 | iOS 清除通知时触发回调 |
| 通知操作 | Positive / Negative action（接听/拒接电话等）|
| 通知过滤 | 白名单或黑名单，按 App ID 过滤 |
| 媒体信息 | 曲目、艺术家、专辑、播放状态、音量、时长、进度 |
| 媒体控制 | Play/Pause/Next/Prev/音量等 14 种命令 |
| 队列信息 | 当前索引、队列总数 |
| 配对 PIN | `IO_CAP_OUT` 模式，PIN 码通过回调推给用户 |

---

## 快速开始

### 安装

Arduino IDE → 工具 → 库管理器，或者直接把 `NodeLib_ANCS` 文件夹放进 `Arduino/libraries/`。

依赖：ESP32 Arduino Core（内置 BLE 库）。

### 最简示例

```cpp
#include "NodeLib_ANCS.h"

NodeLib_ANCS ancs;

void setup() {
    Serial.begin(115200);

    ancs.onNotification([](uint32_t uid, const char* appId,
                            const char* title, const char* subtitle,
                            const char* body) {
        Serial.printf("[%s] %s\n%s\n", appId, title, body);
    });

    ancs.onMediaUpdate([](const NodeLib_ANCS::MediaInfo& m) {
        Serial.printf("%s - %s [%s]\n",
            m.title, m.artist, m.isPlaying ? "PLAY" : "PAUSE");
    });

    ancs.begin("MyDevice");
}

void loop() {
    ancs.loop();
    delay(10);
}
```

### 配对流程

1. 烧录后，iPhone 蓝牙设置里出现设备名（如 `MyDevice`）
2. 点击配对 → iPhone 弹出 PIN 码输入框
3. 注册 `onPairingPin` 后，ESP32 侧会收到 6 位数字供显示
4. 用户在 iPhone 上输入 PIN → 配对完成，开始接收通知

> iPhone 需在 **设置 → 蓝牙 → 设备详情** 里开启"显示通知"权限。

---

## API 参考

### 初始化

```cpp
NodeLib_ANCS ancs;
ancs.begin("DeviceName");
```

`loop()` 必须在 Arduino `loop()` 里持续调用。

---

### 通知回调

```cpp
ancs.onNotification([](uint32_t uid, const char* appId,
                        const char* title, const char* subtitle,
                        const char* body) {
    // appId 示例：com.tencent.xin / com.apple.mobilephone
    // uid 可用于 performNotifAction
});
```

#### 通知移除

```cpp
ancs.onNotificationRemoved([](uint32_t uid) {
    // 对应通知被用户在 iPhone 上清除
});
```

#### 通知操作（接听 / 拒接）

```cpp
ancs.onNotification([](uint32_t uid, const char* appId,
                        const char* title, const char* subtitle,
                        const char* body) {
    // 来电时接听
    ancs.performNotifAction(uid, NodeLib_ANCS::NotifAction::Positive);
    // 或拒接
    // ancs.performNotifAction(uid, NodeLib_ANCS::NotifAction::Negative);
});
```

---

### 通知过滤

```cpp
// 白名单：只接收微信通知
ancs.setNotifFilter("com.tencent.xin", true);

// 黑名单：屏蔽某个 app
ancs.setNotifFilter("com.apple.stocks", false);

// 清空所有规则（接收全部）
ancs.clearNotifFilter();
```

有白名单时，不在白名单中的 app 默认屏蔽；纯黑名单时，未列出的 app 默认放行。

---

### 媒体回调

```cpp
ancs.onMediaUpdate([](const NodeLib_ANCS::MediaInfo& m) {
    Serial.printf("%s - %s [%s] vol=%d%%\n",
        m.title, m.artist, m.isPlaying ? "PLAY" : "PAUSE", m.volume);
    if (m.duration > 0)
        Serial.printf("%.0f / %.0f s  queue %d/%d\n",
            m.elapsed, m.duration, m.queueIndex, m.queueCount);
});
```

`MediaInfo` 字段：

| 字段 | 类型 | 不可用时 |
|------|------|---------|
| `title` / `artist` / `album` | `const char*` | 空字符串 |
| `isPlaying` | `bool` | — |
| `volume` | `uint8_t` 0~100 | `0xFF` |
| `duration` / `elapsed` | `float` 秒 | `-1` |
| `queueIndex` / `queueCount` | `int16_t` | `-1` |

---

### 媒体控制

```cpp
ancs.sendMediaCommand(NodeLib_ANCS::MediaCommand::NextTrack);
ancs.sendMediaCommand(NodeLib_ANCS::MediaCommand::VolumeUp);
```

| 命令 | 说明 |
|------|------|
| `Play` / `Pause` / `TogglePlay` | 播放控制 |
| `NextTrack` / `PrevTrack` | 切曲 |
| `VolumeUp` / `VolumeDown` | 音量 |
| `SkipForward` / `SkipBackward` | 快进/快退 |
| `AdvanceRepeat` / `AdvanceShuffle` | 循环/随机模式 |
| `Like` / `Dislike` / `Bookmark` | 收藏操作 |

---

### 配对 PIN

```cpp
ancs.onPairingPin([](uint32_t pin) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%06lu", (unsigned long)pin);
    // 显示在屏幕上，用户在 iPhone 输入
});
```

---

### 状态

```cpp
ancs.onStateChange([](NodeLib_ANCS::State s) {
    // Idle / Advertising / Connecting / Securing / Discovering / Running / Error
});

NodeLib_ANCS::State s = ancs.getState();
ancs.restart();  // 断连并重新广播
```

---

### Debug 输出

在 sketch 最顶部定义（必须在 `#include` 之前）：

```cpp
#define NODELIB_DEBUG_LEVEL 2   // 0=关 1=错误 2=详细
#include "NodeLib_ANCS.h"
```

---

## 硬件要求

- ESP32 / ESP32-S3 / ESP32-C3（需支持 BLE 4.2+）
- iPhone iOS 14+（iOS 16 验证通过）

---

## 已知限制

- 同一时间只支持连接一台 iOS 设备
- ANCS DS 为单槽解析：若 iOS 快速连发两条通知，第一条可能被第二条覆盖
- iOS 26 可见性尚在测试中

---

## 版本历史

| 版本 | 变更 |
|------|------|
| v1.1.0 | 副标题、通知移除、过滤、媒体命令、专辑/进度/队列、配对 PIN |
| v1.0.0 | ANCS 通知接收、AMS 媒体信息 |
