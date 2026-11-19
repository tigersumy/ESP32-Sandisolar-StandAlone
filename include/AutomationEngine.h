#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "AppConfig.h"
#include "SandisolarRegisters.h"
#include "SandisolarModbus.h"
#include "DebugLogger.h"

struct AutoEventLog {
    uint32_t timestamp_ms;
    String   wall_time;
    String   event_type;
    String   message;
};

class AutomationEngine {
public:
    AutomationEngine(SandisolarModbus& modbus, AppConfig& config)
        : m_modbus(modbus), m_config(config) {
        m_queueMutex = xSemaphoreCreateMutex();
        m_logMutex   = xSemaphoreCreateMutex();
    }

    void enqueueTelegram(const String& htmlMsg) {
        if (!m_config.tg_enabled || m_config.tg_bot_token.length() == 0) return;
        if (xSemaphoreTake(m_queueMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (m_tgOutbox.size() >= 25) {
                m_tgOutbox.erase(m_tgOutbox.begin());
            }
            m_tgOutbox.push_back(htmlMsg);
            xSemaphoreGive(m_queueMutex);
        }
    }

    bool popTelegramMessage(String& outMsg) {
        bool found = false;
        if (xSemaphoreTake(m_queueMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (!m_tgOutbox.empty()) {
                outMsg = m_tgOutbox.front();
                m_tgOutbox.erase(m_tgOutbox.begin());
                found = true;
            }
            xSemaphoreGive(m_queueMutex);
        }
        return found;
    }

    void addEventLog(const String& eventType, const String& msg) {
        DebugLogger::info("AUTO", "[" + eventType + "] " + msg);
        String wTime = DebugLogger::getFormattedTime();
        if (xSemaphoreTake(m_logMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            AutoEventLog item;
            item.timestamp_ms = millis();
            item.wall_time    = wTime;
            item.event_type   = eventType;
            item.message      = msg;
            m_eventLogs.insert(m_eventLogs.begin(), item);
            if (m_eventLogs.size() > 50) {
                m_eventLogs.pop_back();
            }
            m_lastActionText = msg;
            m_lastEventType  = eventType;
            m_lastEventTime  = wTime;
            xSemaphoreGive(m_logMutex);
        }
    }

    std::vector<AutoEventLog> getEventLogs() const {
        std::vector<AutoEventLog> copy;
        if (xSemaphoreTake(m_logMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            copy = m_eventLogs;
            xSemaphoreGive(m_logMutex);
        }
        return copy;
    }

    String getLastAction() const { return m_lastActionText; }
    String getLastEventType() const { return m_lastEventType; }
    String getLastEventTime() const { return m_lastEventTime; }
    bool   isFrozen() const { return m_isFrozen; }
    int    getConsecutiveCount() const { return m_consecutiveCount; }
    String getStableStatus() const { return m_stableStatus; }
    bool   isColdBlocked() const { return m_coldBatteryAlertSent; }
    bool   isAutomationEnabled() const { return m_config.auto_enabled; }

    void setAutomationEnabled(bool en) {
        m_config.auto_enabled = en;
        ConfigManager::save(m_config);
        addEventLog(en ? "auto_resumed" : "auto_paused",
                    en ? "▶️ АВТОМАТИКА: Відновлено автоматичний режим керування"
                       : "⏸️ АВТОМАТИКА: Призупинено (режим тільки моніторинг)");
    }

    uint32_t getSecondsUntilNextEval() const {
        if (m_lastEvalMs == 0) return 0;
        uint32_t elapsed = (millis() - m_lastEvalMs) / 1000;
        if (elapsed >= m_config.eval_interval_s) return 0;
        return m_config.eval_interval_s - elapsed;
    }

    void notifyStartup(const String& vpnIp = "") {
        if (m_startupNotified) return;
        SandisolarTelemetry t = m_modbus.getTelemetry();
        if (!t.online) return; // Wait for first valid Modbus read

        m_startupNotified = true;
        bool hasGrid = (t.grid_voltage >= m_config.grid_v_threshold);
        String invStr = t.power_switch ? "ON (Увімкнено / Генерація)" : "OFF (Вимкнено / Байпас)";

        String msg = "🚀 <b>ESP32-S3 SandiSolar контролер онлайн!</b>\n"
                     "<i>Автономний RS-485 Modbus RTU шлюз (без MQTT/сервера)</i>\n\n"
                     "⚡ <b>Мережа:</b> " + String(t.grid_voltage, 1) + " В (" + (hasGrid ? "🟢 СВІТЛО Є" : "🔴 СВІТЛА НЕМАЄ") + ")\n"
                     "🔋 <b>Батарея:</b> " + String(t.battery_soc) + "% (" + String(t.battery_voltage, 1) + " В, " + String(t.battery_current, 1) + " А)\n"
                     "🔌 <b>Інвертор:</b> " + invStr + " [" + t.inverter_mode_str + "]\n"
                     "🏠 <b>Навантаження:</b> " + String(t.power_out_load, 0) + " Вт | ☀️ <b>Сонце:</b> " + String(t.pv1_power, 0) + " Вт\n"
                     "🌡️ <b>Температури:</b> T1=" + String(t.temp_inverter, 1) + "°C | T2=" + String(t.temp_boost, 1) + "°C | T3=" + String(t.temp_llc, 1) + "°C | T4(АКБ)=" + String(t.bms_battery_temp, 1) + "°C\n"
                     "🌐 <b>LAN Web UI:</b> http://" + WiFi.localIP().toString() + "/\n";
        if (vpnIp.length() > 0) {
            msg += "🔒 <b>Tailscale VPN:</b> http://" + vpnIp + "/\n";
        }
        msg += "⚙️ <b>Автоматика:</b> " + String(m_config.auto_enabled ? "Активна" : "На паузі") +
               " (SoftOFF ≥ " + String(m_config.soc_off_threshold) + "%, Заряд < " + String(m_config.soc_on_threshold) + "%, Цикл " + String(m_config.eval_interval_s) + "с)";
        enqueueTelegram(msg);
    }

    // Immediate evaluation trigger
    void evaluateNow() {
        m_lastEvalMs = millis();
        SandisolarTelemetry t = m_modbus.getTelemetry();
        String nowStr = DebugLogger::getFormattedTime();

        // 1. Check Freeze State (RS-485 communication lost / stale > 20s)
        bool stale = (!t.online) || (t.last_success_ms == 0) || ((millis() - t.last_success_ms) > 20000);
        if (stale) {
            m_freezeCount++;
            m_isFrozen = true;
            if (m_freezeCount == 1) {
                addEventLog("freeze_warn", "⚠️ Freeze State (1/2): Немає відповіді RS-485 від інвертора (" + t.last_error + ")");
                return;
            }
            if (!m_freezeNotified) {
                m_freezeNotified = true;
                String msg = "⚠️ <b>FREEZE STATE: ВТРАЧЕНО ЗВ'ЯЗОК RS-485 З ІНВЕРТОРОМ</b>\n\n"
                             "Помилка: <code>" + t.last_error + "</code>\n"
                             "Автоматичні перемикання тимчасово заблоковано, лічильники дебаунсу заморожено.";
                if (m_config.auto_enabled && m_config.freeze_emergency_softon) {
                    if (m_modbus.setPowerSwitch(true)) {
                        msg += "\n⚡ <b>Виконано аварійний SoftON!</b>";
                    }
                }
                msg += "\n🕒 Час: " + nowStr;
                enqueueTelegram(msg);
                addEventLog("freeze_active", "⚠️ FREEZE STATE: Стійка втрата RS-485 зв'язку з інвертором");
            }
            return;
        }

        // Recovered from Freeze State
        if (m_freezeNotified) {
            m_freezeNotified = false;
            enqueueTelegram("🟢 <b>ЗВ'ЯЗОК RS-485 З ІНВЕРТОРОМ ВІДНОВЛЕНО</b>\n\n"
                            "Автоматику розморожено. Повернення до штатного режиму.\n"
                            "🕒 Час: " + nowStr);
            addEventLog("freeze_recovered", "🟢 Зв'язок RS-485 відновлено, автоматику розморожено");
        }
        m_freezeCount = 0;
        m_isFrozen = false;

        float v       = t.grid_voltage;
        int   soc     = t.battery_soc;
        float tempBms = t.bms_battery_temp;
        bool  running = t.power_switch;

        // 2. Check Inverter Fault state (status == 3)
        if (t.inverter_status == 3 && !m_faultAlertSent) {
            m_faultAlertSent = true;
            enqueueTelegram("🚨 <b>АВАРІЯ ІНВЕРТОРА (FAULT MODE)!</b>\n\n"
                            "Інвертор перейшов у стан <b>Fault (код статусу 3)</b>!\n"
                            "⚡ Мережа: " + String(v, 1) + " В\n"
                            "🔋 Батарея: " + String(soc) + "% (" + String(t.battery_voltage, 1) + " В)\n"
                            "🌡️ T1=" + String(t.temp_inverter, 1) + "°C | T2=" + String(t.temp_boost, 1) + "°C | T3=" + String(t.temp_llc, 1) + "°C | T4=" + String(tempBms, 1) + "°C\n"
                            "🕒 Час: " + nowStr);
            addEventLog("inverter_fault", "🚨 АВАРІЯ: Інвертор у стані FAULT!");
        } else if (t.inverter_status != 3) {
            m_faultAlertSent = false;
        }

        // 3. Initial state capture on first evaluation
        bool isGridOn = (v >= m_config.grid_v_threshold);
        String sampleText = isGridOn ? "СВІТЛО Є" : "СВІТЛА НЕМАЄ";

        if (!m_initialized) {
            m_initialized       = true;
            m_lastSampleGridOn  = isGridOn;
            m_consecutiveCount  = 1;
            m_stableStatus      = sampleText;
            m_prevRunningState  = running;
            m_outageAlertSent   = !isGridOn;
            m_restoredAlertSent = isGridOn;
            addEventLog("init", "Ініціалізація: " + sampleText + " (1/2) | Мережа: " + String(v, 1) +
                                " В | АКБ: " + String(soc) + "% | Інвертор: " + (running ? "ON" : "OFF"));
            return;
        }

        // 4. External state change check (button or HaiPower app)
        if (running != m_prevRunningState) {
            m_prevRunningState = running;
            if (running) {
                enqueueTelegram("⚡ <b>СТАН ЗМІНЕНО: ІНВЕРТОР УВІМКНЕНО (RUNNING)</b>\n\n"
                                "⚡ Мережа: " + String(v, 1) + " В\n"
                                "🔋 Батарея: " + String(soc) + "%\n"
                                "🔌 Генерація 220В активна (зовнішня зміна стану)\n"
                                "🕒 Час: " + nowStr);
                addEventLog("ext_state_on", "Зовнішня зміна стану інвертора на ON (RUNNING)");
            } else {
                enqueueTelegram("🔌 <b>СТАН ЗМІНЕНО: ІНВЕРТОР ВИМКНЕНО (SoftOFF / БАЙПАС)</b>\n\n"
                                "⚡ Мережа: " + String(v, 1) + " В\n"
                                "🔋 Батарея: " + String(soc) + "%\n"
                                "🔌 Працює транзит мережі (зовнішня зміна стану)\n"
                                "🕒 Час: " + nowStr);
                addEventLog("ext_state_off", "Зовнішня зміна стану інвертора на OFF (SoftOFF)");
            }
        }

        // 5. Low Battery Step Alert Ladder (<= 50%, 40%, 30%, 20%, 10%)
        const int LOW_STEPS[5] = {50, 40, 30, 20, 10};
        int currentStep = -1;
        for (int i = 0; i < 5; i++) {
            if (soc <= LOW_STEPS[i]) {
                currentStep = LOW_STEPS[i];
            }
        }
        // Wait: in mqtt_power_monitor.py, it picks the lowest step that soc <= step!
        // Let's match exact logic: if soc=48 -> <=50 (step 50); if soc=18 -> <=20 (step 20).
        currentStep = -1;
        for (int i = 4; i >= 0; i--) {
            if (soc <= LOW_STEPS[i]) {
                currentStep = LOW_STEPS[i];
                break;
            }
        }

        if (currentStep != -1) {
            if (m_lastBatteryAlertStep == -1 || currentStep < m_lastBatteryAlertStep) {
                m_lastBatteryAlertStep = currentStep;
                String emoji = (currentStep <= 20) ? "🪫" : "⚠️";
                String desc  = (currentStep <= 20) ? "Критичний рівень заряду" : "Низький рівень заряду";
                String msg = emoji + " <b>" + desc + ": АКБ ≤ " + String(currentStep) + "% (" + String(soc) + "%)</b>\n\n"
                             "🔋 Поточний заряд: <b>" + String(soc) + "%</b> (" + String(t.battery_voltage, 1) + " В)\n"
                             "⚡ Мережа: " + sampleText + " (" + String(v, 1) + " В)\n"
                             "🔌 Інвертор: " + String(running ? "Увімкнено (генерація)" : "Вимкнено (байпас)") + "\n"
                             "🕒 Час: " + nowStr;
                enqueueTelegram(msg);
                addEventLog("low_battery_alert", emoji + " Сповіщення про розряд АКБ: " + String(soc) + "% (поріг ≤" + String(currentStep) + "%)");
            }
        } else {
            if (soc > 53) {
                m_lastBatteryAlertStep = -1;
            }
        }
        if (m_lastBatteryAlertStep != -1 && currentStep != -1 && currentStep > m_lastBatteryAlertStep) {
            if (soc >= currentStep + 4) {
                m_lastBatteryAlertStep = currentStep;
            }
        }

        // 6. Optional Solar Priority Switching (from sandisolar.yml)
        if (m_config.auto_enabled && m_config.solar_prio_auto) {
            evaluateSolarPriorities(t);
        }

        if (!m_config.auto_enabled) {
            return;
        }

        // 7. Debounce Counter (2x consecutive samples)
        if (isGridOn == m_lastSampleGridOn) {
            m_consecutiveCount++;
        } else {
            m_consecutiveCount = 1;
            m_lastSampleGridOn = isGridOn;
            addEventLog("debounce_1", (isGridOn ? "Мережа з'явилася (1/2): " : "Мережа зникла (1/2): ") +
                                      String(v, 1) + " В | АКБ: " + String(soc) + "%");
            return;
        }

        if (m_consecutiveCount >= 2) {
            m_stableStatus = sampleText;
        } else {
            return;
        }

        // Reset opposite alert flags
        if (m_stableStatus == "СВІТЛО Є") {
            m_outageAlertSent = false;
        } else if (m_stableStatus == "СВІТЛА НЕМАЄ") {
            m_restoredAlertSent = false;
        }

        // 8. Decision Matrix on 2x Consecutive Stable State
        if (!isGridOn && m_consecutiveCount >= 2) {
            // === 2x СВІТЛА НЕМАЄ (OUTAGE) ===
            if (running) {
                addEventLog("trigger_inverter_on_skipped", "SKIP: 2× СВІТЛА НЕМАЄ, інвертор вже працює (SOC " + String(soc) + "%)");
                if (!m_outageAlertSent) {
                    m_outageAlertSent = true;
                    String msg = "⚡ <b>ЗНИКНЕННЯ СВІТЛА (ІНВЕРТОР ВЖЕ ПРАЦЮЄ)</b>\n\n"
                                 "Причина: 2× «СВІТЛА НЕМАЄ» (зникнення живлення)\n"
                                 "Датчик: Power (RS-485)\n"
                                 "Статус: СВІТЛА НЕМАЄ (" + String(v, 1) + "V)\n"
                                 "Інвертор: Увімкнений (живить дім)\n"
                                 "Батарея: " + String(soc) + "%\n"
                                 "Час: " + nowStr;
                    enqueueTelegram(msg);
                }
            } else {
                addEventLog("trigger_inverter_on", "ACTION: 2× СВІТЛА НЕМАЄ → Увімкнення інвертора (SoftON)...");
                if (m_modbus.setPowerSwitch(true)) {
                    m_prevRunningState = true;
                    m_outageAlertSent  = true;
                    addEventLog("trigger_inverter_on_ok", "SUCCESS: Інвертор увімкнено (SoftON), живлення будинку від АКБ");
                    String msg = "⚡ <b>ІНВЕРТОР УВІМКНЕНО</b>\n\n"
                                 "Причина: 2× «СВІТЛА НЕМАЄ» (зникнення живлення)\n"
                                 "Датчик: Power (RS-485)\n"
                                 "Статус: СВІТЛА НЕМАЄ (" + String(v, 1) + "V)\n"
                                 "Інвертор був: off\n"
                                 "Батарея: " + String(soc) + "%\n"
                                 "Час: " + nowStr;
                    enqueueTelegram(msg);
                } else {
                    String err = m_modbus.getTelemetry().last_error;
                    addEventLog("trigger_inverter_on_failed", "❌ Помилка SoftON при аварії мережі: " + err);
                    enqueueTelegram("❌ <b>ПОМИЛКА УВІМКНЕННЯ ІНВЕРТОРА</b>\n\n"
                                    "Причина: 2× «СВІТЛА НЕМАЄ» (зникнення живлення)\n"
                                    "Помилка: " + err + "\n"
                                    "Час: " + nowStr);
                }
            }
        } else if (isGridOn && m_consecutiveCount >= 2) {
            // === 2x СВІТЛО Є (GRID PRESENT / RESTORED) ===
            if (!running) {
                // Inverter is currently OFF (SoftOFF / Bypass)
                if (soc < m_config.soc_on_threshold) {
                    // Needs Grid Charge (< 75%), check Winter Cold Battery Protection (< 3.0°C)
                    if (tempBms > -30.0f && tempBms < m_config.cold_temp_threshold) {
                        addEventLog("trigger_inverter_on_grid_charge_blocked_cold",
                                    "❄️ Холодний захист АКБ (" + String(tempBms, 1) + "°C < " + String(m_config.cold_temp_threshold, 1) + "°C): заряд заблоковано");
                        if (!m_coldBatteryAlertSent) {
                            m_coldBatteryAlertSent = true;
                            String msg = "❄️ <b>ХОЛОДНИЙ ЗАХИСТ АКБ: ЗАРЯДКУ ВІД МЕРЕЖІ ЗАБЛОКОВАНО</b>\n\n"
                                         "🌡️ Температура АКБ: <b>" + String(tempBms, 1) + " °C</b> (поріг безпеки: &lt; " + String(m_config.cold_temp_threshold, 1) + " °C)\n"
                                         "🔋 Рівень заряду: <b>" + String(soc) + "%</b> (&lt; " + String(m_config.soc_on_threshold) + "%)\n"
                                         "⚡ Мережа: СВІТЛО Є (" + String(v, 1) + "V)\n"
                                         "🛡️ <i>Захист LiFePO4: заряджання літієвих осередків при температурі нижче +3 °C викликає незворотну деградацію (металізацію літію).</i>\n"
                                         "🕒 Автозарядка відновиться після прогріву батареї до ≥ " + String(m_config.cold_temp_threshold + 1.0f, 1) + " °C.\n"
                                         "Час: " + nowStr;
                            enqueueTelegram(msg);
                        }
                    } else {
                        if (tempBms >= m_config.cold_temp_threshold + 1.0f) {
                            m_coldBatteryAlertSent = false;
                        }
                        addEventLog("trigger_inverter_on_grid_charge",
                                    "ACTION: Світло є, але АКБ " + String(soc) + "% < " + String(m_config.soc_on_threshold) + "% → SoftON для зарядки");
                        if (m_modbus.setPowerSwitch(true)) {
                            m_prevRunningState  = true;
                            m_restoredAlertSent = true;
                            addEventLog("trigger_inverter_on_grid_charge_ok", "SUCCESS: Інвертор увімкнено для зарядки від мережі");
                            String msg = "🔋 <b>ІНВЕРТОР УВІМКНЕНО (ЗАРЯДКА ВІД МЕРЕЖІ)</b>\n\n"
                                         "Причина: Інвертор ВИМКНЕНИЙ + «СВІТЛО Є» + Батарея " + String(soc) + "% &lt; " + String(m_config.soc_on_threshold) + "%\n"
                                         "Датчик: Power (RS-485)\n"
                                         "Статус: СВІТЛО Є (" + String(v, 1) + "V)\n"
                                         "Інвертор був: off\n"
                                         "Батарея: " + String(soc) + "%\n"
                                         "🌡️ Температура АКБ: " + String(tempBms, 1) + " °C\n"
                                         "Час: " + nowStr;
                            enqueueTelegram(msg);
                        } else {
                            String err = m_modbus.getTelemetry().last_error;
                            addEventLog("trigger_inverter_on_grid_charge_failed", "❌ Помилка SoftON для зарядки: " + err);
                            enqueueTelegram("❌ <b>ПОМИЛКА УВІМКНЕННЯ ІНВЕРТОРА (ЗАРЯДКА)</b>\n\n"
                                            "Причина: Інвертор OFF + «СВІТЛО Є» + Батарея " + String(soc) + "% &lt; " + String(m_config.soc_on_threshold) + "%\n"
                                            "Помилка: " + err + "\n"
                                            "Час: " + nowStr);
                        }
                    }
                } else {
                    // Inverter is OFF and SOC >= 75% (pure bypass)
                    if (tempBms >= m_config.cold_temp_threshold + 1.0f) {
                        m_coldBatteryAlertSent = false;
                    }
                    addEventLog("trigger_inverter_off_skipped",
                                "SKIP: Світло є (" + String(v, 1) + " В), інвертор у SoftOFF/байпасі, АКБ " + String(soc) + "%");
                    if (!m_restoredAlertSent) {
                        m_restoredAlertSent = true;
                        String msg = "🟢 <b>СВІТЛО З'ЯВИЛОСЯ (МЕРЕЖА ВІДНОВЛЕНА)</b>\n\n"
                                     "Причина: 2× «СВІТЛО Є» (напруга мережі повернулася)\n"
                                     "Датчик: Power (RS-485)\n"
                                     "Статус: СВІТЛО Є (" + String(v, 1) + "V)\n"
                                     "Інвертор: Вимкнено (режим очікування/байпас)\n"
                                     "Батарея: " + String(soc) + "%\n"
                                     "Час: " + nowStr;
                        enqueueTelegram(msg);
                    }
                }
            } else {
                // Inverter is currently ON (Running)
                if (soc >= m_config.soc_off_threshold) {
                    // SOC >= 95% -> Turn OFF (SoftOFF)
                    addEventLog("trigger_inverter_off",
                                "ACTION: 2× СВІТЛО Є + АКБ " + String(soc) + "% ≥ " + String(m_config.soc_off_threshold) + "% → Вимкнення (SoftOFF)...");
                    if (m_modbus.setPowerSwitch(false)) {
                        m_prevRunningState  = false;
                        m_restoredAlertSent = true;
                        addEventLog("trigger_inverter_off_ok", "SUCCESS: Інвертор переведено в SoftOFF (чистий байпас мережі)");
                        String msg = "🔌 <b>ІНВЕРТОР ВИМКНЕНО (SoftOFF)</b>\n\n"
                                     "Причина: 2× «СВІТЛО Є» + Running + SOC≥" + String(m_config.soc_off_threshold) + "%\n"
                                     "Датчик: Power (RS-485)\n"
                                     "Статус: СВІТЛО Є (" + String(v, 1) + "V)\n"
                                     "Інвертор був: running\n"
                                     "Батарея: " + String(soc) + "%\n"
                                     "Час: " + nowStr;
                        enqueueTelegram(msg);
                    } else {
                        String err = m_modbus.getTelemetry().last_error;
                        addEventLog("trigger_inverter_off_failed", "❌ Помилка команди SoftOFF: " + err);
                        enqueueTelegram("❌ <b>ПОМИЛКА ВИМКНЕННЯ ІНВЕРТОРА</b>\n\n"
                                        "Причина: 2× «СВІТЛО Є» + Running\n"
                                        "Помилка: " + err + "\n"
                                        "Час: " + nowStr);
                    }
                } else {
                    // SOC < 95% -> Block SoftOFF while charging
                    addEventLog("trigger_inverter_off_blocked",
                                "BLOCK: Світло є (" + String(v, 1) + " В), триває зарядка АКБ (" + String(soc) + "% < " + String(m_config.soc_off_threshold) + "%)");
                    if (!m_restoredAlertSent) {
                        m_restoredAlertSent = true;
                        String msg = "🟢 <b>СВІТЛО З'ЯВИЛОСЯ (МЕРЕЖА ВІДНОВЛЕНА)</b>\n\n"
                                     "Причина: 2× «СВІТЛО Є» (напруга мережі повернулася)\n"
                                     "Датчик: Power (RS-485)\n"
                                     "Статус: СВІТЛО Є (" + String(v, 1) + "V)\n"
                                     "Інвертор: Залишається УВІМКНЕНИМ (триває зарядка АКБ)\n"
                                     "Батарея: " + String(soc) + "% (автовимкнення при досягненні ≥ " + String(m_config.soc_off_threshold) + "%)\n"
                                     "Час: " + nowStr;
                        enqueueTelegram(msg);
                    }
                }
            }
        }
    }

    void loop() {
        uint32_t intervalMs = m_config.eval_interval_s * 1000UL;
        if (m_isFrozen && intervalMs > 15000UL) {
            intervalMs = 15000UL; // Faster 15s check when frozen
        }
        if (m_lastEvalMs == 0 || (millis() - m_lastEvalMs >= intervalMs)) {
            evaluateNow();
        }
    }

private:
    void evaluateSolarPriorities(const SandisolarTelemetry& t) {
        int soc = t.battery_soc;
        // 1. SOC < 25% -> Output Priority to UTI (1)
        if (soc < 25 && t.output_priority != 1) {
            if (m_modbus.writeSingleRegister(182, 1)) {
                addEventLog("solar_prio_uti", "SOC < 25%: Пріоритет виходу перемкнено на UTI (Grid First)");
            }
        }
        // 2. SOC < 15% -> Charge Priority to SNU (1)
        if (soc < 15 && t.charge_priority != 1) {
            if (m_modbus.writeSingleRegister(181, 1)) {
                addEventLog("solar_prio_snu", "SOC < 15%: Пріоритет заряду перемкнено на SNU (Solar + Grid)");
            }
        }
        // 3. SOC > 40% -> Charge Priority back to OSO (2) if it was SNU (1)
        if (soc > 40 && t.charge_priority == 1) {
            if (m_modbus.writeSingleRegister(181, 2)) {
                addEventLog("solar_prio_oso", "SOC > 40%: Пріоритет заряду відновлено на OSO (Only Solar)");
            }
        }
        // 4. SOC > 50% -> Output Priority back to SBU (2) if it was UTI (1)
        if (soc > 50 && t.output_priority == 1) {
            if (m_modbus.writeSingleRegister(182, 2)) {
                addEventLog("solar_prio_sbu", "SOC > 50%: Пріоритет виходу відновлено на SBU (Solar-Battery-Utility)");
            }
        }
    }

    SandisolarModbus& m_modbus;
    AppConfig&        m_config;

    SemaphoreHandle_t m_queueMutex = nullptr;
    mutable SemaphoreHandle_t m_logMutex = nullptr;
    std::vector<String>       m_tgOutbox;
    std::vector<AutoEventLog> m_eventLogs;

    uint32_t m_lastEvalMs          = 0;
    bool     m_initialized         = false;
    bool     m_startupNotified     = false;
    bool     m_lastSampleGridOn    = false;
    int      m_consecutiveCount    = 0;
    String   m_stableStatus        = "unknown";
    bool     m_prevRunningState    = false;
    bool     m_outageAlertSent     = false;
    bool     m_restoredAlertSent   = true;
    int      m_lastBatteryAlertStep = -1;
    bool     m_coldBatteryAlertSent = false;
    bool     m_faultAlertSent       = false;
    bool     m_isFrozen            = false;
    int      m_freezeCount         = 0;
    bool     m_freezeNotified      = false;

    String   m_lastActionText      = "Очікування першого циклу автоматики...";
    String   m_lastEventType       = "init";
    String   m_lastEventTime       = "—";
};
