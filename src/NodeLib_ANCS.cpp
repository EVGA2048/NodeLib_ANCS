/**
 * NodeLib_ANCS — 实现
 * ASTROLAB / node0
 *
 * BLE 连接流程：
 *   Advertising（附带 ANCS solicitation UUID）
 *   → iOS 连接 → 配对/加密
 *   → 发现 ANCS / AMS 服务
 *   → 订阅 characteristic
 *   → Running（持续接收通知和媒体信息）
 *   → 断连后自动回到 Advertising
 */

#include "NodeLib_ANCS.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEClient.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ── Debug 宏 ────────────────────────────────────────────────
#if NODELIB_DEBUG_LEVEL >= 2
  #define NL_LOGI(fmt, ...) Serial.printf("[ANCS] " fmt "\n", ##__VA_ARGS__)
#else
  #define NL_LOGI(...) do {} while(0)
#endif
#if NODELIB_DEBUG_LEVEL >= 1
  #define NL_LOGE(fmt, ...) Serial.printf("[ANCS][E] " fmt "\n", ##__VA_ARGS__)
#else
  #define NL_LOGE(...) do {} while(0)
#endif

// ── ANCS / AMS UUID ─────────────────────────────────────────
// ANCS service (Apple solicitation)
static const char* UUID_ANCS_SVC  = "7905F431-B5CE-4E99-A40F-4B1E122D00D6";
static const char* UUID_ANCS_NS   = "9FBF120D-6301-42D9-8C58-25E699A21DBD"; // Notification Source
static const char* UUID_ANCS_CP   = "69D1D8F3-45E1-49A8-9821-9BBDFDAAD9D9"; // Control Point
static const char* UUID_ANCS_DS   = "22EAC6E9-24D6-4BB5-BE44-B36ACE7C7BFB"; // Data Source

// AMS service
static const char* UUID_AMS_SVC   = "89D3502B-0F36-433A-8EF4-C502AD55F8DC";
static const char* UUID_AMS_RC    = "9B3C81D8-57B1-4A8A-B8DF-0E56F7CA51C2"; // Remote Command
static const char* UUID_AMS_EU    = "2F7CABCE-808D-411F-9A0C-BB92BA96C102"; // Entity Update
static const char* UUID_AMS_EA    = "C6B2F38C-23AB-46D8-A6AB-A3A870BBD5D7"; // Entity Attribute

// ── ANCS 协议常量 ────────────────────────────────────────────
// EventID
#define ANCS_EVT_ADDED    0
#define ANCS_EVT_MODIFIED 1
#define ANCS_EVT_REMOVED  2

// AttributeID（GetNotificationAttributes 请求用）
#define ANCS_ATTR_APP_ID    0
#define ANCS_ATTR_TITLE     1
#define ANCS_ATTR_SUBTITLE  2
#define ANCS_ATTR_MESSAGE   3
#define ANCS_ATTR_MSG_SIZE  4
#define ANCS_ATTR_DATE      5

// CommandID
#define ANCS_CMD_GET_NOTIF_ATTR  0
#define ANCS_CMD_GET_APP_ATTR    1
#define ANCS_CMD_PERFORM_ACTION  2

// AMS EntityID
#define AMS_ENTITY_PLAYER 0
#define AMS_ENTITY_QUEUE  1
#define AMS_ENTITY_TRACK  2

// AMS TrackAttributeID
#define AMS_TRACK_ARTIST  0
#define AMS_TRACK_ALBUM   1
#define AMS_TRACK_TITLE   2
#define AMS_TRACK_DURATION 3

// AMS PlayerAttributeID
#define AMS_PLAYER_NAME        0
#define AMS_PLAYER_PLAYBACK    1
#define AMS_PLAYER_VOLUME      2

// ── Pimpl（隐藏 BLE 对象，不污染头文件）────────────────────
struct NodeLib_ANCS::Impl {
    // BLE 对象
    BLEServer*  server  = nullptr;
    BLEClient*  client  = nullptr;
    BLEAddress* remoteAddr = nullptr;

    // ANCS characteristics
    BLERemoteCharacteristic* ancsNS = nullptr;
    BLERemoteCharacteristic* ancsCP = nullptr;
    BLERemoteCharacteristic* ancsDS = nullptr;

    // AMS characteristics
    BLERemoteCharacteristic* amsEU  = nullptr;
    BLERemoteCharacteristic* amsRC  = nullptr;

    bool     pairingDone     = false;
    bool     ancsFound       = false;
    bool     amsFound        = false;
    uint32_t discoverStartMs = 0;   // _discoverServices 开始时间，loop() 超时用

    // ANCS Data Source 解析状态机
    enum DsState : uint8_t {
        DS_CMD, DS_UID0, DS_UID1, DS_UID2, DS_UID3,
        DS_ATTR_ID, DS_LEN0, DS_LEN1, DS_DATA
    };
    DsState     dsState  = DS_CMD;
    uint8_t     dsCmd    = 0;
    uint32_t    dsUID    = 0;
    uint8_t     dsAttrId = 0;
    uint16_t    dsLen    = 0;
    uint16_t    dsRead   = 0;
    char        dsBuf[256];

    // 当前正在拼装的通知
    uint32_t    pendingUID = 0;
    char        pendingAppId[64];
    char        pendingTitle[128];
    char        pendingBody[256];
    uint8_t     pendingAttrMask = 0;  // bit0=appId bit1=title bit2=body

    // 当前媒体信息
    char        mediaTitle[128];
    char        mediaArtist[128];
    bool        mediaPlaying = false;
    uint8_t     mediaVolume  = 0xFF;

    // 父指针（用于回调转发）
    NodeLib_ANCS* parent = nullptr;
};

// ── 全局单例指针（BLE C 回调需要访问）───────────────────────
static NodeLib_ANCS* s_instance = nullptr;

// ── ServerCallbacks（处理连接/断连）────────────────────────
class AncsServerCB : public BLEServerCallbacks {
    void onConnect(BLEServer* s, esp_ble_gatts_cb_param_t* p) override {
        if (!s_instance) return;
        NL_LOGI("iOS connected");
        s_instance->_impl->remoteAddr =
            new BLEAddress(p->connect.remote_bda);
        s_instance->_onConnect();
    }
    void onDisconnect(BLEServer* s) override {
        if (!s_instance) return;
        NL_LOGI("Disconnected, restarting advertising");
        s_instance->_onDisconnect();
    }
};

// ── SecurityCallbacks（配对完成）────────────────────────────
class AncsSecurityCB : public BLESecurityCallbacks {
    uint32_t onPassKeyRequest() override { return 0; }
    void     onPassKeyNotify(uint32_t passKey) override {}
    bool     onConfirmPIN(uint32_t pin) override { return true; }
    bool     onSecurityRequest() override { return true; }
    void     onAuthenticationComplete(esp_ble_auth_cmpl_t result) override {
        if (!s_instance) return;
        bool ok = (result.success == true);
        NL_LOGI("Auth %s", ok ? "OK" : "FAIL");
        s_instance->_onSecurityDone(ok);
    }
};

// ── ANCS Notification Source 回调 ───────────────────────────
static void onAncsNS(BLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
    // Notification Source 格式: [EventID][EventFlags][CatID][CatCount][UID 4B]
    if (!s_instance || len < 8) return;
    uint8_t  eventId = data[0];
    uint32_t uid = (uint32_t)data[4] | ((uint32_t)data[5] << 8) |
                   ((uint32_t)data[6] << 16) | ((uint32_t)data[7] << 24);

    if (eventId == ANCS_EVT_REMOVED) return;  // 不关心消失的通知

    // 向 Control Point 请求属性
    NodeLib_ANCS::Impl* im = s_instance->_impl;
    if (!im->ancsCP) return;

    im->pendingUID = uid;
    im->pendingAttrMask = 0;
    im->pendingAppId[0] = im->pendingTitle[0] = im->pendingBody[0] = '\0';
    im->dsState = NodeLib_ANCS::Impl::DS_CMD;

    // 构造 GetNotificationAttributes 命令
    // [CMD=0][UID 4B][AttrID][MaxLen 2B] × N
    uint8_t req[20];
    uint8_t idx = 0;
    req[idx++] = ANCS_CMD_GET_NOTIF_ATTR;
    req[idx++] = (uid >>  0) & 0xFF;
    req[idx++] = (uid >>  8) & 0xFF;
    req[idx++] = (uid >> 16) & 0xFF;
    req[idx++] = (uid >> 24) & 0xFF;
    req[idx++] = ANCS_ATTR_APP_ID;
    req[idx++] = ANCS_ATTR_TITLE;   req[idx++] = 0x7F; req[idx++] = 0x00; // max 127
    req[idx++] = ANCS_ATTR_MESSAGE; req[idx++] = 0xFF; req[idx++] = 0x00; // max 255

    im->ancsCP->writeValue(req, idx, true);
    NL_LOGI("Requested attrs for UID %lu", (unsigned long)uid);
}

// ── ANCS Data Source 回调（状态机解析分包数据）──────────────
static void onAncsDS(BLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
    if (!s_instance) return;
    NodeLib_ANCS::Impl* im = s_instance->_impl;

    for (size_t i = 0; i < len; i++) {
        uint8_t b = data[i];
        switch (im->dsState) {
            case NodeLib_ANCS::Impl::DS_CMD:
                im->dsCmd   = b;
                im->dsState = NodeLib_ANCS::Impl::DS_UID0;
                break;
            case NodeLib_ANCS::Impl::DS_UID0: im->dsUID  = b;        im->dsState = NodeLib_ANCS::Impl::DS_UID1; break;
            case NodeLib_ANCS::Impl::DS_UID1: im->dsUID |= (uint32_t)b << 8;  im->dsState = NodeLib_ANCS::Impl::DS_UID2; break;
            case NodeLib_ANCS::Impl::DS_UID2: im->dsUID |= (uint32_t)b << 16; im->dsState = NodeLib_ANCS::Impl::DS_UID3; break;
            case NodeLib_ANCS::Impl::DS_UID3:
                im->dsUID |= (uint32_t)b << 24;
                im->dsState = NodeLib_ANCS::Impl::DS_ATTR_ID;
                break;
            case NodeLib_ANCS::Impl::DS_ATTR_ID:
                im->dsAttrId = b;
                im->dsState  = NodeLib_ANCS::Impl::DS_LEN0;
                break;
            case NodeLib_ANCS::Impl::DS_LEN0:
                im->dsLen   = b;
                im->dsState = NodeLib_ANCS::Impl::DS_LEN1;
                break;
            case NodeLib_ANCS::Impl::DS_LEN1:
                im->dsLen  |= (uint16_t)b << 8;
                im->dsRead  = 0;
                if (im->dsLen == 0) {
                    im->dsState = NodeLib_ANCS::Impl::DS_ATTR_ID; // 空属性，跳到下一个
                } else {
                    im->dsState = NodeLib_ANCS::Impl::DS_DATA;
                }
                break;
            case NodeLib_ANCS::Impl::DS_DATA: {
                uint16_t maxBuf = sizeof(im->dsBuf) - 1;
                if (im->dsRead < maxBuf) im->dsBuf[im->dsRead] = (char)b;
                im->dsRead++;
                if (im->dsRead >= im->dsLen) {
                    im->dsBuf[min((uint16_t)(sizeof(im->dsBuf)-1), im->dsRead)] = '\0';
                    // 根据 attrId 存到对应字段
                    switch (im->dsAttrId) {
                        case ANCS_ATTR_APP_ID:
                            strlcpy(im->pendingAppId, im->dsBuf, sizeof(im->pendingAppId));
                            im->pendingAttrMask |= 0x01;
                            break;
                        case ANCS_ATTR_TITLE:
                            strlcpy(im->pendingTitle, im->dsBuf, sizeof(im->pendingTitle));
                            im->pendingAttrMask |= 0x02;
                            break;
                        case ANCS_ATTR_MESSAGE:
                            strlcpy(im->pendingBody, im->dsBuf, sizeof(im->pendingBody));
                            im->pendingAttrMask |= 0x04;
                            break;
                    }
                    im->dsState = NodeLib_ANCS::Impl::DS_ATTR_ID;

                    // 收到 appId + title + body 才触发回调
                    if (im->pendingAttrMask == 0x07 && s_instance->_cbNotif) {
                        s_instance->_cbNotif(
                            im->pendingUID,
                            im->pendingAppId,
                            im->pendingTitle,
                            im->pendingBody
                        );
                        im->pendingAttrMask = 0;
                    }
                }
                break;
            }
        }
    }
}

// ── AMS Entity Update 回调 ───────────────────────────────────
static void onAmsEU(BLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
    if (!s_instance || len < 3) return;
    NodeLib_ANCS::Impl* im = s_instance->_impl;

    uint8_t entityId = data[0];
    uint8_t attrId   = data[1];
    // data[2] = flags, data[3..] = value string
    char value[128] = {0};
    if (len > 3) {
        uint16_t vlen = min((uint16_t)(len - 3), (uint16_t)(sizeof(value) - 1));
        memcpy(value, data + 3, vlen);
        value[vlen] = '\0';
    }

    bool changed = false;
    if (entityId == AMS_ENTITY_TRACK) {
        if (attrId == AMS_TRACK_TITLE) {
            strlcpy(im->mediaTitle, value, sizeof(im->mediaTitle));
            changed = true;
        } else if (attrId == AMS_TRACK_ARTIST) {
            strlcpy(im->mediaArtist, value, sizeof(im->mediaArtist));
            changed = true;
        }
    } else if (entityId == AMS_ENTITY_PLAYER) {
        if (attrId == AMS_PLAYER_PLAYBACK) {
            im->mediaPlaying = (value[0] == '1' || value[0] == '3'); // 1=playing 3=fastforward
            changed = true;
        } else if (attrId == AMS_PLAYER_VOLUME) {
            im->mediaVolume = (uint8_t)(atof(value) * 100.0f);
            changed = true;
        }
    }

    if (changed && s_instance->_cbMedia) {
        s_instance->_cbMedia(
            im->mediaTitle,
            im->mediaArtist,
            im->mediaPlaying,
            im->mediaVolume
        );
    }
}

// ════════════════════════════════════════════════════════════
//  NodeLib_ANCS 公开方法实现
// ════════════════════════════════════════════════════════════

NodeLib_ANCS::NodeLib_ANCS() {
    _impl = new Impl();
    _impl->parent = this;
}

void NodeLib_ANCS::begin(const char* deviceName) {
    if (s_instance != nullptr) {
        NL_LOGI("Already initialized, call restart() to reconnect");
        return;
    }
    s_instance = this;
    _setState(State::Advertising);

    BLEDevice::init(deviceName);
    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);

    // static：回调对象只分配一次，重复 begin() 不会泄漏
    static AncsSecurityCB secCb;
    static AncsServerCB   srvCb;
    static BLESecurity    sec;
    sec.setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
    sec.setCapability(ESP_IO_CAP_NONE);
    sec.setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    BLEDevice::setSecurityCallbacks(&secCb);

    _impl->server = BLEDevice::createServer();
    _impl->server->setCallbacks(&srvCb);

    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(UUID_ANCS_SVC);
    adv->setScanResponse(true);
    adv->setMinPreferred(0x06);
    BLEDevice::startAdvertising();

    NL_LOGI("Advertising as '%s'", deviceName);
}

// 发现服务超时：若 Discovering 状态持续超过此时长则重连
static constexpr uint32_t DISCOVER_TIMEOUT_MS = 10000;

void NodeLib_ANCS::loop() {
    if (_state == State::Discovering && _impl->discoverStartMs > 0) {
        if (millis() - _impl->discoverStartMs > DISCOVER_TIMEOUT_MS) {
            NL_LOGE("Discovery timeout, reconnecting");
            _impl->discoverStartMs = 0;
            _onDisconnect();
        }
    }
}

void NodeLib_ANCS::onNotification(NotifCallback cb) { _cbNotif = cb; }
void NodeLib_ANCS::onMediaUpdate(MediaCallback cb)  { _cbMedia = cb; }
void NodeLib_ANCS::onStateChange(StateCallback cb)  { _cbState = cb; }

NodeLib_ANCS::State NodeLib_ANCS::getState() const { return _state; }

void NodeLib_ANCS::restart() {
    _onDisconnect();
}

// ── 内部方法 ─────────────────────────────────────────────────

void NodeLib_ANCS::_setState(State s) {
    if (_state == s) return;
    _state = s;
    if (_cbState) _cbState(s);
    NL_LOGI("State → %d", (int)s);
}

void NodeLib_ANCS::_onConnect() {
    _setState(State::Securing);
    // 配对由 AncsSecurityCB::onAuthenticationComplete 完成后触发
}

void NodeLib_ANCS::_onDisconnect() {
    _setState(State::Advertising);
    _impl->pairingDone = false;
    _impl->ancsFound   = false;
    _impl->amsFound    = false;
    _impl->ancsNS = _impl->ancsCP = _impl->ancsDS = nullptr;
    _impl->amsEU  = _impl->amsRC  = nullptr;
    if (_impl->client) {
        _impl->client->disconnect();
        delete _impl->client;
        _impl->client = nullptr;
    }
    if (_impl->remoteAddr) {
        delete _impl->remoteAddr;
        _impl->remoteAddr = nullptr;
    }
    BLEDevice::startAdvertising();
}

void NodeLib_ANCS::_onSecurityDone(bool success) {
    if (!success) {
        NL_LOGE("Pairing failed");
        _setState(State::Error);
        _onDisconnect();
        return;
    }
    _impl->pairingDone = true;
    _discoverServices();
}

void NodeLib_ANCS::_discoverServices() {
    _setState(State::Discovering);
    _impl->discoverStartMs = millis();
    if (!_impl->remoteAddr) { _onDisconnect(); return; }

    _impl->client = BLEDevice::createClient();
    if (!_impl->client->connect(*_impl->remoteAddr)) {
        NL_LOGE("Client connect failed");
        _onDisconnect();
        return;
    }
    NL_LOGI("Client connected, discovering...");
    _subscribeAncs();
    _subscribeAms();

    if (_impl->ancsFound || _impl->amsFound) {
        _setState(State::Running);
        NL_LOGI("Running (ANCS=%d AMS=%d)", _impl->ancsFound, _impl->amsFound);
    } else {
        NL_LOGE("Neither ANCS nor AMS found");
        _onDisconnect();
    }
}

void NodeLib_ANCS::_subscribeAncs() {
    BLERemoteService* svc = _impl->client->getService(UUID_ANCS_SVC);
    if (!svc) { NL_LOGI("ANCS service not found"); return; }

    _impl->ancsNS = svc->getCharacteristic(UUID_ANCS_NS);
    _impl->ancsCP = svc->getCharacteristic(UUID_ANCS_CP);
    _impl->ancsDS = svc->getCharacteristic(UUID_ANCS_DS);

    if (!_impl->ancsNS || !_impl->ancsCP || !_impl->ancsDS) {
        NL_LOGE("ANCS characteristics incomplete");
        return;
    }

    if (_impl->ancsNS->canNotify()) _impl->ancsNS->registerForNotify(onAncsNS);
    if (_impl->ancsDS->canNotify()) _impl->ancsDS->registerForNotify(onAncsDS);

    _impl->ancsFound = true;
    NL_LOGI("ANCS subscribed");
}

void NodeLib_ANCS::_subscribeAms() {
    BLERemoteService* svc = _impl->client->getService(UUID_AMS_SVC);
    if (!svc) { NL_LOGI("AMS service not found"); return; }

    _impl->amsRC = svc->getCharacteristic(UUID_AMS_RC);
    _impl->amsEU = svc->getCharacteristic(UUID_AMS_EU);

    if (!_impl->amsEU) { NL_LOGE("AMS EU characteristic not found"); return; }

    // 订阅 Entity Update：请求 Track(title/artist) 和 Player(playback/volume)
    if (_impl->amsEU->canNotify()) {
        _impl->amsEU->registerForNotify(onAmsEU);
        // 写入订阅命令：[EntityID][AttrID, ...]
        uint8_t trackSub[] = { AMS_ENTITY_TRACK,
                                AMS_TRACK_TITLE, AMS_TRACK_ARTIST };
        uint8_t playerSub[] = { AMS_ENTITY_PLAYER,
                                 AMS_PLAYER_PLAYBACK, AMS_PLAYER_VOLUME };
        _impl->amsEU->writeValue(trackSub,  sizeof(trackSub),  true);
        _impl->amsEU->writeValue(playerSub, sizeof(playerSub), true);
    }

    _impl->amsFound = true;
    NL_LOGI("AMS subscribed");
}
