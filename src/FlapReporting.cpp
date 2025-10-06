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
#include <ArduinoJson.h>
#include <FS.h>
#include <SPIFFS.h>
#include <i2cFlap.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include "FlapTasks.h"
#include "i2cMaster.h"
#include "TracePrint.h"
#include "FlapReporting.h"
#include "FlapRegistry.h"
#include "Liga.h"

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
    renderLigaTable(snap[snapshotIndex ^ 1]);
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
    Serial.println("┌─────┬──────────────────────────┬────────────┬────┬────┬────┬────┬────┬────┬──────┬─────┐");
    Serial.println("│ Pos │ Mannschaft               │ DFB │ Flap │ Sp │  S │  U │  N │  T │ GT │ Diff │ Pkt │");
    Serial.println("├─────┼──────────────────────────┼─────┼──────┼────┼────┼────┼────┼────┼────┼──────┼─────┤");
}

static void printLigaFooter() {
    Serial.println("└─────┴──────────────────────────┴─────┴──────┴────┴────┴────┴────┴────┴────┴──────┴─────┘");
}

// ==== Rendering: nur die Zeilen, Header/Footer ===================
void FlapReporting::printTableRow(const LigaRow& r) {
    // Spalten: │ Pos │ Mannschaft │ DFB │ Flap │ Sp │ S │ U │ N │ T │ GT │ Diff │ Pkt │
    Serial.print("│ ");
    Serial.printf("%*u", W_POS, r.pos);
    Serial.print(" │ ");
    printUtf8Padded(r.team, W_TEAM);
    Serial.print(" │ ");
    printUtf8Padded(r.dfb, W_DFB);
    Serial.print(" │ ");
    Serial.printf("%*.*u", W_FLAP, 2, r.flap);                                  // column width = W_FLAP, 2 digits with leading 0
    Serial.print(" │ ");
    Serial.printf("%*u", W_SP, r.sp);
    Serial.print(" │ ");
    Serial.printf("%*u", W_W, r.w);
    Serial.print(" │ ");
    Serial.printf("%*u", W_D, r.d);
    Serial.print(" │ ");
    Serial.printf("%*u", W_L, r.l);
    Serial.print(" │ ");
    Serial.printf("%*u", W_G, r.g);
    Serial.print(" │ ");
    Serial.printf("%*u", W_OG, r.og);
    Serial.print(" │ ");
    Serial.printf("%*d", W_DIFF, (int)r.diff);                                  // signed!
    Serial.print(" │ ");
    Serial.printf("%*u", W_PKT, r.pkt);
    Serial.println(" │");
}

// render Bundesliga table:
void FlapReporting::renderLigaTable(const LigaSnapshot& s) {
    if (s.teamCount == 0) {
        Liga->ligaPrintln("(League %s) No data available.", leagueShortcut(activeLeague));
        return;
    }

    Serial.print(leagueName(activeLeague));
    Serial.print(F(" Standings: Season "));
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
    Serial.printf("│ FLAP I²C STATISTIC AND HISTORY OF LAST MINUTES     Bus-Frequency: %3dkHz    │\n", I2C_MASTER_FREQ_HZ / 1000);

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

    if (g_webServerHandle != nullptr)
        printTaskInfo(pcTaskGetName(g_webServerHandle), uxTaskGetStackHighWaterMark(g_webServerHandle), STACK_WEB_SERVER, PRIO_WEB_SERVER);

    if (g_remoteControlHandle != nullptr)
        printTaskInfo(pcTaskGetName(g_remoteControlHandle), uxTaskGetStackHighWaterMark(g_remoteControlHandle), STACK_REMOTE, PRIO_REMOTE);

    if (g_statisticHandle != nullptr)
        printTaskInfo(pcTaskGetName(g_statisticHandle), uxTaskGetStackHighWaterMark(g_statisticHandle), STACK_STATISTICS, PRIO_STATISTICS);

    Serial.println("╚════════════════════════════════════════════════════════════════════════════╝");
}
// rechts auffüllen
String padEnd(const String& text, int width, char fill = ' ') {
    String out = text;
    while (out.length() < width) {
        out += fill;
    }
    return out;
}

// links auffüllen
String padStart(const String& text, int width, char fill = ' ') {
    String out = text;
    while (out.length() < width) {
        out = fill + out;
    }
    return out;
}

// ------------------------------
/**
 * @brief render Task Report for consol output
 *
 */
void FlapReporting::renderTaskReport() {
    constexpr int CONTENT_WIDTH = 60;                                           // Gesamtbreite (innen zwischen ║ … ║)
    constexpr int VALUE_COL     = 26;                                           // Spalte ab der der Wert beginnt (1-basiert)

    StaticJsonDocument<1024> doc;
    if (!Store->readFile("/R01.json", doc)) {
        return;                                                                 // could not read file
    }

    // Header
    Serial.println("╔══════════════════════════════════════════════════════════╗");

    // evaluate key-value pairs
    for (JsonPair kv : doc.as<JsonObject>()) {
        String key   = kv.key().c_str();
        String value = kv.value().as<String>();

        // Links Schlüssel, rechts Wert
        String line = "║ ";
        line += key;
        // Bis VALUE_COL auffüllen
        while (line.length() < VALUE_COL)
            line += ' ';
        // Wert hinzufügen
        line += value;
        // Rest auffüllen
        while (line.length() < CONTENT_WIDTH + 1)
            line += ' ';                                                        // +1 wegen linkem ║
        line += "║";
        Serial.println(line);
        if (key == "report")
            Serial.println("╠══════════════════════════════════════════════════════════╣");
    }

    // Footer
    Serial.println("╚══════════════════════════════════════════════════════════╝");
}

// -----------------------------
/**
 * @brief report general task information
 *
 */
void FlapReporting::reportTaskStatus() {
    createTaskStatusJson();                                                     // generate JSON file
    renderTaskReport();                                                         // render for console output
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
    uint32_t now       = millis();
    uint32_t remaining = (pollManagerStartOfWaiting + pollManagerDynamicWait) - now;

    if (remaining < 0)
        remaining = 0;                                                          // falls schon abgelaufen

    return remaining;                                                           // kein Timer aktiv
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
    Serial.println("║                        FLAP REPORTING - ESP32 CHIP INFO        ║");
    Serial.println("╠════════════════════════════════════════════════════════════════╣");

    char buffer[128];
    // ESP32 Info
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    snprintf(buffer, sizeof(buffer), "║ Chip Model                              : %1u                    ║", chip_info.revision);
    Serial.println(buffer);

    snprintf(buffer, sizeof(buffer), "║ Chip Cores                              : %1u                    ║", chip_info.cores);
    Serial.println(buffer);

    const char* wifi = (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi " : "";
    const char* bt   = (chip_info.features & CHIP_FEATURE_BT) ? "BT " : "";
    const char* ble  = (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "";

    snprintf(buffer, sizeof(buffer), "║ Features                                : %s%s%s          ║", wifi, bt, ble);
    Serial.println(buffer);

    snprintf(buffer, sizeof(buffer), "║ Flash Mode                              : %s                  ║", CONFIG_ESPTOOLPY_FLASHMODE);
    Serial.println(buffer);
    snprintf(buffer, sizeof(buffer), "║ Flash Speed                             : %s                  ║", CONFIG_ESPTOOLPY_FLASHFREQ);
    Serial.println(buffer);
    snprintf(buffer, sizeof(buffer), "║ Flash Size                              : %s                  ║", CONFIG_ESPTOOLPY_FLASHSIZE);
    Serial.println(buffer);

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(buffer, sizeof(buffer), "║ MAC Address                             : %02X:%02X:%02X:%02X:%02X:%02X    ║", mac[0], mac[1], mac[2], mac[3],
             mac[4], mac[5]);
    Serial.println(buffer);

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
    result.reserve(symbol.length() * count);                                    // Speicher vorab reservieren
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

/**
 * @brief report about poll manager status
 *
 */
void FlapReporting::reportPollStatus() {
    char liga[32];
    strcpy(liga, leagueName(activeLeague));                                     // current league name

    char       nextKickoff[32];
    struct tm* kickoff = localtime(&currentNextKickoffTime);
    strftime(nextKickoff, sizeof(nextKickoff), "%d.%m.%Y %H:%M", kickoff);
    //                                     1                   2                   3                   4
    //              123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890
    Serial.println("┌───────────────────────────────────────────────────────────────────────────────────────────┐");
    Serial.printf("│ OpenLigaDB Poll Manager Status                         Latest Update: %s │\n", lastScanTimestamp);
    Serial.println("├──────────────────┬──────────────────────────┬──────────────────┬──────────────────────────┤");
    Serial.printf("│ Poll Mode        │ %-24s │ Poll Scope       │ %-24s │\n", pollModeToString(currentPollMode),
                  pollScopeToString(currentPollScope));
    Serial.println("├──────────────────┼──────────────────────────┼──────────────────┼──────────────────────────┤");
    Serial.printf("│ active League    │ %s            │ Season/Matchday  │ %4d/%2d                  │\n", liga, ligaSeason, ligaMatchday);

    if (ligaLiveMatchCount > 0)
        Serial.println("├──────────────────┼──────────────────────────┼──────────────────┼──────────────────────────┤");

    // list off live goals
    for (int i = 0; i < ligaLiveMatchCount; ++i) {
        MatchInfo& match            = liveMatches[i];
        bool       hasGoals         = false;
        bool       hasPrintedHeader = false;

        for (int j = 0; j < liveGoalCount; ++j) {
            LiveMatchGoalInfo& goal = goalsInfos[j];
            if (goal.matchID == match.matchID) {
                if (!hasPrintedHeader) {
                    Serial.print("│ live Scores      │ ");
                    hasPrintedHeader = true;
                } else {
                    Serial.print("│                  │ ");
                }
                if (hasPrintedHeader) {
                    hasGoals = true;
                    printUtf8Padded(match.team1.c_str(), W_TEAM);
                    Serial.printf(" │ %-5s      (%2d') │ ", goal.result, goal.goalMinute);
                    printUtf8Padded(match.team2.c_str(), W_TEAM);
                    Serial.println(" │");
                }
            }
        }
        // Optional: Zeige Spiel ohne Tore
        if (!hasPrintedHeader) {
            Serial.print("│ live Scores      │ ");
            printUtf8Padded(match.team1.c_str(), W_TEAM);
            Serial.printf(" │ %-5s      (%2d') │ ", "0:0", 0);
            printUtf8Padded(match.team2.c_str(), W_TEAM);
            Serial.println(" │");
        }
    }
    if (ligaLiveMatchCount > 0)
        Serial.println("├──────────────────┼──────────────────────────┼──────────────────┼──────────────────────────┤");

    // list off live matches
    for (int i = 0; i < ligaLiveMatchCount; ++i) {
        if (i == 0)
            Serial.print("│ live Matches     │ ");
        else
            Serial.print("│                  │ ");
        printUtf8Padded(liveMatches[i].team1.c_str(), W_TEAM);
        Serial.printf(" │ %-16s │ ", nextKickoff);
        printUtf8Padded(liveMatches[i].team2.c_str(), W_TEAM);
        Serial.println(" │");
    }
    if (ligaNextMatchCount > 0)
        Serial.println("├──────────────────┼──────────────────────────┼──────────────────┼──────────────────────────┤");

    // list off next matches
    for (int i = 0; i < ligaNextMatchCount; ++i) {
        char       nextKickoff[32];
        struct tm* kickoff = localtime(&nextMatches[i].kickoff);
        strftime(nextKickoff, sizeof(nextKickoff), "%d.%m.%Y %H:%M", kickoff);

        if (i == 0)
            Serial.print("│ next Matches     │ ");
        else
            Serial.print("│                  │ ");
        printUtf8Padded(nextMatches[i].team1.c_str(), W_TEAM);
        Serial.printf(" │ %-16s │ ", nextKickoff);
        printUtf8Padded(nextMatches[i].team2.c_str(), W_TEAM);
        Serial.println(" │");
    }

    if (ligaPlanMatchCount > 0)
        Serial.println("├──────────────────┼──────────────────────────┼──────────────────┼──────────────────────────┤");

    // List off planned matches
    for (int i = 0; i < ligaPlanMatchCount; ++i) {
        char       planKickoff[32];
        struct tm* kickoff = localtime(&planMatches[i].kickoff);
        strftime(planKickoff, sizeof(planKickoff), "%d.%m.%Y %H:%M", kickoff);

        if (i == 0)
            Serial.print("│ planned Matches  │ ");
        else
            Serial.print("│                  │ ");
        printUtf8Padded(planMatches[i].team1.c_str(), W_TEAM);
        Serial.printf(" │ %-16s │ ", planKickoff);
        printUtf8Padded(planMatches[i].team2.c_str(), W_TEAM);
        Serial.println(" │");
    }

    Serial.println("└──────────────────┴──────────────────────────┴──────────────────┴──────────────────────────┘");
}

// -----------------------------

/**
 * @brief generate JSON file for report Task Status
 *
 */
void FlapReporting::createTaskStatusJson() {
    if (!Store->available())
        return;

    StaticJsonDocument<1024> doc;
    doc["report"] = "FLAP TASK STATUS REPORT";

    // Tick count
    doc["Tick count"] = String(xTaskGetTickCount());

    // Task count
    doc["Task count"] = String(uxTaskGetNumberOfTasks());

    // Uptime
    TickType_t ticks        = xTaskGetTickCount();
    uint32_t   totalSeconds = ticks * portTICK_PERIOD_MS / 1000;
    uint32_t   days         = totalSeconds / 86400;
    uint32_t   hours        = (totalSeconds % 86400) / 3600;
    uint32_t   minutes      = (totalSeconds % 3600) / 60;
    uint32_t   seconds      = totalSeconds % 60;

    String uptime = String(days) + " Tage, " + (hours < 10 ? "0" : "") + String(hours) + ":" + (minutes < 10 ? "0" : "") + String(minutes) + ":" +
                    (seconds < 10 ? "0" : "") + String(seconds);
    doc["Uptime"] = uptime;

    // Next I2C scan
    uint32_t ms = getNextScanRemainingMs();
    if (ms == 0) {
        doc["Next I2C bus scan in"] = "inactive";
    } else {
        uint32_t m   = ms / 60000;
        uint32_t s   = (ms % 60000) / 1000;
        String   buf = String((unsigned long)m) + ":" + String((unsigned long)s) + " (mm:ss)";

        // sprintf(buf, "%02lu:%02lu (mm:ss)", (unsigned long)m, (unsigned long)s);
        doc["Next I2C bus scan in"] = buf;
    }

    // Next availability check
    ms = getNextAvailabilityRemainingMs();
    if (ms == 0) {
        doc["Next device check in"] = "inactive";
    } else {
        uint32_t m = ms / 60000;
        uint32_t s = (ms % 60000) / 1000;
        char     buf[16];
        sprintf(buf, "%02lu:%02lu (mm:ss)", (unsigned long)m, (unsigned long)s);
        doc["Next device check in"] = buf;
    }

    // Next Liga scan
    ms = getNextLigaScanRemainingMs();
    if (ms == 0) {
        doc["Next OpenLiga scan in"] = "inactive";
    } else {
        uint32_t m = ms / 60000;
        uint32_t s = (ms % 60000) / 1000;
        char     buf[16];
        sprintf(buf, "%02lu:%02lu (mm:ss)", (unsigned long)m, (unsigned long)s);
        doc["Next OpenLiga scan in"] = buf;
    }

    Store->saveFile("/R01.json", doc);
}

// -----------------------------

/**
 * @brief render flat JSON file as html
 *
 */

void sendStatusHtmlStream(const char* filename) {
    StaticJsonDocument<1024> doc;
    Store->readFile(filename, doc);

    // send header once
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html; charset=UTF-8", "");

    // HTML-header
    server.sendContent(
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<title>FLAP Task Status</title>"
        "<style>body{font-family:monospace; background:#f5f5f5; white-space:pre;}</style>"
        "</head><body>\n");

    // Report Header
    // server.sendContent("FLAP TASK STATUS REPORT\n");
    // server.sendContent("=======================\n\n");

    // stream all Key/Value-pairs
    for (JsonPair kv : doc.as<JsonObject>()) {
        String line;
        line.reserve(128);
        if (kv.key() == "report") {
            line += kv.value().as<const char*>();
            line += "\n";
            server.sendContent(line);
            server.sendContent("=======================\n");
        } else {
            line += kv.key().c_str();
            line += " ";
            line += kv.value().as<const char*>();
            line += "\n";
            server.sendContent(line);
        }
    }

    // HTML-End
    server.sendContent("</body></html>");
    server.sendContent("");                                                     // EOF
}
