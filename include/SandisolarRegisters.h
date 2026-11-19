#pragma once

#include <Arduino.h>

struct BatteryEtaInfo {
    String mode = "idle"; // "charging", "discharging", "idle"
    String text = "Стан спокою";
    float  speed_pct_hr = 0.0f;
    String time_str = "—";
};

struct SandisolarTelemetry {
    bool     online = false;
    uint32_t last_success_ms = 0;
    uint32_t tx_count = 0;
    uint32_t rx_count = 0;
    uint32_t err_count = 0;
    String   last_error = "Очікування першого опитування...";

    // --- Input Registers (Function 0x04) ---
    uint16_t inverter_status = 0;
    String   inverter_mode_str = "Unknown";
    float    inverter_voltage = 0.0f;     // reg 2 (V)
    float    inverter_current = 0.0f;     // reg 5 (A)
    float    bus1_voltage = 0.0f;         // reg 8 (V)

    // 4 Thermal Zones (T1 - T4)
    float    temp_inverter = 0.0f;        // reg 10: T1 Heatsink (°C)
    float    temp_boost = 0.0f;           // reg 11: T2 Boost / IPM / IGBT (°C)
    float    temp_llc = 0.0f;             // reg 12: T3 LLC Transformer (°C)
    float    bms_battery_temp = 0.0f;     // reg 142: T4 BMS Battery (°C)

    // Grid & EPS
    float    grid_voltage = 0.0f;         // reg 42 (V)
    float    grid_current = 0.0f;         // reg 43 (A)
    float    grid_frequency = 0.0f;       // reg 51 (Hz)
    float    power_factor = 0.0f;         // reg 52
    float    grid_power = 0.0f;           // Calculated V * A (W)
    float    eps_voltage = 0.0f;          // reg 55 (V)
    float    eps_current = 0.0f;          // reg 56 (A)

    // Solar PV & Generator
    float    pv1_voltage = 0.0f;          // reg 64 (V)
    float    pv1_current = 0.0f;          // reg 65 (A)
    float    pv1_power = 0.0f;            // Calculated V * A (W)
    float    generator_voltage = 0.0f;    // reg 96 (V)
    float    generator_current = 0.0f;    // reg 97 (A)
    uint16_t fan_speed = 0;               // reg 106 (RPM)
    float    total_operation_time = 0.0f; // reg 107..108 (h)

    // Battery & BMS
    float    battery_voltage = 0.0f;      // reg 127 (V)
    int      battery_soc = 0;             // reg 128 (%)
    float    battery_current = 0.0f;      // reg 141 (A, + charge / - discharge)
    float    battery_power = 0.0f;        // Calculated V * A (W)
    float    bms_max_charge_a = 0.0f;     // reg 143 (A)
    float    bms_max_discharge_a = 0.0f;  // reg 144 (A)
    int      battery_soh = 100;           // reg 152 (%)

    // Household Load & Power
    float    power_total_apparent = 0.0f; // reg 284..285 (VA)
    float    power_total_active = 0.0f;   // reg 286..287 (W)
    float    power_total_reactive = 0.0f; // reg 288..289 (var)
    float    power_to_user_total = 0.0f;  // reg 320..321 (W)
    float    power_out_load = 0.0f;       // reg 330..331 (W)

    // Energy Totals (kWh)
    float    solar_gen_today = 0.0f;      // reg 379..380
    float    solar_gen_total = 0.0f;      // reg 381..382
    float    energy_charge_today = 0.0f;  // reg 383..384
    float    energy_charge_total = 0.0f;  // reg 385..386
    float    energy_disch_today = 0.0f;   // reg 387..388
    float    energy_disch_total = 0.0f;   // reg 389..390

    // --- Holding Registers (Function 0x03 / 0x06) ---
    bool     power_switch = false;        // reg 0 bit 0 (true = SoftON / Running, false = SoftOFF)
    uint16_t reg0_raw = 0;
    float    inverter_max_rated_w = 6500.0f; // reg 63..64 (W)

    uint16_t battery_subtype = 1;         // reg 125 (0=Lead-Acid, 1=Lithium, 2=Lithium No Comm)
    float    battery_max_charge_a = 60.0f;// reg 129 (A)
    float    battery_stop_disch_v = 44.0f;// reg 130 (V)
    float    battery_const_charge_v = 56.4f;// reg 131 (V)
    int      battery_charge_stop_soc = 100; // reg 139 (%)
    int      on_grid_disch_stop_soc = 20;   // reg 140 (%)
    int      off_grid_disch_stop_soc = 15;  // reg 141 (%)

    uint16_t charge_priority = 1;         // reg 181 (0=CSO, 1=SNU, 2=OSO)
    uint16_t output_priority = 1;         // reg 182 (0=SOL, 1=UTI, 2=SBU, 10=Grid-Tied)
    int      on_grid_recovery_soc = 30;   // reg 185 (%)
    int      off_grid_recovery_soc = 25;  // reg 187 (%)
    float    battery_uv_cutoff_v = 42.0f; // reg 188 (V)
    float    ac_grid_charge_a = 30.0f;    // reg 189 (A)
    float    battery_float_v = 54.0f;     // reg 190 (V)
    float    battery_to_ac_v = 46.0f;     // reg 191 (V)
    float    ac_to_battery_v = 54.0f;     // reg 192 (V)
    uint16_t boost_charge_time_min = 120; // reg 194 (min)
    float    battery_uv_alarm_v = 44.0f;  // reg 195 (V)

    uint16_t reg201_raw = 0;              // reg 201 bitmask
    bool     eco_mode_enable = false;             // reg 201 bit 0
    bool     overload_auto_restart = false;       // reg 201 bit 1
    bool     overtemp_auto_restart = false;       // reg 201 bit 2
    bool     input_mode_reminder = false;         // reg 201 bit 3
    bool     generator_auto_input = false;        // reg 201 bit 5
    bool     dual_channel_load = false;           // reg 201 bit 6

    bool     buzzer_status = false;               // reg 207 bit 0
    uint16_t ac_input_type = 0;                   // reg 209 (0=APL, 1=UPS)
    uint16_t parallel_mode = 0;                   // reg 211 (0=SIG, 1=PAL, 2=3P1, 3=3P2, 4=3P3)
    uint16_t parallel_device_type = 0;            // reg 213 (0=Master, 1=Slave)
    uint16_t bms_work_mode = 1;                   // reg 214 (0=Disabled, 1=CAN Bus, 2=RS485)
    uint16_t grid_power_compensation = 0;         // reg 215 (W)
    float    gen_input_rated_kw = 5.0f;           // reg 220 (kW)
    int      smart_load_off_soc = 50;             // reg 226 (%)
    bool     bluetooth_status = false;            // reg 231 bit 0
    String   cloud_conn_status = "Невідомо";      // reg 234
    String   logger_comm_status = "Невідомо";     // reg 235

    static String decodeInverterMode(uint16_t status) {
        switch (status) {
            case 0:   return "Waiting";
            case 1:   return "On-grid";
            case 2:   return "Off-grid";
            case 3:   return "Fault";
            case 4:   return "Flashing";
            case 5:   return "Bypass";
            case 6:   return "Self-charge";
            case 7:   return "Generator";
            case 255: return "SoftOFF (Вимкнено)";
            default:  return "Unknown (" + String(status) + ")";
        }
    }

    static BatteryEtaInfo calculateEta(int soc, float batt_a, float capacity_ah = 100.0f) {
        BatteryEtaInfo info;
        if (capacity_ah < 10.0f) capacity_ah = 100.0f;

        if (fabsf(batt_a) < 1.0f) {
            info.mode = "idle";
            info.text = "Стан спокою";
            info.speed_pct_hr = 0.0f;
            info.time_str = "—";
            return info;
        }

        if (batt_a >= 1.0f) {
            int rem_pct = max(0, 100 - soc);
            float speed = (batt_a / capacity_ah) * 100.0f;
            info.mode = "charging";
            info.speed_pct_hr = speed;
            if (speed > 0.1f && rem_pct > 0) {
                float hours = (float)rem_pct / speed;
                int h = (int)hours;
                int m = (int)((hours - (float)h) * 60.0f);
                char buf[32];
                if (h > 0) snprintf(buf, sizeof(buf), "%d год %02d хв", h, m);
                else snprintf(buf, sizeof(buf), "%d хв", m);
                info.time_str = String(buf);
                info.text = "~" + info.time_str + " до 100%";
            } else {
                info.time_str = "< 10 хв";
                info.text = "завершення заряду";
            }
            return info;
        } else {
            float dis_a = fabsf(batt_a);
            float speed = (dis_a / capacity_ah) * 100.0f;
            int cutoff_soc = 20;
            int usable_pct = max(0, soc - cutoff_soc);
            info.mode = "discharging";
            info.speed_pct_hr = -speed;
            if (speed > 0.1f && usable_pct > 0) {
                float hours = (float)usable_pct / speed;
                int h = (int)hours;
                int m = (int)((hours - (float)h) * 60.0f);
                char buf[32];
                if (h > 0) snprintf(buf, sizeof(buf), "%d год %02d хв", h, m);
                else snprintf(buf, sizeof(buf), "%d хв", m);
                info.time_str = String(buf);
                info.text = "~" + info.time_str + " до " + String(cutoff_soc) + "%";
            } else if (soc <= cutoff_soc) {
                info.time_str = "Критичний";
                info.text = "резерв ≤ " + String(cutoff_soc) + "%";
            } else {
                info.time_str = "—";
                info.text = "розряджання";
            }
            return info;
        }
    }
};
