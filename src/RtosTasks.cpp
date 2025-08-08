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
#include "RemoteParser.h"
#include "RemoteControl.h"
#include "RtosTasks.h"

// ----------------------------
// freeRTOS Task Remote Control
void remoteControl(void* pvParameters) {
    while (true) {
        Control.getRemote();
        vTaskDelay(pdMS_TO_TICKS(10));                                          // Delay for 10ms
    }
}

// ----------------------------
// freeRTOS Task Parser
void parserTask(void* pvParameters) {
    Parser        = new RemoteParser();                                         // create object for task
    g_parserQueue = xQueueCreate(1, sizeof(uint64_t));                          // Create parser Queue

    while (true) {
        if (g_parserQueue != nullptr) {                                         // if queue exists
            Parser->handleQueueMessage();                                       // read remote code from parser entry queue and filter
            vTaskDelay(10 / portTICK_PERIOD_MS);                                // Delay for 10 milliseconds, waiting for double click
            Parser->analyseClickEvent();
            if (Parser->_receivedEvent.type != CLICK_NONE && Parser->_receivedEvent.key != Key21::NONE) {
                Parser->dispatchToTwins();                                      // execute key stroke
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
    Register->registerUnregistered();
    Register->scan_i2c_bus();
    shortScanTimer =
        xTimerCreate("ScanShort", pdMS_TO_TICKS(SHORT_SCAN_COUNTDOWN), pdTRUE, nullptr, shortScanCallback); // 1) Short-Scan-Timer (Auto-Reload)
    longScanTimer =
        xTimerCreate("ScanLong", pdMS_TO_TICKS(LONG_SCAN_COUNTDOWN), pdTRUE, nullptr, longScanCallback); // 2) Long-Scan-Timer (Auto-Reload)
    availCheckTimer = xTimerCreate("AvailChk", pdMS_TO_TICKS(AVAILABILITY_CHECK_COUNTDOWN), pdTRUE, nullptr,
                                   availCheckCallback);                         // 3) Availability-Check-Timer (Auto-Reload)

    xTimerStart(shortScanTimer, 0);                                             // starte Short-Scan now
    vTaskDelay(pdMS_TO_TICKS(2 * 60 * 1000));                                   // start Availability-Check with 2 Min-Offset
    xTimerStart(availCheckTimer, 0);
    vTaskSuspend(nullptr);                                                      // suspend task,  Callbacks are doing the job
}

// ----------------------------
// freeRTOS Task Twin[n]
void slaveTwinTask(void* pvParameters) {
    int mod = *(int*)pvParameters;                                              // Twin Number
    Twin[mod]->createQueue();                                                   // Create twin Queue

    while (true) {
        Twin[mod]->readQueue();                                                 // read event/command from twin Queue
    }
}

// ----------------------------
// freeRTOS Task Statistic
void statisticTask(void* param) {
    TickType_t lastWakeTime = xTaskGetTickCount();
    DataEvaluation          = new FlapStatistics();                             // create Object for statistic task
    while (true) {
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(60000));                   // every 1 Minute

        {
            TraceScope trace;                                                   // use semaphore to protect this block
            #ifdef STATISTICVERBOSE
                masterPrintln("I²C statistic cycle - access: ", DataEvaluation->busAccessCounter, " data write: ", DataEvaluation->busDataCounter,
                " data read: ", DataEvaluation->busReadCounter);
            #endif
        }
        DataEvaluation->makeHistory();                                          // transfer counter to next history cycle
    }
}

// ----------------------------
// freeRTOS Task Reporting
void reportTask(void* pvParameters) {
    uint32_t receivedValue;
    Key21    receivedKey;

    FlapReporting* Reports;                                                     // create object for task
    g_reportQueue = xQueueCreate(1, sizeof(uint32_t));                          // Create task Queue

    while (true) {
        if (xQueueReceive(g_reportQueue, &receivedValue, portMAX_DELAY)) {
            receivedKey = Control.ircodeToKey21(receivedValue);
            if (receivedKey != Key21::NONE) {
                if (receivedKey == Key21::KEY_100_PLUS) {
                    Reports->reportPrintln("======== Flap Master Health Overview ========");
                    Reports->reportTaskStatus();                                // Report Header
                    Reports->reportMemory();                                    // ESP32 (RAM status)
                    Reports->reportRtosTasks();                                 // show Task List
                    Reports->reportAllTwinStepsByFlap();                        // show Slave steps prt Flap
                    Reports->reportSlaveRegistry();                             // show registry
                    Reports->reportI2CStatistic();                              // shoe I2C usage
                    Reports->reportPrintln("======== Flap Master Health Overview End ====");
                }
                vTaskDelay(100 / portTICK_PERIOD_MS);
            }
        }
    }
}
