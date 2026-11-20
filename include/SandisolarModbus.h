#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "AppConfig.h"
#include "SandisolarRegisters.h"
#include "DebugLogger.h"

class SandisolarModbus {
public:
    SandisolarModbus() : m_serial(1) {
        m_busMutex = xSemaphoreCreateMutex();
        m_dataMutex = xSemaphoreCreateMutex();
    }

    void begin(AppConfig& cfg) {
        m_cfgPtr = &cfg;
        if (xSemaphoreTake(m_busMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            m_slaveId = cfg.modbus_slave_id;
            m_rxPin   = cfg.rs485_rx_pin;
            m_txPin   = cfg.rs485_tx_pin;
            m_baud    = cfg.modbus_baud;

            m_serial.end();
            m_serial.setRxBufferSize(512);
            m_serial.begin(m_baud, SERIAL_8N1, m_rxPin, m_txPin);
            xSemaphoreGive(m_busMutex);
            DebugLogger::info("MODBUS", "RS-485 Master initialized on RX=GPIO" + String(m_rxPin) +
                              ", TX=GPIO" + String(m_txPin) + " @ " + String(m_baud) +
                              " bps (Slave ID=" + String(m_slaveId) + ")");
        }
    }

    // Swap RX and TX pins dynamically (e.g. GPIO 17 <-> GPIO 18) and re-initialize UART1
    void swapRxTxPins(bool persistToNvs = true) {
        if (!m_cfgPtr) return;
        int8_t oldRx = m_cfgPtr->rs485_rx_pin;
        int8_t oldTx = m_cfgPtr->rs485_tx_pin;
        m_cfgPtr->rs485_rx_pin = oldTx;
        m_cfgPtr->rs485_tx_pin = oldRx;
        begin(*m_cfgPtr);
        if (persistToNvs) {
            ConfigManager::save(*m_cfgPtr);
        }
        DebugLogger::warn("MODBUS", "🔄 Swapped RS-485 pins: RX=GPIO" + String(m_rxPin) +
                          ", TX=GPIO" + String(m_txPin));
    }

    int8_t getRxPin() const { return m_rxPin; }
    int8_t getTxPin() const { return m_txPin; }

    SandisolarTelemetry getTelemetry() const {
        SandisolarTelemetry copy;
        if (xSemaphoreTake(m_dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            copy = m_telemetry;
            xSemaphoreGive(m_dataMutex);
        }
        return copy;
    }

    // Compute standard Modbus CRC-16 (poly 0xA001)
    static uint16_t calcCRC16(const uint8_t* buf, size_t len) {
        uint16_t crc = 0xFFFF;
        for (size_t pos = 0; pos < len; pos++) {
            crc ^= (uint16_t)buf[pos];
            for (int i = 8; i != 0; i--) {
                if ((crc & 0x0001) != 0) {
                    crc >>= 1;
                    crc ^= 0xA001;
                } else {
                    crc >>= 1;
                }
            }
        }
        return crc;
    }

    // Read Input Registers (0x04) or Holding Registers (0x03)
    bool readRegisters(uint8_t funcCode, uint16_t startAddr, uint16_t count, uint16_t* outRegs, uint32_t timeoutMs = 500) {
        if (count == 0 || count > 64) return false;
        if (xSemaphoreTake(m_busMutex, pdMS_TO_TICKS(1500)) != pdTRUE) return false;

        // Flush stale RX bytes before transmitting
        while (m_serial.available()) m_serial.read();

        uint8_t req[8];
        req[0] = m_slaveId;
        req[1] = funcCode;
        req[2] = (uint8_t)(startAddr >> 8);
        req[3] = (uint8_t)(startAddr & 0xFF);
        req[4] = (uint8_t)(count >> 8);
        req[5] = (uint8_t)(count & 0xFF);
        uint16_t crc = calcCRC16(req, 6);
        req[6] = (uint8_t)(crc & 0xFF);
        req[7] = (uint8_t)(crc >> 8);

        DebugLogger::logTx("MODBUS", req, 8, "FC=" + String(funcCode) + " Addr=" + String(startAddr) + " Cnt=" + String(count));
        m_serial.write(req, 8);
        m_serial.flush();
        // Flush any TX self-echo/crosstalk accumulated during the 8.33ms transmission
        // (200us is < 2 bit-times at 9600 bps, whereas 1 response byte takes 1042us to shift in)
        delayMicroseconds(200);
        while (m_serial.available()) m_serial.read();

        if (xSemaphoreTake(m_dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            m_telemetry.tx_count++;
            xSemaphoreGive(m_dataMutex);
        }

        size_t expectedLen = 5 + (size_t)count * 2;
        uint8_t resp[192];
        size_t idx = 0;
        uint32_t startMs = millis();
        size_t maxToRead = expectedLen + 16; // Headroom for any leading line noise/glitch (e.g. F9)

        while ((millis() - startMs) < timeoutMs && idx < maxToRead) {
            while (m_serial.available() && idx < maxToRead) {
                resp[idx++] = (uint8_t)m_serial.read();
            }
            if (idx >= 5) {
                for (size_t k = 0; k <= idx - 5; k++) {
                    if (resp[k] == m_slaveId && (resp[k+1] == funcCode || resp[k+1] == (funcCode | 0x80))) {
                        if (resp[k+1] & 0x80) { // Exception frame (5 bytes)
                            if (idx - k >= 5) goto done_rx_loop;
                        } else if (idx - k >= expectedLen) {
                            goto done_rx_loop;
                        }
                    }
                }
            }
            vTaskDelay(pdMS_TO_TICKS(2));
        }
done_rx_loop:
        xSemaphoreGive(m_busMutex);

        if (idx > 0) {
            DebugLogger::logRx("MODBUS", resp, idx);
        }

        // Search for frame start (m_slaveId + funcCode or exception)
        int startOffset = -1;
        for (size_t k = 0; k + 5 <= idx; k++) {
            if (resp[k] == m_slaveId && (resp[k+1] == funcCode || resp[k+1] == (funcCode | 0x80))) {
                startOffset = (int)k;
                break;
            }
        }

        if (startOffset < 0) {
            if (idx == 0) {
                setError("Таймаут відповіді RS-485 @ reg " + String(startAddr) + " (0 байт)");
            } else {
                setError("Невірний заголовок кадру @ reg " + String(startAddr) + " (отримано " + String(idx) + " байт, перший: 0x" + String(resp[0], HEX) + ")");
            }
            return false;
        }

        uint8_t* pFrame = &resp[startOffset];
        size_t frameLen = idx - startOffset;

        // Modbus Exception check
        if (pFrame[1] == (funcCode | 0x80)) {
            if (frameLen < 5) {
                setError("Неповний кадр помилки Modbus @ reg " + String(startAddr));
                return false;
            }
            uint16_t rxCrc = (uint16_t)pFrame[3] | ((uint16_t)pFrame[4] << 8);
            if (rxCrc == calcCRC16(pFrame, 3)) {
                setError("Modbus Exception 0x" + String(pFrame[2], HEX) + " @ reg " + String(startAddr));
            } else {
                setError("Modbus Exception CRC error @ reg " + String(startAddr));
            }
            return false;
        }

        if (frameLen < expectedLen) {
            setError("Таймаут відповіді RS-485 @ reg " + String(startAddr) + " (отримано " + String(frameLen) + "/" + String(expectedLen) + " байт)");
            return false;
        }

        if (pFrame[2] != (uint8_t)(count * 2)) {
            setError("Невідповідність довжини даних @ reg " + String(startAddr));
            return false;
        }

        uint16_t rxCrc = (uint16_t)pFrame[expectedLen - 2] | ((uint16_t)pFrame[expectedLen - 1] << 8);
        uint16_t calcCrc = calcCRC16(pFrame, expectedLen - 2);
        if (rxCrc != calcCrc) {
            setError("Помилка CRC-16 @ reg " + String(startAddr));
            return false;
        }

        for (uint16_t i = 0; i < count; i++) {
            outRegs[i] = ((uint16_t)pFrame[3 + i * 2] << 8) | (uint16_t)pFrame[4 + i * 2];
        }

        if (xSemaphoreTake(m_dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            m_telemetry.rx_count++;
            xSemaphoreGive(m_dataMutex);
        }
        return true;
    }

    // Write Single Holding Register (Function 0x06)
    bool writeSingleRegister(uint16_t regAddr, uint16_t value, uint32_t timeoutMs = 600) {
        if (xSemaphoreTake(m_busMutex, pdMS_TO_TICKS(2000)) != pdTRUE) return false;

        while (m_serial.available()) m_serial.read();

        uint8_t req[8];
        req[0] = m_slaveId;
        req[1] = 0x06;
        req[2] = (uint8_t)(regAddr >> 8);
        req[3] = (uint8_t)(regAddr & 0xFF);
        req[4] = (uint8_t)(value >> 8);
        req[5] = (uint8_t)(value & 0xFF);
        uint16_t crc = calcCRC16(req, 6);
        req[6] = (uint8_t)(crc & 0xFF);
        req[7] = (uint8_t)(crc >> 8);

        DebugLogger::logTx("MODBUS", req, 8, "WRITE Reg=" + String(regAddr) + " Val=" + String(value));
        m_serial.write(req, 8);
        m_serial.flush();
        delayMicroseconds(200);
        while (m_serial.available()) m_serial.read();

        if (xSemaphoreTake(m_dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            m_telemetry.tx_count++;
            xSemaphoreGive(m_dataMutex);
        }

        uint8_t resp[48];
        size_t idx = 0;
        uint32_t startMs = millis();

        while ((millis() - startMs) < timeoutMs && idx < 16) {
            while (m_serial.available() && idx < 16) {
                resp[idx++] = (uint8_t)m_serial.read();
            }
            if (idx >= 5) {
                for (size_t k = 0; k <= idx - 5; k++) {
                    if (resp[k] == m_slaveId && (resp[k+1] == 0x06 || resp[k+1] == 0x86)) {
                        if (resp[k+1] == 0x86 && (idx - k >= 5)) goto done_tx_loop;
                        if (resp[k+1] == 0x06 && (idx - k >= 8)) goto done_tx_loop;
                    }
                }
            }
            vTaskDelay(pdMS_TO_TICKS(2));
        }
done_tx_loop:
        xSemaphoreGive(m_busMutex);

        if (idx > 0) {
            DebugLogger::logRx("MODBUS", resp, idx);
        }

        int startOffset = -1;
        for (size_t k = 0; k + 5 <= idx; k++) {
            if (resp[k] == m_slaveId && (resp[k+1] == 0x06 || resp[k+1] == 0x86)) {
                startOffset = (int)k;
                break;
            }
        }

        if (startOffset < 0) {
            setError("Write Timeout @ reg " + String(regAddr));
            return false;
        }

        uint8_t* pFrame = &resp[startOffset];
        size_t frameLen = idx - startOffset;

        if (pFrame[1] == 0x86) {
            setError("Write Exception 0x" + String(pFrame[2], HEX) + " @ reg " + String(regAddr));
            return false;
        }
        if (frameLen < 8) {
            setError("Write Incomplete Response @ reg " + String(regAddr));
            return false;
        }

        uint16_t rxCrc = (uint16_t)pFrame[6] | ((uint16_t)pFrame[7] << 8);
        if (rxCrc != calcCRC16(pFrame, 6)) {
            setError("Write CRC error @ reg " + String(regAddr));
            return false;
        }

        if (xSemaphoreTake(m_dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            m_telemetry.rx_count++;
            xSemaphoreGive(m_dataMutex);
        }

        m_forceFullHoldingPoll = true;
        return true;
    }

    // High-level Inverter Power Switch Control (Holding Register 0, bit 0)
    bool setPowerSwitch(bool turnOn) {
        uint16_t reg0 = 0;
        if (!readRegisters(0x03, 0, 1, &reg0)) {
            // Fallback to direct 0x0001 / 0x0000 write
            reg0 = turnOn ? 1 : 0;
        } else {
            if (turnOn) reg0 |= 0x0001;
            else reg0 &= (uint16_t)~0x0001;
        }
        vTaskDelay(pdMS_TO_TICKS(60));
        bool ok = writeSingleRegister(0, reg0);
        if (ok) {
            if (xSemaphoreTake(m_dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                m_telemetry.reg0_raw = reg0;
                m_telemetry.power_switch = turnOn;
                xSemaphoreGive(m_dataMutex);
            }
            DebugLogger::info("MODBUS", String("Команду ") + (turnOn ? "SoftON (reg 0 = 1)" : "SoftOFF (reg 0 = 0)") + " успішно виконано");
        }
        return ok;
    }

    // Modify single bit in Holding Register 201 (Read-Modify-Write as in sandisolar.yml)
    bool setReg201Bit(uint16_t bitMask, bool enable) {
        uint16_t base = 0;
        if (!readRegisters(0x03, 201, 1, &base)) {
            base = getTelemetry().reg201_raw;
        }
        uint16_t val = enable ? (base | bitMask) : (base & (uint16_t)~bitMask);
        vTaskDelay(pdMS_TO_TICKS(60));
        bool ok = writeSingleRegister(201, val);
        if (ok && xSemaphoreTake(m_dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            updateReg201Fields(val);
            xSemaphoreGive(m_dataMutex);
        }
        return ok;
    }

    // Complete Poll Cycle (Input Registers + Holding Registers)
    bool pollAll(bool forceHolding = false) {
        uint16_t buf[40];
        bool anyCoreSuccess = false;

        // 1. Holding Register 0 (Power Switch state)
        if (readRegisters(0x03, 0, 1, buf)) {
            anyCoreSuccess = true;
            if (xSemaphoreTake(m_dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                m_telemetry.reg0_raw = buf[0];
                m_telemetry.power_switch = ((buf[0] & 0x0001) != 0);
                xSemaphoreGive(m_dataMutex);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(70));

        // 2. Input Block 1: Regs 0..12 (Status, Inverter V/A, Bus1, Temps T1..T3)
        if (readRegisters(0x04, 0, 13, buf)) {
            anyCoreSuccess = true;
            if (xSemaphoreTake(m_dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                m_telemetry.inverter_status   = buf[0];
                m_telemetry.inverter_mode_str = SandisolarTelemetry::decodeInverterMode(buf[0]);
                m_telemetry.inverter_voltage  = (float)buf[2] * 0.1f;
                m_telemetry.inverter_current  = (float)((int16_t)buf[5]) * 0.1f;
                m_telemetry.bus1_voltage      = (float)buf[8] * 0.1f;
                m_telemetry.temp_inverter     = (float)((int16_t)buf[10]) * 0.1f;
                m_telemetry.temp_boost        = (float)((int16_t)buf[11]) * 0.1f;
                m_telemetry.temp_llc          = (float)((int16_t)buf[12]) * 0.1f;
                xSemaphoreGive(m_dataMutex);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(70));

        // 3. Input Block 2: Regs 42..65 (24 regs: Grid V/A/Hz/PF, EPS V/A, PV1 V/A)
        if (readRegisters(0x04, 42, 24, buf)) {
            anyCoreSuccess = true;
            if (xSemaphoreTake(m_dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                m_telemetry.grid_voltage   = (float)buf[0] * 0.1f;                // reg 42
                m_telemetry.grid_current   = (float)((int16_t)buf[1]) * 0.1f;     // reg 43
                m_telemetry.grid_frequency = (float)buf[9] * 0.01f;               // reg 51
                m_telemetry.power_factor   = (float)((int16_t)buf[10]) * 0.0001f; // reg 52
                m_telemetry.eps_voltage    = (float)buf[13] * 0.1f;               // reg 55
                m_telemetry.eps_current    = (float)buf[14] * 0.1f;               // reg 56
                m_telemetry.pv1_voltage    = (float)buf[22] * 0.1f;               // reg 64
                m_telemetry.pv1_current    = (float)buf[23] * 0.1f;               // reg 65
                m_telemetry.pv1_power      = m_telemetry.pv1_voltage * m_telemetry.pv1_current;
                m_telemetry.grid_power     = (m_telemetry.grid_voltage >= 50.0f)
                                             ? (m_telemetry.grid_voltage * m_telemetry.grid_current)
                                             : 0.0f;
                xSemaphoreGive(m_dataMutex);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(70));

        // 4. Input Block 4: Regs 127..152 (26 regs: Battery V, SOC, Current, Temp T4, BMS limits, SOH)
        if (readRegisters(0x04, 127, 26, buf)) {
            anyCoreSuccess = true;
            if (xSemaphoreTake(m_dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                m_telemetry.battery_voltage     = (float)buf[0] * 0.1f;             // reg 127
                m_telemetry.battery_soc         = (int)buf[1];                      // reg 128
                m_telemetry.battery_current     = (float)((int16_t)buf[14]) * 0.01f;// reg 141
                m_telemetry.battery_power       = m_telemetry.battery_voltage * m_telemetry.battery_current;
                m_telemetry.bms_battery_temp    = (float)((int16_t)buf[15]) * 0.1f; // reg 142 (T4)
                m_telemetry.bms_max_charge_a    = (float)buf[16] * 0.1f;            // reg 143
                m_telemetry.bms_max_discharge_a = (float)buf[17] * 0.1f;            // reg 144
                m_telemetry.battery_soh         = (int)buf[25];                     // reg 152
                xSemaphoreGive(m_dataMutex);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(70));

        // 5. Input Block 5 & 6: Power Totals (Regs 284..289 and 320..331)
        if (readRegisters(0x04, 284, 6, buf)) {
            if (xSemaphoreTake(m_dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                uint32_t appRaw = ((uint32_t)buf[0] << 16) | (uint32_t)buf[1];
                int32_t  actRaw = (int32_t)(((uint32_t)buf[2] << 16) | (uint32_t)buf[3]);
                int32_t  reaRaw = (int32_t)(((uint32_t)buf[4] << 16) | (uint32_t)buf[5]);
                m_telemetry.power_total_apparent = (float)appRaw * 0.1f;
                m_telemetry.power_total_active   = (float)actRaw * -0.1f;
                m_telemetry.power_total_reactive = (float)reaRaw * 0.1f;
                xSemaphoreGive(m_dataMutex);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(70));

        if (readRegisters(0x04, 320, 12, buf)) {
            if (xSemaphoreTake(m_dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                uint32_t toUserRaw = ((uint32_t)buf[0] << 16) | (uint32_t)buf[1];   // 320..321
                uint32_t outLoadRaw = ((uint32_t)buf[10] << 16) | (uint32_t)buf[11];// 330..331
                m_telemetry.power_to_user_total = (float)toUserRaw * 0.1f;
                m_telemetry.power_out_load      = (float)outLoadRaw * 0.1f;
                xSemaphoreGive(m_dataMutex);
            }
        }

        // Poll slower blocks (Generator, Energy Counters, and Configuration Holding Registers) every 3rd cycle or when forced
        m_pollCycle++;
        if (forceHolding || m_forceFullHoldingPoll || (m_pollCycle % 3 == 1)) {
            m_forceFullHoldingPoll = false;
            vTaskDelay(pdMS_TO_TICKS(70));

            // Input Block 3: Regs 96..108 (13 regs: Gen V/A, Fan, Total Op Time)
            if (readRegisters(0x04, 96, 13, buf)) {
                if (xSemaphoreTake(m_dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    m_telemetry.generator_voltage    = (float)buf[0] * 0.1f;
                    m_telemetry.generator_current    = (float)buf[1] * 0.1f;
                    m_telemetry.fan_speed            = buf[10]; // reg 106
                    uint32_t opRaw = ((uint32_t)buf[11] << 16) | (uint32_t)buf[12]; // 107..108
                    m_telemetry.total_operation_time = (float)opRaw * 0.00833333f;
                    xSemaphoreGive(m_dataMutex);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(70));

            // Input Block 7: Regs 379..390 (12 regs: Solar & Battery Energy Today/Total)
            if (readRegisters(0x04, 379, 12, buf)) {
                if (xSemaphoreTake(m_dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    m_telemetry.solar_gen_today     = (float)(((uint32_t)buf[0] << 16) | buf[1]) * 0.1f;
                    m_telemetry.solar_gen_total     = (float)(((uint32_t)buf[2] << 16) | buf[3]) * 0.1f;
                    m_telemetry.energy_charge_today = (float)(((uint32_t)buf[4] << 16) | buf[5]) * 0.1f;
                    m_telemetry.energy_charge_total = (float)(((uint32_t)buf[6] << 16) | buf[7]) * 0.1f;
                    m_telemetry.energy_disch_today  = (float)(((uint32_t)buf[8] << 16) | buf[9]) * 0.1f;
                    m_telemetry.energy_disch_total  = (float)(((uint32_t)buf[10] << 16) | buf[11]) * 0.1f;
                    xSemaphoreGive(m_dataMutex);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(70));

            // Holding Block B: Regs 125..141 (17 regs)
            if (readRegisters(0x03, 125, 17, buf)) {
                if (xSemaphoreTake(m_dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    m_telemetry.battery_subtype         = buf[0];                  // 125
                    m_telemetry.battery_max_charge_a    = (float)buf[4] * 0.01f;   // 129
                    m_telemetry.battery_stop_disch_v    = (float)buf[5] * 0.01f;   // 130
                    m_telemetry.battery_const_charge_v  = (float)buf[6] * 0.01f;   // 131
                    m_telemetry.battery_charge_stop_soc = (int)buf[14];            // 139
                    m_telemetry.on_grid_disch_stop_soc  = (int)buf[15];            // 140
                    m_telemetry.off_grid_disch_stop_soc = (int)buf[16];            // 141
                    xSemaphoreGive(m_dataMutex);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(70));

            // Holding Block C: Regs 181..195 (15 regs)
            if (readRegisters(0x03, 181, 15, buf)) {
                if (xSemaphoreTake(m_dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    m_telemetry.charge_priority       = buf[0];                // 181
                    m_telemetry.output_priority       = buf[1];                // 182
                    m_telemetry.on_grid_recovery_soc  = (int)buf[4];           // 185
                    m_telemetry.off_grid_recovery_soc = (int)buf[6];           // 187
                    m_telemetry.battery_uv_cutoff_v   = (float)buf[7] * 0.1f;  // 188
                    m_telemetry.ac_grid_charge_a      = (float)buf[8] * 0.1f;  // 189
                    m_telemetry.battery_float_v       = (float)buf[9] * 0.01f; // 190
                    m_telemetry.battery_to_ac_v       = (float)buf[10] * 0.1f; // 191
                    m_telemetry.ac_to_battery_v       = (float)buf[11] * 0.1f; // 192
                    m_telemetry.boost_charge_time_min = buf[13];               // 194
                    m_telemetry.battery_uv_alarm_v    = (float)buf[14] * 0.1f; // 195
                    xSemaphoreGive(m_dataMutex);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(70));

            // Holding Block D: Regs 201..235 (35 regs)
            if (readRegisters(0x03, 201, 35, buf)) {
                if (xSemaphoreTake(m_dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    updateReg201Fields(buf[0]);                                // 201
                    m_telemetry.buzzer_status           = ((buf[6] & 1) != 0); // 207
                    m_telemetry.ac_input_type           = buf[8];              // 209
                    m_telemetry.parallel_mode           = buf[10];             // 211
                    m_telemetry.parallel_device_type    = buf[12];             // 213
                    m_telemetry.bms_work_mode           = buf[13];             // 214
                    m_telemetry.grid_power_compensation = buf[14];             // 215
                    m_telemetry.gen_input_rated_kw      = (float)buf[19] * 0.01f; // 220
                    m_telemetry.smart_load_off_soc      = (int)buf[25];        // 226
                    m_telemetry.bluetooth_status        = ((buf[30] & 1) != 0);// 231

                    uint8_t cloudByte = (uint8_t)(buf[33] & 0xFF); // 234
                    if (cloudByte == 0xAA) m_telemetry.cloud_conn_status = "Норма (Підключено)";
                    else if (cloudByte == 0x55) m_telemetry.cloud_conn_status = "Помилка хмари";
                    else m_telemetry.cloud_conn_status = "Статус 0x" + String(cloudByte, HEX);

                    uint8_t commByte = (uint8_t)(buf[34] & 0xFF);  // 235
                    uint8_t commHi   = (uint8_t)(buf[34] >> 8);
                    if (commByte == 0xAA || commHi == 0xAA) m_telemetry.logger_comm_status = "Зв'язок OK";
                    else if (commByte == 0x55 || commHi == 0x55) m_telemetry.logger_comm_status = "Помилка зв'язку з платою";
                    else m_telemetry.logger_comm_status = "Статус 0x" + String(buf[34], HEX);

                    xSemaphoreGive(m_dataMutex);
                }
            }
        }

        bool shouldAutoSwap = false;
        if (xSemaphoreTake(m_dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (anyCoreSuccess) {
                m_telemetry.online = true;
                m_telemetry.last_success_ms = millis();
                m_telemetry.last_error = "OK";
                m_noRxCycles = 0;
            } else {
                if (millis() - m_telemetry.last_success_ms > 15000) {
                    m_telemetry.online = false;
                }
                if (m_telemetry.rx_count == 0) {
                    m_noRxCycles++;
                    if (m_noRxCycles >= 3) {
                        m_noRxCycles = 0;
                        shouldAutoSwap = true;
                    }
                }
            }
            xSemaphoreGive(m_dataMutex);
        }

        if (shouldAutoSwap && m_cfgPtr != nullptr) {
            DebugLogger::warn("MODBUS", "⚠️ 0 відповідей за 3 цикли — авто-перевірка реверсу RX/TX (GPIO " +
                              String(m_rxPin) + " <-> GPIO " + String(m_txPin) + ")...");
            swapRxTxPins(true);
        }

        return anyCoreSuccess;
    }

private:
    void updateReg201Fields(uint16_t val) {
        m_telemetry.reg201_raw            = val;
        m_telemetry.eco_mode_enable       = (val & 0x0001) != 0;
        m_telemetry.overload_auto_restart = (val & 0x0002) != 0;
        m_telemetry.overtemp_auto_restart = (val & 0x0004) != 0;
        m_telemetry.input_mode_reminder   = (val & 0x0008) != 0;
        m_telemetry.generator_auto_input  = (val & 0x0020) != 0;
        m_telemetry.dual_channel_load     = (val & 0x0040) != 0;
    }

    void setError(const String& err) {
        if (xSemaphoreTake(m_dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            m_telemetry.err_count++;
            m_telemetry.last_error = err;
            xSemaphoreGive(m_dataMutex);
        }
    }

    HardwareSerial      m_serial;
    SemaphoreHandle_t   m_busMutex = nullptr;
    mutable SemaphoreHandle_t m_dataMutex = nullptr;
    SandisolarTelemetry m_telemetry;
    AppConfig*          m_cfgPtr = nullptr;
    uint8_t             m_slaveId = 1;
    int8_t              m_rxPin = 17;
    int8_t              m_txPin = 18;
    uint32_t            m_baud = 9600;
    uint32_t            m_pollCycle = 0;
    uint32_t            m_noRxCycles = 0;
    bool                m_forceFullHoldingPoll = true;
};
