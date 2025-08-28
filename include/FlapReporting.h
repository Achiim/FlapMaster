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
constexpr int W_SHORT = 10;                                                     // „Union Berlin“ ggf. gekürzt
constexpr int W_DFB   = 3;                                                      // 3-Letter
constexpr int W_SP    = 2;                                                      // Spiele (1..34)
constexpr int W_DIFF  = 4;                                                      // -99..+99
constexpr int W_PKT   = 3;                                                      // 0..99

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

    void        reportTaskStatus();                                             // show Task status report
    void        reportMemory();                                                 // show memory usage
    void        reportRtosTasks();                                              // show Tasklist with remaining stack size
    void        reportAllTwinStepsByFlap(int wrapWidth = 20);                   // show steps per flap for all Twins
    void        reportSlaveRegistry();                                          // show registry content
    void        reportI2CStatistic();                                           // show I2C usage history
    void        reportLigaTable();
    static void printTableRow(const LigaRow& r);
    static void renderLigaTable(const LigaSnapshot& s);

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
    void        printTableRow(LigaRow& r);
    void        renderLigaTabelle(LigaSnapshot& s);

    // box drawing helpers for steps by flap report
    String repeatChar(const String& symbol, int count);                         // Helper for unicode
    String padStart(String val, int length, char fill = ' ');                   // Helper to fill some blanks
    void   drawTwinChunk(const SlaveTwin& twin, int offset, int wrapWidth);     // to draw report line

    uint32_t getNextScanRemainingMs();                                          // next i2c scan time
    uint32_t getNextAvailabilityRemainingMs();                                  // next availability checl time

#endif                                                                          // FlapReporting_h
};