#include <Arduino.h>

#ifndef FlapStatistics_h
    #define FlapStatistics_h

    // global defined statistic counters and history
    #define HISTORY_SIZE 10                                                     // i2c statistics history length in minutes

class FlapStatistics {
   public:
    // Constructor
    FlapStatistics();

    // ----------------------------
    void makeHistory();                                                         // transfer actual conter to history
    void increment(uint32_t access = 0, uint32_t sentData = 0, uint32_t readData = 0, uint32_t timeOut = 0); // count

    uint32_t _accessHistory[HISTORY_SIZE];                                      // history for number of i2c accesses
    uint32_t _dataHistory[HISTORY_SIZE];                                        // history for data bytes send by master via i2c
    uint32_t _readHistory[HISTORY_SIZE];                                        // history for data bytes read by master via i2c
    uint32_t _timeoutHistory[HISTORY_SIZE];                                     // history for timeouts via i2c
    uint8_t  _historyIndex;                                                     // index for histroy
    uint32_t _busAccessCounter;                                                 // counter for I2C accesses
    uint32_t _busDataCounter;                                                   // counter for I2C written data
    uint32_t _busReadCounter;                                                   // counter for I2C read data
    uint32_t _busTimeoutCounter;                                                // counter for I2C timeouts

   private:
    SemaphoreHandle_t _statsMutex = nullptr;
};
#endif                                                                          // FlapStatistics_h
