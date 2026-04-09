/**
 * NodeLib_ANCS — iOS 通知接收库（BLE ANCS + AMS）
 * ASTROLAB / node0
 *
 * 规范：NodeLib/CONVENTIONS.md
 * 用法：见 examples/Basic/Basic.ino
 */
#pragma once
#include <Arduino.h>
#include <functional>

#define NODELIB_ANCS_VERSION_MAJOR 1
#define NODELIB_ANCS_VERSION_MINOR 0
#define NODELIB_ANCS_VERSION_PATCH 0
#define NODELIB_ANCS_VERSION "1.0.0"

// Debug 输出级别（在 sketch 里 #define NODELIB_DEBUG_LEVEL 2 开启）
#ifndef NODELIB_DEBUG_LEVEL
  #define NODELIB_DEBUG_LEVEL 0
#endif

class NodeLib_ANCS {
public:
    // ── 状态枚举（公开，用户可查询）──────────────────────────
    enum class State : uint8_t {
        Idle,           // 未启动
        Advertising,    // BLE 广播中，等待 iOS 连接
        Connecting,     // 正在建立连接
        Securing,       // 等待配对/加密完成
        Discovering,    // 正在发现 ANCS/AMS 服务
        Running,        // 正常运行，接收通知和媒体信息
        Error           // 发生错误，会自动尝试重连
    };

    // ── 回调类型 ────────────────────────────────────────────
    using NotifCallback = std::function<void(
        uint32_t    uid,
        const char* appId,
        const char* title,
        const char* body
    )>;

    using MediaCallback = std::function<void(
        const char* title,
        const char* artist,
        bool        isPlaying,
        uint8_t     volume     // 0~100，不支持时为 0xFF
    )>;

    using StateCallback = std::function<void(State newState)>;

    // ── 公开 API ────────────────────────────────────────────

    NodeLib_ANCS();

    // 启动 BLE，开始广播
    void begin(const char* deviceName = "NodeLib");

    // 在 loop() 里调用，处理状态机和超时
    void loop();

    // 注册回调（begin() 前后均可，覆盖式设置）
    void onNotification(NotifCallback cb);
    void onMediaUpdate(MediaCallback cb);
    void onStateChange(StateCallback cb);

    // 查询当前状态
    State getState() const;

    // 主动断连并重新广播（可用于"切换设备"场景）
    void restart();

private:
    // 内部状态和实现细节对用户不可见
    State           _state      = State::Idle;
    NotifCallback   _cbNotif;
    MediaCallback   _cbMedia;
    StateCallback   _cbState;

    void _setState(State s);
    void _onConnect();
    void _onDisconnect();
    void _onSecurityDone(bool success);
    void _discoverServices();
    void _subscribeAncs();
    void _subscribeAms();
    // NOTE: _parseAncsData/_parseAmsData 不存在实现，禁止在此声明
    //       数据解析逻辑在 onAncsDS / onAmsEU 静态回调中直接完成
    // BLE 对象（前向声明，避免头文件暴露 BLE 依赖）
    struct Impl;
    Impl* _impl = nullptr;
};
