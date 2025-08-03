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
// freeRTOS Task Remote Parser
void remoteParser(void* pvParameters) {
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
    TickType_t lastScanTimeShort    = xTaskGetTickCount();
    TickType_t lastScanTimeLong     = xTaskGetTickCount();
    TickType_t lastAvailabilityTime = xTaskGetTickCount();

    Register = new FlapRegistry();                                              // create object for task

    // first job
    Register->registerUnregistered();                                           // collect unregistered slaves
    Register->scan_i2c_bus();                                                   // Ask I2C Bus who is there, and register unknown slaves

    while (true) {
        const TickType_t scanDelayTicksShort = 10000;                           // every 10 Seconds
        const TickType_t scanDelayTicksLong  = 300000;                          // every 300 Seconds
        const TickType_t availableDelayTicks = 20000;                           // every 20 Seconds

        TickType_t now = xTaskGetTickCount();

        if (Register->numberOfRegisterdDevices() < numberOfTwins) {
            if (now - lastScanTimeShort >= scanDelayTicksShort) {
                Register->scan_i2c_bus();                                       // Ask I2C Bus who is there, and register unknown slaves
                Register->registerUnregistered();                               // collect unregistered slaves
                lastScanTimeShort = now;
            }
        } else {
            if (now - lastScanTimeLong >= scanDelayTicksLong) {
                Register->scan_i2c_bus();                                       // Ask I2C Bus who is there, and register unknown slaves
                Register->registerUnregistered();                               // collect unregistered slaves
                lastScanTimeLong = now;
            }
        }

        if (now - lastAvailabilityTime >= availableDelayTicks) {
            Register->check_slave_availability();                               // check if all registerd slaves are still available, if not deregister
            lastAvailabilityTime = now;
        }
        vTaskDelay(5000 / portTICK_PERIOD_MS);                                  // Delay for 5 seconds
    }
}

// ----------------------------
// freeRTOS Task Twin[n]
void slaveTwinTask(void* pvParameters) {
    int        mod = *(int*)pvParameters;                                       // Twin Number
    ClickEvent receivedEvent;
    g_twinQueue[mod] = xQueueCreate(1, sizeof(ClickEvent));                     // Create twin Queue

    while (true) {
        if (g_twinQueue[mod] != nullptr) {                                      // if queue exists
            if (xQueueReceive(g_twinQueue[mod], &receivedEvent, portMAX_DELAY)) {
                if (receivedEvent.key != Key21::NONE)
                    Master->TwinControl(receivedEvent, mod);                    // send corresponding Flap-Command to device
            }
        }
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
void reportingTask(void* pvParameters) {
    uint32_t receivedValue;
    Key21    receivedKey;

    FlapReporting* Reports;                                                     // create object for task
    g_reportingQueue = xQueueCreate(1, sizeof(uint32_t));                       // Create task Queue

    while (true) {
        if (xQueueReceive(g_reportingQueue, &receivedValue, portMAX_DELAY)) {
            receivedKey = Control.ir2Key21(receivedValue);
            if (receivedKey != Key21::NONE) {
                if (receivedKey == Key21::KEY_100_PLUS) {
                    Reports->reportPrintln("======== Flap Master Health Overview ========");
                    Reports->reportHeader();                                    // Report Hrader
                    Reports->reportMemory();                                    // ESP32 (RAM status)
                    Reports->reportTasks();                                     // show Task List
                    Reports->reportAllTwins();                                  // show Slave steps prt Flap
                    Reports->reportSlaveRegistry();                             // show registry
                    Reports->reportI2CStatistic();                              // shoe I2C usage
                    Reports->reportPrintln("======== Flap Master Health Overview End ====");
                }
                vTaskDelay(100 / portTICK_PERIOD_MS);
            }
        }
    }
}
