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

void FlapReporting::reportSlaveRegistry() {
    Serial.println("╔══════════════════════════════════════════════════════════════════════════════════════════╗");
    Serial.println("║                             FLAP I²C DEVICE REGISTRY                                     ║");
    Serial.println("╠══════╦══════════════════════╦════════╦═════╦═══════╦═══════╦══════╦══════╦═══════════════╣");
    Serial.println("║ I²C  ║ Device               ║ Offset ║ rpm ║ ms/Rev║ St/Rev║ Flaps║ Pos  ║ Sensor Status ║");
    Serial.println("╟──────╫──────────────────────╫────────╫─────╫───────╫───────╫──────╫──────╫───────────────╢");

    for (const auto& [address, device] : g_slaveRegistry) {
        if (!device) {
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "║ 0x%02X ║       (nullptr)      ║   ---  ║ --- ║  ---  ║  ---  ║  --- ║ ---  ║     [invalid]   ║",
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
        snprintf(line, sizeof(line), "║ 0x%02X ║ %-20s ║ %6u ║ %3u ║ %5u ║ %5u ║ %4u ║ %4u ║ %-13s ║", address, name, offset, (speedMs > 0 ? rpm : 0),
                 speedMs, steps, flaps, pos, sensorStatus);

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
        snprintf(buffer, sizeof(buffer), "║  %-16s │ %7u       │ %6u       │ %6u      │   %2u      ║", name, reserve, size, used, prio);
        Serial.println(buffer);
    };

    // Aktuelle Task (ReportingTask)
    printTaskInfo(pcTaskGetName(NULL), uxTaskGetStackHighWaterMark(NULL), STACK_REPORTING, PRIO_REPORTING);

    // Weitere registrierte Tasks
    if (g_remoteControlHandle != nullptr)
        printTaskInfo(pcTaskGetName(g_remoteControlHandle), uxTaskGetStackHighWaterMark(g_remoteControlHandle), STACK_REMOTE, PRIO_REMOTE);

    if (g_remoteParserHandle != nullptr)
        printTaskInfo(pcTaskGetName(g_remoteParserHandle), uxTaskGetStackHighWaterMark(g_remoteParserHandle), STACK_PARSER, PRIO_PARSER);

    if (g_twinRegisterHandle != nullptr)
        printTaskInfo(pcTaskGetName(g_twinRegisterHandle), uxTaskGetStackHighWaterMark(g_twinRegisterHandle), STACK_REGISTRY, PRIO_REGISTRY);

    if (g_statisticTaskHandle != nullptr)
        printTaskInfo(pcTaskGetName(g_statisticTaskHandle), uxTaskGetStackHighWaterMark(g_statisticTaskHandle), STACK_STATISTICS, PRIO_STATISTICS);

    for (uint8_t i = 0; i < numberOfTwins; i++) {
        if (g_twinHandle[i] != nullptr)
            printTaskInfo(pcTaskGetName(g_twinHandle[i]), uxTaskGetStackHighWaterMark(g_twinHandle[i]), STACK_TWIN, PRIO_TWIN);
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

    snprintf(buffer, sizeof(buffer), "║ Task count: %10u                                   ║", uxTaskGetNumberOfTasks());
    Serial.println(buffer);

    // Uptime formatting
    TickType_t ticks        = xTaskGetTickCount();
    uint32_t   totalSeconds = ticks * portTICK_PERIOD_MS / 1000;
    uint32_t   days         = totalSeconds / 86400;
    uint32_t   hours        = (totalSeconds % 86400) / 3600;
    uint32_t   minutes      = (totalSeconds % 3600) / 60;
    uint32_t   seconds      = totalSeconds % 60;

    snprintf(buffer, sizeof(buffer), "║ Uptime:       %3u Tage, %02u:%02u:%02u                         ║", days, hours, minutes, seconds);
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
    snprintf(buffer, sizeof(buffer), "║ Free RAM now                     (kByte): %7u              ║", ESP.getFreeHeap() / 1024);
    Serial.println(buffer);

    snprintf(buffer, sizeof(buffer), "║ Lowest level of free RAM         (kByte): %7u              ║", ESP.getMinFreeHeap() / 1024);
    Serial.println(buffer);

    snprintf(buffer, sizeof(buffer), "║ Biggest unfragmented RAM block   (kByte): %7u              ║", ESP.getMaxAllocHeap() / 1024);
    Serial.println(buffer);

    Serial.println("╚════════════════════════════════════════════════════════════════╝");
}

#include <vector>                                                               // oben in der .cpp

void FlapReporting::reportAllTwins(int wrapWidth) {
    for (int i = 0; i < numberOfTwins; ++i) {
        SlaveTwin* twin = Twin[i];
        if (!twin)
            continue;

        // Header info
        char serialBuf[64];
        snprintf(serialBuf, sizeof(serialBuf), "%s", formatSerialNumber(twin->parameter.serialnumber));
        int stepsPerRev = twin->parameter.steps;

        int count = twin->parameter.flaps;
        if (count <= 0)
            continue;

        // compute min/max for sparkline
        int minVal = INT_MAX, maxVal = 0;
        for (int j = 0; j < count; ++j) {
            int v  = twin->stepsByFlap[j];
            minVal = min(minVal, v);
            maxVal = max(maxVal, v);
        }

        struct Chunk {
            String flaps;
            String steps;
            String index;
        };
        std::vector<Chunk> chunks;
        int                widest = 0;

        for (int offset = 0; offset < count; offset += wrapWidth) {
            int  lineCount  = min(wrapWidth, count - offset);
            bool firstChunk = (offset == 0);

            // Flaps line
            String flapsLine = "           |  Flaps  :";
            for (int k = 0; k < lineCount; ++k) {
                int         v      = twin->stepsByFlap[offset + k];
                const char* barStr = selectSparklineLevel(v, minVal, maxVal);
                flapsLine += "  ";
                flapsLine += barStr;
                flapsLine += " ";
            }

            // Steps line
            String stepsLine;
            if (firstChunk) {
                char addrBuf[32];
                snprintf(addrBuf, sizeof(addrBuf), "  0x%02X |  Steps  :", twin->slaveAddress);
                stepsLine = String(addrBuf);
            } else {
                stepsLine = "           |  Steps  :";
            }
            for (int k = 0; k < lineCount; ++k) {
                char buf[8];
                int  v = twin->stepsByFlap[offset + k];
                snprintf(buf, sizeof(buf), " %03d", v);
                stepsLine += buf;
            }

            // Index line
            String indexLine = "           |  Index  :";
            for (int k = 0; k < lineCount; ++k) {
                char buf[8];
                int  idx = offset + k + 1;
                snprintf(buf, sizeof(buf), " %2d", idx);
                indexLine += buf;
            }

            widest = max(widest, (int)flapsLine.length());
            widest = max(widest, (int)stepsLine.length());
            widest = max(widest, (int)indexLine.length());
            chunks.push_back({flapsLine, stepsLine, indexLine});
        }

        // Build header content, right-align Steps/Rev
        String headerContent = String(serialBuf);
        char   rightPart[32];
        snprintf(rightPart, sizeof(rightPart), "Steps/Rev: %d", stepsPerRev);
        int inner_space = widest - (int)headerContent.length() - (int)strlen(rightPart);
        if (inner_space < 1)
            inner_space = 1;
        for (int s = 0; s < inner_space; ++s)
            headerContent += ' ';
        headerContent += rightPart;

        int boxWidth = headerContent.length() + 2;                              // include vertical bars

        // draw box top
        Serial.print("┌");
        for (int j = 0; j < boxWidth - 2; ++j)
            Serial.print("─");
        Serial.println("┐");
        // header line
        Serial.print("│");
        Serial.print(headerContent);
        Serial.println("│");
        // separator line
        Serial.print("├");
        for (int j = 0; j < boxWidth - 2; ++j)
            Serial.print("─");
        Serial.println("┤");

        // content chunks (no extra blank after separator)
        for (size_t ci = 0; ci < chunks.size(); ++ci) {
            Serial.println(chunks[ci].flaps);
            Serial.println(chunks[ci].steps);
            Serial.println(chunks[ci].index);
            if (ci + 1 < chunks.size()) {
                Serial.println();                                               // gap between chunks
            }
        }

        // bottom border
        Serial.print("└");
        for (int j = 0; j < boxWidth - 2; ++j)
            Serial.print("─");
        Serial.println("┘");

        Serial.println();                                                       // space between twins
    }
}

// private
void FlapReporting::printStepsByFlapReport(SlaveTwin& twin, int wrapWidth) {
    int count = twin.parameter.flaps;
    if (count <= 0)
        return;

    // compute min/max for sparkline
    int minVal = INT_MAX, maxVal = 0;
    for (int i = 0; i < count; ++i) {
        int v  = twin.stepsByFlap[i];
        minVal = min(minVal, v);
        maxVal = max(maxVal, v);
    }

    // Build all chunk lines first to determine width
    int widest = 0;
    struct Chunk {
        String flaps;
        String steps;
        String index;
    };
    std::vector<Chunk> chunks;
    for (int offset = 0; offset < count; offset += wrapWidth) {
        int    lineCount  = min(wrapWidth, count - offset);
        bool   firstChunk = (offset == 0);
        String flapsLine  = buildFlapsLine(twin, offset, lineCount, minVal, maxVal);
        String stepsLine  = buildStepsLine(twin, offset, lineCount, firstChunk);
        String indexLine  = buildIndexLine(offset, lineCount);
        widest            = max(widest, (int)flapsLine.length());
        widest            = max(widest, (int)stepsLine.length());
        widest            = max(widest, (int)indexLine.length());
        chunks.push_back({flapsLine, stepsLine, indexLine});
    }

    // Header serial + Steps/Rev
    char serialBuf[64];
    snprintf(serialBuf, sizeof(serialBuf), "%s", formatSerialNumber(twin.parameter.serialnumber));
    char rightPart[32];
    snprintf(rightPart, sizeof(rightPart), "Steps/Rev: %d", twin.parameter.steps);
    String headerContent = String(serialBuf);
    int    inner_space   = widest - (int)headerContent.length() - (int)strlen(rightPart);
    if (inner_space < 1)
        inner_space = 1;
    for (int i = 0; i < inner_space; ++i)
        headerContent += ' ';
    headerContent += rightPart;

    // total box width = 2 for vertical borders + headerContent length
    int boxWidth = headerContent.length() + 2;

    // draw box
    // top
    Serial.print("┌");
    for (int i = 0; i < boxWidth - 2; ++i)
        Serial.print("─");
    Serial.println("┐");
    // header
    Serial.print("│");
    Serial.print(headerContent);
    Serial.println("│");
    // sep
    Serial.print("├");
    for (int i = 0; i < boxWidth - 2; ++i)
        Serial.print("─");
    Serial.println("┤");
    Serial.println();                                                           // blank line as in sample

    // content chunks
    bool first = true;
    for (auto& c : chunks) {
        Serial.println(c.flaps);
        Serial.println(c.steps);
        Serial.println(c.index);
        if (&c != &chunks.back())
            Serial.println();                                                   // gap between chunks
    }

    // bottom
    Serial.print("└");
    for (int i = 0; i < boxWidth - 2; ++i)
        Serial.print("─");
    Serial.println("┘");
}

const char* FlapReporting::selectSparklineLevel(int value, int minVal, int maxVal) {
    if (maxVal == minVal) {
        return (minVal == 0) ? SPARKLINE_LEVELS[0] : SPARKLINE_LEVELS[SPARKLINE_LEVEL_COUNT - 1];
    }
    int idx = ((long)(value - minVal) * (SPARKLINE_LEVEL_COUNT - 1)) / (maxVal - minVal);
    if (idx < 0)
        idx = 0;
    if (idx >= SPARKLINE_LEVEL_COUNT)
        idx = SPARKLINE_LEVEL_COUNT - 1;
    return SPARKLINE_LEVELS[idx];
}

// Box drawing helpers
void FlapReporting::printTopBorder(int width) {
    Serial.print("┌");
    for (int i = 0; i < width - 2; ++i)
        Serial.print("─");
    Serial.println("┐");
}

void FlapReporting::printSepBorder(int width) {
    Serial.print("├");
    for (int i = 0; i < width - 2; ++i)
        Serial.print("─");
    Serial.println("┤");
}

void FlapReporting::printBottomBorder(int width) {
    Serial.print("└");
    for (int i = 0; i < width - 2; ++i)
        Serial.print("─");
    Serial.println("┘");
}

void FlapReporting::printHeaderLine(const char* serial, int stepsPerRev, int total_width) {
    char rightPart[32];
    snprintf(rightPart, sizeof(rightPart), "Steps/Rev: %d", stepsPerRev);
    int leftLen     = strlen(serial);
    int rightLen    = strlen(rightPart);
    int inner_space = total_width - 2 - leftLen - rightLen;                     // account for box sides
    if (inner_space < 1)
        inner_space = 1;

    Serial.print("│ ");
    Serial.print(serial);
    for (int i = 0; i < inner_space; ++i)
        Serial.print(' ');
    Serial.print(rightPart);
    Serial.println(" │");
}

String FlapReporting::buildFlapsLine(SlaveTwin& twin, int offset, int lineCount, int minVal, int maxVal) {
    String s = "           |  Flaps  :";
    for (int i = 0; i < lineCount; ++i) {
        int         v      = twin.stepsByFlap[offset + i];
        const char* barStr = FlapReporting().selectSparklineLevel(v, minVal, maxVal); // oder falls non-static: use instance
        s += "  ";
        s += barStr;
        s += " ";
    }
    return s;
}

String FlapReporting::buildStepsLine(SlaveTwin& twin, int offset, int lineCount, bool firstChunk) {
    String s;
    if (firstChunk) {
        char addrBuf[16];
        snprintf(addrBuf, sizeof(addrBuf), "  0x%02X |  Steps  :", twin.slaveAddress);
        s += addrBuf;
    } else {
        s += "           |  Steps  :";
    }
    for (int i = 0; i < lineCount; ++i) {
        char buf[8];
        int  v = twin.stepsByFlap[offset + i];
        snprintf(buf, sizeof(buf), " %03d", v);
        s += buf;
    }
    return s;
}

String FlapReporting::buildIndexLine(int offset, int lineCount) {
    String s = "           |  Index  :";
    for (int i = 0; i < lineCount; ++i) {
        char buf[8];
        int  idx = offset + i + 1;
        snprintf(buf, sizeof(buf), " %2d", idx);
        s += buf;
    }
    return s;
}
