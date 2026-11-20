#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <WiFiUdp.h>
#include "AppConfig.h"
#include "DebugLogger.h"
#include "SandisolarModbus.h"
#include "AutomationEngine.h"

#define TTY_TCP_PORT 2323
#define TTY_UDP_PORT 4444

class RemoteConsoleManager {
public:
    RemoteConsoleManager(SandisolarModbus& modbus, AutomationEngine& engine, AppConfig& config)
        : m_tcpServer(TTY_TCP_PORT), m_modbus(modbus), m_engine(engine), m_config(config) {}

    void setTailscaleCallbacks(std::function<String()> getVpnIp, std::function<String()> getTsStatus, std::function<void()> restartTs) {
        m_getVpnIp    = getVpnIp;
        m_getTsStatus = getTsStatus;
        m_restartTs   = restartTs;
    }

    void begin() {
        m_tcpServer.begin();
        m_tcpServer.setNoDelay(true);
        m_udp.begin(TTY_UDP_PORT);
        DebugLogger::info("TTY", "Remote TTY servers started: TCP/Telnet port " + String(TTY_TCP_PORT) +
                          ", UDP port " + String(TTY_UDP_PORT) + ", Web /tty");
    }

    void loop() {
        handleTcpTty();
        handleUdpCli();
    }

    // Unified CLI Command Executor (used by TCP TTY, UDP CLI, Web /tty, and Telegram)
    String executeCommand(const String& rawLine, bool* outToggleWatch = nullptr) {
        String line = rawLine;
        line.trim();
        // Strip leading slash if called from Telegram style
        if (line.startsWith("/")) {
            line = line.substring(1);
        }
        if (line.length() == 0) return "";

        String lower = line;
        lower.toLowerCase();

        if (lower == "help" || lower == "?") {
            String h = "=== SandiSolar ESP32-S3 Remote TTY Console ===\n";
            h += "  status               - Повний стан інвертора, RS-485, Wi-Fi та Tailscale\n";
            h += "  log [N]              - Показати останні N рядків системного логу (за замовч. 30)\n";
            h += "  clear                - Очистити буфер системного логу\n";
            h += "  trace on | off       - Увімк/вимк HEX-трасування пакетів RS-485\n";
            h += "  watch on | off       - Живий потік логів у TCP/Telnet сесію (tail -f)\n";
            h += "  swap                 - Поміняти місцями піни RS-485 RX <-> TX (GPIO 17 <-> 18)\n";
            h += "  poll                 - Примусово опитати всі регістри RS-485 зараз\n";
            h += "  mb <fc> <addr> <cnt> - Читання Modbus (напр.: mb 3 0 1 або mb 4 42 10)\n";
            h += "  write <reg> <val>    - Запис Holding регістра 0x06 (напр.: write 0 1)\n";
            h += "  on | off             - Увімкнути (SoftON) або вимкнути (SoftOFF) інвертор\n";
            h += "  auto on | off        - Увімкнути або призупинити автоматику SoftOFF/SoftON\n";
            h += "  set <key> <val>      - Змінити параметр NVS:\n";
            h += "                         rx_pin, tx_pin, baud, slave, soc_off, soc_on, cold_t,\n";
            h += "                         grid_v, ts_en (0/1), ts_key (<key>|reset), ts_host\n";
            h += "  reboot               - Перезавантажити контролер ESP32-S3\n";
            return h;
        }

        if (lower == "status" || lower == "st") {
            SandisolarTelemetry t = m_modbus.getTelemetry();
            BatteryEtaInfo eta = SandisolarTelemetry::calculateEta(t.battery_soc, t.battery_current, m_config.battery_capacity_ah);
            String vpnIp = m_getVpnIp ? m_getVpnIp() : "0.0.0.0";
            String tsSt  = m_getTsStatus ? m_getTsStatus() : "N/A";

            String s = "=== СТАН КОНТРОЛЕРА ТА ІНВЕРТОРА ===\n";
            s += "Час: " + DebugLogger::getFormattedTime() + " | Uptime: " + String(millis() / 1000) + " с\n";
            s += "Wi-Fi: " + WiFi.SSID() + " (" + WiFi.localIP().toString() + ", " + String(WiFi.RSSI()) + " dBm)\n";
            s += "Tailscale: " + String(m_config.ts_enabled ? "EN" : "OFF") + " | IP: " + vpnIp + " | " + tsSt +
                 " | Ключ: " + String(m_config.hasCustomTailscaleKey() ? "Власний" : "Вшитий") + "\n";
            s += "Пам'ять: Heap=" + String(ESP.getFreeHeap() / 1024) + " KB | PSRAM=" + String(ESP.getFreePsram() / 1024) + " KB\n";
            s += "RS-485: " + String(t.online ? "ONLINE" : "OFFLINE") +
                 " | Піни: RX=GPIO" + String(m_modbus.getRxPin()) + ", TX=GPIO" + String(m_modbus.getTxPin()) +
                 " @ " + String(m_config.modbus_baud) + " bps (Slave=" + String(m_config.modbus_slave_id) + ")\n";
            s += "Лічильники RS-485: TX=" + String(t.tx_count) + " | RX=" + String(t.rx_count) +
                 " | Err=" + String(t.err_count) + " (" + t.last_error + ")\n";
            s += "Режим інвертора: " + t.inverter_mode_str + " | Живлення (reg 0): " + String(t.power_switch == 1 ? "ON" : "OFF") + "\n";
            s += "Мережа 220В: " + String(t.grid_voltage, 1) + " В / " + String(t.grid_power) + " Вт (" + String(t.grid_frequency, 1) + " Гц)\n";
            s += "Навантаження: " + String(t.power_out_load) + " Вт (" + String(t.power_total_apparent) + " ВА, " + String(t.inverter_voltage, 1) + " В)\n";
            s += "Сонце (PV1): " + String(t.pv1_power) + " Вт (" + String(t.pv1_voltage, 1) + " В, " + String(t.pv1_current, 1) + " А)\n";
            s += "Батарея: SOC=" + String(t.battery_soc) + "% | " + String(t.battery_voltage, 2) + " В | " +
                 String(t.battery_current, 1) + " А (" + eta.text + ")\n";
            s += "Температури: T1(Інв)=" + String(t.temp_inverter, 1) + "°C | T2(Boost)=" + String(t.temp_boost, 1) +
                 "°C | T3(LLC)=" + String(t.temp_llc, 1) + "°C | T4(АКБ)=" + String(t.bms_battery_temp, 1) + "°C\n";
            s += "Автоматика: " + String(m_config.auto_enabled ? "АКТИВНА" : "ПАУЗА") +
                 " (SoftOFF>=" + String(m_config.soc_off_threshold) + "%, SoftON<" + String(m_config.soc_on_threshold) +
                 "%, Холод<" + String(m_config.cold_temp_threshold, 1) + "°C)\n";
            return s;
        }

        if (lower == "log" || lower.startsWith("log ")) {
            int count = 30;
            if (line.length() > 4) {
                count = constrain(line.substring(4).toInt(), 1, 200);
            }
            return DebugLogger::toPlainText((size_t)count);
        }

        if (lower == "clear") {
            DebugLogger::clear();
            return "🧹 Буфер системного логу очищено.\n";
        }

        if (lower == "trace on") {
            DebugLogger::setTraceModbus(true);
            return "🟢 HEX-трасування пакетів RS-485 УВІМКНЕНО.\n";
        }
        if (lower == "trace off") {
            DebugLogger::setTraceModbus(false);
            return "⚪ HEX-трасування пакетів RS-485 ВИМКНЕНО.\n";
        }

        if (lower == "watch on") {
            if (outToggleWatch) *outToggleWatch = true;
            return "🟢 Живий потік логів (watch) УВІМКНЕНО для цієї сесії.\n";
        }
        if (lower == "watch off") {
            if (outToggleWatch) *outToggleWatch = false;
            return "⚪ Живий потік логів (watch) ВИМКНЕНО.\n";
        }

        if (lower == "swap" || lower == "swap_pins") {
            m_modbus.swapRxTxPins(true);
            bool ok = m_modbus.pollAll(true);
            return "🔄 Піни RS-485 поміняно місцями: RX=GPIO" + String(m_modbus.getRxPin()) +
                   ", TX=GPIO" + String(m_modbus.getTxPin()) +
                   " | Тестове опитування: " + (ok ? "✅ RS485 ONLINE!" : ("❌ " + m_modbus.getTelemetry().last_error)) + "\n";
        }

        if (lower == "poll") {
            bool ok = m_modbus.pollAll(true);
            return ok ? "✅ Всі регістри RS-485 успішно оновлено!\n"
                      : ("❌ Помилка опитування RS-485: " + m_modbus.getTelemetry().last_error + "\n");
        }

        if (lower == "on") {
            bool ok = m_modbus.setPowerSwitch(true);
            return ok ? "⚡ Інвертор УВІМКНЕНО (SoftON, reg 0 = 1)\n"
                      : ("❌ Помилка SoftON: " + m_modbus.getTelemetry().last_error + "\n");
        }

        if (lower == "off") {
            bool ok = m_modbus.setPowerSwitch(false);
            return ok ? "🛑 Інвертор ВИМКНЕНО (SoftOFF, reg 0 = 0)\n"
                      : ("❌ Помилка SoftOFF: " + m_modbus.getTelemetry().last_error + "\n");
        }

        if (lower == "auto on") {
            m_engine.setAutomationEnabled(true);
            return "▶️ Автоматику SoftOFF/SoftON УВІМКНЕНО.\n";
        }

        if (lower == "auto off") {
            m_engine.setAutomationEnabled(false);
            return "⏸️ Автоматику SoftOFF/SoftON ПРИЗУПИНЕНО.\n";
        }

        if (lower.startsWith("mb ") || lower.startsWith("raw ")) {
            // Syntax: mb <fc> <addr> <cnt>
            int firstSpace = line.indexOf(' ');
            String args = line.substring(firstSpace + 1);
            args.trim();
            int s1 = args.indexOf(' ');
            int s2 = args.indexOf(' ', s1 + 1);
            if (s1 < 0) {
                return "⚠️ Формат: mb <fc 3|4> <addr> [cnt=1] (напр.: mb 3 0 1)\n";
            }
            uint8_t fc = (uint8_t)args.substring(0, s1).toInt();
            uint16_t addr = 0;
            uint16_t cnt = 1;
            if (s2 < 0) {
                addr = (uint16_t)args.substring(s1 + 1).toInt();
            } else {
                addr = (uint16_t)args.substring(s1 + 1, s2).toInt();
                cnt = (uint16_t)constrain(args.substring(s2 + 1).toInt(), 1, 40);
            }
            if (fc != 3 && fc != 4) fc = 3;
            uint16_t regs[40];
            bool ok = m_modbus.readRegisters(fc, addr, cnt, regs);
            if (!ok) {
                return "❌ Помилка читання FC=0x0" + String(fc) + " addr=" + String(addr) +
                       ": " + m_modbus.getTelemetry().last_error + "\n";
            }
            String out = "✅ Відповідь Modbus (FC=0x0" + String(fc) + ", Addr=" + String(addr) + ", Cnt=" + String(cnt) + "):\n";
            for (uint16_t i = 0; i < cnt; ++i) {
                char buf[64];
                snprintf(buf, sizeof(buf), "  Reg %3u (0x%04X): %5u (0x%04X) | int16: %d\n",
                         (unsigned)(addr + i), (unsigned)(addr + i),
                         (unsigned)regs[i], (unsigned)regs[i], (int)(int16_t)regs[i]);
                out += buf;
            }
            return out;
        }

        if (lower.startsWith("write ")) {
            String args = line.substring(6);
            args.trim();
            int s1 = args.indexOf(' ');
            if (s1 < 0) return "⚠️ Формат: write <reg> <val> (напр.: write 0 1)\n";
            uint16_t reg = (uint16_t)args.substring(0, s1).toInt();
            uint16_t val = (uint16_t)args.substring(s1 + 1).toInt();
            bool ok = m_modbus.writeSingleRegister(reg, val);
            return ok ? ("✅ Записано регістр " + String(reg) + " = " + String(val) + "\n")
                      : ("❌ Помилка запису reg " + String(reg) + ": " + m_modbus.getTelemetry().last_error + "\n");
        }

        if (lower.startsWith("set ")) {
            String args = line.substring(4);
            args.trim();
            int s1 = args.indexOf(' ');
            if (s1 < 0) return "⚠️ Формат: set <key> <val>\n";
            String key = args.substring(0, s1);
            key.toLowerCase();
            String val = args.substring(s1 + 1);
            val.trim();

            bool rs485Changed = false;
            bool tsChanged = false;

            if (key == "rx_pin") { m_config.rs485_rx_pin = (int8_t)val.toInt(); rs485Changed = true; }
            else if (key == "tx_pin") { m_config.rs485_tx_pin = (int8_t)val.toInt(); rs485Changed = true; }
            else if (key == "baud") { m_config.modbus_baud = (uint32_t)val.toInt(); rs485Changed = true; }
            else if (key == "slave") { m_config.modbus_slave_id = (uint8_t)val.toInt(); rs485Changed = true; }
            else if (key == "soc_off") { m_config.soc_off_threshold = constrain(val.toInt(), 50, 100); }
            else if (key == "soc_on") { m_config.soc_on_threshold = constrain(val.toInt(), 15, 95); }
            else if (key == "cold_t") { m_config.cold_temp_threshold = val.toFloat(); }
            else if (key == "grid_v") { m_config.grid_v_threshold = val.toFloat(); }
            else if (key == "ts_en") { m_config.ts_enabled = (val == "1" || val == "true" || val == "on"); tsChanged = true; }
            else if (key == "ts_host") { m_config.ts_hostname = val; tsChanged = true; }
            else if (key == "ts_key") {
                if (val == "reset" || val == "default" || val == "builtin") {
                    m_config.ts_custom_key = "";
                } else {
                    m_config.ts_custom_key = val;
                }
                m_config.ts_auth_key = m_config.getEffectiveTailscaleKey();
                tsChanged = true;
            }
            else if (key == "tg_en") { m_config.tg_enabled = (val == "1" || val == "true" || val == "on"); }
            else if (key == "tg_tok") { m_config.tg_bot_token = val; }
            else if (key == "tg_chat") { m_config.tg_chat_id = val; }
            else {
                return "❌ Невідомий параметр '" + key + "'. Доступні: rx_pin, tx_pin, baud, slave, soc_off, soc_on, cold_t, grid_v, ts_en, ts_key, ts_host, tg_en, tg_tok, tg_chat\n";
            }

            ConfigManager::save(m_config);
            if (rs485Changed) m_modbus.begin(m_config);
            if (tsChanged && m_restartTs) m_restartTs();
            return "✅ Параметр '" + key + "' збережено та застосовано!\n";
        }

        if (lower == "reboot" || lower == "restart") {
            m_pendingRebootMs = millis() + 600;
            return "🔄 Перезавантаження ESP32-S3...\n";
        }

        return "❓ Невідома команда: '" + line + "'. Введіть 'help' для списку команд.\n";
    }

private:
    void handleTcpTty() {
        if (m_pendingRebootMs > 0 && millis() >= m_pendingRebootMs) {
            ESP.restart();
        }

        if (m_tcpServer.hasClient()) {
            WiFiClient incoming = m_tcpServer.accept();
            if (incoming) {
                if (m_tcpClient && m_tcpClient.connected()) {
                    m_tcpClient.stop();
                }
                m_tcpClient = incoming;
                m_tcpClient.setNoDelay(true);
                m_tcpBuf = "";
                m_watchLogs = false;
                m_lastWatchSeq = DebugLogger::getCurrentSeq();
                m_tcpClient.print("\r\n⚡ SandiSolar 6.5 Pro — ESP32-S3 Remote TTY Console (Port 2323)\r\n");
                m_tcpClient.print("Введіть 'help' для списку команд, 'status' для телеметрії, 'watch on' для живого логу.\r\n");
                m_tcpClient.print("sandisolar-s3> ");
            }
        }

        if (m_tcpClient && m_tcpClient.connected()) {
            while (m_tcpClient.available()) {
                char c = (char)m_tcpClient.read();
                // Ignore Telnet IAC negotiation bytes (0xFF ...)
                if ((uint8_t)c == 0xFF) {
                    if (m_tcpClient.available()) m_tcpClient.read();
                    if (m_tcpClient.available()) m_tcpClient.read();
                    continue;
                }
                if (c == '\r') continue;
                if (c == '\n') {
                    m_tcpBuf.trim();
                    if (m_tcpBuf.length() > 0) {
                        if (m_tcpBuf == "exit" || m_tcpBuf == "quit") {
                            m_tcpClient.print("Сесію закрито.\r\n");
                            m_tcpClient.stop();
                            m_tcpBuf = "";
                            return;
                        }
                        String resp = executeCommand(m_tcpBuf, &m_watchLogs);
                        m_lastWatchSeq = DebugLogger::getCurrentSeq();
                        sendChunkedToTcp(resp);
                    }
                    m_tcpBuf = "";
                    m_tcpClient.print("sandisolar-s3> ");
                } else if (m_tcpBuf.length() < 256) {
                    m_tcpBuf += c;
                }
            }

            if (m_watchLogs) {
                String newLogs = DebugLogger::getNewLinesSince(m_lastWatchSeq);
                if (newLogs.length() > 0) {
                    sendChunkedToTcp(newLogs);
                }
            }
        }
    }

    void sendChunkedToTcp(const String& text) {
        if (!m_tcpClient || !m_tcpClient.connected()) return;
        // Convert \n to \r\n and send in small MTU-safe chunks (<= 512 bytes)
        const size_t chunkSize = 512;
        size_t len = text.length();
        size_t pos = 0;
        while (pos < len) {
            size_t end = min(pos + chunkSize, len);
            m_tcpClient.write((const uint8_t*)(text.c_str() + pos), end - pos);
            pos = end;
        }
    }

    void handleUdpCli() {
        int packetSize = m_udp.parsePacket();
        if (packetSize > 0) {
            char buf[256];
            int len = m_udp.read(buf, sizeof(buf) - 1);
            if (len > 0) {
                buf[len] = '\0';
                String cmd(buf);
                String resp = executeCommand(cmd);
                // Keep UDP reply under 1100 bytes for WireGuard/DERP MTU
                if (resp.length() > 1100) {
                    resp = resp.substring(resp.length() - 1100);
                }
                m_udp.beginPacket(m_udp.remoteIP(), m_udp.remotePort());
                m_udp.write((const uint8_t*)resp.c_str(), resp.length());
                m_udp.endPacket();
            }
        }
    }

    WiFiServer          m_tcpServer;
    WiFiClient          m_tcpClient;
    WiFiUDP             m_udp;
    String              m_tcpBuf;
    bool                m_watchLogs = false;
    uint32_t            m_lastWatchSeq = 0;
    uint32_t            m_pendingRebootMs = 0;

    SandisolarModbus&   m_modbus;
    AutomationEngine&   m_engine;
    AppConfig&          m_config;

    std::function<String()> m_getVpnIp;
    std::function<String()> m_getTsStatus;
    std::function<void()>   m_restartTs;
};
