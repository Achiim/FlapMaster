#include <Arduino.h>

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
    void printI2CStatistic();
    void printFramedHistory();
    void printUptime();                                                         // Helper to report up time
    void traceSlaveRegistry();                                                  // Helper to report registry content
    void reportTasks();                                                         // show Tasklist with remaining stack size
    void reportHeader();                                                        // show Ticks and uptime
    void reportHeaderAlt();                                                     // show Ticks and uptime
    void reportMemory();

   private:
    static const char BLOCK_LIGHT[];                                            // bar pattern for Access
    static const char BLOCK_MEDIUM[];                                           // bar pattern for Send
    static const char BLOCK_DENSE[];                                            // bar pattern for Read
};
#endif                                                                          // FlapReporting_h
