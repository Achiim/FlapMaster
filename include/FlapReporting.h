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
        tracePrint("[FLAP  REPORTING] ", args...);
    }
    template <typename... Args>
    void reportPrintln(const Args&... args) {
        tracePrintln("[FLAP  REPORTING] ", args...);
    }

    // Constructor
    FlapReporting();

    // ----------------------------
    uint32_t maxValueFromHistory(uint32_t* history);
    //    void     printI2CHistoryDescending(uint32_t* history, uint32_t maxValue, uint8_t currentIndex);
    void printBar(uint32_t value, float scale, const char* symbol, uint8_t maxLength);
    void reportI2CStatistic();
    void printFramedHistory();
    void printUptime();                                                         // Helper to report up time
    void reportSlaveRegistry();                                                 // Helper to report registry content
    void reportTasks();                                                         // show Tasklist with remaining stack size
    void reportHeader();                                                        // show Ticks and uptime
    void reportHeaderAlt();                                                     // show Ticks and uptime
    void reportMemory();
    void reportAllTwins(int wrapWidth = 20);                                    // Ausgabe der Reports aller globalen Twins. wrapWidth bestimmt, wie viele Flaps pro Block/Zeile.

   private:
    static const char    BLOCK_LIGHT[];                                         // bar pattern for Access
    static const char    BLOCK_MEDIUM[];                                        // bar pattern for Send
    static const char    BLOCK_DENSE[];                                         // bar pattern for Read
    static const char*   SPARKLINE_LEVELS[];                                    // declaration
    static constexpr int SPARKLINE_LEVEL_COUNT = 8;                             // number of levels

    void        printStepsByFlapReport(SlaveTwin& twin, int wrapWidth);         // Einzelreport für einen Twin (wird intern benutzt).
    const char* selectSparklineLevel(int value, int minVal, int maxVal);        // helper to select sparkling

    // box drawing helpers
    String repeatChar(const String& symbol, int count);                         // Helper for unicode
    String padStart(String val, int length, char fill = ' ');
    void   drawTwinChunk(const SlaveTwin& twin, int offset, int wrapWidth);
};
#endif                                                                          // FlapReporting_h
