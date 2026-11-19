#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "AppConfig.h"
#include "SandisolarRegisters.h"
#include "SandisolarModbus.h"
#include "AutomationEngine.h"
#include "DebugLogger.h"

class TelegramBotManager {
public:
    TelegramBotManager(SandisolarModbus& modbus, AutomationEngine& engine, AppConfig& config)
        : m_modbus(modbus), m_engine(engine), m_config(config) {}

    void begin() {
        xTaskCreatePinnedToCore(
            [](void* param) {
                TelegramBotManager* self = static_cast<TelegramBotManager*>(param);
                self->workerLoop();
            },
            "tg_worker",
            12288,
            this,
            1,
            &m_taskHandle,
            0 // Pin to Core 0 (Network Core)
        );
        DebugLogger::info("TELEGRAM", "Interactive Telegram Bot task started on Core 0 (12KB stack)");
    }

    void setTailscaleIp(const String& ip) {
        m_vpnIp = ip;
    }

    bool sendTestMessage(String& outErr) {
        if (m_config.tg_bot_token.length() == 0 || m_config.tg_chat_id.length() == 0) {
            outErr = "Не заповнено Token або Chat ID";
            return false;
        }
        if (WiFi.status() != WL_CONNECTED) {
            outErr = "Wi-Fi не підключено";
            return false;
        }
        String msg = buildStatusCard();
        msg = "🔔 <b>Тестове з'єднання з ESP32-S3 SandiSolar!</b>\n\n" + msg;
        return sendMessage(m_config.tg_chat_id, msg, true, &outErr);
    }

private:
    static String getMainKeyboardJson() {
        return R"rawliteral({"keyboard":[[{"text":"📊 Загальний статус"}],[{"text":"🔋 Батарея"},{"text":"⚡ Мережа"}],[{"text":"☀️ Сонце"},{"text":"🏠 Споживання"}],[{"text":"🌡 Температури"},{"text":"⚙️ Автоматика"}],[{"text":"🟢 SoftON"},{"text":"🛑 SoftOFF"}]],"resize_keyboard":true,"is_persistent":true})rawliteral";
    }

    bool isChatAuthorized(const String& chatId) const {
        if (chatId.length() == 0) return false;
        if (chatId == m_config.tg_chat_id) return true;
        String list = m_config.tg_allowed_ids;
        int start = 0;
        while (start < (int)list.length()) {
            int sep = list.indexOf(',', start);
            if (sep == -1) sep = list.indexOf(' ', start);
            if (sep == -1) sep = list.indexOf(';', start);
            if (sep == -1) sep = list.length();
            String token = list.substring(start, sep);
            token.trim();
            if (token.length() > 0 && token == chatId) return true;
            start = sep + 1;
        }
        return false;
    }

    bool sendMessage(const String& chatId, const String& htmlText, bool withKeyboard = true, String* outErr = nullptr) {
        if (m_config.tg_bot_token.length() == 0 || chatId.length() == 0) return false;
        if (WiFi.status() != WL_CONNECTED) return false;

        WiFiClientSecure client;
        client.setInsecure();
        client.setTimeout(10);

        HTTPClient http;
        http.setTimeout(10000);
        String url = "https://api.telegram.org/bot" + m_config.tg_bot_token + "/sendMessage";

        JsonDocument doc;
        doc["chat_id"] = chatId;
        doc["text"] = htmlText;
        doc["parse_mode"] = "HTML";
        doc["disable_web_page_preview"] = true;
        if (withKeyboard) {
            JsonDocument kbDoc;
            deserializeJson(kbDoc, getMainKeyboardJson());
            doc["reply_markup"] = kbDoc.as<JsonObject>();
        }

        String payload;
        serializeJson(doc, payload);

        bool ok = false;
        if (http.begin(client, url)) {
            http.addHeader("Content-Type", "application/json");
            int code = http.POST(payload);
            if (code == 200) {
                ok = true;
            } else {
                String resp = http.getString();
                if (outErr) *outErr = "HTTP " + String(code) + ": " + resp;
                DebugLogger::warn("TELEGRAM", "sendMessage failed HTTP " + String(code));
            }
            http.end();
        } else {
            if (outErr) *outErr = "HTTPS begin failed";
        }
        return ok;
    }

    // --- Telemetry Cards (ported 1:1 from telegram_bot.py) ---
    String buildStatusCard() const {
        SandisolarTelemetry t = m_modbus.getTelemetry();
        String nowStr = DebugLogger::getFormattedTime();
        bool hasGrid = (t.grid_voltage >= m_config.grid_v_threshold);

        String battDir;
        if (t.battery_current > 1.0f) {
            battDir = "⏳ Заряджання (+" + String(t.battery_current, 1) + " А)";
        } else if (t.battery_current < -1.0f) {
            battDir = "⚡ Розряджання (" + String(t.battery_current, 1) + " А)";
        } else {
            battDir = "⏸ Очікування (0.0 А)";
        }

        String battIcon = (t.battery_soc >= 30) ? "🔋" : "🪫";
        String gridIcon = hasGrid ? "🟢 СВІТЛО Є" : "🔴 СВІТЛА НЕМАЄ";

        String faultBanner = "";
        if (t.inverter_status == 3) {
            faultBanner = "🚨 <b>УВАГА: Інвертор у стані FAULT / АВАРІЯ!</b>\n\n";
        } else if (!t.online) {
            faultBanner = "⚠️ <b>УВАГА: Немає зв'язку RS-485 з інвертором!</b>\n\n";
        }

        String t4Badge = (t.bms_battery_temp < m_config.cold_temp_threshold && t.bms_battery_temp > -30.0f)
                         ? ("❄️ T4: " + String(t.bms_battery_temp, 1) + "°C (Холод!)")
                         : ("T4: " + String(t.bms_battery_temp, 1) + "°C");

        BatteryEtaInfo eta = SandisolarTelemetry::calculateEta(t.battery_soc, t.battery_current, m_config.battery_capacity_ah);
        String etaInline = (eta.mode != "idle") ? (" | ⏳ " + eta.text) : "";

        String msg = faultBanner +
            "📊 <b>Загальний статус SandiSolar 6.5 Pro</b>\n"
            "🕒 <i>" + nowStr + "</i>\n\n"
            "⚡ <b>Мережа:</b> " + gridIcon + " (" + String(t.grid_voltage, 1) + " В, " + String(t.grid_frequency, 2) + " Гц)\n"
            "🔌 <b>Інвертор:</b> " + String(t.power_switch ? "Увімкнено (генерація)" : "Вимкнено (байпас/SoftOFF)") + " [<i>" + t.inverter_mode_str + "</i>]\n" +
            battIcon + " <b>АКБ:</b> <b>" + String(t.battery_soc) + "%</b> (" + String(t.battery_voltage, 1) + " В, " + battDir + etaInline + ")\n"
            "🏠 <b>Споживання будинку:</b> " + String(t.power_out_load, 0) + " Вт\n"
            "☀️ <b>Сонячні панелі:</b> " + String(t.pv1_power, 0) + " Вт (сьогодні: " + String(t.solar_gen_today, 2) + " кВт·год)\n"
            "🌡 <b>Температури:</b> T1: " + String(t.temp_inverter, 1) + "° | T2: " + String(t.temp_boost, 1) + "° | T3: " + String(t.temp_llc, 1) + "° | " + t4Badge + "\n"
            "🌐 <b>LAN:</b> <code>http://" + WiFi.localIP().toString() + "/</code>";
        if (m_vpnIp.length() > 0) {
            msg += "\n🔒 <b>Tailscale:</b> <code>http://" + m_vpnIp + "/</code>";
        }
        return msg;
    }

    String buildBatteryCard() const {
        SandisolarTelemetry t = m_modbus.getTelemetry();
        String nowStr = DebugLogger::getFormattedTime();

        String dirText;
        if (t.battery_current > 1.0f) {
            dirText = "🟢 Заряджання (+" + String(t.battery_current, 1) + " А / +" + String(fabsf(t.battery_power), 0) + " Вт)";
        } else if (t.battery_current < -1.0f) {
            dirText = "🔴 Розряджання (" + String(t.battery_current, 1) + " А / -" + String(fabsf(t.battery_power), 0) + " Вт)";
        } else {
            dirText = "⚪ Очікування (буферний режим)";
        }

        String battIcon = (t.battery_soc >= 30) ? "🔋" : "🪫";
        BatteryEtaInfo eta = SandisolarTelemetry::calculateEta(t.battery_soc, t.battery_current, m_config.battery_capacity_ah);
        String etaSection;
        if (eta.mode == "charging") {
            etaSection = "⏱ <b>Прогноз заряду:</b> " + eta.text + " (+" + String(eta.speed_pct_hr, 1) + "%/год)\n";
        } else if (eta.mode == "discharging") {
            etaSection = "⏱ <b>Прогноз автономності:</b> " + eta.text + " (" + String(eta.speed_pct_hr, 1) + "%/год)\n";
        } else {
            etaSection = "⏱ <b>Режим батареї:</b> Буферний стан спокою\n";
        }

        String tempStatus = "";
        if (t.bms_battery_temp < m_config.cold_temp_threshold && t.bms_battery_temp > -30.0f) {
            tempStatus = "\n❄️ <b>Холодний захист:</b> Активний (&lt; " + String(m_config.cold_temp_threshold, 1) + " °C — заряд від мережі блокується для захисту LiFePO4)\n";
        } else if (t.bms_battery_temp < 10.0f && t.bms_battery_temp > -30.0f) {
            tempStatus = "\n🌡️ <b>Температурний стан:</b> Прохолодно (3–10 °C, рекомендовано ощадливий струм)\n";
        }

        String msg = battIcon + " <b>Стан акумуляторної батареї (BMS)</b>\n"
            "🕒 <i>" + nowStr + "</i>\n\n"
            "📊 <b>Заряд (SOC):</b> <b>" + String(t.battery_soc) + "%</b>\n"
            "⚡ <b>Напруга:</b> " + String(t.battery_voltage, 2) + " В\n"
            "🔄 <b>Режим струму:</b> " + dirText + "\n" +
            etaSection +
            "🩺 <b>Стан здоров'я (SOH):</b> " + String(t.battery_soh) + "%\n"
            "🌡 <b>Температура АКБ (T4 BMS):</b> " + String(t.bms_battery_temp, 1) + " °C\n" +
            tempStatus + "\n"
            "🛡 <b>Дозволені ліміти BMS / Налаштування:</b>\n"
            "  • Ліміт BMS заряд / розряд: " + String(t.bms_max_charge_a, 1) + " А / " + String(t.bms_max_discharge_a, 1) + " А\n"
            "  • Заряд від мережі (reg 189): " + String(t.ac_grid_charge_a, 0) + " А\n"
            "  • Макс. сумарний заряд (reg 129): " + String(t.battery_max_charge_a, 0) + " А\n"
            "  • Заряд сьогодні / всього: " + String(t.energy_charge_today, 1) + " / " + String(t.energy_charge_total, 1) + " кВт·год\n"
            "  • Розряд сьогодні / всього: " + String(t.energy_disch_today, 1) + " / " + String(t.energy_disch_total, 1) + " кВт·год";
        return msg;
    }

    String buildGridCard() const {
        SandisolarTelemetry t = m_modbus.getTelemetry();
        String nowStr = DebugLogger::getFormattedTime();
        bool hasGrid = (t.grid_voltage >= m_config.grid_v_threshold);
        String badge = hasGrid ? "🟢 СВІТЛО Є (Мережа підключена)" : "🔴 СВІТЛА НЕМАЄ (Аварія / Відключення)";

        return "⚡ <b>Параметри міської електромережі</b>\n"
               "🕒 <i>" + nowStr + "</i>\n\n"
               "💡 <b>Статус:</b> <b>" + badge + "</b>\n"
               "🔌 <b>Напруга:</b> " + String(t.grid_voltage, 1) + " В\n"
               "📈 <b>Вхідний струм:</b> " + String(t.grid_current, 2) + " А\n"
               "⚡ <b>Потужність на вході:</b> " + String(t.grid_power, 0) + " Вт\n"
               "〰️ <b>Частота мережі:</b> " + String(t.grid_frequency, 2) + " Гц\n"
               "🛡 <b>Режим чутливості AC (reg 209):</b> " + String(t.ac_input_type == 1 ? "UPS (170-280V)" : "APL (90-280V)");
    }

    String buildSolarCard() const {
        SandisolarTelemetry t = m_modbus.getTelemetry();
        String nowStr = DebugLogger::getFormattedTime();
        String icon = (t.pv1_power > 50.0f) ? "☀️" : "🌙";

        return icon + " <b>Сонячна генерація (PV1 Стринг)</b>\n"
               "🕒 <i>" + nowStr + "</i>\n\n"
               "⚡ <b>Поточна потужність:</b> <b>" + String(t.pv1_power, 0) + " Вт</b>\n"
               "🔌 <b>Напруга стринга:</b> " + String(t.pv1_voltage, 1) + " В\n"
               "📈 <b>Струм стринга:</b> " + String(t.pv1_current, 2) + " А\n"
               "📅 <b>Генерація за сьогодні:</b> " + String(t.solar_gen_today, 2) + " кВт·год\n"
               "🌐 <b>Загальна генерація:</b> " + String(t.solar_gen_total, 1) + " кВт·год\n"
               "⛽ <b>Вхід генератора:</b> " + String(t.generator_voltage, 1) + " В / " + String(t.generator_current, 1) + " А";
    }

    String buildLoadCard() const {
        SandisolarTelemetry t = m_modbus.getTelemetry();
        String nowStr = DebugLogger::getFormattedTime();
        String source = (t.grid_voltage >= m_config.grid_v_threshold && !t.power_switch)
                        ? "Міська мережа (Чистий Байпас / SoftOFF)"
                        : (t.grid_voltage >= m_config.grid_v_threshold ? "Мережа + Інвертор" : "Акумулятор (Автономна генерація 220В)");

        return "🏠 <b>Навантаження та споживання будинку</b>\n"
               "🕒 <i>" + nowStr + "</i>\n\n"
               "⚡ <b>Активна потужність дому:</b> <b>" + String(t.power_out_load, 0) + " Вт</b>\n"
               "🔌 <b>Повна потужність:</b> " + String(t.power_total_apparent, 0) + " ВА\n"
               "〰️ <b>Реактивна потужність:</b> " + String(t.power_total_reactive, 0) + " ВАр\n"
               "📐 <b>Коефіцієнт потужності (cos φ):</b> " + String(t.power_factor, 2) + "\n"
               "⚡ <b>Вихід інвертора / EPS:</b> " + String(t.inverter_voltage, 1) + " В / " + String(t.eps_voltage, 1) + " В\n"
               "🔋 <b>Джерело живлення:</b> " + source + "\n"
               "🕹 <b>Стан Power Switch (reg 0):</b> " + String(t.power_switch ? "ON (SoftON)" : "OFF (SoftOFF)");
    }

    static String tempBadge(float val, bool isBattery = false) {
        if (val <= -20.0f) return "⚪";
        if (isBattery) {
            if (val < 3.0f)  return "❄️";
            if (val < 10.0f) return "🔵";
            if (val < 45.0f) return "🟢";
            if (val < 55.0f) return "🟡";
            return "🔴";
        } else {
            if (val < 55.0f) return "🟢";
            if (val < 70.0f) return "🟡";
            return "🔴";
        }
    }

    String buildTempsCard() const {
        SandisolarTelemetry t = m_modbus.getTelemetry();
        String nowStr = DebugLogger::getFormattedTime();
        String coldWarn = "";
        if (t.bms_battery_temp < m_config.cold_temp_threshold && t.bms_battery_temp > -20.0f) {
            coldWarn = "\n❄️ <b>УВАГА:</b> Температура АКБ нижче +" + String(m_config.cold_temp_threshold, 1) +
                       " °C! Автоматика блокує заряд для захисту осередків LiFePO4.\n";
        }

        return "🌡 <b>Температурна діагностика системи (T1–T4)</b>\n"
               "🕒 <i>" + nowStr + "</i>\n\n" +
               tempBadge(t.temp_inverter) + " <b>T1 — Радіатор інвертора:</b> " + String(t.temp_inverter, 1) + " °C\n" +
               tempBadge(t.temp_boost) + " <b>T2 — Boost / IPM (DC-DC / IGBT):</b> " + String(t.temp_boost, 1) + " °C\n" +
               tempBadge(t.temp_llc) + " <b>T3 — LLC силовий трансформатор:</b> " + String(t.temp_llc, 1) + " °C\n" +
               tempBadge(t.bms_battery_temp, true) + " <b>T4 — Акумулятор (BMS сенсор):</b> " + String(t.bms_battery_temp, 1) + " °C\n"
               "🌀 <b>Оберти вентилятора:</b> " + String(t.fan_speed) + " RPM\n"
               "⏳ <b>Загальний час роботи:</b> " + String(t.total_operation_time, 1) + " год\n" +
               coldWarn + "\n"
               "ℹ️ <i>Нормальний діапазон інвертора: до 60–65 °C (захист &gt;85 °C). Безпечний заряд LiFePO4: від +3 °C до +45 °C.</i>";
    }

    String buildAutoCard() const {
        SandisolarTelemetry t = m_modbus.getTelemetry();
        String nowStr = DebugLogger::getFormattedTime();
        bool hasGrid = (t.grid_voltage >= m_config.grid_v_threshold);
        String coldState = (t.bms_battery_temp < m_config.cold_temp_threshold && t.bms_battery_temp > -20.0f)
                           ? "❄️ АКТИВНИЙ (заряд заблоковано)"
                           : "🟢 Норма (заряд дозволено)";

        return "⚙️ <b>Параметри та статус автоматики захисту (ESP32-S3)</b>\n"
               "🕒 <i>" + nowStr + "</i>\n\n"
               "🤖 <b>Ядро автоматики:</b> " + String(m_config.auto_enabled ? "🟢 Активне" : "⏸ На паузі (ручний режим)") + "\n"
               "⚡ <b>Мережевий статус:</b> " + String(hasGrid ? "СВІТЛО Є" : "СВІТЛА НЕМАЄ") + " (" + String(t.grid_voltage, 1) + " В, дебаунс " + String(m_engine.getConsecutiveCount()) + "/2)\n"
               "🔋 <b>Поточний заряд АКБ:</b> " + String(t.battery_soc) + "%\n"
               "🌡️ <b>Температура АКБ:</b> " + String(t.bms_battery_temp, 1) + " °C\n"
               "⏱ <b>Наступна перевірка через:</b> " + String(m_engine.getSecondsUntilNextEval()) + " с\n\n"
               "📋 <b>Встановлені пороги:</b>\n"
               "  • Вимкнення (SoftOFF) при поверненні світла: <b>SOC ≥ " + String(m_config.soc_off_threshold) + "%</b>\n"
               "  • Примусовий заряд при наявності світла: <b>SOC &lt; " + String(m_config.soc_on_threshold) + "%</b>\n"
               "  • Зимовий захист АКБ від холоду: <b>&lt; " + String(m_config.cold_temp_threshold, 1) + " °C</b> (" + coldState + ")\n"
               "  • Поріг розпізнавання аварії мережі: <b>&lt; " + String(m_config.grid_v_threshold, 1) + " В</b> (2 цикли по " + String(m_config.eval_interval_s) + " с)\n\n"
               "📝 <b>Остання зафіксована подія:</b>\n"
               "<code>" + m_engine.getLastEventTime() + " | " + m_engine.getLastAction() + "</code>\n\n"
               "🕹 <b>Команди керування з чату:</b>\n"
               "• /on або кнопка <b>🟢 SoftON</b> — Увімкнути інвертор\n"
               "• /off або кнопка <b>🛑 SoftOFF</b> — Вимкнути (Байпас)\n"
               "• /auto_on / /auto_off — Увімкнути / призупинити автоматику\n"
               "• <code>/ac_curr 30</code> — Встановити струм заряду від мережі (А)\n"
               "• <code>/soc_off 95</code> — Встановити поріг SoftOFF (%)\n"
               "• <code>/soc_on 75</code> — Встановити поріг старту зарядки (%)";
    }

    String buildHelpCard() const {
        return "👋 <b>SandiSolar 6.5 Pro — ESP32-S3 Standalone Bot</b>\n\n"
               "Я працюю безпосередньо на мікроконтролері ESP32-S3 через ізольований порт RS-485 (XY-S485) без зовнішніх серверів чи MQTT.\n\n"
               "<b>Доступні розділи та команди:</b>\n"
               "• <b>/status</b> — Загальний зріз (мережа, АКБ, інвертор, навантаження, сонце, IP)\n"
               "• <b>/battery</b> — Детальна телеметрія акумулятора, BMS та прогноз ETA\n"
               "• <b>/grid</b> — Параметри міської мережі (напруга, частота, потужність)\n"
               "• <b>/solar</b> — Поточна потужність та генерація сонячних панелей\n"
               "• <b>/load</b> — Споживання будинку та навантаження\n"
               "• <b>/temps</b> — Температурна карта компонентів (T1–T4) та вентилятор\n"
               "• <b>/auto</b> — Стан, пороги та журнал автоматики SoftOFF\n"
               "• <b>/on</b> (🟢 SoftON) — Примусово увімкнути генерацію інвертора\n"
               "• <b>/off</b> (🛑 SoftOFF) — Перевести інвертор у SoftOFF (чистий байпас)\n"
               "• <b>/auto_on</b> / <b>/auto_off</b> — Увімк./вимк. автоматичне керування\n"
               "• <b>/menu</b> — Показати кнопки меню";
    }

    void handleIncomingCommand(const String& chatId, const String& userName, const String& rawText) {
        String text = rawText;
        text.trim();
        String cmd = text;
        cmd.toLowerCase();

        DebugLogger::info("TELEGRAM", "Cmd from @" + userName + " (" + chatId + "): " + text);

        if (cmd == "/start" || cmd == "/help" || cmd == "/menu" || cmd == "меню" || cmd == "допомога") {
            sendMessage(chatId, buildHelpCard(), true);
        } else if (cmd == "/status" || cmd == "📊 загальний статус" || cmd == "статус") {
            sendMessage(chatId, buildStatusCard(), true);
        } else if (cmd == "/battery" || cmd == "🔋 батарея" || cmd == "акб" || cmd == "акумулятор") {
            sendMessage(chatId, buildBatteryCard(), true);
        } else if (cmd == "/grid" || cmd == "⚡ мережа" || cmd == "мережа" || cmd == "світло") {
            sendMessage(chatId, buildGridCard(), true);
        } else if (cmd == "/solar" || cmd == "☀️ сонце" || cmd == "сонце" || cmd == "панелі") {
            sendMessage(chatId, buildSolarCard(), true);
        } else if (cmd == "/load" || cmd == "/power" || cmd == "🏠 споживання" || cmd == "споживання") {
            sendMessage(chatId, buildLoadCard(), true);
        } else if (cmd == "/temps" || cmd == "🌡 температури" || cmd == "температури") {
            sendMessage(chatId, buildTempsCard(), true);
        } else if (cmd == "/auto" || cmd == "/rules" || cmd == "⚙️ автоматика" || cmd == "автоматика") {
            sendMessage(chatId, buildAutoCard(), true);
        } else if (cmd == "/on" || cmd == "🟢 softon" || cmd == "softon") {
            bool ok = m_modbus.setPowerSwitch(true);
            m_engine.addEventLog("tg_cmd_on", String("Команда з Telegram (@") + userName + "): SoftON -> " + (ok ? "OK" : "ERROR"));
            sendMessage(chatId, ok ? "✅ <b>Команду SoftON (reg 0 = 1) успішно виконано!</b>\nІнвертор увімкнено."
                                   : ("❌ <b>Помилка виконання SoftON:</b> " + m_modbus.getTelemetry().last_error), true);
        } else if (cmd == "/off" || cmd == "🛑 softoff" || cmd == "softoff") {
            bool ok = m_modbus.setPowerSwitch(false);
            m_engine.addEventLog("tg_cmd_off", String("Команда з Telegram (@") + userName + "): SoftOFF -> " + (ok ? "OK" : "ERROR"));
            sendMessage(chatId, ok ? "🛑 <b>Команду SoftOFF (reg 0 = 0) успішно виконано!</b>\nІнвертор переведено в режим очікування / байпас."
                                   : ("❌ <b>Помилка виконання SoftOFF:</b> " + m_modbus.getTelemetry().last_error), true);
        } else if (cmd == "/auto_on") {
            m_engine.setAutomationEnabled(true);
            sendMessage(chatId, "▶️ <b>Автоматичне керування інвертором УВІМКНЕНО!</b>", true);
        } else if (cmd == "/auto_off") {
            m_engine.setAutomationEnabled(false);
            sendMessage(chatId, "⏸ <b>Автоматичне керування ПРИЗУПИНЕНО (режим моніторингу).</b>", true);
        } else if (cmd.startsWith("/ac_curr ")) {
            float val = cmd.substring(9).toFloat();
            if (val >= 0.0f && val <= 100.0f) {
                uint16_t raw = (uint16_t)roundf(val * 10.0f);
                bool ok = m_modbus.writeSingleRegister(189, raw);
                sendMessage(chatId, ok ? ("✅ <b>Струм заряду від мережі (reg 189) встановлено на " + String(val, 1) + " А!</b>")
                                       : "❌ Помилка запису регістра 189", true);
            } else {
                sendMessage(chatId, "⚠️ Вкажіть струм від 0 до 100 А, наприклад: <code>/ac_curr 30</code>", true);
            }
        } else if (cmd.startsWith("/max_curr ")) {
            float val = cmd.substring(10).toFloat();
            if (val >= 10.0f && val <= 120.0f) {
                uint16_t raw = (uint16_t)roundf(val * 100.0f);
                bool ok = m_modbus.writeSingleRegister(129, raw);
                sendMessage(chatId, ok ? ("✅ <b>Макс. сумарний струм заряду (reg 129) встановлено на " + String(val, 1) + " А!</b>")
                                       : "❌ Помилка запису регістра 129", true);
            } else {
                sendMessage(chatId, "⚠️ Вкажіть струм від 10 до 120 А, наприклад: <code>/max_curr 60</code>", true);
            }
        } else if (cmd.startsWith("/soc_off ")) {
            int val = cmd.substring(9).toInt();
            if (val >= 50 && val <= 100) {
                m_config.soc_off_threshold = val;
                ConfigManager::save(m_config);
                m_engine.addEventLog("tg_cfg", "Змінено поріг SoftOFF через Telegram на " + String(val) + "%");
                sendMessage(chatId, "✅ <b>Поріг вимкнення інвертора (SoftOFF) збережено: " + String(val) + "%</b>", true);
            } else {
                sendMessage(chatId, "⚠️ Вкажіть значення від 50 до 100%, наприклад: <code>/soc_off 95</code>", true);
            }
        } else if (cmd.startsWith("/soc_on ")) {
            int val = cmd.substring(8).toInt();
            if (val >= 20 && val <= 95) {
                m_config.soc_on_threshold = val;
                ConfigManager::save(m_config);
                m_engine.addEventLog("tg_cfg", "Змінено поріг старту зарядки через Telegram на " + String(val) + "%");
                sendMessage(chatId, "✅ <b>Поріг увімкнення зарядки від мережі збережено: " + String(val) + "%</b>", true);
            } else {
                sendMessage(chatId, "⚠️ Вкажіть значення від 20 до 95%, наприклад: <code>/soc_on 75</code>", true);
            }
        } else {
            sendMessage(chatId, "❓ Команда <code>" + text + "</code> не розпізнана.\nСкористайтеся кнопками меню внизу або надішліть /help.", true);
        }
    }

    void pollUpdates() {
        if (!m_config.tg_enabled || m_config.tg_bot_token.length() == 0) return;
        if (WiFi.status() != WL_CONNECTED) return;

        WiFiClientSecure client;
        client.setInsecure();
        client.setTimeout(12);

        HTTPClient http;
        http.setTimeout(12000);
        String url = "https://api.telegram.org/bot" + m_config.tg_bot_token +
                     "/getUpdates?offset=" + String(m_lastUpdateId + 1) + "&limit=5&timeout=3";

        if (http.begin(client, url)) {
            int code = http.GET();
            if (code == 200) {
                String body = http.getString();
                JsonDocument doc;
                DeserializationError err = deserializeJson(doc, body);
                if (!err && doc["ok"].as<bool>()) {
                    JsonArray arr = doc["result"].as<JsonArray>();
                    for (JsonObject upd : arr) {
                        int64_t updId = upd["update_id"].as<int64_t>();
                        if (updId > m_lastUpdateId) {
                            m_lastUpdateId = updId;
                        }
                        if (upd["message"].is<JsonObject>()) {
                            JsonObject msg = upd["message"].as<JsonObject>();
                            String chatId = String(msg["chat"]["id"].as<int64_t>());
                            String user = msg["from"]["username"].is<const char*>()
                                          ? String(msg["from"]["username"].as<const char*>())
                                          : String(msg["from"]["first_name"] | "User");
                            String text = String(msg["text"] | "");
                            if (text.length() == 0) continue;

                            if (!isChatAuthorized(chatId)) {
                                DebugLogger::warn("TELEGRAM", "⛔ Unauthorized Chat ID " + chatId + " (@" + user + "): " + text);
                                sendMessage(chatId, "⛔ <b>Доступ заборонено!</b>\nВаш Chat ID: <code>" + chatId + "</code> не внесено у білий список контролера.", false);
                                continue;
                            }
                            handleIncomingCommand(chatId, user, text);
                        }
                    }
                }
            }
            http.end();
        }
    }

    void workerLoop() {
        vTaskDelay(pdMS_TO_TICKS(3000));
        while (true) {
            if (WiFi.status() == WL_CONNECTED && m_config.tg_enabled && m_config.tg_bot_token.length() > 0) {
                // 1. Flush any outgoing alerts queued by AutomationEngine
                String outMsg;
                while (m_engine.popTelegramMessage(outMsg)) {
                    sendMessage(m_config.tg_chat_id, outMsg, true);
                    vTaskDelay(pdMS_TO_TICKS(300));
                }

                // 2. Poll incoming interactive bot commands
                if (millis() - m_lastPollMs >= 3500) {
                    m_lastPollMs = millis();
                    pollUpdates();
                }
            }
            vTaskDelay(pdMS_TO_TICKS(400));
        }
    }

    SandisolarModbus& m_modbus;
    AutomationEngine& m_engine;
    AppConfig&        m_config;
    TaskHandle_t      m_taskHandle = nullptr;
    int64_t           m_lastUpdateId = 0;
    uint32_t          m_lastPollMs = 0;
    String            m_vpnIp = "";
};
