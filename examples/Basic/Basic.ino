/**
 * NodeLib_ANCS — 基础示例
 *
 * 功能：连接 iOS，打印收到的通知和媒体信息到串口
 * 硬件：ESP32-WROOM-32E（或任何 ESP32）
 *
 * 使用方法：
 *   1. 烧录后打开串口监视器（115200）
 *   2. 在 iOS 设置 → 蓝牙 里找到 "NODELIB-DEMO" 并连接
 *   3. 弹出配对确认，点击"配对"
 *   4. 之后 iOS 的通知和音乐信息会实时打印到串口
 */

// 开启 info 级别 debug 输出（可改为 0 关闭，3 看详细）
#define NODELIB_DEBUG_LEVEL 2

#include "NodeLib_ANCS.h"

NodeLib_ANCS ancs;

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("NodeLib_ANCS Basic Example");
    Serial.println("NodeLib v" NODELIB_ANCS_VERSION);

    ancs.onStateChange([](NodeLib_ANCS::State s) {
        const char* names[] = {
            "Idle", "Advertising", "Connecting",
            "Securing", "Discovering", "Running", "Error"
        };
        Serial.printf("State: %s\n", names[(int)s]);
    });

    ancs.onNotification([](uint32_t uid, const char* appId,
                            const char* title, const char* body) {
        Serial.printf("--- Notification ---\n");
        Serial.printf("  UID  : %lu\n", (unsigned long)uid);
        Serial.printf("  App  : %s\n", appId);
        Serial.printf("  Title: %s\n", title);
        Serial.printf("  Body : %s\n", body);
    });

    ancs.onMediaUpdate([](const char* title, const char* artist,
                           bool isPlaying, uint8_t volume) {
        Serial.printf("--- Media ---\n");
        Serial.printf("  %s - %s [%s] vol=%d\n",
            title, artist, isPlaying ? "PLAYING" : "PAUSED", volume);
    });

    ancs.begin("NODELIB-DEMO");
}

void loop() {
    ancs.loop();
    delay(10);
}
