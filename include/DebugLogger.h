#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

enum LogLevel {
    LOG_LVL_INFO = 0,
    LOG_LVL_WARN = 1,
    LOG_LVL_ERROR = 2,
    LOG_LVL_TX = 3,
    LOG_LVL_RX = 4
};

struct LogEntry {
    uint32_t timestamp_ms;
    String   wall_time;
    uint8_t  level;
    String   tag;
    String   message;
};

class DebugLogger {
public:
    static void init(size_t maxEntries = 200) {
        if (!s_mutex) {
            s_mutex = xSemaphoreCreateMutex();
        }
        s_maxEntries = maxEntries;
    }

    static String getFormattedTime() {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 5)) {
            char buf[24];
            strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
            return String(buf);
        }
        uint32_t sec = millis() / 1000;
        char buf[24];
        snprintf(buf, sizeof(buf), "+%lus", (unsigned long)sec);
        return String(buf);
    }

    static void log(uint8_t level, const String& tag, const String& msg) {
        if (!s_mutex) init();
        String wTime = getFormattedTime();
        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            LogEntry entry;
            entry.timestamp_ms = millis();
            entry.wall_time = wTime;
            entry.level = level;
            entry.tag = tag;
            entry.message = msg;

            if (s_entries.size() >= s_maxEntries) {
                s_entries.erase(s_entries.begin());
            }
            s_entries.push_back(entry);
            xSemaphoreGive(s_mutex);
        }
        const char* prefix = "[INFO]";
        if (level == LOG_LVL_WARN) prefix = "[WARN]";
        else if (level == LOG_LVL_ERROR) prefix = "[ERR]";
        else if (level == LOG_LVL_TX) prefix = "[TX >>]";
        else if (level == LOG_LVL_RX) prefix = "[RX <<]";
        Serial.printf("%s %s [%s] %s\n", wTime.c_str(), prefix, tag.c_str(), msg.c_str());
    }

    static void info(const String& tag, const String& msg) {
        log(LOG_LVL_INFO, tag, msg);
    }

    static void warn(const String& tag, const String& msg) {
        log(LOG_LVL_WARN, tag, msg);
    }

    static void error(const String& tag, const String& msg) {
        log(LOG_LVL_ERROR, tag, msg);
    }

    static void logTx(const String& tag, const uint8_t* data, size_t len, const String& desc = "") {
        if (!s_traceModbus && tag == "MODBUS") return;
        String hexStr;
        hexStr.reserve(len * 3 + 24);
        for (size_t i = 0; i < len; ++i) {
            char buf[4];
            snprintf(buf, sizeof(buf), "%02X ", data[i]);
            hexStr += buf;
        }
        if (desc.length() > 0) {
            hexStr += "(" + desc + ")";
        }
        log(LOG_LVL_TX, tag, hexStr);
    }

    static void logRx(const String& tag, const uint8_t* data, size_t len, const String& desc = "") {
        if (!s_traceModbus && tag == "MODBUS") return;
        String hexStr;
        hexStr.reserve(len * 3 + 24);
        for (size_t i = 0; i < len; ++i) {
            char buf[4];
            snprintf(buf, sizeof(buf), "%02X ", data[i]);
            hexStr += buf;
        }
        if (desc.length() > 0) {
            hexStr += "(" + desc + ")";
        }
        log(LOG_LVL_RX, tag, hexStr);
    }

    static void setTraceModbus(bool en) { s_traceModbus = en; }
    static bool getTraceModbus() { return s_traceModbus; }

    static void clear() {
        if (!s_mutex) init();
        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            s_entries.clear();
            xSemaphoreGive(s_mutex);
        }
    }

    static String toJson(size_t maxCount = 120) {
        if (!s_mutex) init();
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();

        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            size_t total = s_entries.size();
            size_t startIdx = (total > maxCount) ? (total - maxCount) : 0;
            for (size_t i = startIdx; i < total; ++i) {
                const auto& entry = s_entries[i];
                JsonObject obj = arr.add<JsonObject>();
                obj["time"] = entry.timestamp_ms;
                obj["wtime"] = entry.wall_time;
                obj["lvl"]  = entry.level;
                obj["tag"]  = entry.tag;
                obj["msg"]  = entry.message;
            }
            xSemaphoreGive(s_mutex);
        }

        String json;
        serializeJson(doc, json);
        return json;
    }

private:
    static inline std::vector<LogEntry> s_entries;
    static inline size_t s_maxEntries = 200;
    static inline SemaphoreHandle_t s_mutex = nullptr;
    static inline bool s_traceModbus = false;
};
