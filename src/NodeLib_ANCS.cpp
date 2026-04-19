/**
 * NodeLib_ANCS — 实现 v1.1.0
 * ASTROLAB / node0
 */

#include "NodeLib_ANCS.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEClient.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <map>
#include <string>
#include <vector>

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

// ── UUID ─────────────────────────────────────────────────────
static const char* UUID_ANCS_SVC = "7905F431-B5CE-4E99-A40F-4B1E122D00D0";
static const char* UUID_ANCS_NS  = "9FBF120D-6301-42D9-8C58-25E699A21DBD";
static const char* UUID_ANCS_CP  = "69D1D8F3-45E1-49A8-9821-9BBDFDAAD9D9";
static const char* UUID_ANCS_DS  = "22EAC6E9-24D6-4BB5-BE44-B36ACE7C7BFB";

static const char* UUID_AMS_SVC  = "89D3502B-0F36-433A-8EF4-C502AD55F8DC";
static const char* UUID_AMS_EU   = "2F7CABCE-808D-411F-9A0C-BB92BA96C102";
static const char* UUID_AMS_RC   = "9B3C81D8-57B1-4A8A-B8DF-0E56F7CA51C2";

// ── ANCS 协议常量 ────────────────────────────────────────────
#define ANCS_EVT_ADDED    0
#define ANCS_EVT_MODIFIED 1
#define ANCS_EVT_REMOVED  2

#define ANCS_ATTR_APP_ID   0
#define ANCS_ATTR_TITLE    1
#define ANCS_ATTR_SUBTITLE 2
#define ANCS_ATTR_MESSAGE  3

#define ANCS_CMD_GET_NOTIF_ATTR  0
#define ANCS_CMD_PERFORM_ACTION  2
#define ANCS_ACTION_POSITIVE     0
#define ANCS_ACTION_NEGATIVE     1

// ── AMS 协议常量 ─────────────────────────────────────────────
#define AMS_ENTITY_PLAYER  0
#define AMS_ENTITY_QUEUE   1
#define AMS_ENTITY_TRACK   2

#define AMS_PLAYER_PLAYBACK 1
#define AMS_PLAYER_VOLUME   2

#define AMS_QUEUE_INDEX    0
#define AMS_QUEUE_COUNT    1

#define AMS_TRACK_ARTIST   0
#define AMS_TRACK_ALBUM    1
#define AMS_TRACK_TITLE    2
#define AMS_TRACK_DURATION 3

// ── 过滤规则 ─────────────────────────────────────────────────
struct FilterRule { std::string appId; bool allow; };

// ── Pimpl ────────────────────────────────────────────────────
struct NodeLib_ANCS::Impl {
    BLEServer*  server     = nullptr;
    BLEClient*  client     = nullptr;
    BLEAddress* remoteAddr = nullptr;

    BLERemoteCharacteristic* ancsNS = nullptr;
    BLERemoteCharacteristic* ancsCP = nullptr;
    BLERemoteCharacteristic* ancsDS = nullptr;
    BLERemoteCharacteristic* amsEU  = nullptr;
    BLERemoteCharacteristic* amsRC  = nullptr;

    bool     needDiscover    = false;
    bool     ancsFound       = false;
    bool     amsFound        = false;
    uint32_t discoverStartMs = 0;

    // CP 请求队列（单槽，loop() 里执行）
    bool     pendingRequest  = false;
    uint32_t targetUID       = 0;
    uint32_t requestQueued   = 0;

    // DS 解析状态机
    enum DsState : uint8_t {
        DS_CMD,
        DS_UID0, DS_UID1, DS_UID2, DS_UID3,
        DS_ATTR_ID, DS_LEN0, DS_LEN1, DS_DATA
    };
    DsState  dsState   = DS_CMD;
    uint8_t  dsCmd     = 0;
    uint32_t dsUID     = 0;
    uint8_t  dsAttrId  = 0;
    uint16_t dsLen     = 0;
    uint16_t dsRead    = 0;
    char     dsBuf[512];

    // 拼装中的通知
    uint32_t activeUID = 0;
    char pendingAppId[64];
    char pendingTitle[256];
    char pendingSubtitle[256];
    char pendingBody[512];
    uint8_t pendingMask = 0;  // bit0=appId bit1=title bit2=subtitle bit3=body

    // 媒体信息
    MediaInfo media;
    char _mediaTitle[256];
    char _mediaArtist[256];
    char _mediaAlbum[256];

    // 通知过滤规则（有白名单时只收白名单，无白名单时黑名单生效）
    std::vector<FilterRule> filterRules;
    bool hasWhitelist = false;

    NodeLib_ANCS* parent = nullptr;

    void mediaInit() {
        _mediaTitle[0] = _mediaArtist[0] = _mediaAlbum[0] = '\0';
        media.title      = _mediaTitle;
        media.artist     = _mediaArtist;
        media.album      = _mediaAlbum;
        media.isPlaying  = false;
        media.volume     = 0xFF;
        media.duration   = -1;
        media.elapsed    = -1;
        media.queueIndex = -1;
        media.queueCount = -1;
    }

    // 检查 appId 是否通过过滤规则
    bool passFilter(const char* appId) const {
        if (filterRules.empty()) return true;
        for (auto& r : filterRules) {
            if (r.appId == appId) return r.allow;
        }
        // 有白名单时，未列出的 app 默认拒绝；纯黑名单时默认放行
        return !hasWhitelist;
    }
};

static NodeLib_ANCS* s_instance = nullptr;

// ── Server 回调 ──────────────────────────────────────────────
class AncsServerCB : public BLEServerCallbacks {
    void onConnect(BLEServer*, esp_ble_gatts_cb_param_t* p) override {
        if (!s_instance) return;
        if (s_instance->_impl->remoteAddr) delete s_instance->_impl->remoteAddr;
        s_instance->_impl->remoteAddr = new BLEAddress(p->connect.remote_bda);
        NL_LOGI("Connected: %s", s_instance->_impl->remoteAddr->toString().c_str());
        s_instance->_onConnect();
    }
    void onDisconnect(BLEServer*) override {
        if (!s_instance) return;
        NL_LOGI("Disconnected");
        s_instance->_onDisconnect();
    }
};

// ── Security 回调 ────────────────────────────────────────────
class AncsSecurityCB : public BLESecurityCallbacks {
    uint32_t onPassKeyRequest() override { return 0; }
    void onPassKeyNotify(uint32_t passKey) override {
        Serial.printf("[ANCS] Pairing PIN: %06lu\n", (unsigned long)passKey);
        if (s_instance && s_instance->_cbPin) s_instance->_cbPin(passKey);
    }
    bool onConfirmPIN(uint32_t) override { return true; }
    bool onSecurityRequest() override { return true; }
    void onAuthenticationComplete(esp_ble_auth_cmpl_t result) override {
        if (!s_instance) return;
        NL_LOGI("Auth %s", result.success ? "OK" : "FAIL");
        s_instance->_onSecurityDone(result.success);
    }
};

// ── ANCS NS 回调 ─────────────────────────────────────────────
void onAncsNS(BLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
    if (!s_instance || len < 8) return;
    uint8_t  eventId = data[0];
    uint32_t uid = (uint32_t)data[4] | ((uint32_t)data[5] << 8)
                 | ((uint32_t)data[6] << 16) | ((uint32_t)data[7] << 24);

    NL_LOGI("NS: eventId=%d uid=%lu", eventId, (unsigned long)uid);

    if (eventId == ANCS_EVT_REMOVED) {
        if (s_instance->_cbRemoved) s_instance->_cbRemoved(uid);
        return;
    }

    NodeLib_ANCS::Impl* im = s_instance->_impl;
    im->targetUID      = uid;
    im->pendingRequest = true;
    im->requestQueued  = millis();
}

// ── ANCS DS 回调 ─────────────────────────────────────────────
void onAncsDS(BLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
    if (!s_instance) return;
    NodeLib_ANCS::Impl* im = s_instance->_impl;

    for (size_t i = 0; i < len; i++) {
        uint8_t b = data[i];
        switch (im->dsState) {
            case NodeLib_ANCS::Impl::DS_CMD:
                im->dsCmd = b; im->dsState = NodeLib_ANCS::Impl::DS_UID0; break;
            case NodeLib_ANCS::Impl::DS_UID0:
                im->dsUID  = b; im->dsState = NodeLib_ANCS::Impl::DS_UID1; break;
            case NodeLib_ANCS::Impl::DS_UID1:
                im->dsUID |= (uint32_t)b << 8;  im->dsState = NodeLib_ANCS::Impl::DS_UID2; break;
            case NodeLib_ANCS::Impl::DS_UID2:
                im->dsUID |= (uint32_t)b << 16; im->dsState = NodeLib_ANCS::Impl::DS_UID3; break;
            case NodeLib_ANCS::Impl::DS_UID3:
                im->dsUID |= (uint32_t)b << 24; im->dsState = NodeLib_ANCS::Impl::DS_ATTR_ID; break;
            case NodeLib_ANCS::Impl::DS_ATTR_ID:
                im->dsAttrId = b; im->dsState = NodeLib_ANCS::Impl::DS_LEN0; break;
            case NodeLib_ANCS::Impl::DS_LEN0:
                im->dsLen = b; im->dsState = NodeLib_ANCS::Impl::DS_LEN1; break;
            case NodeLib_ANCS::Impl::DS_LEN1: {
                im->dsLen |= (uint16_t)b << 8;
                im->dsRead = 0;
                im->dsBuf[0] = '\0';
                if (im->dsLen == 0) {
                    // 空属性直接存空字符串
                    switch (im->dsAttrId) {
                        case ANCS_ATTR_APP_ID:   im->pendingAppId[0]   = '\0'; im->pendingMask |= 0x01; break;
                        case ANCS_ATTR_TITLE:    im->pendingTitle[0]   = '\0'; im->pendingMask |= 0x02; break;
                        case ANCS_ATTR_SUBTITLE: im->pendingSubtitle[0]= '\0'; im->pendingMask |= 0x04; break;
                        case ANCS_ATTR_MESSAGE:
                            im->pendingBody[0] = '\0';
                            im->pendingMask |= 0x08;
                            if (im->passFilter(im->pendingAppId) && s_instance->_cbNotif)
                                s_instance->_cbNotif(im->activeUID, im->pendingAppId,
                                    im->pendingTitle, im->pendingSubtitle, im->pendingBody);
                            break;
                    }
                    im->dsState = NodeLib_ANCS::Impl::DS_ATTR_ID;
                } else {
                    im->dsState = NodeLib_ANCS::Impl::DS_DATA;
                }
                break;
            }
            case NodeLib_ANCS::Impl::DS_DATA: {
                if (im->dsRead < sizeof(im->dsBuf) - 1) im->dsBuf[im->dsRead] = (char)b;
                im->dsRead++;
                if (im->dsRead >= im->dsLen) {
                    im->dsBuf[min((uint16_t)(sizeof(im->dsBuf)-1), im->dsRead)] = '\0';
                    switch (im->dsAttrId) {
                        case ANCS_ATTR_APP_ID:
                            strlcpy(im->pendingAppId, im->dsBuf, sizeof(im->pendingAppId));
                            im->pendingMask |= 0x01;
                            break;
                        case ANCS_ATTR_TITLE:
                            strlcpy(im->pendingTitle, im->dsBuf, sizeof(im->pendingTitle));
                            im->pendingMask |= 0x02;
                            break;
                        case ANCS_ATTR_SUBTITLE:
                            strlcpy(im->pendingSubtitle, im->dsBuf, sizeof(im->pendingSubtitle));
                            im->pendingMask |= 0x04;
                            break;
                        case ANCS_ATTR_MESSAGE:
                            strlcpy(im->pendingBody, im->dsBuf, sizeof(im->pendingBody));
                            im->pendingMask |= 0x08;
                            NL_LOGI("notif uid=%lu app=%s title=%s body=%s",
                                (unsigned long)im->activeUID, im->pendingAppId,
                                im->pendingTitle, im->pendingBody);
                            if (im->passFilter(im->pendingAppId) && s_instance->_cbNotif)
                                s_instance->_cbNotif(im->activeUID, im->pendingAppId,
                                    im->pendingTitle, im->pendingSubtitle, im->pendingBody);
                            break;
                    }
                    im->dsState = NodeLib_ANCS::Impl::DS_ATTR_ID;
                }
                break;
            }
        }
    }
}

// ── AMS EU 回调 ──────────────────────────────────────────────
void onAmsEU(BLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
    if (!s_instance || len < 3) return;
    NodeLib_ANCS::Impl* im = s_instance->_impl;
    NodeLib_ANCS::MediaInfo& m = im->media;

    uint8_t entityId = data[0];
    uint8_t attrId   = data[1];
    char    value[256] = {0};
    if (len > 3) {
        uint16_t vlen = (uint16_t)min((size_t)(sizeof(value)-1), len-3);
        memcpy(value, data+3, vlen);
    }

    bool changed = false;
    if (entityId == AMS_ENTITY_TRACK) {
        if (attrId == AMS_TRACK_TITLE)    { strlcpy(im->_mediaTitle,  value, sizeof(im->_mediaTitle));  changed = true; }
        if (attrId == AMS_TRACK_ARTIST)   { strlcpy(im->_mediaArtist, value, sizeof(im->_mediaArtist)); changed = true; }
        if (attrId == AMS_TRACK_ALBUM)    { strlcpy(im->_mediaAlbum,  value, sizeof(im->_mediaAlbum));  changed = true; }
        if (attrId == AMS_TRACK_DURATION) { m.duration = atof(value); changed = true; }
    } else if (entityId == AMS_ENTITY_PLAYER) {
        if (attrId == AMS_PLAYER_PLAYBACK) {
            // PlaybackInfo: "playbackState,playbackRate,elapsedTime"
            int s1 = String(value).indexOf(',');
            int s2 = String(value).lastIndexOf(',');
            if (s1 > 0) {
                m.isPlaying = (value[0] == '1' || value[0] == '3');
                if (s2 > s1) m.elapsed = atof(value + s2 + 1);
            } else {
                m.isPlaying = (value[0] == '1' || value[0] == '3');
            }
            changed = true;
        }
        if (attrId == AMS_PLAYER_VOLUME) { m.volume = (uint8_t)(atof(value) * 100.0f); changed = true; }
    } else if (entityId == AMS_ENTITY_QUEUE) {
        if (attrId == AMS_QUEUE_INDEX) { m.queueIndex = (int16_t)atoi(value); changed = true; }
        if (attrId == AMS_QUEUE_COUNT) { m.queueCount = (int16_t)atoi(value); changed = true; }
    }

    if (changed && s_instance->_cbMedia) s_instance->_cbMedia(m);
}

// ════════════════════════════════════════════════════════════
NodeLib_ANCS::NodeLib_ANCS() {
    _impl = new Impl();
    _impl->parent = this;
    _impl->mediaInit();
}

void NodeLib_ANCS::begin(const char* deviceName) {
    if (s_instance) { NL_LOGI("Already initialized"); return; }
    s_instance = this;
    _setState(State::Advertising);

    BLEDevice::init(deviceName);
    BLEDevice::setMTU(517);
    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT_MITM);

    static AncsSecurityCB secCb;
    static AncsServerCB   srvCb;
    BLEDevice::setSecurityCallbacks(&secCb);

    _impl->server = BLEDevice::createServer();
    _impl->server->setCallbacks(&srvCb);

    _impl->server->createService(BLEUUID("180A"))->start();
    _impl->server->createService(BLEUUID((uint16_t)0x1812))->start();

    static BLESecurity sec;
    sec.setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
    sec.setCapability(ESP_IO_CAP_OUT);
    sec.setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

    BLEAdvertisementData advData;
    advData.setFlags(0x06);
    advData.setName(deviceName);
    advData.setAppearance(0x00C2);
    advData.setCompleteServices(BLEUUID((uint16_t)0x1812));

    BLEAdvertisementData scanData;
    {
        esp_bt_uuid_t* raw = BLEUUID(UUID_ANCS_SVC).getNative();
        String sol;
        sol += (char)17; sol += (char)0x15;
        for (int i = 0; i < 16; i++) sol += (char)(raw->uuid.uuid128[15-i]);
        scanData.addData(sol);
    }

    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->setAdvertisementData(advData);
    adv->setScanResponseData(scanData);
    adv->setScanResponse(true);
    BLEDevice::startAdvertising();
    NL_LOGI("Advertising as '%s'", deviceName);
}

static constexpr uint32_t DISCOVER_TIMEOUT_MS = 10000;
static constexpr uint32_t REQUEST_DELAY_MS    = 50;

void NodeLib_ANCS::loop() {
    if (_impl->needDiscover) {
        _impl->needDiscover = false;
        _discoverServices();
        return;
    }
    if (_state == State::Discovering && _impl->discoverStartMs > 0) {
        if (millis() - _impl->discoverStartMs > DISCOVER_TIMEOUT_MS) {
            NL_LOGE("Discovery timeout");
            _impl->discoverStartMs = 0;
            _onDisconnect();
        }
        return;
    }
    if (_state == State::Running && _impl->pendingRequest) {
        if (millis() - _impl->requestQueued >= REQUEST_DELAY_MS) {
            _impl->pendingRequest = false;
            if (_impl->ancsCP) {
                uint32_t uid = _impl->targetUID;
                _impl->activeUID         = uid;
                _impl->pendingAppId[0]   = '\0';
                _impl->pendingTitle[0]   = '\0';
                _impl->pendingSubtitle[0]= '\0';
                _impl->pendingBody[0]    = '\0';
                _impl->pendingMask       = 0;
                _impl->dsState           = Impl::DS_CMD;

                // 请求 appId / title / subtitle / message
                uint8_t req[17];
                uint8_t idx = 0;
                req[idx++] = ANCS_CMD_GET_NOTIF_ATTR;
                req[idx++] = (uid >>  0) & 0xFF;
                req[idx++] = (uid >>  8) & 0xFF;
                req[idx++] = (uid >> 16) & 0xFF;
                req[idx++] = (uid >> 24) & 0xFF;
                req[idx++] = ANCS_ATTR_APP_ID;
                req[idx++] = ANCS_ATTR_TITLE;    req[idx++] = 0xFF; req[idx++] = 0x00;
                req[idx++] = ANCS_ATTR_SUBTITLE; req[idx++] = 0xFF; req[idx++] = 0x00;
                req[idx++] = ANCS_ATTR_MESSAGE;  req[idx++] = 0xFF; req[idx++] = 0x00;
                _impl->ancsCP->writeValue(req, idx, true);
                NL_LOGI("CP: requested uid=%lu", (unsigned long)uid);
            }
        }
    }
}

void NodeLib_ANCS::onNotification(NotifCallback cb)      { _cbNotif   = cb; }
void NodeLib_ANCS::onNotificationRemoved(RemovedCallback cb) { _cbRemoved = cb; }
void NodeLib_ANCS::onMediaUpdate(MediaCallback cb)       { _cbMedia   = cb; }
void NodeLib_ANCS::onStateChange(StateCallback cb)       { _cbState   = cb; }
void NodeLib_ANCS::onPairingPin(PinCallback cb)          { _cbPin     = cb; }

NodeLib_ANCS::State NodeLib_ANCS::getState() const { return _state; }
void NodeLib_ANCS::restart() { _onDisconnect(); }

void NodeLib_ANCS::sendMediaCommand(MediaCommand cmd) {
    if (!_impl->amsRC) { NL_LOGE("AMS RC not available"); return; }
    uint8_t c = (uint8_t)cmd;
    _impl->amsRC->writeValue(&c, 1, true);
    NL_LOGI("Media cmd: %d", (int)cmd);
}

void NodeLib_ANCS::performNotifAction(uint32_t uid, NotifAction action) {
    if (!_impl->ancsCP) { NL_LOGE("ANCS CP not available"); return; }
    uint8_t req[6];
    req[0] = ANCS_CMD_PERFORM_ACTION;
    req[1] = (uid >>  0) & 0xFF;
    req[2] = (uid >>  8) & 0xFF;
    req[3] = (uid >> 16) & 0xFF;
    req[4] = (uid >> 24) & 0xFF;
    req[5] = (action == NotifAction::Positive) ? ANCS_ACTION_POSITIVE : ANCS_ACTION_NEGATIVE;
    _impl->ancsCP->writeValue(req, sizeof(req), true);
    NL_LOGI("Action uid=%lu act=%d", (unsigned long)uid, (int)action);
}

void NodeLib_ANCS::setNotifFilter(const char* appId, bool allow) {
    if (!appId) { clearNotifFilter(); return; }
    for (auto& r : _impl->filterRules) {
        if (r.appId == appId) {
            r.allow = allow;
            // 重新计算是否还有白名单
            _impl->hasWhitelist = false;
            for (auto& x : _impl->filterRules) if (x.allow) { _impl->hasWhitelist = true; break; }
            return;
        }
    }
    _impl->filterRules.push_back({std::string(appId), allow});
    if (allow) _impl->hasWhitelist = true;
}

void NodeLib_ANCS::clearNotifFilter() {
    _impl->filterRules.clear();
    _impl->hasWhitelist = false;
}

// ── 内部 ─────────────────────────────────────────────────────
void NodeLib_ANCS::_setState(State s) {
    if (_state == s) return;
    _state = s;
    if (_cbState) _cbState(s);
    NL_LOGI("State -> %d", (int)s);
}

void NodeLib_ANCS::_onConnect() { _setState(State::Securing); }

void NodeLib_ANCS::_onDisconnect() {
    _setState(State::Advertising);
    _impl->needDiscover = _impl->pendingRequest = false;
    _impl->ancsFound = _impl->amsFound = false;
    _impl->discoverStartMs = 0;
    _impl->ancsNS = _impl->ancsCP = _impl->ancsDS = nullptr;
    _impl->amsEU  = _impl->amsRC  = nullptr;
    if (_impl->client) { _impl->client->disconnect(); delete _impl->client; _impl->client = nullptr; }
    if (_impl->remoteAddr) { delete _impl->remoteAddr; _impl->remoteAddr = nullptr; }
    BLEDevice::startAdvertising();
}

void NodeLib_ANCS::_onSecurityDone(bool success) {
    if (!success) { NL_LOGE("Pairing failed"); _setState(State::Error); _onDisconnect(); return; }
    _setState(State::Discovering);
    _impl->discoverStartMs = millis();
    _impl->needDiscover    = true;
}

void NodeLib_ANCS::_discoverServices() {
    if (!_impl->remoteAddr) { _onDisconnect(); return; }
    _impl->client = BLEDevice::createClient();
    if (!_impl->client->connect(*_impl->remoteAddr)) {
        NL_LOGE("Client connect failed");
        delete _impl->client; _impl->client = nullptr;
        _onDisconnect(); return;
    }
    NL_LOGI("Client connected, discovering...");
    _subscribeAncs();
    _subscribeAms();
    if (_impl->ancsFound || _impl->amsFound) {
        _setState(State::Running);
        NL_LOGI("Running (ANCS=%d AMS=%d)", _impl->ancsFound, _impl->amsFound);
    } else {
        NL_LOGE("No services found");
        _onDisconnect();
    }
}

void NodeLib_ANCS::_subscribeAncs() {
    BLERemoteService* svc = _impl->client->getService(UUID_ANCS_SVC);
    if (!svc) {
        Serial.println("[ANCS] ANCS service NOT found - dumping:");
        auto* svcs = _impl->client->getServices();
        if (svcs) for (auto& kv : *svcs)
            Serial.printf("  - %s\n", kv.second->getUUID().toString().c_str());
        return;
    }
    _impl->ancsNS = svc->getCharacteristic(UUID_ANCS_NS);
    _impl->ancsCP = svc->getCharacteristic(UUID_ANCS_CP);
    _impl->ancsDS = svc->getCharacteristic(UUID_ANCS_DS);
    Serial.printf("[ANCS] chars: NS=%d CP=%d DS=%d\n",
        _impl->ancsNS!=nullptr, _impl->ancsCP!=nullptr, _impl->ancsDS!=nullptr);
    if (!_impl->ancsNS || !_impl->ancsCP || !_impl->ancsDS) return;
    if (_impl->ancsNS->canNotify()) _impl->ancsNS->registerForNotify(onAncsNS);
    if (_impl->ancsDS->canNotify()) _impl->ancsDS->registerForNotify(onAncsDS);
    _impl->ancsFound = true;
    Serial.println("[ANCS] ANCS subscribed OK");
}

void NodeLib_ANCS::_subscribeAms() {
    BLERemoteService* svc = _impl->client->getService(UUID_AMS_SVC);
    if (!svc) { NL_LOGI("AMS service not found (optional)"); return; }
    _impl->amsRC = svc->getCharacteristic(UUID_AMS_RC);
    _impl->amsEU = svc->getCharacteristic(UUID_AMS_EU);
    if (!_impl->amsEU || !_impl->amsEU->canNotify()) { NL_LOGE("AMS EU unavailable"); return; }
    _impl->amsEU->registerForNotify(onAmsEU);

    // 订阅 Track、Player、Queue
    uint8_t trackSub[]  = { AMS_ENTITY_TRACK,  AMS_TRACK_TITLE, AMS_TRACK_ARTIST,
                             AMS_TRACK_ALBUM, AMS_TRACK_DURATION };
    uint8_t playerSub[] = { AMS_ENTITY_PLAYER, AMS_PLAYER_PLAYBACK, AMS_PLAYER_VOLUME };
    uint8_t queueSub[]  = { AMS_ENTITY_QUEUE,  AMS_QUEUE_INDEX, AMS_QUEUE_COUNT };
    _impl->amsEU->writeValue(trackSub,  sizeof(trackSub),  true); delay(20);
    _impl->amsEU->writeValue(playerSub, sizeof(playerSub), true); delay(20);
    _impl->amsEU->writeValue(queueSub,  sizeof(queueSub),  true);

    _impl->amsFound = true;
    NL_LOGI("AMS subscribed OK");
}
