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

// Definitionen der Symbole mit Null-Terminator
const char FlapReporting::BLOCK_LIGHT[]  = u8"░";
const char FlapReporting::BLOCK_MEDIUM[] = u8"▒";
const char FlapReporting::BLOCK_DENSE[]  = u8"▓";

// ----------------------------
// Constructor
FlapReporting::FlapReporting() {}

// -----------------------------------
// trace print I2C usage statistic
void FlapReporting::printI2CStatistic() {
    Serial.println("- FLAP I²C STATISTIC AND HISTRORY OF LAST MINUTES");
    if (DataEvaluation == nullptr) {
        #ifdef MASTERVERBOSE
            Serial.println("reportingTask(): DataEvaluation not available!");
        #endif
        return;
    }
    printFramedHistory();
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
void FlapReporting::printFramedHistory() {
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

        // FRame beginning
        Serial.println("┌─────────────────────────────────────────────────────────────────────────────┐");

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

        // Frame finish
        Serial.println("└─────────────────────────────────────────────────────────────────────────────┘");
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

void FlapReporting::traceSlaveRegistry() {
    Serial.println("╔══════════════════════════════════════════════════════════════════════════════════════════╗");
    Serial.println("║                             FLAP I²C DEVICE REGISTRY                                     ║");
    Serial.println("╠══════╦══════════════════════╦════════╦═════╦═══════╦═══════╦══════╦══════╦═══════════════╣");
    Serial.println("║ I²C  ║ Device               ║ Offset ║ rpm ║ ms/Rev║ St/Rev║ Flaps║ Pos  ║ Sensor Status ║");
    Serial.println("╟──────╫──────────────────────╫────────╫─────╫───────╫───────╫──────╫──────╫───────────────╢");

    for (const auto& [address, device] : g_slaveRegistry) {
        if (!device) {
            char buffer[64];
            snprintf(buffer, sizeof(buffer),
                     "║ 0x%02X ║       (nullptr)      ║   ---  ║ --- ║  ---  ║  ---  ║  --- ║ ---  ║     [invalid]   ║",
                     address);
            Serial.println(buffer);
            continue;
        }

        const char* sensorStatus = device->parameter.sensorworking ? "WORKING" : "BROKEN";
        const char* name         = formatSerialNumber(device->parameter.serialnumber);

        uint16_t offset  = device->parameter.offset;
        uint16_t speedMs = device->parameter.speed;
        uint16_t rpm     = (speedMs > 0) ? (60000 / speedMs) : 0;
        uint16_t steps   = device->parameter.steps;
        uint8_t  flaps   = device->parameter.flaps;
        uint16_t pos     = device->position;

        char line[128];
        snprintf(line, sizeof(line), "║ 0x%02X ║ %-20s ║ %6u ║ %3u ║ %5u ║ %5u ║ %4u ║ %4u ║ %-13s ║", address, name,
                 offset, (speedMs > 0 ? rpm : 0), speedMs, steps, flaps, pos, sensorStatus);

        Serial.println(line);
    }

    Serial.println("╚══════╩══════════════════════╩════════╩═════╩═══════╩═══════╩══════╩══════╩═══════════════╝");
}

void FlapReporting::reportTasks() {
    Serial.println("╔════════════════════════════════════════════════════════════════════════════╗");
    Serial.println("║                             RTOS Task Report                               ║");
    Serial.println("╠════════════════════════════════════════════════════════════════════════════╣");
    Serial.println("║ Task Name         │ Stack Reserve │  Stack Size  │  Peak Usage │ Priority  ║");
    Serial.println("╟───────────────────┼───────────────┼──────────────┼─────────────┼───────────╢");

    auto printTaskInfo = [](const char* name, UBaseType_t reserve, uint16_t size, uint8_t prio) {
        uint16_t used = size - reserve;
        char     buffer[128];
        snprintf(buffer, sizeof(buffer), "║  %-16s │ %7u       │ %6u       │ %6u      │   %2u      ║", name, reserve,
                 size, used, prio);
        Serial.println(buffer);
    };

    // Aktuelle Task (ReportingTask)
    printTaskInfo(pcTaskGetName(NULL), uxTaskGetStackHighWaterMark(NULL), STACK_REPORTING, PRIO_REPORTING);

    // Weitere registrierte Tasks
    if (g_remoteControlHandle != nullptr)
        printTaskInfo(pcTaskGetName(g_remoteControlHandle), uxTaskGetStackHighWaterMark(g_remoteControlHandle),
                      STACK_REMOTE, PRIO_REMOTE);

    if (g_remoteParserHandle != nullptr)
        printTaskInfo(pcTaskGetName(g_remoteParserHandle), uxTaskGetStackHighWaterMark(g_remoteParserHandle),
                      STACK_PARSER, PRIO_PARSER);

    if (g_twinRegisterHandle != nullptr)
        printTaskInfo(pcTaskGetName(g_twinRegisterHandle), uxTaskGetStackHighWaterMark(g_twinRegisterHandle),
                      STACK_REGISTRY, PRIO_REGISTRY);

    if (g_statisticTaskHandle != nullptr)
        printTaskInfo(pcTaskGetName(g_statisticTaskHandle), uxTaskGetStackHighWaterMark(g_statisticTaskHandle),
                      STACK_STATISTICS, PRIO_STATISTICS);

    for (uint8_t i = 0; i < numberOfTwins; i++) {
        if (g_twinHandle[i] != nullptr)
            printTaskInfo(pcTaskGetName(g_twinHandle[i]), uxTaskGetStackHighWaterMark(g_twinHandle[i]), STACK_TWIN,
                          PRIO_TWIN);
    }

    Serial.println("╚════════════════════════════════════════════════════════════════════════════╝");
}
void FlapReporting::reportHeader() {
    Serial.println("╔══════════════════════════════════════════════════════════╗");
    Serial.println("║                  FLAP TASKS STATUS REPORT                ║");
    Serial.println("╠══════════════════════════════════════════════════════════╣");

    char buffer[128];
    snprintf(buffer, sizeof(buffer), "║ Tick count: %10lu                                   ║", xTaskGetTickCount());
    Serial.println(buffer);

    snprintf(buffer, sizeof(buffer), "║ Task count: %10u                                   ║",
             uxTaskGetNumberOfTasks());
    Serial.println(buffer);

    // Uptime formatting
    TickType_t ticks        = xTaskGetTickCount();
    uint32_t   totalSeconds = ticks * portTICK_PERIOD_MS / 1000;
    uint32_t   days         = totalSeconds / 86400;
    uint32_t   hours        = (totalSeconds % 86400) / 3600;
    uint32_t   minutes      = (totalSeconds % 3600) / 60;
    uint32_t   seconds      = totalSeconds % 60;

    snprintf(buffer, sizeof(buffer), "║ Uptime:       %3u Tage, %02u:%02u:%02u                         ║", days, hours,
             minutes, seconds);
    Serial.println(buffer);

    Serial.println("╚══════════════════════════════════════════════════════════╝");
}

void FlapReporting::reportHeaderAlt() {
    reportPrintln("");
    reportPrintln("");
    reportPrintln("======== Flap Tasks status ========");
    reportPrint("  Tick count: ");
    Serial.println(xTaskGetTickCount());
    reportPrint("  Task count: ");
    Serial.println(uxTaskGetNumberOfTasks());
    printUptime();
    reportPrintln("");
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
    snprintf(buffer, sizeof(buffer), "║ Free RAM now                     (kByte): %7u              ║",
             ESP.getFreeHeap() / 1024);
    Serial.println(buffer);

    snprintf(buffer, sizeof(buffer), "║ Lowest level of free RAM         (kByte): %7u              ║",
             ESP.getMinFreeHeap() / 1024);
    Serial.println(buffer);

    snprintf(buffer, sizeof(buffer), "║ Biggest unfragmented RAM block   (kByte): %7u              ║",
             ESP.getMaxAllocHeap() / 1024);
    Serial.println(buffer);

    Serial.println("╚════════════════════════════════════════════════════════════════╝");
}