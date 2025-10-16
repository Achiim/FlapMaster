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
 */
#include <Arduino.h>
#include <vector>
#include "SlaveTwin.h"
#include "FlapTasks.h"

#ifndef FlapReporting_h
    #define FlapReporting_h

    // global defined statistic counters and history
    #define HISTORY_SIZE 10                                                     // i2c statistics history length in minutes

    #define MAXB_TEAM 32
    #define MAXB_SHORT 16
    #define MAXB_DFB 4

// Kompakte Spaltenbreiten
constexpr int W_POS   = 3;                                                      // 1..18
constexpr int W_TEAM  = 24;                                                     // z. B. „Borussia Mönchengladbach“
constexpr int W_SHORT = 12;                                                     // „Union Berlin“ ggf. gekürzt
constexpr int W_DFB   = 3;                                                      // 3-Letter
constexpr int W_FLAP  = 4;                                                      // 4
constexpr int W_SP    = 2;                                                      // Spiele (1..34)
constexpr int W_DIFF  = 4;                                                      // -99..+99
constexpr int W_PKT   = 3;                                                      // 0..99
constexpr int W_W     = 2;                                                      // won 0..99
constexpr int W_L     = 2;                                                      // lost 0..99
constexpr int W_D     = 2;                                                      // drawn 0..99
constexpr int W_OG    = 2;                                                      // own goals 0..99
constexpr int W_G     = 2;                                                      // goals 0..99

// globar routines for JSON Format
void   sendStatusHtmlStream(const char* filename);
String formatIsoTime(time_t t);
String getIsoTimestamp();

class FlapReporting {
   public:
    int statistic = 0;

    // Report trace
    template <typename... Args>                                                 // Report trace
    void reportPrint(const Args&... args) {
        tracePrint("[FLAP - REPORT  ] ", args...);
    }

    template <typename... Args>                                                 // Report trace with new line
    void reportPrintln(const Args&... args) {
        tracePrintln("[FLAP - REPORT  ] ", args...);
    }

    // Constructor
    FlapReporting();

    // ----------------------------
    uint32_t maxValueFromHistory(uint32_t* history);                            // get maximum value from history

    void createTaskStatusJson();                                                // generate JSON Format
    void reportTaskStatus();                                                    // show Task status report
    void renderTaskReport();

    void createPollStatusJson();                                                // generate JSON Format

    void reportMemory();                                                        // show memory usage
    void reportRtosTasks();                                                     // show Tasklist with remaining stack size
    void reportAllTwinStepsByFlap(int wrapWidth = 20);                          // show steps per flap for all Twins
    void reportSlaveRegistry();                                                 // show registry content
    void reportI2CStatistic();                                                  // show I2C usage history
    void reportLigaTable();                                                     // show Bundesliga table
    void reportPollStatus();                                                    // show poll manager status

   private:
    static const char    BLOCK_LIGHT[];                                         // bar pattern for Access
    static const char    BLOCK_MEDIUM[];                                        // bar pattern for Send
    static const char    BLOCK_DENSE[];                                         // bar pattern for Read
    static const char*   SPARKLINE_LEVELS[];                                    // declaration
    static constexpr int SPARKLINE_LEVEL_COUNT = 8;                             // number of levels

    void        printStepsByFlapReport(SlaveTwin& twin, int wrapWidth);         // Einzelreport für einen Twin (wird intern benutzt).
    const char* selectSparklineLevel(int value, int minVal, int maxVal);        // helper to select sparkling
    void        printBar(uint32_t value, float scale, const char* symbol, uint8_t maxLength); // print bar with value
    void        printI2CHistory();                                              // print I2C history
    void        printUptime();                                                  // Helper to report up time
    static void printTableRow(const LigaRow& r);                                // Bundesliga table row
    static void renderLigaTable(const LigaSnapshot& s);                         // build Bundesliga tabel trace

    // box drawing helpers for steps by flap report
    String repeatChar(const String& symbol, int count);                         // Helper for unicode
    String padStart(String val, int length, char fill = ' ');                   // Helper to fill some blanks
    void   drawTwinChunk(const SlaveTwin& twin, int offset, int wrapWidth);     // to draw report line

    uint32_t getNextScanRemainingMs();                                          // next i2c scan time
    uint32_t getNextAvailabilityRemainingMs();                                  // next availability checl time
    uint32_t getNextLigaScanRemainingMs();                                      // next liga scan time

#endif                                                                          // FlapReporting_h
};