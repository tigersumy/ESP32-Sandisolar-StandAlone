#pragma once

#include <Arduino.h>
#include <Wire.h>
#include "AppConfig.h"
#include "SandisolarRegisters.h"
#include "AutomationEngine.h"
#include "DebugLogger.h"

// Compact 5x7 ASCII Font (0x20 to 0x7E)
static const uint8_t FONT5X7[][5] PROGMEM = {
    {0x00,0x00,0x00,0x00,0x00}, // ' '
    {0x00,0x00,0x5F,0x00,0x00}, // '!'
    {0x00,0x07,0x00,0x07,0x00}, // '"'
    {0x14,0x7F,0x14,0x7F,0x14}, // '#'
    {0x24,0x2A,0x7F,0x2A,0x12}, // '$'
    {0x23,0x13,0x08,0x64,0x62}, // '%'
    {0x36,0x49,0x55,0x22,0x50}, // '&'
    {0x00,0x05,0x03,0x00,0x00}, // '\''
    {0x00,0x1C,0x22,0x41,0x00}, // '('
    {0x00,0x41,0x22,0x1C,0x00}, // ')'
    {0x14,0x08,0x3E,0x08,0x14}, // '*'
    {0x08,0x08,0x3E,0x08,0x08}, // '+'
    {0x00,0x50,0x30,0x00,0x00}, // ','
    {0x08,0x08,0x08,0x08,0x08}, // '-'
    {0x00,0x60,0x60,0x00,0x00}, // '.'
    {0x20,0x10,0x08,0x04,0x02}, // '/'
    {0x3E,0x51,0x49,0x45,0x3E}, // '0'
    {0x00,0x42,0x7F,0x40,0x00}, // '1'
    {0x42,0x61,0x51,0x49,0x46}, // '2'
    {0x21,0x41,0x45,0x4B,0x31}, // '3'
    {0x18,0x14,0x12,0x7F,0x10}, // '4'
    {0x27,0x45,0x45,0x45,0x39}, // '5'
    {0x3C,0x4A,0x49,0x49,0x30}, // '6'
    {0x01,0x71,0x09,0x05,0x03}, // '7'
    {0x36,0x49,0x49,0x49,0x36}, // '8'
    {0x06,0x49,0x49,0x29,0x1E}, // '9'
    {0x00,0x36,0x36,0x00,0x00}, // ':'
    {0x00,0x56,0x36,0x00,0x00}, // ';'
    {0x08,0x14,0x22,0x41,0x00}, // '<'
    {0x14,0x14,0x14,0x14,0x14}, // '='
    {0x00,0x41,0x22,0x14,0x08}, // '>'
    {0x02,0x01,0x51,0x09,0x06}, // '?'
    {0x32,0x49,0x79,0x41,0x3E}, // '@'
    {0x7E,0x11,0x11,0x11,0x7E}, // 'A'
    {0x7F,0x49,0x49,0x49,0x36}, // 'B'
    {0x3E,0x41,0x41,0x41,0x22}, // 'C'
    {0x7F,0x41,0x41,0x22,0x1C}, // 'D'
    {0x7F,0x49,0x49,0x49,0x41}, // 'E'
    {0x7F,0x09,0x09,0x09,0x01}, // 'F'
    {0x3E,0x41,0x49,0x49,0x7A}, // 'G'
    {0x7F,0x08,0x08,0x08,0x7F}, // 'H'
    {0x00,0x41,0x7F,0x41,0x00}, // 'I'
    {0x20,0x40,0x41,0x3F,0x01}, // 'J'
    {0x7F,0x08,0x14,0x22,0x41}, // 'K'
    {0x7F,0x40,0x40,0x40,0x40}, // 'L'
    {0x7F,0x02,0x0C,0x02,0x7F}, // 'M'
    {0x7F,0x04,0x08,0x10,0x7F}, // 'N'
    {0x3E,0x41,0x41,0x41,0x3E}, // 'O'
    {0x7F,0x09,0x09,0x09,0x06}, // 'P'
    {0x3E,0x41,0x51,0x21,0x5E}, // 'Q'
    {0x7F,0x09,0x19,0x29,0x46}, // 'R'
    {0x46,0x49,0x49,0x49,0x31}, // 'S'
    {0x01,0x01,0x7F,0x01,0x01}, // 'T'
    {0x3F,0x40,0x40,0x40,0x3F}, // 'U'
    {0x1F,0x20,0x40,0x20,0x1F}, // 'V'
    {0x3F,0x40,0x38,0x40,0x3F}, // 'W'
    {0x63,0x14,0x08,0x14,0x63}, // 'X'
    {0x07,0x08,0x70,0x08,0x07}, // 'Y'
    {0x61,0x51,0x49,0x45,0x43}, // 'Z'
    {0x00,0x7F,0x41,0x41,0x00}, // '['
    {0x02,0x04,0x08,0x10,0x20}, // '\\'
    {0x00,0x41,0x41,0x7F,0x00}, // ']'
    {0x04,0x02,0x01,0x02,0x04}, // '^'
    {0x40,0x40,0x40,0x40,0x40}, // '_'
    {0x00,0x01,0x02,0x04,0x00}, // '`'
    {0x20,0x54,0x54,0x54,0x78}, // 'a'
    {0x7F,0x48,0x44,0x44,0x38}, // 'b'
    {0x38,0x44,0x44,0x44,0x20}, // 'c'
    {0x38,0x44,0x44,0x48,0x7F}, // 'd'
    {0x38,0x54,0x54,0x54,0x18}, // 'e'
    {0x08,0x7E,0x09,0x01,0x02}, // 'f'
    {0x0C,0x52,0x52,0x52,0x3E}, // 'g'
    {0x7F,0x08,0x04,0x04,0x78}, // 'h'
    {0x00,0x44,0x7D,0x40,0x00}, // 'i'
    {0x20,0x40,0x44,0x3D,0x00}, // 'j'
    {0x7F,0x10,0x28,0x44,0x00}, // 'k'
    {0x00,0x41,0x7F,0x40,0x00}, // 'l'
    {0x7C,0x04,0x18,0x04,0x78}, // 'm'
    {0x7C,0x08,0x04,0x04,0x78}, // 'n'
    {0x38,0x44,0x44,0x44,0x38}, // 'o'
    {0x7C,0x14,0x14,0x14,0x08}, // 'p'
    {0x08,0x14,0x14,0x18,0x7C}, // 'q'
    {0x7C,0x08,0x04,0x04,0x08}, // 'r'
    {0x48,0x54,0x54,0x54,0x20}, // 's'
    {0x04,0x3F,0x44,0x40,0x20}, // 't'
    {0x3C,0x40,0x40,0x20,0x7C}, // 'u'
    {0x1C,0x20,0x40,0x20,0x1C}, // 'v'
    {0x3C,0x40,0x30,0x40,0x3C}, // 'w'
    {0x44,0x28,0x10,0x28,0x44}, // 'x'
    {0x0C,0x50,0x50,0x50,0x3C}, // 'y'
    {0x44,0x64,0x54,0x4C,0x44}, // 'z'
    {0x00,0x08,0x36,0x41,0x00}, // '{'
    {0x00,0x00,0x7F,0x00,0x00}, // '|'
    {0x00,0x41,0x36,0x08,0x00}, // '}'
    {0x08,0x04,0x08,0x10,0x08}  // '~'
};

class OledDisplayManager {
public:
    OledDisplayManager() {
        memset(m_buffer, 0, sizeof(m_buffer));
    }

    bool begin(const AppConfig& cfg) {
        m_sdaPin = cfg.oled_sda_pin;
        m_sclPin = cfg.oled_scl_pin;
        m_oledType = cfg.oled_type;
        m_pageSec  = cfg.oled_page_sec;

        Wire.end();
        Wire.begin(m_sdaPin, m_sclPin, 400000);

        // Probe I2C address 0x3C or 0x3D
        Wire.beginTransmission(0x3C);
        if (Wire.endTransmission() == 0) {
            m_i2cAddr = 0x3C;
            m_present = true;
        } else {
            Wire.beginTransmission(0x3D);
            if (Wire.endTransmission() == 0) {
                m_i2cAddr = 0x3D;
                m_present = true;
            } else {
                m_present = false;
                DebugLogger::warn("OLED", "I2C OLED not found on SDA=GPIO" + String(m_sdaPin) + ", SCL=GPIO" + String(m_sclPin));
                return false;
            }
        }

        initController();
        clear();
        drawString(12, 16, "SANDISOLAR 6.5 PRO", 1);
        drawString(18, 32, "ESP32-S3 RS-485", 1);
        drawString(24, 48, "Initializing...", 1);
        flush();

        DebugLogger::info("OLED", String("Initialized ") + (m_oledType == OLED_TYPE_SH1106 ? "1.3\" SH1106" : "0.96\" SSD1306") +
                          " @ 0x" + String(m_i2cAddr, HEX) + " (SDA=GPIO" + String(m_sdaPin) + ", SCL=GPIO" + String(m_sclPin) + ")");
        return true;
    }

    void updateConfig(const AppConfig& cfg) {
        bool reinitPins = (cfg.oled_sda_pin != m_sdaPin || cfg.oled_scl_pin != m_sclPin);
        m_oledType = cfg.oled_type;
        m_pageSec  = cfg.oled_page_sec;
        if (reinitPins) {
            begin(cfg);
        }
    }

    void render(const SandisolarTelemetry& t, const AutomationEngine& engine, const AppConfig& cfg, bool isApMode, const String& vpnIp, const String& tsStatus) {
        if (!m_present) return;
        if (millis() - m_lastRenderMs < 500) return;
        m_lastRenderMs = millis();

        m_oledType = cfg.oled_type;
        m_pageSec  = cfg.oled_page_sec;

        if (m_pageSec > 0) {
            if (millis() - m_lastPageSwitchMs >= (uint32_t)m_pageSec * 1000UL) {
                m_lastPageSwitchMs = millis();
                m_currentPage = (m_currentPage + 1) % 3;
            }
        } else {
            m_currentPage = 0;
        }

        clear();

        if (isApMode) {
            renderApScreen();
            flush();
            return;
        }

        if (m_currentPage == 0) {
            renderMainDashboard(t, engine, cfg);
        } else if (m_currentPage == 1) {
            renderThermalAndBms(t, cfg);
        } else {
            renderNetworkAndModbus(t, engine, cfg, vpnIp, tsStatus);
        }

        flush();
    }

private:
    void sendCmd(uint8_t cmd) {
        Wire.beginTransmission(m_i2cAddr);
        Wire.write(0x00); // Command byte
        Wire.write(cmd);
        Wire.endTransmission();
    }

    void initController() {
        const uint8_t initSeq[] = {
            0xAE,       // Display OFF
            0xD5, 0x80, // Set display clock divide ratio
            0xA8, 0x3F, // Set multiplex ratio (1 to 64)
            0xD3, 0x00, // Set display offset = 0
            0x40,       // Set start line = 0
            0x8D, 0x14, // Enable charge pump
            0x20, 0x02, // Page addressing mode (compatible with both SSD1306 & SH1106!)
            0xA1,       // Segment re-map (col 127 mapped to SEG0)
            0xC8,       // COM output scan direction remapped
            0xDA, 0x12, // COM pins hardware configuration
            0x81, 0xCF, // Contrast control
            0xD9, 0xF1, // Pre-charge period
            0xDB, 0x40, // VCOMH deselect level
            0xA4,       // Entire display ON from RAM
            0xA6,       // Normal display (not inverted)
            0xAF        // Display ON
        };
        for (size_t i = 0; i < sizeof(initSeq); i++) {
            sendCmd(initSeq[i]);
        }
    }

    void clear() {
        memset(m_buffer, 0, sizeof(m_buffer));
    }

    void setPixel(int x, int y, bool on = true) {
        if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
        if (on) m_buffer[x + (y / 8) * 128] |= (1 << (y & 7));
        else    m_buffer[x + (y / 8) * 128] &= ~(1 << (y & 7));
    }

    void drawHLine(int x, int y, int w, bool on = true) {
        for (int i = 0; i < w; i++) setPixel(x + i, y, on);
    }

    void drawRect(int x, int y, int w, int h, bool on = true) {
        for (int i = 0; i < w; i++) {
            setPixel(x + i, y, on);
            setPixel(x + i, y + h - 1, on);
        }
        for (int j = 0; j < h; j++) {
            setPixel(x, y + j, on);
            setPixel(x + w - 1, y + j, on);
        }
    }

    void fillRect(int x, int y, int w, int h, bool on = true) {
        for (int j = 0; j < h; j++) {
            for (int i = 0; i < w; i++) {
                setPixel(x + i, y + j, on);
            }
        }
    }

    void drawChar(int x, int y, char c, uint8_t scale = 1, bool inverted = false) {
        if (c < 0x20 || c > 0x7E) c = '?';
        uint8_t idx = (uint8_t)(c - 0x20);
        for (int col = 0; col < 5; col++) {
            uint8_t line = pgm_read_byte(&FONT5X7[idx][col]);
            for (int row = 0; row < 7; row++) {
                bool bit = (line & (1 << row)) != 0;
                if (inverted) bit = !bit;
                if (scale == 1) {
                    if (bit) setPixel(x + col, y + row, true);
                    else if (inverted) setPixel(x + col, y + row, false);
                } else {
                    for (uint8_t sx = 0; sx < scale; sx++) {
                        for (uint8_t sy = 0; sy < scale; sy++) {
                            if (bit) setPixel(x + col * scale + sx, y + row * scale + sy, true);
                            else if (inverted) setPixel(x + col * scale + sx, y + row * scale + sy, false);
                        }
                    }
                }
            }
        }
    }

    void drawString(int x, int y, const String& str, uint8_t scale = 1, bool inverted = false) {
        int curX = x;
        for (size_t i = 0; i < str.length(); i++) {
            if (curX + 5 * scale > 128) break;
            drawChar(curX, y, str[i], scale, inverted);
            curX += 6 * scale;
        }
    }

    void flush() {
        // SH1106 has 132-column internal RAM, so 128px screen starts at column offset 2.
        // SSD1306 has 128-column internal RAM, so column offset is 0.
        uint8_t colOffset = (m_oledType == OLED_TYPE_SH1106) ? 2 : 0;

        for (uint8_t page = 0; page < 8; page++) {
            sendCmd(0xB0 + page);                       // Set page address (0..7)
            sendCmd(0x00 | (colOffset & 0x0F));         // Set lower column start address
            sendCmd(0x10 | ((colOffset >> 4) & 0x0F));  // Set higher column start address

            // Send 128 bytes of page data in 32-byte I2C chunks
            for (uint8_t chunk = 0; chunk < 4; chunk++) {
                Wire.beginTransmission(m_i2cAddr);
                Wire.write(0x40); // Data stream byte
                Wire.write(&m_buffer[page * 128 + chunk * 32], 32);
                Wire.endTransmission();
            }
        }
    }

    void renderApScreen() {
        fillRect(0, 0, 128, 11, true);
        drawString(10, 2, "WIFI SETUP (AP MODE)", 1, true);
        drawString(0, 16, "SSID: Sandisolar-S3", 1);
        drawString(0, 28, "IP:   192.168.4.1", 1);
        drawString(0, 40, "Open in browser:", 1);
        drawString(0, 52, "http://192.168.4.1", 1);
    }

    void renderMainDashboard(const SandisolarTelemetry& t, const AutomationEngine& engine, const AppConfig& cfg) {
        // Top header bar (11px high)
        fillRect(0, 0, 128, 11, true);
        String modeLabel;
        if (!t.online) modeLabel = "RS485 OFFLINE";
        else if (!t.power_switch) modeLabel = "SoftOFF/BYP";
        else if (t.inverter_status == 2) modeLabel = "OFF-GRID";
        else if (t.inverter_status == 3) modeLabel = "! FAULT !";
        else modeLabel = "RUNNING/ON";

        drawString(2, 2, modeLabel, 1, true);
        String autoTag = cfg.auto_enabled ? ("A:" + String(engine.getConsecutiveCount()) + "/2") : "MANUAL";
        drawString(128 - autoTag.length() * 6 - 2, 2, autoTag, 1, true);

        // Middle Row: Big Battery SOC + Progress Bar + V/A
        char socBuf[12];
        snprintf(socBuf, sizeof(socBuf), "%d%%", t.battery_soc);
        drawString(0, 14, socBuf, 2); // Big 2x font (14px tall)

        // Battery Voltage & Current on the right side
        char vaBuf[20];
        snprintf(vaBuf, sizeof(vaBuf), "%.1fV %+.1fA", t.battery_voltage, t.battery_current);
        drawString(52, 14, vaBuf, 1);

        // Battery progress bar at x=52, y=23, w=74, h=6
        drawRect(52, 23, 74, 6, true);
        int fillW = constrain((t.battery_soc * 70) / 100, 0, 70);
        if (fillW > 0) fillRect(54, 25, fillW, 2, true);

        drawHLine(0, 32, 128, true);

        // Grid & Frequency row
        bool hasGrid = (t.grid_voltage >= cfg.grid_v_threshold);
        char gridBuf[24];
        snprintf(gridBuf, sizeof(gridBuf), "GRID: %.1fV %s", t.grid_voltage, hasGrid ? "[OK]" : "[LOST]");
        drawString(0, 36, gridBuf, 1);

        // Load & Solar PV row
        char pwrBuf[24];
        snprintf(pwrBuf, sizeof(pwrBuf), "LOAD:%4.0fW PV:%4.0fW", t.power_out_load, t.pv1_power);
        drawString(0, 46, pwrBuf, 1);

        // Temps summary row
        char tmpBuf[24];
        snprintf(tmpBuf, sizeof(tmpBuf), "T1:%.0fC BMS:%.1fC 1/3", t.temp_inverter, t.bms_battery_temp);
        drawString(0, 56, tmpBuf, 1);
    }

    void renderThermalAndBms(const SandisolarTelemetry& t, const AppConfig& cfg) {
        fillRect(0, 0, 128, 11, true);
        drawString(4, 2, "THERMAL & BMS [2/3]", 1, true);

        char l1[24], l2[24], l3[24], l4[24], l5[24];
        snprintf(l1, sizeof(l1), "T1 Inv: %5.1fC  T2:%4.1fC", t.temp_inverter, t.temp_boost);
        snprintf(l2, sizeof(l2), "T3 LLC: %5.1fC  T4:%4.1fC", t.temp_llc, t.bms_battery_temp);

        BatteryEtaInfo eta = SandisolarTelemetry::calculateEta(t.battery_soc, t.battery_current, cfg.battery_capacity_ah);
        snprintf(l3, sizeof(l3), "Pbat:%+5.0fW (%+.1f%%/h)", t.battery_power, eta.speed_pct_hr);
        snprintf(l4, sizeof(l4), "BMS Lim: +%.0fA / -%.0fA", t.bms_max_charge_a, t.bms_max_discharge_a);
        snprintf(l5, sizeof(l5), "AC Chg:%.0fA  Fan:%uRPM", t.ac_grid_charge_a, t.fan_speed);

        drawString(0, 15, l1, 1);
        drawString(0, 25, l2, 1);
        drawString(0, 35, l3, 1);
        drawString(0, 45, l4, 1);
        drawString(0, 55, l5, 1);
    }

    void renderNetworkAndModbus(const SandisolarTelemetry& t, const AutomationEngine& engine, const AppConfig& cfg, const String& vpnIp, const String& tsStatus) {
        fillRect(0, 0, 128, 11, true);
        drawString(4, 2, "NET & RS-485  [3/3]", 1, true);

        String lanStr = "LAN: " + WiFi.localIP().toString();
        String vpnStr = "VPN: " + (vpnIp.length() > 0 ? vpnIp : tsStatus);
        char mbStr[24];
        snprintf(mbStr, sizeof(mbStr), "485: TX:%lu RX:%lu E:%lu",
                 (unsigned long)(t.tx_count % 10000),
                 (unsigned long)(t.rx_count % 10000),
                 (unsigned long)(t.err_count % 1000));
        char autoStr[24];
        snprintf(autoStr, sizeof(autoStr), "OFF>=%d%% ON<%d%% %us",
                 cfg.soc_off_threshold, cfg.soc_on_threshold, (unsigned)engine.getSecondsUntilNextEval());
        char wifiStr[24];
        snprintf(wifiStr, sizeof(wifiStr), "WiFi: %ddBm Up:%lum", WiFi.RSSI(), (unsigned long)(millis() / 60000UL));

        drawString(0, 15, lanStr, 1);
        drawString(0, 25, vpnStr, 1);
        drawString(0, 35, mbStr, 1);
        drawString(0, 45, autoStr, 1);
        drawString(0, 55, wifiStr, 1);
    }

    uint8_t  m_buffer[128 * 8];
    int8_t   m_sdaPin = 8;
    int8_t   m_sclPin = 9;
    uint8_t  m_i2cAddr = 0x3C;
    uint8_t  m_oledType = OLED_TYPE_SH1106;
    uint8_t  m_pageSec = 5;
    bool     m_present = false;
    uint8_t  m_currentPage = 0;
    uint32_t m_lastPageSwitchMs = 0;
    uint32_t m_lastRenderMs = 0;
};
