// #################################################################################################################
//
//  ███████ ██       █████  ██████      ███████ ████████  █████  ████████ ██ ███████ ████████ ██  ██████ ███████
//  ██      ██      ██   ██ ██   ██     ██         ██    ██   ██    ██    ██ ██         ██    ██ ██      ██
//  █████   ██      ███████ ██████      ███████    ██    ███████    ██    ██ ███████    ██    ██ ██      ███████
//  ██      ██      ██   ██ ██               ██    ██    ██   ██    ██    ██      ██    ██    ██ ██           ██
//  ██      ███████ ██   ██ ██          ███████    ██    ██   ██    ██    ██ ███████    ██    ██  ██████ ███████
//
// ################################################################################################## by Achim ####
// Banner created:
// https://patorjk.com/software/taag/#p=display&c=c%2B%2B&f=ANSI%20Regular&t=FLAP%20Statistics
//
/*

    Real time Tasks for Flap Master

    Features:

    - makes I2C Statistics

*/
#include <freertos/FreeRTOS.h>                                                  // Real Time OS
#include <freertos/task.h>
#include <FlapGlobal.h>
#include <i2cFlap.h>
#include "i2cMaster.h"
#include "FlapStatistics.h"
#include "MasterPrint.h"

// ----------------------------
// Constructor
FlapStatistics::FlapStatistics() {
    _statsMutex = xSemaphoreCreateMutex();                                      // generate Semaphor to protect access to statistics
    if (_statsMutex == NULL) {
        #ifdef MASTERVERBOSE
            masterPrint("FATAL ERROR: Failed to create I2C mutex!");
        #endif
    }

    _busAccessCounter  = 0;                                                     // init statistic counter
    _busDataCounter    = 0;                                                     // init statistic counter
    _busReadCounter    = 0;                                                     // init statistic counter
    _busTimeoutCounter = 0;                                                     // init statistic counter
    for (int h = 0; h < HISTORY_SIZE; h++) {
        _accessHistory[h]  = 0;                                                 // init statistic counter
        _dataHistory[h]    = 0;                                                 // init statistic counter
        _readHistory[h]    = 0;                                                 // init statistic counter
        _timeoutHistory[h] = 0;                                                 // init statistic counter
    }

    _historyIndex = 0;                                                          // init statistic index
};

// -------------------------------------------------------------------------
// every cycle transfer statisic counter to histery and switch histery index
void FlapStatistics::makeHistory() {
    xSemaphoreTake(_statsMutex, portMAX_DELAY);

    #ifdef STATISTICVERBOSE
        TraceScope trace;                                                       // use semaphore to protect this block
        {
        masterPrint("I²C statistic cycle - access: ");
        Serial.print(_busAccessCounter);
        Serial.print(" send: ");
        Serial.print(_busDataCounter);
        Serial.print(" read: ");
        Serial.println(_busReadCounter);
        }
    #endif

    _accessHistory[_historyIndex]  = _busAccessCounter;                         // transfer counter to history
    _dataHistory[_historyIndex]    = _busDataCounter;                           // transfer counter to history
    _readHistory[_historyIndex]    = _busReadCounter;                           // transfer counter to history
    _timeoutHistory[_historyIndex] = _busTimeoutCounter;                        // transfer counter to history
    _busAccessCounter              = 0;                                         // reset counter for counting in next cycle
    _busDataCounter                = 0;                                         // reset counter for counting in next cycle
    _busReadCounter                = 0;                                         // reset counter for counting in next cycle
    _busTimeoutCounter             = 0;                                         // reset counter for counting in next cycle

    _historyIndex = (_historyIndex + 1) % HISTORY_SIZE;                         // round robin for history index
    xSemaphoreGive(_statsMutex);
}

// ----------------------------
// Helper to actualize statistics protected by semaphores
//
// parameter:
// access = couter for i2c accesses
// data = number of byte send by master via i2c
// read = number of byte read by master via i2c (got from slaves)
void FlapStatistics::increment(uint32_t access, uint32_t sentData, uint32_t readData, uint32_t timeOut) {
    xSemaphoreTake(_statsMutex, portMAX_DELAY);
    _busAccessCounter += access;                                                // count
    _busDataCounter += sentData;                                                // count
    _busReadCounter += readData;                                                // count
    _busTimeoutCounter += timeOut;                                              // count
    _accessHistory[_historyIndex]  = _busAccessCounter;                         // actualize history of current cycle
    _dataHistory[_historyIndex]    = _busDataCounter;                           // actualize history of current cycle
    _readHistory[_historyIndex]    = _busReadCounter;                           // actualize history of current cycle
    _timeoutHistory[_historyIndex] = _busTimeoutCounter;                        // actualize history of current cycle
    xSemaphoreGive(_statsMutex);

    #ifdef STATISTICVERBOSE
        TraceScope trace;                                                       // use semaphore to protect this block
        {
        masterPrint("I²C statistic increment - access: ");
        Serial.print(_busAccessCounter);
        Serial.print(" send: ");
        Serial.print(_busDataCounter);
        Serial.print(" read: ");
        Serial.println(_busReadCounter);
        }
    #endif
}
