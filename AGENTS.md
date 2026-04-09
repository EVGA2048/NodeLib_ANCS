# NodeLib_ANCS — AI 协作上下文

ESP32 上的 iOS 通知接收库（BLE ANCS + AMS）。
`NodeLib_ESP32_ANCS` 的重写版，遵循 `NodeLib/CONVENTIONS.md`。

---

## 职责

通过 BLE 连接 iOS 设备，接收：
- **ANCS**（Apple Notification Center Service）：通知推送（APP、标题、正文）
- **AMS**（Apple Media Service）：媒体状态（曲目、艺术家、播放状态、音量）

---

## 依赖

- ESP32 Arduino Core 3.x
- `BLEDevice`（ESP32 内置 BLE 栈）
- 无其他第三方依赖

---

## 公开 API（目标设计）

```cpp
#include "NodeLib_ANCS.h"

NodeLib_ANCS ancs;

void setup() {
    // 注册通知回调
    ancs.onNotification([](uint32_t uid, const char* appId,
                           const char* title, const char* body) {
        // 处理通知
    });

    // 注册媒体回调
    ancs.onMediaUpdate([](const char* title, const char* artist,
                          bool isPlaying, uint8_t volume) {
        // 处理媒体状态
    });

    // 注册状态变化回调
    ancs.onStateChange([](NodeLib_ANCS::State s) {
        // State::Idle / Advertising / Connecting / Running / Error
    });

    ancs.begin("PULSAR");  // BLE 广播名
}

void loop() {
    ancs.loop();
}
```

---

## 相比旧版的改进

| 项目 | 旧版 NodeLib_ESP32_ANCS | 新版 |
|------|------------------------|------|
| 回调类型 | 裸函数指针 | `std::function`（支持 lambda） |
| 字符串 | `String` 混用 | public API 全 `const char*` |
| 内部方法 | `_underscore` 暴露在 public | 全部 `private` |
| 断连处理 | 不完整 | 内置状态机，自动重连 |
| AMS 支持 | 部分实现 | 完整（曲目/艺术家/状态/音量） |
| Android GMCS | 无 | 计划支持 |
| Debug 输出 | 常开 Serial | `NODELIB_DEBUG_LEVEL` 控制 |

---

## 与 NodeLib_GMCS 并用的约束（重要）

**ESP32 上只能有一个 `BLEDevice::init()`，两库不能同时 `begin()`。**

正确用法：应用层按连接的设备类型二选一：
```cpp
// iOS → 用 NodeLib_ANCS
NodeLib_ANCS ancs;  ancs.begin("PULSAR");

// Android → 用 NodeLib_GMCS（另编译或运行时判断）
// NodeLib_GMCS gmcs;  gmcs.begin("PULSAR");
```

若需自动识别设备类型并切换，需在应用层实现 BLE 协调器，
统一 `BLEDevice::init()`，按扫描结果决定启用哪个库。
该复杂度不属于两个库的职责范围。

---

## 已知 BLE ANCS 注意事项

- iOS 需要在系统设置 → 蓝牙 → 设备 → 允许通知，才能收到 ANCS 数据
- ANCS 服务 UUID：`7905F431-B5CE-4E99-A40F-4B1E122D00D6`（Apple 专有，非标准）
- AMS 服务 UUID：`89D3502B-0F36-433A-8EF4-C502AD55F8DC`
- 配对必须完成（`esp_ble_set_security_param` 配置）才能订阅 ANCS characteristic
- 断连后 iOS 不会主动重连，需要 ESP32 重新广播让用户从设置页重连

---

## 文件结构

```
NodeLib_ANCS/
├── src/
│   ├── NodeLib_ANCS.h        公开接口
│   └── NodeLib_ANCS.cpp      实现
├── examples/
│   └── Basic/Basic.ino       最小示例
├── library.properties
└── AGENTS.md                 本文件
```
