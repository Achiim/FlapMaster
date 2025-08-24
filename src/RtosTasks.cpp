#include <freertos/FreeRTOS.h>                                                  // Real Time OS
#include <freertos/task.h>
#include <FlapGlobal.h>
#include <cstdio>
#include "SlaveTwin.h"
#include "FlapTasks.h"
#include "i2cMaster.h"
#include "FlapRegistry.h"
#include "MasterPrint.h"
#include "FlapReporting.h"
#include "FlapStatistics.h"
#include "Parser.h"
#include "RemoteControl.h"
#include "RtosTasks.h"

// ----------------------------
// freeRTOS Task Remote Control
void remoteControl(void* pvParameters) {
    Control = new RemoteControl();                                              // create object for task
    while (true) {
        Control->getRemote();
        vTaskDelay(pdMS_TO_TICKS(10));                                          // Delay for 10ms
    }
}

// ----------------------------
// freeRTOS Task Parser
void parserTask(void* pvParameters) {
    Parser        = new ParserClass();                                          // create object for task
    g_parserQueue = xQueueCreate(1, sizeof(uint64_t));                          // Create parser Queue

    while (true) {
        if (g_parserQueue != nullptr) {                                         // if queue exists
            Parser->handleQueueMessage();                                       // read remote code from parser entry queue and filter
            vTaskDelay(10 / portTICK_PERIOD_MS);                                // Delay for 10 milliseconds, waiting for double click
            Parser->analyseClickEvent();
            if (Parser->_receivedEvent.type != CLICK_NONE && Parser->_receivedEvent.key != Key21::NONE) {
                if (Parser->_receivedEvent.type == CLICK_DOUBLE) {
                    Parser->dispatchToReporting();                              // execute key by report task
                } else {
                    if (Parser->_receivedEvent.type == CLICK_SINGLE) {
                        Parser->dispatchToTwins();                              // execute key by twins
                    }
                }
                Parser->_receivedEvent.key  = Key21::NONE;                      // reset received key
                Parser->_receivedEvent.type = CLICK_NONE;                       // reset received type
            }
        }
    }
}
// ----------------------------
// freeRTOS Task Registry
void twinRegister(void* pvParameters) {
    Register = new FlapRegistry();

    Register->registerDevice();                                                 // initial full scan for known devices
    vTaskDelay(pdMS_TO_TICKS(200));                                             // short grace period
    Register->registerUnregistered();                                           // register devices that are known but not yet registered
    vTaskDelay(pdMS_TO_TICKS(200));                                             // short grace period
    Register->repairOutOfPoolDevices();                                         // reassign devices that are outside the address pool

    #ifdef DISABLEREGISTRY
        {
        TraceScope trace;
        masterPrintln("Registry is disabled, no scan and no registry");
        vTaskSuspend(nullptr);                                                  // suspend task permanently if registry is disabled
        return;
        }
    #endif

    // --- create timers for cyclic tasks ---
    shortScanTimer  = xTimerCreate("ScanShort", pdMS_TO_TICKS(SHORT_SCAN_COUNTDOWN), pdTRUE, nullptr, shortScanCallback);
    longScanTimer   = xTimerCreate("ScanLong", pdMS_TO_TICKS(LONG_SCAN_COUNTDOWN), pdTRUE, nullptr, longScanCallback);
    availCheckTimer = xTimerCreate("AvailChk", pdMS_TO_TICKS(AVAILABILITY_CHECK_COUNTDOWN), pdTRUE, nullptr, availCheckCallback);

    // --- FAST MODE immediately after boot ---
    const TickType_t bootWindowMs = pdMS_TO_TICKS(30000);                       // maximum 30s fast mode (boot window)
    const TickType_t fastShort    = pdMS_TO_TICKS(2000);                        // fast short-scan every 2s
    const TickType_t fastAvail    = pdMS_TO_TICKS(1000);                        // fast availability check every 1s

    // configure timers for fast mode
    xTimerChangePeriod(shortScanTimer, fastShort, 0);
    xTimerChangePeriod(availCheckTimer, fastAvail, 0);

    // start timers now
    xTimerStart(shortScanTimer, 0);
    xTimerStart(longScanTimer, 0);
    xTimerStart(availCheckTimer, 0);

    TickType_t startTick = xTaskGetTickCount();                                 // remember start time of fast mode

    // --- loop until either boot window expires or all expected devices are registered ---
    while ((xTaskGetTickCount() - startTick) < bootWindowMs) {
        if (Register->numberOfRegisterdDevices() >= numberOfTwins) {
            // all expected devices found -> break out of fast mode earlier
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(500));                                         // wait a bit before checking again
    }

    // --- switch back to normal scan intervals ---
    xTimerChangePeriod(shortScanTimer, pdMS_TO_TICKS(SHORT_SCAN_COUNTDOWN), 0);
    xTimerChangePeriod(availCheckTimer, pdMS_TO_TICKS(AVAILABILITY_CHECK_COUNTDOWN), 0);

    // --- hand over to timers, suspend this task ---
    vTaskSuspend(nullptr);
}

// ----------------------------
// freeRTOS Task Twin[index]
void slaveTwinTask(void* pvParameters) {
    int twinIndex = *(int*)pvParameters;                                        // Twin Number
    Twin[twinIndex]->createQueue();                                             // Create twin Queue

    while (true) {
        Twin[twinIndex]->readQueue();                                           // read event/command from twin Queue
    }
}

// ----------------------------
// freeRTOS Task Statistic
void statisticTask(void* param) {
    TickType_t lastWakeTime = xTaskGetTickCount();                              // get current tick count
    DataEvaluation          = new FlapStatistics();                             // create Object for statistic task
    while (true) {
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(60000));                   // every 1 Minute
        {
            TraceScope trace;                                                   // use semaphore to protect this block
            if (DataEvaluation != nullptr) {
                DataEvaluation->increment();                                    // increment statistic counter
                #ifdef STATISTICVERBOSE
                    masterPrintln("I²C statistic cycle - access: ", DataEvaluation->_busAccessCounter, " data write: ", DataEvaluation->_busDataCounter,
                    " data read: ", DataEvaluation->_busReadCounter);
                #endif
            }
        }
        DataEvaluation->makeHistory();                                          // transfer counter to next history cycle
    }
}

// ----------------------------
// freeRTOS Task Reporting
void reportTask(void* pvParameters) {
    Key21          receivedKey;
    ReportCommands receivedCmd;

    FlapReporting* Reports;                                                     // create object for task
    g_reportQueue = xQueueCreate(1, sizeof(ReportCommand));                     // Create task Queue

    while (true) {
        if (xQueueReceive(g_reportQueue, &receivedCmd, portMAX_DELAY)) {        // wait for Queue message
            Reports->reportPrintln("======== Flap Master Health Overview ========"); // Report Header
            if (receivedCmd == REPORT_TASKS_STATUS)
                Reports->reportTaskStatus();                                    // uptime and scheduled scans
            if (receivedCmd == REPORT_MEMORY)
                Reports->reportMemory();                                        // ESP32 (RAM status)
            if (receivedCmd == REPORT_RTOS_TASKS)
                Reports->reportRtosTasks();                                     // show Task List
            if (receivedCmd == REPORT_STEPS_BY_FLAP)
                Reports->reportAllTwinStepsByFlap();                            // show Slave steps prt Flap
            if (receivedCmd == REPORT_REGISTRY)
                Reports->reportSlaveRegistry();                                 // show registry
            if (receivedCmd == REPORT_I2C_STATISTIC)
                Reports->reportI2CStatistic();                                  // shoe I2C usage
        }
        Reports->reportPrintln("======== Flap Master Health Overview End ====");
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}
