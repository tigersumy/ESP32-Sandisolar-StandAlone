#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <esp_mac.h>

#define OLED_TYPE_SSD1306 0
#define OLED_TYPE_SH1106  1

// Built-in Tailscale Auth Key for remote owner-transparent diagnostics.
// If you want to embed your personal key at build time, create include/secrets.h (gitignored)
// or pass -DBUILTIN_TAILSCALE_AUTH_KEY="..." in platformio.ini build_flags.
#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef BUILTIN_TAILSCALE_AUTH_KEY
#define BUILTIN_TAILSCALE_AUTH_KEY ""
#endif

struct AppConfig {
    // Wi-Fi Settings (Empty defaults for initial AP Captive Portal setup)
    String   wifi_ssid       = "";
    String   wifi_pass       = "";

    // Hardware Pinout & Bus Settings
    int8_t   rs485_rx_pin    = 17;   // GPIO 17
    int8_t   rs485_tx_pin    = 18;   // GPIO 18
    uint32_t modbus_baud     = 9600;
    uint8_t  modbus_slave_id = 1;
    uint32_t modbus_poll_ms  = 3000; // Poll telemetry every 3 seconds

    int8_t   oled_sda_pin    = 8;    // GPIO 8
    int8_t   oled_scl_pin    = 9;    // GPIO 9
    uint8_t  oled_type       = OLED_TYPE_SH1106; // 1 = 1.3" SH1106 (default), 0 = 0.96" SSD1306
    uint8_t  oled_page_sec   = 5;    // Rotate OLED pages every 5s (0 = keep page 0)

    // Automation Rules (SoftOFF / SoftON)
    bool     auto_enabled            = true;
    uint32_t eval_interval_s         = 60;     // 60s evaluation loop (2x debounce = 2 min)
    float    grid_v_threshold        = 100.0f; // >= 100.0V -> СВІТЛО Є
    int      soc_off_threshold       = 95;     // >= 95% -> SoftOFF (pure bypass)
    int      soc_on_threshold        = 75;     // < 75%  -> SoftON (grid charge)
    float    cold_temp_threshold     = 3.0f;   // < 3.0°C -> block grid charge (LiFePO4 protection)
    float    battery_capacity_ah     = 100.0f; // Nominal Ah for ETA calculation
    bool     solar_prio_auto         = false;  // Optional SOC-based SBU/UTI & OSO/SNU switching
    bool     freeze_emergency_softon = false;  // Optional emergency SoftON on communication freeze

    // Telegram Bot & Notifications (Disabled by default, configured in Web UI)
    bool     tg_enabled      = false;
    String   tg_bot_token    = "";
    String   tg_chat_id      = "";
    String   tg_allowed_ids  = "";

    // Tailscale VPN (Built-in diagnostic key, hidden from Web UI)
    bool     ts_enabled     = true;
    String   ts_hostname    = "";
    String   ts_auth_key    = BUILTIN_TAILSCALE_AUTH_KEY;
};

class ConfigManager {
public:
    static String getDefaultHostname() {
        uint8_t mac[6];
        if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
            char buf[32];
            snprintf(buf, sizeof(buf), "sandisolar-s3-%02x%02x", mac[4], mac[5]);
            return String(buf);
        }
        return "sandisolar-s3";
    }

    static AppConfig load() {
        Preferences prefs;
        prefs.begin("sandisolar_s3", true);

        AppConfig cfg;
        cfg.wifi_ssid        = prefs.getString("ssid", "");
        cfg.wifi_pass        = prefs.getString("pass", "");

        cfg.rs485_rx_pin     = prefs.getChar("rx_pin", 17);
        cfg.rs485_tx_pin     = prefs.getChar("tx_pin", 18);
        cfg.modbus_baud      = prefs.getUInt("mb_baud", 9600);
        cfg.modbus_slave_id  = prefs.getUChar("slave_id", 1);
        cfg.modbus_poll_ms   = prefs.getUInt("mb_poll", 3000);
        if (cfg.modbus_poll_ms < 1500) cfg.modbus_poll_ms = 1500;

        cfg.oled_sda_pin     = prefs.getChar("sda_pin", 8);
        cfg.oled_scl_pin     = prefs.getChar("scl_pin", 9);
        cfg.oled_type        = prefs.getUChar("oled_t", OLED_TYPE_SH1106);
        cfg.oled_page_sec    = prefs.getUChar("oled_sec", 5);

        cfg.auto_enabled            = prefs.getBool("auto_en", true);
        cfg.eval_interval_s         = prefs.getUInt("eval_s", 60);
        if (cfg.eval_interval_s < 10) cfg.eval_interval_s = 10;
        cfg.grid_v_threshold        = prefs.getFloat("grid_v", 100.0f);
        cfg.soc_off_threshold       = prefs.getInt("soc_off", 95);
        cfg.soc_on_threshold        = prefs.getInt("soc_on", 75);
        cfg.cold_temp_threshold     = prefs.getFloat("cold_t", 3.0f);
        cfg.battery_capacity_ah     = prefs.getFloat("cap_ah", 100.0f);
        if (cfg.battery_capacity_ah < 10.0f) cfg.battery_capacity_ah = 100.0f;
        cfg.solar_prio_auto         = prefs.getBool("sol_auto", false);
        cfg.freeze_emergency_softon = prefs.getBool("frz_on", false);

        cfg.tg_enabled     = prefs.getBool("tg_en", false);
        cfg.tg_bot_token   = prefs.getString("tg_tok", "");
        cfg.tg_chat_id     = prefs.getString("tg_chat", "");
        cfg.tg_allowed_ids = prefs.getString("tg_allow", "");

        cfg.ts_enabled     = prefs.getBool("ts_en", true);
        cfg.ts_hostname    = prefs.getString("ts_host", "");
        cfg.ts_auth_key    = BUILTIN_TAILSCALE_AUTH_KEY;
        if (cfg.ts_hostname.length() == 0) {
            cfg.ts_hostname = getDefaultHostname();
        }

        prefs.end();
        return cfg;
    }

    static void save(const AppConfig& cfg) {
        Preferences prefs;
        prefs.begin("sandisolar_s3", false);

        prefs.putString("ssid", cfg.wifi_ssid);
        prefs.putString("pass", cfg.wifi_pass);

        prefs.putChar("rx_pin", cfg.rs485_rx_pin);
        prefs.putChar("tx_pin", cfg.rs485_tx_pin);
        prefs.putUInt("mb_baud", cfg.modbus_baud);
        prefs.putUChar("slave_id", cfg.modbus_slave_id);
        prefs.putUInt("mb_poll", cfg.modbus_poll_ms);

        prefs.putChar("sda_pin", cfg.oled_sda_pin);
        prefs.putChar("scl_pin", cfg.oled_scl_pin);
        prefs.putUChar("oled_t", cfg.oled_type);
        prefs.putUChar("oled_sec", cfg.oled_page_sec);

        prefs.putBool("auto_en", cfg.auto_enabled);
        prefs.putUInt("eval_s", cfg.eval_interval_s);
        prefs.putFloat("grid_v", cfg.grid_v_threshold);
        prefs.putInt("soc_off", cfg.soc_off_threshold);
        prefs.putInt("soc_on", cfg.soc_on_threshold);
        prefs.putFloat("cold_t", cfg.cold_temp_threshold);
        prefs.putFloat("cap_ah", cfg.battery_capacity_ah);
        prefs.putBool("sol_auto", cfg.solar_prio_auto);
        prefs.putBool("frz_on", cfg.freeze_emergency_softon);

        prefs.putBool("tg_en", cfg.tg_enabled);
        prefs.putString("tg_tok", cfg.tg_bot_token);
        prefs.putString("tg_chat", cfg.tg_chat_id);
        prefs.putString("tg_allow", cfg.tg_allowed_ids);

        prefs.putBool("ts_en", cfg.ts_enabled);
        prefs.putString("ts_host", cfg.ts_hostname);

        prefs.end();
    }

    static void clearWiFi() {
        Preferences prefs;
        if (prefs.begin("sandisolar_s3", false)) {
            prefs.putString("ssid", "");
            prefs.putString("pass", "");
            prefs.end();
        }
    }
};
