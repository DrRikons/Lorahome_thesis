#pragma once

// Gateway-specific Elog sink kept with the shared first-party library.

#include <Arduino.h>
#include <Elog.h>
#include <SD.h>
#include <time.h>

class LogSink {
public:
    using LineObserver = void (*)(const char* line);

    LogSink() : sdStream(*this) {}

    void begin(bool sdAvailable) {
        sdReady = sdAvailable && prepareSd();
        if (loggerStarted) return;

        Logger.configure(LOG_BUFFER_LINES, false);
        Logger.configureSerial(2);
        constexpr uint8_t outputFlags = ELOG_FLAG_NO_TIME | ELOG_FLAG_NO_SERVICE | ELOG_FLAG_NO_LEVEL;
        Logger.registerSerial(LOG_ID, ELOG_LEVEL_DEBUG, "GATEWAY", Serial, outputFlags);
        Logger.registerSerial(LOG_ID, ELOG_LEVEL_DEBUG, "GATEWAY", sdStream, outputFlags);
        loggerStarted = true;
    }

    bool isSdReady() const { return sdReady; }
    void setLineObserver(LineObserver observer) { lineObserver = observer; }

    template <typename... Args>
    __attribute__((noinline)) void write(uint8_t level, const char* levelName, const char* functionName, const char* format, Args... args) {
        if (!loggerStarted) return;

        char message[MAX_LINE - 80];
        snprintf(message, sizeof(message), format, args...);

        char line[MAX_LINE];
        time_t now;
        time(&now);
        struct tm timeInfo;
        if (localtime_r(&now, &timeInfo)) {
            strftime(line, sizeof(line), "[%Y-%m-%d %H:%M:%S]", &timeInfo);
        } else {
            snprintf(line, sizeof(line), "[1970-01-01 00:00:00]");
        }
        snprintf(line + strlen(line), sizeof(line) - strlen(line), " [%s] [%s] \"%s\"", levelName, functionName, message);
        if (lineObserver) lineObserver(line);
        Logger.log(LOG_ID, level, "%s", line);
    }

private:
    static constexpr uint8_t LOG_ID = 1;
    static constexpr uint16_t LOG_BUFFER_LINES = 32;
    static constexpr size_t MAX_LINE = 384;
    static constexpr uint32_t MAX_ACTIVE_SIZE = 1024UL * 1024UL;

    class SdLogStream : public Stream {
    public:
        explicit SdLogStream(LogSink& sink) : sink(sink) {}

        size_t write(uint8_t value) override {
            if (value == '\r') return 1;
            if (value == '\n') {
                if (!lineOverflowed) {
                    line[lineLength] = '\0';
                    sink.writeToSd(line);
                }
                lineLength = 0;
                lineOverflowed = false;
                return 1;
            }
            if (lineLength < sizeof(line) - 1) {
                line[lineLength++] = static_cast<char>(value);
            } else {
                lineOverflowed = true;
            }
            return 1;
        }

        size_t write(const uint8_t* buffer, size_t size) override {
            for (size_t i = 0; i < size; ++i) write(buffer[i]);
            return size;
        }

        int available() override { return 0; }
        int read() override { return -1; }
        int peek() override { return -1; }

    private:
        LogSink& sink;
        char line[MAX_LINE];
        size_t lineLength = 0;
        bool lineOverflowed = false;
    };

    SdLogStream sdStream;
    bool loggerStarted = false;
    volatile bool sdReady = false;
    LineObserver lineObserver = nullptr;
    int lastDay = -1;

    bool prepareSd() {
        SD.mkdir("/logs");
        SD.mkdir("/logs/archive");
        File active = SD.open("/logs/active.log", FILE_APPEND);
        if (!active) return false;
        active.close();
        return true;
    }

    void writeToSd(const char* line) {
        if (!sdReady) return;
        if (SD.cardType() == CARD_NONE) {
            sdReady = false;
            return;
        }
        SD.mkdir("/logs");
        SD.mkdir("/logs/archive");
        rotateIfNeeded();
        File file = SD.open("/logs/active.log", FILE_APPEND);
        if (file) {
            file.println(line);
            file.close();
        } else {
            sdReady = false;
        }
    }

    void rotateIfNeeded() {
        File active = SD.open("/logs/active.log", FILE_READ);
        if (!active) return;
        bool sizeExceeded = active.size() >= MAX_ACTIVE_SIZE;
        active.close();
        time_t now; time(&now);
        struct tm timeInfo;
        bool hasValidDay = now >= 1704067200UL && localtime_r(&now, &timeInfo);
        if (hasValidDay && lastDay < 0) lastDay = timeInfo.tm_yday;
        bool dayChanged = hasValidDay && timeInfo.tm_yday != lastDay;
        if (!sizeExceeded && !dayChanged) return;
        char archive[64];
        if (now >= 1704067200UL && localtime_r(&now, &timeInfo)) {
            strftime(archive, sizeof(archive), "/logs/archive/gateway-%Y%m%d-%H%M%S.log", &timeInfo);
        } else {
            snprintf(archive, sizeof(archive), "/logs/archive/gateway-%lu.log", millis());
        }
        if (SD.exists(archive)) SD.remove(archive);
        SD.rename("/logs/active.log", archive);
        if (hasValidDay) lastDay = timeInfo.tm_yday;
    }
};

static LogSink gatewayLogSink;

#define GW_LOG_DEBUG(format, ...) gatewayLogSink.write(ELOG_LEVEL_DEBUG, "DEBUG", __func__, format, ##__VA_ARGS__)
#define GW_LOG_INFO(format, ...)  gatewayLogSink.write(ELOG_LEVEL_INFO, "INFO", __func__, format, ##__VA_ARGS__)
#define GW_LOG_WARN(format, ...)  gatewayLogSink.write(ELOG_LEVEL_WARNING, "WARN", __func__, format, ##__VA_ARGS__)
#define GW_LOG_ERROR(format, ...) gatewayLogSink.write(ELOG_LEVEL_ERROR, "ERROR", __func__, format, ##__VA_ARGS__)
