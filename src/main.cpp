#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <nvs_flash.h>
#include <time.h>

#include "AppConfig.h"
#include "DebugLogger.h"
#include "SandisolarRegisters.h"
#include "SandisolarModbus.h"
#include "AutomationEngine.h"
#include "TelegramBotManager.h"
#include "OledDisplayManager.h"
#include "RemoteConsoleManager.h"
#include "WebServerManager.h"

#if ENABLE_TAILSCALE
extern "C" {
#include "microlink.h"
#include "microlink_internal.h"
}
#endif

#define BOOT_BUTTON_PIN 0

static AppConfig            currentConfig;
static SandisolarModbus     modbus;
static AutomationEngine     engine(modbus, currentConfig);
static TelegramBotManager   tgBot(modbus, engine, currentConfig);
static OledDisplayManager   oled;
static RemoteConsoleManager remoteConsole(modbus, engine, currentConfig);
static WebServerManager     webServer(modbus, engine, tgBot, oled, currentConfig);

static bool     isApMode = false;
static uint32_t lastModbusPollMs = 0;

#if ENABLE_TAILSCALE
static microlink_t*      mlHandle = nullptr;
static microlink_state_t tsState = ML_STATE_IDLE;
static String            tsVpnIpStr = "";
static String            tsStatusStr = "IDLE";

extern "C" void debug_log_c(int level, const char* tag, const char* msg) {
    DebugLogger::log((uint8_t)level, String(tag), String(msg));
}

static void onTailscaleStateChange(microlink_t* ml, microlink_state_t state, void* user_data) {
    tsState = state;
    const char* stateNames[] = {
        "IDLE", "WIFI_WAIT", "CONNECTING", "REGISTERING",
        "CONNECTED", "RECONNECTING", "ERROR"
    };
    tsStatusStr = (state < sizeof(stateNames) / sizeof(stateNames[0])) ? stateNames[state] : "UNKNOWN";
    DebugLogger::info("TAILSCALE", "State: " + tsStatusStr);

    if (state == ML_STATE_CONNECTED) {
        uint32_t ip = microlink_get_vpn_ip(ml);
        char ipStr[16];
        microlink_ip_to_str(ip, ipStr);
        tsVpnIpStr = String(ipStr);
        tgBot.setTailscaleIp(tsVpnIpStr);
        DebugLogger::info("TAILSCALE", "Connected to Tailnet! VPN IP: " + tsVpnIpStr);
    }
}

void startTailscaleClient() {
    currentConfig.ts_auth_key = currentConfig.getEffectiveTailscaleKey();
    if (!currentConfig.ts_enabled || currentConfig.ts_auth_key.length() == 0) {
        DebugLogger::warn("TAILSCALE", "Disabled or no Auth Key configured");
        tsStatusStr = "DISABLED";
        return;
    }
    if (mlHandle != nullptr) return;

    DebugLogger::info("TAILSCALE", "Starting Tailscale client (hostname: '" + currentConfig.ts_hostname +
                      "', key: " + String(currentConfig.hasCustomTailscaleKey() ? "custom" : "built-in") + ")...");
    microlink_config_t cfg = {
        .auth_key = currentConfig.ts_auth_key.c_str(),
        .device_name = currentConfig.ts_hostname.c_str(),
        .enable_derp = true,
        .enable_stun = true,
        .enable_disco = true,
        .max_peers = 32,
        .wifi_tx_power_dbm = 15,
        .priority_peer_ip = microlink_parse_ip("100.80.147.23"),
        .disco_heartbeat_ms = 0,
        .stun_interval_ms = 0,
        .ctrl_watchdog_ms = 0
    };

    mlHandle = microlink_init(&cfg);
    if (!mlHandle) {
        DebugLogger::error("TAILSCALE", "Failed to initialize MicroLink!");
        tsStatusStr = "INIT_FAILED";
        return;
    }

    microlink_set_state_callback(mlHandle, onTailscaleStateChange, nullptr);
    esp_err_t err = microlink_start(mlHandle);
    if (err != ESP_OK) {
        DebugLogger::error("TAILSCALE", "microlink_start error: " + String(err));
        tsStatusStr = "START_FAILED";
    } else {
        tsStatusStr = "CONNECTING";
    }
}

void stopTailscaleClient() {
    if (mlHandle != nullptr) {
        microlink_destroy(mlHandle);
        mlHandle = nullptr;
        tsState = ML_STATE_IDLE;
        tsStatusStr = "STOPPED";
        tsVpnIpStr = "";
        tgBot.setTailscaleIp("");
    }
}

void restartTailscaleClient() {
    DebugLogger::info("TAILSCALE", "Soft-restarting Tailscale client...");
    stopTailscaleClient();
    delay(400);
    startTailscaleClient();
}
#endif

// Core 1 Realtime Task: RS-485 Modbus RTU Master + Automation Engine + 1.3" OLED Display
static void core1HardwareTask(void* param) {
    DebugLogger::info("CORE1", "Hardware & Automation worker started on Core 1");
    uint32_t lastPoll = 0;

    while (true) {
        uint32_t now = millis();

        // 1. Poll RS-485 Modbus RTU every modbus_poll_ms (default 3000ms)
        if (lastPoll == 0 || (now - lastPoll >= currentConfig.modbus_poll_ms)) {
            lastPoll = now;
            modbus.pollAll(false);
            if (WiFi.status() == WL_CONNECTED) {
                engine.notifyStartup(
#if ENABLE_TAILSCALE
                    tsVpnIpStr
#else
                    ""
#endif
                );
            }
        }

        // 2. Run SoftOFF / SoftON Automation Engine evaluation loop
        engine.loop();

        // 3. Render 1.3" SH1106 / 0.96" SSD1306 OLED Display
        oled.render(
            modbus.getTelemetry(),
            engine,
            currentConfig,
            isApMode,
#if ENABLE_TAILSCALE
            tsVpnIpStr,
            tsStatusStr
#else
            "",
            "DISABLED"
#endif
        );

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void setup() {
    Serial.begin(115200);
    delay(800);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    DebugLogger::init(200);
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

    Serial.println("\n=======================================================");
    Serial.println("  SandiSolar 6.5 Pro — ESP32-S3-N16R8 Standalone Node");
    Serial.println("  RS-485 (XY-S485) + 1.3\" OLED + Telegram + Tailscale");
    Serial.println("=======================================================");

    char sysBuf[160];
    snprintf(sysBuf, sizeof(sysBuf), "Chip: %s | CPU: %d MHz | Flash: %u MB | Heap: %u KB | PSRAM: %u / %u KB",
             ESP.getChipModel(), ESP.getCpuFreqMHz(),
             ESP.getFlashChipSize() / (1024 * 1024),
             ESP.getFreeHeap() / 1024,
             ESP.getFreePsram() / 1024,
             ESP.getPsramSize() / 1024);
    DebugLogger::info("SYSTEM", sysBuf);

    currentConfig = ConfigManager::load();

    // Initialize 1.3" SH1106 / SSD1306 OLED (SDA=GPIO8, SCL=GPIO9)
    oled.begin(currentConfig);

    // Initialize Isolated RS-485 XY-S485 (RX=GPIO17, TX=GPIO18)
    modbus.begin(currentConfig);

    // Connect Wi-Fi or start AP Captive Portal
    if (currentConfig.wifi_ssid.length() > 0) {
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);
        WiFi.begin(currentConfig.wifi_ssid.c_str(), currentConfig.wifi_pass.c_str());
        DebugLogger::info("WIFI", "Connecting to '" + currentConfig.wifi_ssid + "'...");

        uint32_t startMs = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - startMs) < 18000) {
            delay(400);
            Serial.print(".");
        }
        Serial.println();

        if (WiFi.status() == WL_CONNECTED) {
            DebugLogger::info("WIFI", "Connected! LAN IP: " + WiFi.localIP().toString());

            // Configure NTP for Ukraine Timezone (EET/EEST)
            configTzTime("EET-2EEST,M3.5.0/3,M10.5.0/4", "pool.ntp.org", "time.google.com");

            if (MDNS.begin("sandisolar")) {
                MDNS.addService("http", "tcp", 80);
                DebugLogger::info("MDNS", "mDNS responder started: http://sandisolar.local/");
            }

            ArduinoOTA.setHostname(currentConfig.ts_hostname.c_str());
            ArduinoOTA.onStart([]() {
                DebugLogger::info("OTA", "ArduinoOTA network update started...");
            });
            ArduinoOTA.onEnd([]() {
                DebugLogger::info("OTA", "ArduinoOTA update finished!");
            });
            ArduinoOTA.onError([](ota_error_t error) {
                DebugLogger::error("OTA", "ArduinoOTA Error: " + String(error));
            });
            ArduinoOTA.begin();

#if ENABLE_TAILSCALE
            startTailscaleClient();
#endif
        } else {
            DebugLogger::warn("WIFI", "Wi-Fi connection timeout -> Starting AP Captive Portal");
            isApMode = true;
            WiFi.disconnect(false, true); // Cancel station connecting state to free radio for AP & scanning
        }
    } else {
        isApMode = true;
    }

#if ENABLE_TAILSCALE
    webServer.setTailscaleCallbacks(
        []() { return tsVpnIpStr; },
        []() { return tsStatusStr; },
        []() { restartTailscaleClient(); }
    );
    remoteConsole.setTailscaleCallbacks(
        []() { return tsVpnIpStr; },
        []() { return tsStatusStr; },
        []() { restartTailscaleClient(); }
    );
#endif

    // Attach Remote Console (TCP/Telnet 2323, UDP 4444, Web /tty)
    webServer.setRemoteConsole(&remoteConsole);
    remoteConsole.begin();

    // Start WebServer (Port 80)
    webServer.begin(isApMode);

    // Start Interactive Telegram Bot Task on Core 0
    tgBot.begin();

    // Start RS-485 Modbus + Automation + OLED Task on Core 1
    xTaskCreatePinnedToCore(core1HardwareTask, "hw_worker", 10240, nullptr, 2, nullptr, 1);
}

void loop() {
    webServer.loop();
    remoteConsole.loop();
    if (WiFi.status() == WL_CONNECTED) {
        ArduinoOTA.handle();
    }

    // Monitor BOOT Button (GPIO 0): Hold 3 seconds to reset Wi-Fi & enter AP Setup Mode
    static uint32_t bootHoldStart = 0;
    if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
        if (bootHoldStart == 0) {
            bootHoldStart = millis();
        } else if (millis() - bootHoldStart >= 3000) {
            DebugLogger::warn("SYSTEM", "BOOT button held for 3s -> Resetting Wi-Fi to AP Setup Mode...");
            ConfigManager::clearWiFi();
            delay(500);
            ESP.restart();
        }
    } else {
        bootHoldStart = 0;
    }

#if ENABLE_TAILSCALE
    // Tailscale DERP Watchdog (auto-restart if DERP disconnected > 360s)
    static uint32_t derpDiscStartMs = 0;
    if (currentConfig.ts_enabled && WiFi.status() == WL_CONNECTED && mlHandle != nullptr) {
        if (mlHandle->derp.connected) {
            derpDiscStartMs = 0;
        } else {
            if (derpDiscStartMs == 0) {
                derpDiscStartMs = millis();
            } else if (millis() - derpDiscStartMs >= 360000) {
                DebugLogger::warn("TAILSCALE", "Watchdog: DERP disconnected > 360s -> Soft-restarting Tailscale...");
                derpDiscStartMs = millis();
                restartTailscaleClient();
            }
        }
    } else {
        derpDiscStartMs = 0;
    }
#endif

    // Periodic Wi-Fi Reconnect Check (every 30s if in STA mode)
    static uint32_t lastWifiCheckMs = 0;
    if (!isApMode && millis() - lastWifiCheckMs >= 30000) {
        lastWifiCheckMs = millis();
        if (WiFi.status() != WL_CONNECTED) {
            DebugLogger::warn("WIFI", "Wi-Fi connection lost, attempting reconnect...");
            WiFi.reconnect();
        }
    }

    delay(5);
}
