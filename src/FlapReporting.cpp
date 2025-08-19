// #################################################################################################################
//  ████████  █████  ███████ ██   ██     ██████  ███████ ██████   ██████  ██████  ████████ ██ ███    ██  ██████
//     ██    ██   ██ ██      ██  ██      ██   ██ ██      ██   ██ ██    ██ ██   ██    ██    ██ ████   ██ ██
//     ██    ███████ ███████ █████       ██████  █████   ██████  ██    ██ ██████     ██    ██ ██ ██  ██ ██   ███
//     ██    ██   ██      ██ ██  ██      ██   ██ ██      ██      ██    ██ ██   ██    ██    ██ ██  ██ ██ ██    ██
//     ██    ██   ██ ███████ ██   ██     ██   ██ ███████ ██       ██████  ██   ██    ██    ██ ██   ████  ██████
//
//
// ################################################################################################## by Achim ####
// Banner created:
// https://patorjk.com/software/taag/#p=display&c=c%2B%2B&f=ANSI%20Regular&t=Task%20Reporting
//
/*

    Real time Tasks for Flap Master

    Features:

    - makes I2C Reporting

*/
#include <freertos/FreeRTOS.h>                                                  // Real Time OS
#include <freertos/task.h>
#include <FlapGlobal.h>
#include <i2cFlap.h>
#include "FlapTasks.h"
#include "i2cMaster.h"
#include "TracePrint.h"
#include "FlapReporting.h"
#include "FlapRegistry.h"

// Unicode symbols for reports
const char  FlapReporting::BLOCK_LIGHT[]      = u8"░";
const char  FlapReporting::BLOCK_MEDIUM[]     = u8"▒";
const char  FlapReporting::BLOCK_DENSE[]      = u8"▓";
const char* FlapReporting::SPARKLINE_LEVELS[] = {u8"▁", u8"▂", u8"▃", u8"▄", u8"▅", u8"▆", u8"▇", u8"█"};

// ----------------------------
// Constructor
FlapReporting::FlapReporting() {}

// -----------------------------------
// trace print I2C usage statistic
void FlapReporting::reportI2CStatistic() {
    if (DataEvaluation == nullptr) {
        #ifdef MASTERVERBOSE
            Serial.println("reportingTask(): DataEvaluation not available!");
        #endif
        return;
    }
    // Frame beginning
    //                                     1                   2                   3                   4
    //              123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890
    Serial.println("┌─────────────────────────────────────────────────────────────────────────────┐");
    Serial.println("│               FLAP I²C STATISTIC AND HISTRORY OF LAST MINUTES               │");

    printI2CHistory();                                                          // show history of I2C usage

    // Frame finish
    Serial.println("└─────────────────────────────────────────────────────────────────────────────┘");
}

// -----------------------------------
// return maxiumum value in history cycle
uint32_t FlapReporting::maxValueFromHistory(uint32_t* history) {
    uint32_t maxValue = 0;
    for (int i = 0; i < HISTORY_SIZE; ++i)
        if (history[i] > maxValue)
            maxValue = history[i];
    return maxValue;
}
// -----------------------------------
// print I2C usage statistic with frames
void FlapReporting::printI2CHistory() {
    const uint32_t maxBarWidth = 45;
    const uint8_t  FRAME_WIDTH = 80;                                            // make frame
    const uint8_t  INNER_WIDTH = FRAME_WIDTH - 2;                               // inner with without frame (││)
    int            barSpace    = INNER_WIDTH - 33;
    if (barSpace < 0)
        barSpace = 0;

    uint32_t maxAccess = maxValueFromHistory(DataEvaluation->_accessHistory);
    uint32_t maxSend   = maxValueFromHistory(DataEvaluation->_dataHistory);
    uint32_t maxRead   = maxValueFromHistory(DataEvaluation->_readHistory);
    uint32_t maxTime   = maxValueFromHistory(DataEvaluation->_timeoutHistory);
    uint32_t maxTotal  = max(maxAccess, max(maxSend, maxRead));

    float scale = (maxTotal > 0) ? ((float)maxBarWidth / maxTotal) : 1.0f;

    for (int i = 0; i < HISTORY_SIZE - 5; ++i) {
        uint8_t index  = (DataEvaluation->_historyIndex + HISTORY_SIZE - i) % HISTORY_SIZE;
        int     minute = -i;

        uint32_t valAccess = DataEvaluation->_accessHistory[index];
        uint32_t valSend   = DataEvaluation->_dataHistory[index];
        uint32_t valRead   = DataEvaluation->_readHistory[index];
        uint32_t valTime   = DataEvaluation->_timeoutHistory[index];

        Serial.println("├─────────────────────────────────────────────────────────────────────────────┤");
        // Access-row with ░
        Serial.printf("│ Minute [%3d]   Access  [%4u] ", minute, valAccess);
        printBar(valAccess, scale, BLOCK_LIGHT, barSpace);
        Serial.println(" │");

        // Send-Row with ▒
        Serial.print("│                Send    [");
        Serial.printf("%4u] ", valSend);
        printBar(valSend, scale, BLOCK_MEDIUM, barSpace);
        Serial.println(" │");

        // Read-Row with ▓
        Serial.print("│                Read    [");
        Serial.printf("%4u] ", valRead);
        printBar(valRead, scale, BLOCK_DENSE, barSpace);
        Serial.println(" │");

        // Timeout-Row with ░
        Serial.print("│                Timeout [");
        Serial.printf("%4u] ", valTime);
        printBar(valTime, scale, BLOCK_LIGHT, barSpace);
        Serial.println(" │");
    }
}

// -----------------------------------
// print bar with "symbol"
void FlapReporting::printBar(uint32_t value, float scale, const char* symbol, uint8_t maxLength) {
    uint32_t len = static_cast<uint32_t>(value * scale);
    len          = (len > maxLength) ? maxLength : len;                         // lenght of bar in charachter

    for (uint32_t i = 0; i < len; ++i)                                          // fill bar symbol
        Serial.print(symbol);

    for (uint32_t i = len; i < maxLength; ++i)                                  // fill with blanks to get equal long lines
        Serial.print(' ');
}

void FlapReporting::reportSlaveRegistry() {
    //                                     1                   2                   3                   4
    //              123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890
    Serial.println("┌────────────────────────────────────────────────────────────────────────────────────┐");
    Serial.println("│                              FLAP I²C DEVICE REGISTRY                              │");
    Serial.println("├──────┬──────────────────┬─────┬───┬──────┬──────┬────────┬──────┬─────────┬────────┤");
    Serial.println("│ I²C  │ Device           │Flaps│rpm│ms/Rev│St/Rev│ Offset │ Pos  │ Sensor  │ State  │");
    Serial.println("├──────┼──────────────────┼─────┼───┼──────┼──────┼────────┼──────┼─────────┼────────┤");

    for (const auto& [address, device] : g_slaveRegistry) {
        const char* sensorStatus = device->parameter.sensorworking ? "WORKING" : "BROKEN";
        const char* deviceStatus = device->bootFlag ? "boot" : "online";
        const char* name         = formatSerialNumber(device->parameter.serialnumber);

        uint16_t offset  = device->parameter.offset;
        uint16_t speedMs = device->parameter.speed;
        uint16_t rpm     = (speedMs > 0) ? (60000 / speedMs) : 0;
        uint16_t steps   = device->parameter.steps;
        uint8_t  flaps   = device->parameter.flaps;
        uint16_t pos     = device->position;

        char line[128];
        snprintf(line, sizeof(line), "│ 0x%02X │ %-16s │ %3u │%3u│%5u │%5u │ %6u │ %4u │ %-7s │ %-6s │", address, name, flaps,
                 (speedMs > 0 ? rpm : 0), speedMs, steps, offset, pos, sensorStatus, deviceStatus);

        Serial.println(line);
    }

    Serial.println("└──────┴──────────────────┴─────┴───┴──────┴──────┴────────┴──────┴─────────┴────────┘");
    //              123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890
    //                                     1                   2                   3                   4
}

void FlapReporting::reportRtosTasks() {
    Serial.println("╔════════════════════════════════════════════════════════════════════════════╗");
    Serial.println("║                             RTOS Task Report                               ║");
    Serial.println("╠════════════════════════════════════════════════════════════════════════════╣");
    Serial.println("║ Task Name         │ Stack Reserve │  Stack Size  │  Peak Usage │ Priority  ║");
    Serial.println("╟───────────────────┼───────────────┼──────────────┼─────────────┼───────────╢");

    auto printTaskInfo = [](const char* name, UBaseType_t reserve, uint16_t size, uint8_t prio) {
        uint16_t used = size - reserve;
        char     buffer[128];
        snprintf(buffer, sizeof(buffer), "║  %-16s │ %7u       │ %6u       │ %6u      │   %2u      ║", name, reserve, size, used, prio);
        Serial.println(buffer);
    };

    // Aktuelle Task (ReportingTask)
    printTaskInfo(pcTaskGetName(NULL), uxTaskGetStackHighWaterMark(NULL), STACK_REPORT, PRIO_REPORT);

    // Weitere registrierte Tasks
    if (g_remoteControlHandle != nullptr)
        printTaskInfo(pcTaskGetName(g_remoteControlHandle), uxTaskGetStackHighWaterMark(g_remoteControlHandle), STACK_REMOTE, PRIO_REMOTE);

    if (g_parserHandle != nullptr)
        printTaskInfo(pcTaskGetName(g_parserHandle), uxTaskGetStackHighWaterMark(g_parserHandle), STACK_PARSER, PRIO_PARSER);

    if (g_registryHandle != nullptr)
        printTaskInfo(pcTaskGetName(g_registryHandle), uxTaskGetStackHighWaterMark(g_registryHandle), STACK_REGISTRY, PRIO_REGISTRY);

    if (g_statisticHandle != nullptr)
        printTaskInfo(pcTaskGetName(g_statisticHandle), uxTaskGetStackHighWaterMark(g_statisticHandle), STACK_STATISTICS, PRIO_STATISTICS);

    for (uint8_t i = 0; i < numberOfTwins; i++) {
        if (g_twinHandle[i] != nullptr)
            printTaskInfo(pcTaskGetName(g_twinHandle[i]), uxTaskGetStackHighWaterMark(g_twinHandle[i]), STACK_TWIN, PRIO_TWIN);
    }

    Serial.println("╚════════════════════════════════════════════════════════════════════════════╝");
}
void FlapReporting::reportTaskStatus() {
    constexpr int CONTENT_WIDTH = 58;                                           // Breite zwischen den Rahmenlinien
    constexpr int VALUE_COL     = 22;                                           // Spalte (1-basiert) ab der der Wert beginnt

    auto printTop    = [&]() { Serial.println("╔══════════════════════════════════════════════════════════╗"); };
    auto printSep    = [&]() { Serial.println("╠══════════════════════════════════════════════════════════╣"); };
    auto printBottom = [&]() { Serial.println("╚══════════════════════════════════════════════════════════╝"); };

    // Hilfs-Lambda, um eine Zeile "║ label + spaces + value + spaces ║" zu drucken
    auto printKV = [&](const char* label, const char* value) {
        char buf[CONTENT_WIDTH + 1];
        int  pos = 0;

        // 1) Label kopieren
        int lblLen = strlen(label);
        memcpy(buf + pos, label, lblLen);
        pos += lblLen;

        // 2) Zwischenraum bis VALUE_COL füllen
        int pad = VALUE_COL - 1 - lblLen;
        if (pad < 1)
            pad = 1;
        memset(buf + pos, ' ', pad);
        pos += pad;

        // 3) Wert einfügen
        int valLen = strlen(value);
        memcpy(buf + pos, value, valLen);
        pos += valLen;

        // 4) Rest mit Leerzeichen auffüllen
        if (pos < CONTENT_WIDTH) {
            memset(buf + pos, ' ', CONTENT_WIDTH - pos);
        }
        buf[CONTENT_WIDTH] = '\0';

        // 5) ausgeben
        Serial.print("║");
        Serial.print(buf);
        Serial.println("║");
    };

    printTop();
    // Titel zentriert oder linksbündig – hier linksbündig
    printKV(" FLAP TASKS STATUS REPORT", "");
    printSep();

    // Tick count
    {
        char val[32];
        snprintf(val, sizeof(val), "%10lu", (unsigned long)xTaskGetTickCount());
        printKV(" Tick count:", val);
    }

    // Task count
    {
        char val[32];
        snprintf(val, sizeof(val), "%10u", (unsigned)uxTaskGetNumberOfTasks());
        printKV(" Task count:", val);
    }

    // Uptime
    {
        TickType_t ticks        = xTaskGetTickCount();
        uint32_t   totalSeconds = ticks * portTICK_PERIOD_MS / 1000;
        uint32_t   days         = totalSeconds / 86400;
        uint32_t   hours        = (totalSeconds % 86400) / 3600;
        uint32_t   minutes      = (totalSeconds % 3600) / 60;
        uint32_t   seconds      = totalSeconds % 60;
        char       val[32];
        snprintf(val, sizeof(val), "%3u Tage, %02u:%02u:%02u", days, hours, minutes, seconds);
        printKV(" Uptime:", val);
    }

    // Next scan
    {
        uint32_t ms = getNextScanRemainingMs();
        char     val[32];
        if (ms == 0) {
            strcpy(val, "inactive");
        } else {
            uint32_t m = ms / 60000;
            uint32_t s = (ms % 60000) / 1000;
            snprintf(val, sizeof(val), "%02lu:%02lu (mm:ss)", (unsigned long)m, (unsigned long)s);
        }
        printKV(" Next I2C bus scan in:", val);
    }

    // Next availability check
    {
        uint32_t ms = getNextAvailabilityRemainingMs();
        char     val[32];
        if (ms == 0) {
            strcpy(val, "inactive");
        } else {
            uint32_t m = ms / 60000;
            uint32_t s = (ms % 60000) / 1000;
            snprintf(val, sizeof(val), "%02lu:%02lu (mm:ss)", (unsigned long)m, (unsigned long)s);
        }
        printKV(" Next device check in:", val);
    }
    printBottom();
}

// ----------------------------
// Helper
// get remaining time for next Scan (short or long)
uint32_t FlapReporting::getNextScanRemainingMs() {
    TickType_t now = xTaskGetTickCount();
    // wenn shortScan aktiv ist, nimmt dessen nächstes Ablaufdatum,
    // sonst das von longScan (falls aktiv)
    if (shortScanTimer && xTimerIsTimerActive(shortScanTimer)) {
        TickType_t expiry = xTimerGetExpiryTime(shortScanTimer);
        return (expiry > now) ? (expiry - now) * portTICK_PERIOD_MS : 0;
    }
    if (longScanTimer && xTimerIsTimerActive(longScanTimer)) {
        TickType_t expiry = xTimerGetExpiryTime(longScanTimer);
        return (expiry > now) ? (expiry - now) * portTICK_PERIOD_MS : 0;
    }
    return 0;                                                                   // no Timer aktive
}

// ----------------------------
// Helper
// get remaining time for next Availability-Check
uint32_t FlapReporting::getNextAvailabilityRemainingMs() {
    TickType_t now = xTaskGetTickCount();
    if (availCheckTimer && xTimerIsTimerActive(availCheckTimer)) {
        TickType_t expiry = xTimerGetExpiryTime(availCheckTimer);
        return (expiry > now) ? (expiry - now) * portTICK_PERIOD_MS : 0;
    }
    return 0;
}

// ----------------------------
// Helper
void FlapReporting::printUptime() {
    TickType_t ticks        = xTaskGetTickCount();
    uint32_t   totalSeconds = ticks * portTICK_PERIOD_MS / 1000;

    uint32_t days    = totalSeconds / 86400;
    uint32_t hours   = (totalSeconds % 86400) / 3600;
    uint32_t minutes = (totalSeconds % 3600) / 60;
    uint32_t seconds = totalSeconds % 60;

    reportPrint("  Uptime:     ");
    Serial.printf("%u Tage, %02u:%02u:%02u\n", days, hours, minutes, seconds);
}

void FlapReporting::reportMemory() {
    Serial.println("╔════════════════════════════════════════════════════════════════╗");
    Serial.println("║                        FLAP REPORTING - ESP32 RAM              ║");
    Serial.println("╠════════════════════════════════════════════════════════════════╣");

    char buffer[128];
    snprintf(buffer, sizeof(buffer), "║ Free RAM now                     (kByte): %7u              ║", ESP.getFreeHeap() / 1024);
    Serial.println(buffer);

    snprintf(buffer, sizeof(buffer), "║ Lowest level of free RAM         (kByte): %7u              ║", ESP.getMinFreeHeap() / 1024);
    Serial.println(buffer);

    snprintf(buffer, sizeof(buffer), "║ Biggest unfragmented RAM block   (kByte): %7u              ║", ESP.getMaxAllocHeap() / 1024);
    Serial.println(buffer);

    Serial.println("╚════════════════════════════════════════════════════════════════╝");
}

void FlapReporting::reportAllTwinStepsByFlap(int wrapWidth) {
    for (int i = 0; i < numberOfTwins; ++i) {
        SlaveTwin* twin = Twin[i];
        printStepsByFlapReport(*twin, wrapWidth);

        // Optionale Trennlinie zwischen Geräten
        // Serial.println();                                                       // oder ein dekorativer Rahmen
    }
}

void FlapReporting::printStepsByFlapReport(SlaveTwin& twin, int wrapWidth) {
    int count = twin._parameter.flaps;
    if (count <= 0)
        return;

    // Min/Max für Sparkline
    int minVal = INT_MAX, maxVal = 0;
    for (int i = 0; i < count; ++i) {
        int v  = twin.stepsByFlap[i];
        minVal = min(minVal, v);
        maxVal = max(maxVal, v);
    }

    // Baue alle Zeilen vorab, um Breite zu bestimmen
    struct Chunk {
        String bars, steps, index;
    };
    std::vector<Chunk> chunks;
    int                columnWidth     = 4;
    int                chunkFieldWidth = wrapWidth * 4;                         // 3 Zeichen pro Feld + 1 Leerzeichen
    int                flapCount       = twin._parameter.flaps;
    if (wrapWidth < twin._parameter.flaps)
        flapCount = wrapWidth;
    int tableWidth = 10 + flapCount * columnWidth;                              // z.B. columnWidth = 4 oder 5

    // Header mit I²C, Seriennummer und Steps/Rev
    char addrBuf[6];
    snprintf(addrBuf, sizeof(addrBuf), "0x%02X", twin._slaveAddress);
    char serialBuf[64];
    snprintf(serialBuf, sizeof(serialBuf), "%s", formatSerialNumber(twin._parameter.serialnumber));
    char stepsBuf[32];
    snprintf(stepsBuf, sizeof(stepsBuf), " Steps/Rev: %d", twin._parameter.steps);

    String headerLine = "│ ";
    headerLine += addrBuf;
    headerLine += "  |";
    headerLine += serialBuf;
    int    padding = tableWidth - headerLine.length() - String(stepsBuf).length();
    String spaces  = "";
    for (int i = 0; i < padding; ++i)
        spaces += ' ';

    headerLine += spaces;
    headerLine += stepsBuf;
    headerLine += " │";

    // Rahmen oben
    Serial.print("┌");
    Serial.print(repeatChar("─", tableWidth - 2));
    Serial.println("┐");
    Serial.println(headerLine);
    Serial.print("├");
    Serial.print(repeatChar("─", tableWidth - 2));
    Serial.println("┤");

    for (int offset = 0; offset < count; offset += wrapWidth) {
        drawTwinChunk(twin, offset, wrapWidth);
    }
    // Rahmen unten
    Serial.print("└");
    Serial.print(repeatChar("─", tableWidth - 2));
    Serial.println("┘");
}

String FlapReporting::repeatChar(const String& symbol, int count) {
    String result;
    for (int i = 0; i < count; ++i) {
        result += symbol;
    }
    return result;
}

String FlapReporting::padStart(String val, int length, char fill) {
    while (val.length() < length) {
        val = String(fill) + val;
    }
    return val;
}

void FlapReporting::drawTwinChunk(const SlaveTwin& twin, int offset, int wrapWidth) {
    const int chunkWidth = wrapWidth * 4;

    String barsLine, stepsLine, flapLine;

    // Sparkline Grenzwerte holen
    int count  = twin._parameter.flaps;
    int end    = min(offset + wrapWidth, count);
    int minVal = INT_MAX, maxVal = 0;
    for (int i = offset; i < end; ++i) {
        int v  = twin.stepsByFlap[i];
        minVal = min(minVal, v);
        maxVal = max(maxVal, v);
    }

    int flapCount = twin._parameter.flaps;
    if (wrapWidth < twin._parameter.flaps)
        flapCount = wrapWidth;
    for (int i = 0; i < flapCount; ++i) {
        int         v   = twin.stepsByFlap[offset + i];
        const char* bar = selectSparklineLevel(v, minVal, maxVal);

        flapLine += padStart(String(offset + i), 3, ' ') + " ";                 // z. B. "  0 "
        barsLine += String(bar) + String(bar) + String(bar) + " ";              // z. B. "▁▁▁ "
        stepsLine += padStart(String(v), 3, ' ') + " ";                         // z. B. "409 "
    }

    // Header (optional)
    Serial.print("│  flap │");
    Serial.print(flapLine);
    Serial.println("│");

    Serial.print("│       │");
    Serial.print(barsLine);
    Serial.println("│");

    Serial.print("│ steps │");
    Serial.print(stepsLine);
    Serial.println("│");

    barsLine  = "";
    stepsLine = "";
    flapLine  = "";
}

const char* FlapReporting::selectSparklineLevel(int value, int minValue, int maxValue) {
    if (maxValue == minValue)
        return SPARKLINE_LEVELS[0];

    float ratio = static_cast<float>(value - minValue) / (maxValue - minValue);
    int   index = round(ratio * (SPARKLINE_LEVEL_COUNT - 1));
    index       = std::max(0, std::min(index, SPARKLINE_LEVEL_COUNT - 1));
    return SPARKLINE_LEVELS[index];
}