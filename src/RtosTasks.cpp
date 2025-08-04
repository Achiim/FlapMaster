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
    // Initialize timestamps with offset to desynchronize 10 min availability check
    TickType_t lastScanTimeShort    = xTaskGetTickCount();
    TickType_t lastScanTimeLong     = xTaskGetTickCount();
    TickType_t lastAvailabilityTime = xTaskGetTickCount() + (1000 * 60 * 2);    // +2 minute offset

    Register = new FlapRegistry();                                              // Create registry object

    // Initial job: discover and register unknown devices
    Register->registerUnregistered();
    Register->scan_i2c_bus();

    while (true) {
        // Task intervals
        const TickType_t scanDelayTicksShort = 1000 * 10;                       // every 10 seconds
        const TickType_t scanDelayTicksLong  = 1000 * 60 * 20;                  // every 20 minutes
        const TickType_t availableDelayTicks = 1000 * 60 * 10;                  // every 10 minutes

        TickType_t now = xTaskGetTickCount();

        // If not all expected twins are registered, perform fast scan
        if (Register->numberOfRegisterdDevices() < numberOfTwins) {
            if (now - lastScanTimeShort >= scanDelayTicksShort) {
                Register->scan_i2c_bus();                                       // Scan I2C bus
                Register->registerUnregistered();                               // Register newly discovered devices
                lastScanTimeShort = now;
            }
        } else {
            // Perform long interval scan every 20 minutes
            if (now - lastScanTimeLong >= scanDelayTicksLong) {
                Register->scan_i2c_bus();                                       // Scan I2C bus
                Register->registerUnregistered();                               // Register newly discovered devices
                lastScanTimeLong = now;
            }
        }

        // Desynchronized availability check (offset by 2 minutes)
        if (now - lastAvailabilityTime >= availableDelayTicks) {
            Register->check_slave_availability();                               // Verify device availability, deregister if missing
            lastAvailabilityTime = now;
        }

        vTaskDelay(5000 / portTICK_PERIOD_MS);                                  // Delay execution by 5 seconds
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
