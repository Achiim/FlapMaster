#include <Arduino.h>
#include <vector>
#include "SlaveTwin.h"

#ifndef FlapReporting_h
    #define FlapReporting_h

    // global defined statistic counters and history
    #define HISTORY_SIZE 10                                                     // i2c statistics history length in minutes

class FlapReporting {
   public:
    int statistic = 0;

    // Report
    template <typename... Args>
    void reportPrint(const Args&... args) {
        tracePrint("[FLAP - REPORT  ] ", args...);
    }
    template <typename... Args>
    void reportPrintln(const Args&... args) {
        tracePrintln("[FLAP - REPORT  ] ", args...);
    }

    // Constructor
    FlapReporting();

    // ----------------------------
    uint32_t maxValueFromHistory(uint32_t* history);

    void reportTaskStatus();                                                    // show Task status report
    void reportMemory();                                                        // show memory usage
    void reportRtosTasks();                                                     // show Tasklist with remaining stack size
    void reportAllTwinStepsByFlap(int wrapWidth = 20);                          // show steps per flap for all Twins
    void reportSlaveRegistry();                                                 // show registry content
    void reportI2CStatistic();                                                  // show I2C usage history

   private:
    static const char    BLOCK_LIGHT[];                                         // bar pattern for Access
    static const char    BLOCK_MEDIUM[];                                        // bar pattern for Send
    static const char    BLOCK_DENSE[];                                         // bar pattern for Read
    static const char*   SPARKLINE_LEVELS[];                                    // declaration
    static constexpr int SPARKLINE_LEVEL_COUNT = 8;                             // number of levels

    void        printStepsByFlapReport(SlaveTwin& twin, int wrapWidth);         // Einzelreport für einen Twin (wird intern benutzt).
    const char* selectSparklineLevel(int value, int minVal, int maxVal);        // helper to select sparkling
    void        printBar(uint32_t value, float scale, const char* symbol, uint8_t maxLength);
    void        printI2CHistory();
    void        printUptime();                                                  // Helper to report up time

    // box drawing helpers for steps by flap report
    String repeatChar(const String& symbol, int count);                         // Helper for unicode
    String padStart(String val, int length, char fill = ' ');                   // Helper to fill some blanks
    void   drawTwinChunk(const SlaveTwin& twin, int offset, int wrapWidth);     // to draw report line

    uint32_t getNextScanRemainingMs();                                          // next i2c scan time
    uint32_t getNextAvailabilityRemainingMs();                                  // next availability checl time

#endif                                                                          // FlapReporting_h
};