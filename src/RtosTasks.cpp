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
                    Parser->_receivedEvent.key  = Key21::NONE;                  // reset received key
                    Parser->_receivedEvent.type = CLICK_NONE;                   // reset received type
                }
            }
        }
    }

    // ----------------------------
    // freeRTOS Task Registry
    void twinRegister(void* pvParameters) {
        Register = new FlapRegistry();
        Register->repairOutOfPoolDevices();                                     // repair devices that are out of the address pool
        Register->registerUnregistered();                                       // register all twins which are not registered yet
        Register->scan_i2c_bus();                                               // scan i2c bus for new twins

        #ifdef DISABLEREGISTRY
            {
            TraceScope trace;                                                   // use semaphore to protect this block
            masterPrintln("Registry is disabled, no scan and no registry");
            vTaskSuspend(nullptr);                                              // suspend task,  do not use registry
            return;
            }
        #endif

        shortScanTimer =
            xTimerCreate("ScanShort", pdMS_TO_TICKS(SHORT_SCAN_COUNTDOWN), pdTRUE, nullptr, shortScanCallback); // 1) Short-Scan-Timer (Auto-Reload)
        longScanTimer =
            xTimerCreate("ScanLong", pdMS_TO_TICKS(LONG_SCAN_COUNTDOWN), pdTRUE, nullptr, longScanCallback); // 2) Long-Scan-Timer (Auto-Reload)
        availCheckTimer = xTimerCreate("AvailChk", pdMS_TO_TICKS(AVAILABILITY_CHECK_COUNTDOWN), pdTRUE, nullptr,
                                       availCheckCallback);                     // 3) Availability-Check-Timer (Auto-Reload)

        vTaskDelay(pdMS_TO_TICKS(1 * 60 * 1000));                               // start Availability-Check with 1 Min-Offset
        xTimerStart(availCheckTimer, 0);

        xTimerStart(shortScanTimer, 0);                                         // starte Short-Scan now
        vTaskSuspend(nullptr);                                                  // suspend task,  Callbacks are doing the job
    }

    // ----------------------------
    // freeRTOS Task Twin[index]
    void slaveTwinTask(void* pvParameters) {
        int twinIndex = *(int*)pvParameters;                                    // Twin Number
        Twin[twinIndex]->createQueue();                                         // Create twin Queue

        while (true) {
            Twin[twinIndex]->readQueue();                                       // read event/command from twin Queue
        }
    }

    // ----------------------------
    // freeRTOS Task Statistic
    void statisticTask(void* param) {
        TickType_t lastWakeTime = xTaskGetTickCount();                          // get current tick count
        DataEvaluation          = new FlapStatistics();                         // create Object for statistic task
        while (true) {
            vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(60000));               // every 1 Minute
            {
                TraceScope trace;                                               // use semaphore to protect this block
                if (DataEvaluation != nullptr) {
                    DataEvaluation->increment();                                // increment statistic counter
                    #ifdef STATISTICVERBOSE
                        masterPrintln("I²C statistic cycle - access: ", DataEvaluation->_busAccessCounter,
                        " data write: ", DataEvaluation->_busDataCounter, " data read: ", DataEvaluation->_busReadCounter);
                    #endif
                }
            }
            DataEvaluation->makeHistory();                                      // transfer counter to next history cycle
        }
    }

    // ----------------------------
    // freeRTOS Task Reporting
    void reportTask(void* pvParameters) {
        Key21          receivedKey;
        ReportCommands receivedCmd;

        FlapReporting* Reports;                                                 // create object for task
        g_reportQueue = xQueueCreate(1, sizeof(ReportCommand));                 // Create task Queue

        while (true) {
            if (xQueueReceive(g_reportQueue, &receivedCmd, portMAX_DELAY)) {    // wait for Queue message
                Reports->reportPrintln("======== Flap Master Health Overview ========"); // Report Header
                if (receivedCmd == REPORT_TASKS_STATUS)
                    Reports->reportTaskStatus();                                // uptime and scheduled scans
                if (receivedCmd == REPORT_MEMORY)
                    Reports->reportMemory();                                    // ESP32 (RAM status)
                if (receivedCmd == REPORT_RTOS_TASKS)
                    Reports->reportRtosTasks();                                 // show Task List
                if (receivedCmd == REPORT_STEPS_BY_FLAP)
                    Reports->reportAllTwinStepsByFlap();                        // show Slave steps prt Flap
                if (receivedCmd == REPORT_REGISTRY)
                    Reports->reportSlaveRegistry();                             // show registry
                if (receivedCmd == REPORT_I2C_STATISTIC)
                    Reports->reportI2CStatistic();                              // shoe I2C usage
            }
            Reports->reportPrintln("======== Flap Master Health Overview End ====");
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
    }
