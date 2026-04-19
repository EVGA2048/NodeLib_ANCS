/**
 * NodeLib_ANCS — INTERCEPT
 *
 * 功能：连接 iOS，将通知/媒体信息显示在 0.96" OLED（SSD1306，I2C）
 *       配对时全屏显示 PIN 码
 *
 * 硬件：ESP32-S3 + 0.96" OLED (SSD1306, 128×64, I2C)
 *   SDA → GPIO8 / SCL → GPIO9
 *
 * 使用方法：
 *   1. 烧录，OLED 显示 INTERCEPT 开场 → ADV 状态
 *   2. iPhone 设置 → 蓝牙 → 找到 "INTERCEPT" → 点击配对
 *   3. 屏幕显示 6 位 PIN，在 iPhone 上输入确认
 *   4. 之后通知/音乐实时显示
 *
 * 依赖：NodeLib_ANCS / Adafruit SSD1306 / Adafruit GFX
 */

// 开启 debug 输出（0=关，1=错误，2=详细）
#define NODELIB_DEBUG_LEVEL 2

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "NodeLib_ANCS.h"

// ── 硬件 ─────────────────────────────────────────────────────
#define OLED_W    128
#define OLED_H    64
#define OLED_ADDR 0x3C
#define PIN_SDA   8
#define PIN_SCL   9

// ── 视图 ID ──────────────────────────────────────────────────
#define VIEW_NOTIF  0
#define VIEW_MEDIA  1
#define VIEW_PIN    2   // 配对 PIN 全屏

// ── 全局对象 ─────────────────────────────────────────────────
Adafruit_SSD1306 oled(OLED_W, OLED_H, &Wire, -1);
NodeLib_ANCS     ancs;

// ── 状态缓存 ─────────────────────────────────────────────────
static char    g_state[8]       = "IDLE";
static char    g_appId[48]      = "";
static char    g_title[64]      = "";
static char    g_subtitle[64]   = "";
static char    g_body[128]      = "";
static char    g_mediaTitle[64] = "";
static char    g_mediaArtist[64]= "";
static bool    g_isPlaying      = false;
static uint8_t g_volume         = 0xFF;
static uint32_t g_pin           = 0;

static uint8_t  g_view          = VIEW_NOTIF;
static uint32_t g_mediaTs       = 0;

static uint32_t g_lastUID       = 0;

// 前向声明
static void serialHelp();

// ── 工具 ─────────────────────────────────────────────────────
// 最多 maxLen 个字符，超出时末尾替换为 "..."
static void fitStr(char* dst, const char* src, uint8_t maxLen) {
    strncpy(dst, src, maxLen);
    dst[maxLen] = '\0';
    if (strlen(src) > maxLen && maxLen >= 3) {
        dst[maxLen-1] = '.';
        dst[maxLen-2] = '.';
        dst[maxLen-3] = '.';
    }
}

// ── 绘制函数 ─────────────────────────────────────────────────
static void drawStatusBar() {
    // 左：固定标题
    oled.setCursor(0, 0);
    oled.print("INTERCEPT");
    // 右：状态标签（右对齐）
    uint8_t sw = strlen(g_state) * 6;
    oled.setCursor(OLED_W - sw, 0);
    oled.print(g_state);
    // 分割线
    oled.drawFastHLine(0, 9, OLED_W, SSD1306_WHITE);
}

static void drawNotifView() {
    char tmp[22];

    // 第1行：App ID（小写显示更紧凑）
    if (g_appId[0]) {
        fitStr(tmp, g_appId, 21);
        oled.setCursor(0, 12);
        oled.print(tmp);
    } else {
        oled.setCursor(0, 12);
        oled.setTextColor(SSD1306_WHITE);
        oled.print("waiting...");
    }

    // 第2行：标题
    if (g_title[0]) {
        fitStr(tmp, g_title, 21);
        oled.setCursor(0, 22);
        oled.print(tmp);
    }

    // 第3行：副标题（有则显示，否则显示 body 第一行）
    if (g_subtitle[0]) {
        fitStr(tmp, g_subtitle, 21);
        oled.setCursor(0, 32);
        oled.print(tmp);
        if (g_body[0]) {
            fitStr(tmp, g_body, 21);
            oled.setCursor(0, 42);
            oled.print(tmp);
        }
    } else if (g_body[0]) {
        fitStr(tmp, g_body, 21);
        oled.setCursor(0, 32);
        oled.print(tmp);
        if (strlen(g_body) > 21) {
            char tmp2[22];
            fitStr(tmp2, g_body + 21, 21);
            oled.setCursor(0, 42);
            oled.print(tmp2);
        }
    }

    // 底部：媒体 mini 条（有媒体时才显示）
    if (g_mediaTitle[0]) {
        oled.drawFastHLine(0, 54, OLED_W, SSD1306_WHITE);
        fitStr(tmp, g_mediaTitle, 17);
        oled.setCursor(0, 56);
        oled.print(g_isPlaying ? "\x10 " : "  "); // ▶ 或空格
        oled.print(tmp);
    }
}

static void drawMediaView() {
    char tmp[22];

    // 播放状态 + 音量
    oled.setCursor(0, 12);
    if (g_isPlaying) {
        oled.print("\x10 PLAYING");
    } else {
        oled.print("  PAUSED");
    }
    if (g_volume != 0xFF) {
        char vol[8];
        snprintf(vol, sizeof(vol), "%3d%%", g_volume);
        oled.setCursor(OLED_W - 24, 12);
        oled.print(vol);
    }

    // 曲目
    fitStr(tmp, g_mediaTitle, 21);
    oled.setCursor(0, 24);
    oled.print(tmp);

    // 艺术家
    fitStr(tmp, g_mediaArtist, 21);
    oled.setCursor(0, 34);
    oled.print(tmp);
}

static void drawPinView() {
    // 全屏 PIN 显示（不含状态栏）
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setCursor(22, 4);
    oled.print("-- PAIR CODE --");

    // PIN 用 size=2 居中显示
    char pinStr[8];
    snprintf(pinStr, sizeof(pinStr), "%06lu", (unsigned long)g_pin);
    oled.setTextSize(2);
    // size=2 每字符 12px 宽，6字符=72px，居中起点 = (128-72)/2 = 28
    oled.setCursor(28, 22);
    oled.print(pinStr);

    oled.setTextSize(1);
    oled.setCursor(10, 48);
    oled.print("Enter on your iPhone");
    oled.display();
}

static void redraw() {
    if (g_view == VIEW_PIN) { drawPinView(); return; }

    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);

    drawStatusBar();

    if (g_view == VIEW_MEDIA) {
        drawMediaView();
    } else {
        drawNotifView();
    }

    oled.display();
}

// ── 状态名 ───────────────────────────────────────────────────
static const char* stateName(NodeLib_ANCS::State s) {
    switch (s) {
        case NodeLib_ANCS::State::Advertising: return "ADV";
        case NodeLib_ANCS::State::Connecting:  return "CONN";
        case NodeLib_ANCS::State::Securing:    return "SEC";
        case NodeLib_ANCS::State::Discovering: return "DISC";
        case NodeLib_ANCS::State::Running:     return "RUN";
        case NodeLib_ANCS::State::Error:       return "ERR";
        default:                               return "IDLE";
    }
}

// ── setup / loop ─────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("=== INTERCEPT boot ===");

    Wire.begin(PIN_SDA, PIN_SCL);
    if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println("[ERR] OLED init failed");
        while (1) delay(100);
    }

    // 开场画面
    oled.clearDisplay();
    oled.setTextSize(2);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(4, 16);
    oled.print("INTERCEPT");
    oled.setTextSize(1);
    oled.setCursor(28, 38);
    oled.print("NodeLib_ANCS");
    oled.setCursor(34, 48);
    oled.print("v" NODELIB_ANCS_VERSION);
    oled.display();
    delay(1500);

    // ── 回调注册 ───────────────────────────────────────────
    ancs.onStateChange([](NodeLib_ANCS::State s) {
        strncpy(g_state, stateName(s), sizeof(g_state)-1);
        // PIN 视图：状态变化后（配对完成/失败）自动退出
        if (g_view == VIEW_PIN && s != NodeLib_ANCS::State::Securing) {
            g_view = VIEW_NOTIF;
        }
        Serial.printf("[STATE] -> %s\n", g_state);
        redraw();
    });

    ancs.onPairingPin([](uint32_t pin) {
        g_pin  = pin;
        g_view = VIEW_PIN;
        Serial.printf("[PIN] %06lu\n", (unsigned long)pin);
        drawPinView();   // 立即绘制，不等 redraw
    });

    ancs.onNotification([](uint32_t uid, const char* appId,
                            const char* title, const char* subtitle,
                            const char* body) {
        g_lastUID = uid;
        strncpy(g_appId,    appId,    sizeof(g_appId)-1);
        strncpy(g_title,    title,    sizeof(g_title)-1);
        strncpy(g_subtitle, subtitle, sizeof(g_subtitle)-1);
        strncpy(g_body,     body,     sizeof(g_body)-1);
        if (g_view != VIEW_PIN) g_view = VIEW_NOTIF;
        redraw();

        Serial.println("[NOTIF] ----");
        Serial.printf("  UID      : %lu\n", (unsigned long)uid);
        Serial.printf("  App      : %s\n",  appId);
        Serial.printf("  Title    : %s\n",  title);
        if (subtitle[0]) Serial.printf("  Subtitle : %s\n", subtitle);
        Serial.printf("  Body     : %s\n",  body[0] ? body : "(empty)");
    });

    ancs.onMediaUpdate([](const NodeLib_ANCS::MediaInfo& m) {
        strncpy(g_mediaTitle,  m.title,  sizeof(g_mediaTitle)-1);
        strncpy(g_mediaArtist, m.artist, sizeof(g_mediaArtist)-1);
        g_isPlaying  = m.isPlaying;
        g_volume     = m.volume;
        g_mediaTs    = millis();
        if (g_view != VIEW_PIN) g_view = VIEW_MEDIA;
        redraw();

        Serial.printf("[MEDIA] %s - %s [%s]",
            m.title[0] ? m.title : "?",
            m.artist[0] ? m.artist : "?",
            m.isPlaying ? "PLAY" : "PAUSE");
        if (m.volume != 0xFF) Serial.printf(" vol=%d%%", m.volume);
        if (m.duration > 0)   Serial.printf(" %.0f/%.0fs", m.elapsed, m.duration);
        if (m.queueCount > 0) Serial.printf(" [%d/%d]", m.queueIndex+1, m.queueCount);
        Serial.println();
    });

    ancs.begin("INTERCEPT");
    redraw();
    serialHelp();
}

// ── 串口菜单 ─────────────────────────────────────────────────
static void serialHelp() {
    Serial.println("┌─ INTERCEPT serial menu ──────────────────");
    Serial.println("│  Media:  p=play/pause  n=next  b=prev");
    Serial.println("│          +=vol+        -=vol-");
    Serial.println("│  Notif:  y=positive    x=negative  (last notif)");
    Serial.println("│  Filter: f=wechat only  F=clear filter");
    Serial.println("│  View:   1=notif  2=media  r=restart BLE");
    Serial.println("│  ?=this help");
    Serial.println("└──────────────────────────────────────────");
}

static void handleSerial() {
    if (!Serial.available()) return;
    char c = Serial.read();
    // 吞掉多余的换行
    while (Serial.available() && (Serial.peek() == '\n' || Serial.peek() == '\r'))
        Serial.read();

    switch (c) {
        // ── 媒体控制 ──────────────────────────────────────
        case 'p':
            ancs.sendMediaCommand(NodeLib_ANCS::MediaCommand::TogglePlay);
            Serial.println("[CMD] TogglePlay");
            break;
        case 'n':
            ancs.sendMediaCommand(NodeLib_ANCS::MediaCommand::NextTrack);
            Serial.println("[CMD] NextTrack");
            break;
        case 'b':
            ancs.sendMediaCommand(NodeLib_ANCS::MediaCommand::PrevTrack);
            Serial.println("[CMD] PrevTrack");
            break;
        case '+':
            ancs.sendMediaCommand(NodeLib_ANCS::MediaCommand::VolumeUp);
            Serial.println("[CMD] VolumeUp");
            break;
        case '-':
            ancs.sendMediaCommand(NodeLib_ANCS::MediaCommand::VolumeDown);
            Serial.println("[CMD] VolumeDown");
            break;
        case 'r':
            ancs.sendMediaCommand(NodeLib_ANCS::MediaCommand::AdvanceRepeat);
            Serial.println("[CMD] AdvanceRepeat");
            break;
        case 's':
            ancs.sendMediaCommand(NodeLib_ANCS::MediaCommand::AdvanceShuffle);
            Serial.println("[CMD] AdvanceShuffle");
            break;
        case 'l':
            ancs.sendMediaCommand(NodeLib_ANCS::MediaCommand::Like);
            Serial.println("[CMD] Like");
            break;

        // ── 通知操作 ──────────────────────────────────────
        case 'y':
            if (g_lastUID) {
                ancs.performNotifAction(g_lastUID, NodeLib_ANCS::NotifAction::Positive);
                Serial.printf("[ACTION] Positive on uid=%lu\n", (unsigned long)g_lastUID);
            } else {
                Serial.println("[ACTION] No notification received yet");
            }
            break;
        case 'x':
            if (g_lastUID) {
                ancs.performNotifAction(g_lastUID, NodeLib_ANCS::NotifAction::Negative);
                Serial.printf("[ACTION] Negative on uid=%lu\n", (unsigned long)g_lastUID);
            } else {
                Serial.println("[ACTION] No notification received yet");
            }
            break;

        // ── 通知过滤 ──────────────────────────────────────
        case 'f':
            ancs.clearNotifFilter();
            ancs.setNotifFilter("com.tencent.xin", true);  // 只收微信
            Serial.println("[FILTER] WeChat only");
            break;
        case 'F':
            ancs.clearNotifFilter();
            Serial.println("[FILTER] Cleared (all apps)");
            break;

        // ── 视图切换 ──────────────────────────────────────
        case '1':
            g_view = VIEW_NOTIF;
            redraw();
            break;
        case '2':
            g_view = VIEW_MEDIA;
            redraw();
            break;

        // ── 重连 ──────────────────────────────────────────
        case 'R':
            Serial.println("[BLE] Restarting...");
            ancs.restart();
            break;

        case '?':
        case 'h':
            serialHelp();
            break;

        default:
            break;
    }
}

void loop() {
    ancs.loop();
    handleSerial();

    // 媒体视图 6 秒后自动切回通知视图
    if (g_view == VIEW_MEDIA && millis() - g_mediaTs > 6000) {
        g_view = VIEW_NOTIF;
        redraw();
    }

    delay(10);
}
