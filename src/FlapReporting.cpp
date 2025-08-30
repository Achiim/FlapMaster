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

/**
 * @brief Construct a new Flap Reporting:: Flap Reporting object
 *
 */
FlapReporting::FlapReporting() {}

// trace liga tabelle
void FlapReporting::reportLigaTable() {
    LigaSnapshot snap;                                                          // local copy of liga table (small)
    Liga->get(snap);
    renderLigaTable(snap);
};

// ==== UTF-8 helpers: crop by code points (not bytes), pad with spaces ====
static inline bool isUtf8Cont(uint8_t b) {
    return (b & 0xC0) == 0x80;
}

// returns number of BYTES you may keep to get at most `maxCols` code points
static size_t utf8_prefix_bytes_for_cols(const char* s, size_t maxCols) {
    size_t bytes = 0, cols = 0;
    while (s[bytes] && cols < maxCols) {
        uint8_t c    = (uint8_t)s[bytes];
        size_t  step = 1;
        if ((c & 0x80) == 0x00)
            step = 1;                                                           // ASCII
        else if ((c & 0xE0) == 0xC0)
            step = 2;                                                           // 2-byte
        else if ((c & 0xF0) == 0xE0)
            step = 3;                                                           // 3-byte
        else if ((c & 0xF8) == 0xF0)
            step = 4;                                                           // 4-byte
        else {
            step = 1;
        }                                                                       // fallback
        // avoid cutting inside multi-byte sequence
        for (size_t k = 1; k < step; ++k) {
            if (s[bytes + k] == '\0' || !isUtf8Cont((uint8_t)s[bytes + k])) {
                step = 1;
                break;                                                          // broken sequence: treat as single
            }
        }
        // advance
        bytes += step;
        cols += 1;
    }
    return bytes;
}

// print cropped (by code points) and right-pad to `cols` with spaces
static void printUtf8Padded(const char* s, size_t cols) {
    if (!s)
        s = "";
    size_t keep = utf8_prefix_bytes_for_cols(s, cols);

    // print safe prefix
    for (size_t i = 0; i < keep; ++i)
        Serial.write((uint8_t)s[i]);

    // count printed code points for correct padding
    size_t count = 0;
    for (size_t i = 0; i < keep;) {
        uint8_t c    = (uint8_t)s[i];
        size_t  step = 1;
        if ((c & 0x80) == 0x00)
            step = 1;
        else if ((c & 0xE0) == 0xC0)
            step = 2;
        else if ((c & 0xF0) == 0xE0)
            step = 3;
        else if ((c & 0xF8) == 0xF0)
            step = 4;
        count++;
        i += step;
    }

    // pad spaces to fill the column
    for (size_t i = count; i < cols; ++i)
        Serial.write(' ');
}

static inline void printUIntRight(unsigned v, int width) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%*u", width, v);
    Serial.print(buf);
}
static inline void printIntRight(int v, int width) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%*d", width, v);
    Serial.print(buf);
}

// returns number of code points in the first `nbytes` (for padding)
static size_t utf8_count_codepoints(const char* s, size_t nbytes) {
    size_t cols = 0;
    for (size_t i = 0; i < nbytes;) {
        uint8_t c    = (uint8_t)s[i];
        size_t  step = 1;
        if ((c & 0x80) == 0x00)
            step = 1;
        else if ((c & 0xE0) == 0xC0)
            step = 2;
        else if ((c & 0xF0) == 0xE0)
            step = 3;
        else if ((c & 0xF8) == 0xF0)
            step = 4;
        cols++;
        i += step;
    }
    return cols;
}

static void printIntRight(int v, uint8_t width) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%*d", width, v);
    Serial.print(buf);
}

static void printLigaHeader() {
    Serial.println("┌─────┬──────────────────────────┬──────────────┬─────┬──────┬────┬──────┬─────┐");
    Serial.println("│ Pos │ Mannschaft               │ Name         │ DFB │ Flap │ Sp │ Diff │ Pkt │");
    Serial.println("├─────┼──────────────────────────┼──────────────┼─────┼──────┼────┼──────┼─────┤");
}

static void printLigaFooter() {
    Serial.println("└─────┴──────────────────────────┴──────────────┴─────┴──────┴────┴──────┴─────┘");
}

// ==== Rendering: nur die Zeilen, Header/Footer ===================
void FlapReporting::printTableRow(const LigaRow& r) {
    // Spalten: │ Pos │ Mannschaft │ Name │ DFB │ Flap │ Sp │ Diff │ Pkt │
    Serial.print("│ ");
    Serial.printf("%*u", W_POS, r.pos);
    Serial.print(" │ ");
    printUtf8Padded(r.team, W_TEAM);
    Serial.print(" │ ");
    printUtf8Padded(r.shortName, W_SHORT);
    Serial.print(" │ ");
    printUtf8Padded(r.dfb, W_DFB);
    Serial.print(" │ ");
    Serial.printf("%*.*u", W_FLAP, 2, r.flap);                                  // column width = W_FLAP, 2 digits with leading 0
    Serial.print(" │ ");
    Serial.printf("%*u", W_SP, r.sp);
    Serial.print(" │ ");
    Serial.printf("%*d", W_DIFF, (int)r.diff);                                  // signed!
    Serial.print(" │ ");
    Serial.printf("%*u", W_PKT, r.pkt);
    Serial.println(" │");
}

// render Bundesliga table:
void FlapReporting::renderLigaTable(const LigaSnapshot& s) {
    if (s.teamCount == 0) {
        Serial.println(F("(Liga) No data available."));
        return;
    }

    Serial.print(F("Bundesliga Table: Season "));
    Serial.print(s.season);
    Serial.print(F(", Matchday "));
    Serial.println(s.matchday);

    printLigaHeader();                                                          // UTF-8 Kopfzeile
    for (uint8_t i = 0; i < s.teamCount; ++i) {
        printTableRow(s.rows[i]);                                               // Row
    }
    printLigaFooter();                                                          // UTF-8 Fußzeile
}

// -----------------------------------
// trace print I2C usage statistic
void FlapReporting::reportI2CStatistic() {
    if (DataEvaluation == nullptr) {
        #ifdef ERRORVERBOSE
            Serial.println("reportingTask(): DataEvaluation not available!");
        #endif
        return;
    }
    // Frame beginning
    //                                     1                   2                   3                   4
    //              123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890
    Serial.println("┌─────────────────────────────────────────────────────────────────────────────┐");
    Serial.printf("│ FLAP I²C STATISTIC AND HISTORY OF LAST MINUTES     Bus-Frequency: %3dkHz   │\n", I2C_MASTER_FREQ_HZ / 1000);

    printI2CHistory();                                                          // show history of I2C usage

    // Frame finish
    Serial.println("└─────────────────────────────────────────────────────────────────────────────┘");
}

// -----------------------------------

/**
 * @brief return maxiumum value in history cycle
 *
 * @param history
 * @return uint32_t
 */
uint32_t FlapReporting::maxValueFromHistory(uint32_t* history) {
    uint32_t maxValue = 0;
    for (int i = 0; i < HISTORY_SIZE; ++i)
        if (history[i] > maxValue)
            maxValue = history[i];
    return maxValue;
}
// -----------------------------------

/**
 * @brief print I2C usage statistic with frames
 *
 */
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

/**
 * @brief print bar with "symbol"
 *
 * @param value
 * @param scale
 * @param symbol
 * @param maxLength
 */
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

// -------------------------------

/**
 * @brief report RTOS tasks
 *
 */
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

    // registrierte Tasks nach Priorität absteigend gelistet
    if (g_ligaHandle != nullptr)
        printTaskInfo(pcTaskGetName(g_ligaHandle), uxTaskGetStackHighWaterMark(g_ligaHandle), STACK_LIGA, PRIO_LIGA);

    for (uint8_t i = 0; i < numberOfTwins; i++) {
        if (g_twinHandle[i] != nullptr)
            printTaskInfo(pcTaskGetName(g_twinHandle[i]), uxTaskGetStackHighWaterMark(g_twinHandle[i]), STACK_TWIN, PRIO_TWIN);
    }

    if (g_registryHandle != nullptr)
        printTaskInfo(pcTaskGetName(g_registryHandle), uxTaskGetStackHighWaterMark(g_registryHandle), STACK_REGISTRY, PRIO_REGISTRY);

    if (g_parserHandle != nullptr)
        printTaskInfo(pcTaskGetName(g_parserHandle), uxTaskGetStackHighWaterMark(g_parserHandle), STACK_PARSER, PRIO_PARSER);

    if (g_reportHandle != nullptr)
        printTaskInfo(pcTaskGetName(g_reportHandle), uxTaskGetStackHighWaterMark(g_reportHandle), STACK_REPORT, PRIO_REPORT);

    if (g_remoteControlHandle != nullptr)
        printTaskInfo(pcTaskGetName(g_remoteControlHandle), uxTaskGetStackHighWaterMark(g_remoteControlHandle), STACK_REMOTE, PRIO_REMOTE);

    if (g_statisticHandle != nullptr)
        printTaskInfo(pcTaskGetName(g_statisticHandle), uxTaskGetStackHighWaterMark(g_statisticHandle), STACK_STATISTICS, PRIO_STATISTICS);

    Serial.println("╚════════════════════════════════════════════════════════════════════════════╝");
}

// -----------------------------
/**
 * @brief report memory usage of ESP32
 *
 */
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

    // next Liga scan
    {
        uint32_t ls = getNextLigaScanRemainingMs();
        char     val[32];
        if (ls == 0) {
            strcpy(val, "inactive");
        } else {
            uint32_t m = ls / 60000;
            uint32_t s = (ls % 60000) / 1000;
            snprintf(val, sizeof(val), "%02lu:%02lu (mm:ss)", (unsigned long)m, (unsigned long)s);
        }
        printKV(" Next liga scan in   :", val);
    }
    printBottom();
}

// ----------------------------

/**
 * @brief get remaining time for next Scan (short or long)
 *
 * @return uint32_t
 */
uint32_t FlapReporting::getNextScanRemainingMs() {
    TickType_t now = xTaskGetTickCount();
    // wenn shortScan aktiv ist, nimmt dessen nächstes Ablaufdatum,
    // sonst das von longScan (falls aktiv)
    if (regiScanTimer && xTimerIsTimerActive(regiScanTimer)) {
        TickType_t expiry = xTimerGetExpiryTime(regiScanTimer);
        return (expiry > now) ? (expiry - now) * portTICK_PERIOD_MS : 0;
    }
    return 0;                                                                   // no Timer aktive
}

// ----------------------------

/**
 * @brief get remaining time for next Availability-Check
 *
 * @return uint32_t
 */
uint32_t FlapReporting::getNextAvailabilityRemainingMs() {
    TickType_t now = xTaskGetTickCount();
    if (availCheckTimer && xTimerIsTimerActive(availCheckTimer)) {
        TickType_t expiry = xTimerGetExpiryTime(availCheckTimer);
        return (expiry > now) ? (expiry - now) * portTICK_PERIOD_MS : 0;
    }
    return 0;
}

// ----------------------------

// ----------------------------------

/**
 * @brief Milliseconds remaining until the next scheduled Liga scan.
 *
 * Uses the timer's absolute expiry tick and a signed delta to be safe across
 * tick wraparound. Returns 0 if the timer is inactive or overdue.
 *
 * @return uint32_t  Remaining time in milliseconds (0 if none).
 */
uint32_t FlapReporting::getNextLigaScanRemainingMs() {
    // Quick outs: no timer or not active → no countdown
    if (!ligaScanTimer || xTimerIsTimerActive(ligaScanTimer) != pdTRUE) {
        return 0u;
    }

    const TickType_t now    = xTaskGetTickCount();
    const TickType_t expiry = xTimerGetExpiryTime(ligaScanTimer);

    // Signed subtraction is rollover-safe in FreeRTOS tick math
    const int32_t diff = (int32_t)(expiry - now);
    return (diff > 0) ? pdTICKS_TO_MS((TickType_t)diff) : 0u;
}

// ----------------------------

/**
 * @brief report uptime of ESP32
 *
 */
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

/**
 * @brief report steps by flaps
 *
 * @param wrapWidth
 */
void FlapReporting::reportAllTwinStepsByFlap(int wrapWidth) {
    for (int i = 0; i < numberOfTwins; ++i) {
        SlaveTwin* twin = Twin[i];
        printStepsByFlapReport(*twin, wrapWidth);

        // Optionale Trennlinie zwischen Geräten
        // Serial.println();                                                       // oder ein dekorativer Rahmen
    }
}

/**
 * @brief
 *
 * @param twin
 * @param wrapWidth
 */
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
/**
 * @brief fill with blanks
 *
 * @param symbol
 * @param count
 * @return String
 */
String FlapReporting::repeatChar(const String& symbol, int count) {
    String result;
    for (int i = 0; i < count; ++i) {
        result += symbol;
    }
    return result;
}
/**
 * @brief fill with blanks
 *
 * @param val
 * @param length
 * @param fill
 * @return String
 */
String FlapReporting::padStart(String val, int length, char fill) {
    while (val.length() < length) {
        val = String(fill) + val;
    }
    return val;
}
/**
 * @brief draw chunk
 *
 * @param twin
 * @param offset
 * @param wrapWidth
 */
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
/**
 * @brief select symbol to build sparkling
 *
 * @param value
 * @param minValue
 * @param maxValue
 * @return const char*
 */
const char* FlapReporting::selectSparklineLevel(int value, int minValue, int maxValue) {
    if (maxValue == minValue)
        return SPARKLINE_LEVELS[0];

    float ratio = static_cast<float>(value - minValue) / (maxValue - minValue);
    int   index = round(ratio * (SPARKLINE_LEVEL_COUNT - 1));
    index       = std::max(0, std::min(index, SPARKLINE_LEVEL_COUNT - 1));
    return SPARKLINE_LEVELS[index];
}