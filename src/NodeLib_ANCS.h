/**
 * NodeLib_ANCS — iOS 通知接收库（BLE ANCS + AMS）
 * ASTROLAB / node0
 */
#pragma once
#include <Arduino.h>
#include <functional>

#define NODELIB_ANCS_VERSION_MAJOR 1
#define NODELIB_ANCS_VERSION_MINOR 1
#define NODELIB_ANCS_VERSION_PATCH 0
#define NODELIB_ANCS_VERSION "1.1.0"

#ifndef NODELIB_DEBUG_LEVEL
  #define NODELIB_DEBUG_LEVEL 0
#endif

class NodeLib_ANCS {
public:
    // ── 状态 ────────────────────────────────────────────────
    enum class State : uint8_t {
        Idle, Advertising, Connecting, Securing, Discovering, Running, Error
    };

    // ── 媒体命令 ─────────────────────────────────────────────
    enum class MediaCommand : uint8_t {
        Play         = 0,
        Pause        = 1,
        TogglePlay   = 2,
        NextTrack    = 3,
        PrevTrack    = 4,
        VolumeUp     = 5,
        VolumeDown   = 6,
        AdvanceRepeat = 7,
        AdvanceShuffle = 8,
        SkipForward  = 9,
        SkipBackward = 10,
        Like         = 11,
        Dislike      = 12,
        Bookmark     = 13,
    };

    // ── 通知操作 ─────────────────────────────────────────────
    enum class NotifAction : uint8_t {
        Positive = 0,   // 接听电话、确认等
        Negative = 1,   // 拒绝电话、关闭等
    };

    // ── 媒体信息结构体 ───────────────────────────────────────
    struct MediaInfo {
        const char* title;
        const char* artist;
        const char* album;
        bool        isPlaying;
        uint8_t     volume;        // 0~100，不支持时 0xFF
        float       duration;      // 秒，不支持时 -1
        float       elapsed;       // 秒，不支持时 -1
        int16_t     queueIndex;    // 当前曲目在队列中的索引，不支持时 -1
        int16_t     queueCount;    // 队列总数，不支持时 -1
    };

    // ── 回调类型 ─────────────────────────────────────────────
    using NotifCallback = std::function<void(
        uint32_t    uid,
        const char* appId,
        const char* title,
        const char* subtitle,   // 新增
        const char* body
    )>;

    using RemovedCallback = std::function<void(uint32_t uid)>;
    using MediaCallback   = std::function<void(const MediaInfo& info)>;
    using StateCallback   = std::function<void(State newState)>;
    using PinCallback     = std::function<void(uint32_t pin)>;

    // ── 公开 API ─────────────────────────────────────────────
    NodeLib_ANCS();

    void begin(const char* deviceName = "NodeLib");
    void loop();

    void onNotification(NotifCallback cb);
    void onNotificationRemoved(RemovedCallback cb);
    void onMediaUpdate(MediaCallback cb);
    void onStateChange(StateCallback cb);
    void onPairingPin(PinCallback cb);

    State getState() const;
    void  restart();

    // 发送媒体控制命令（Running 状态且有 AMS 时有效）
    void sendMediaCommand(MediaCommand cmd);

    // 执行通知操作（uid 来自 onNotification 回调）
    void performNotifAction(uint32_t uid, NotifAction action);

    // 通知过滤：allow=true 为白名单（只收这个 app），allow=false 为黑名单（屏蔽这个 app）
    // 可多次调用添加多条规则，传 nullptr 清空所有规则
    void setNotifFilter(const char* appId, bool allow);
    void clearNotifFilter();

private:
    State           _state    = State::Idle;
    NotifCallback   _cbNotif;
    RemovedCallback _cbRemoved;
    MediaCallback   _cbMedia;
    StateCallback   _cbState;
    PinCallback     _cbPin;

    void _setState(State s);
    void _onConnect();
    void _onDisconnect();
    void _onSecurityDone(bool success);
    void _discoverServices();
    void _subscribeAncs();
    void _subscribeAms();

    struct Impl;
    Impl* _impl = nullptr;

    friend class AncsServerCB;
    friend class AncsSecurityCB;
    friend void onAncsNS(class BLERemoteCharacteristic*, uint8_t*, size_t, bool);
    friend void onAncsDS(class BLERemoteCharacteristic*, uint8_t*, size_t, bool);
    friend void onAmsEU (class BLERemoteCharacteristic*, uint8_t*, size_t, bool);
};
