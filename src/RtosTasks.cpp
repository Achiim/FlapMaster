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
#include "Liga.h"
#include "RemoteControl.h"
#include "RtosTasks.h"

// ----------------------------

/**
 * @brief freeRTOS task Liga
 *
 * @param pvParameters
 */
void ligaTask(void* pvParameters) {
    int           season   = 0;
    int           matchday = 0;
    ReportCommand repCmd;

    Liga = new LigaTable();
    Liga->connect();                                                            // connect to openLigaDB

    while (true) {
        if (Liga->pollLastChange(&season, &matchday)) {
            LigaSnapshot snap;                                                  // snapshot of table
            snap.clear();                                                       // clear snapshot
            Liga->fetchTable(snap);                                             // fill snapshot
            Liga->commit(snap);                                                 // Commit snapshot LigaTable (double-buffer flip)
            repCmd.repCommand   = REPORT_LIGA_TABLE;
            repCmd.responsQueue = nullptr;
            if (g_reportQueue)                                                  // Trigger trace snapshot
                xQueueOverwrite(g_reportQueue, &repCmd);

            Liga->openLigaDBHealth();                                           // check if openLigaDB is online
            Liga->getNextMatch();                                               // get next match
            Liga->getGoal();                                                    // get goal event
        }
        vTaskDelay(pdMS_TO_TICKS(Liga->decidePollMs()));                        // dynamic Delay depending on current game or not
    }
}

// ----------------------------

/**
 * @brief freeRTOS Task Remote Control
 *
 * @param pvParameters
 */
void remoteControl(void* pvParameters) {
    Control = new RemoteControl();                                              // create object for task
    while (true) {
        Control->getRemote();
        vTaskDelay(pdMS_TO_TICKS(10));                                          // Delay for 10ms
    }
}

// ----------------------------

/**
 * @brief freeRTOS Task Parser
 *
 * @param pvParameters
 */
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
                    Parser->dispatchToOther();                                  // execute key by other task
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

/**
 * @brief freeRTOS Task Registry
 *
 * @param pvParameters
 */
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
        if (Register->size() >= Register->capacity()) {
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

/**
 * @brief freeRTOS Task Twin[index]
 *
 * @param pvParameters
 */
void slaveTwinTask(void* pvParameters) {
    int twinIndex = *(int*)pvParameters;                                        // Twin Number
    Twin[twinIndex]->createQueue();                                             // Create twin Queue

    while (true) {
        Twin[twinIndex]->readQueue();                                           // read event/command from twin Queue
    }
}

// ----------------------------

/**
 * @brief freeRTOS Task Statistic
 *
 * @param param
 */
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

/**
 * @brief freeRTOS Task Reporting
 *
 * @param pvParameters
 */
void reportTask(void* pvParameters) {
    Key21          receivedKey;
    ReportCommands receivedCmd;

    FlapReporting* Reports = new FlapReporting();                               // create instance for object
    g_reportQueue          = xQueueCreate(1, sizeof(ReportCommands));           // Create task Queue

    while (true) {
        if (xQueueReceive(g_reportQueue, &receivedCmd, portMAX_DELAY)) {        // wait for Queue message
            Reports->reportPrintln("======== Flap Master Report ========");     // Report Header
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
                Reports->reportI2CStatistic();                                  // show I2C usage
            if (receivedCmd == REPORT_LIGA_TABLE)
                Reports->reportLigaTable();                                     // show liga tabelle
            Reports->reportPrintln("======== Flap Master Report End ====");
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}
