// ###################################################################################################
//
//  ███    ███  █████  ███████ ████████ ███████ ██████      ███████ ███████ ████████ ██    ██ ██████
//  ████  ████ ██   ██ ██         ██    ██      ██   ██     ██      ██         ██    ██    ██ ██   ██
//  ██ ████ ██ ███████ ███████    ██    █████   ██████      ███████ █████      ██    ██    ██ ██████
//  ██  ██  ██ ██   ██      ██    ██    ██      ██   ██          ██ ██         ██    ██    ██ ██
//  ██      ██ ██   ██ ███████    ██    ███████ ██   ██     ███████ ███████    ██     ██████  ██
//
//
// ################################################################################################## by Achim ####
// Banner created:
// https://patorjk.com/software/taag/#p=display&c=c%2B%2B&f=ANSI%20Regular&t=MASTER%20SETUP
/*

*/

#include <Arduino.h>
#include <FlapGlobal.h>
#include "MasterPrint.h"
#include "i2cMaster.h"
#include "SlaveTwin.h"
#include "FlapTasks.h"
#include "RemoteControl.h"
#include "RtosTasks.h"
#include "FlapStatistics.h"
#include "MasterSetup.h"

/**
 * @brief Print out Header of Master
 *
 */
void masterIntroduction() {
    Serial.begin(115200);
    Serial.print("\n\n\n");
    Serial.print("              =========================\n");
    Serial.print("              I²C-MASTER - FLAP DISPLAY\n");
    Serial.print("              ================(c)=2025=\n");
    Serial.print("\n\n\n");
}
// ---------------------------

/**
 * @brief setup I2C connection as Master
 *
 */
void masterI2Csetup() {
    #ifdef MASTERVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        masterPrintln("introduce as I2C Master to I2C bus");
        }
    #endif
    i2csetup();                                                                 // Register as Master to I2C bus
}

// ---------------------------

/**
 * @brief generate I2C Address Pool
 *
 */
void masterAddressPool() {
    #ifdef MASTERVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        masterPrintln("generate I2C Address Pool");
        }
    #endif
    if (!initAddressPool()) {                                                   // Register as Master to I2C bus
        {
            #ifdef ERRORVERBOSE
                {
                TraceScope trace;                                               // use semaphore to protect this block
                masterPrintln("ERROR: I2C Address Pool too small for numberOfTwins, not supported");
                }
            #endif
            return;                                                             // exit if address pool is too small
        }
    } else {
        #ifdef MASTERVERBOSE
            {
            TraceScope trace;                                                   // use semaphore to protect this block
            masterPrint("I2C Address Pool generated 0x");
            Serial.print(g_slaveAddressPool[0], HEX);
            Serial.print(" - 0x");
            Serial.println(g_slaveAddressPool[numberOfTwins - 1], HEX);
            }
        #endif
    }
}

// ---------------------------

/**
 * @brief create IR receiver object for Key21 remote control
 *
 */
void masterRemoteControl() {
    #ifdef MASTERVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        masterPrintln("create IR receiver object for Key21 remote control");
        }
    #endif
    // Create IR Receiver
    irController.enableIRIn();                                                  // start remote control receiver
}

// ---------------------------

/**
 * @brief create Master control object, to control TWINs
 *
 */
void masterSlaveControlObject() {
    #ifdef MASTERVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        masterPrintln("create Master control object, to control TWINs");
        }
    #endif
    Master = new FlapTask();                                                    // create Master frame object

    #ifdef MASTERVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        masterPrintln("create Slave TWIN objects and command queues");
        }
    #endif
    // create Master control object, to control TWINs
    for (int m = 0; m < numberOfTwins; m++) {
        Twin[m] = new SlaveTwin(g_slaveAddressPool[m]);                         // create twins
    }
}

// ---------------------------

/**
 * @brief start all RTOS tasks
 *
 */
void masterStartRtosTasks() {
    createStatisticTask();                                                      // create statistics task
    createReportTask();                                                         // Create report tasks
    createTwinTasks();                                                          // Create twin tasks
    createRemoteControlTask();                                                  // Create remote control
    createParserTask();                                                         // create remote creator task
    createRegisterTwinsTask();                                                  // Create Register Twins task
}

// ---------------------------

/**
 * @brief finish message of master startup
 *
 */
void masterOutrodution() {
    #ifdef MASTERVERBOSE
        TraceScope trace;                                                       // use semaphore to protect this block
        {
        masterPrintln("I²C Master is up and running...");
        }
    #endif
}

// ---------------------------

/**
 * @brief Create a Twin Tasks object and start freeRTOS task: SlaveTwin
 *
 */
void createTwinTasks() {
    #ifdef MASTERVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        masterPrintln("start freeRTOS task: SlaveTwin 0..%d", numberOfTwins - 1);
        }
    #endif

    int twinNumber[numberOfTwins];                                              // storage for Twin Number
    for (int i = 0; i < numberOfTwins; ++i) {
        twinNumber[i] = i;                                                      // Nummer 1–18
        char taskName[16];
        snprintf(taskName, sizeof(taskName), "SlaveTwin-%02d", i + 1);
        #ifdef MEMORYVERBOSE
            {
            TraceScope trace;                                                   // use semaphore to protect this block
            masterPrint("starting freeRTOS task: ");
            Serial.println(taskName);
            uint32_t heepBefore = ESP.getFreeHeap();
            masterPrint("free heap before start of task: ");
            Serial.println(heepBefore);
            }
        #endif
        xTaskCreate(slaveTwinTask, taskName, STACK_TWIN, &twinNumber[i], PRIO_TWIN, &g_twinHandle[i]);
        vTaskDelay(200 / portTICK_PERIOD_MS);                                   // start next twin task with some delay
        #ifdef MEMORYVERBOSE
            {
            TraceScope trace;                                                   // use semaphore to protect this block
            uint32_t   heepAfter = ESP.getFreeHeap();
            masterPrint("free heap after start of task: ");
            Serial.println(heepAfter);
            masterPrint("used heap by task (in bytes): ");
            Serial.println(heepBefore - heepAfter);
            }
        #endif
    }
}

// ---------------------------

/**
 * @brief Create a Remote Control Task object and start freeRTOS task: remote control receiver for Key21 control
 *
 */
void createRemoteControlTask() {
    #ifdef MASTERVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        masterPrintln("start freeRTOS task: remote control receiver for Key21 control");
        }
    #endif
    xTaskCreate(remoteControl, "RemoteControl", STACK_REMOTE, NULL, PRIO_REMOTE, &g_remoteControlHandle);
}

// ---------------------------

/**
 * @brief Create a Statistic Task object and start freeRTOS task: StatisticTask
 *
 */
void createStatisticTask() {
    #ifdef MASTERVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        masterPrintln("start freeRTOS task: StatisticTask");
        }
    #endif
    xTaskCreate(statisticTask, "StatisticTask", STACK_STATISTICS, NULL, PRIO_STATISTICS, &g_statisticHandle);
}

// ---------------------------

/**
 * @brief Create a Report Task object and tart freeRTOS task: ReportTask and corresponding queue
 *
 */
void createReportTask() {
    #ifdef MASTERVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        masterPrintln("start freeRTOS task: ReportTask and corresponding queue");
        }
    #endif
    xTaskCreate(reportTask, "ReportTask", STACK_REPORT, NULL, PRIO_REPORT, &g_reportHandle);
}

// ---------------------------

/**
 * @brief Create a Register Twins Task object and start freeRTOS task: TwinRegister
 *
 */
void createRegisterTwinsTask() {
    #ifdef MASTERVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        masterPrintln("start freeRTOS task: TwinRegister");
        }
    #endif
    xTaskCreate(twinRegister, "TwinRegister", STACK_REGISTRY, NULL, PRIO_REGISTRY, &g_registryHandle);
    vTaskDelay(500 / portTICK_PERIOD_MS);                                       // give task some time for registering new devices
}

// ---------------------------

/**
 * @brief Create a Parser Task object and start freeRTOS task: Parser
 *
 */
void createParserTask() {
    #ifdef MASTERVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        masterPrintln("start freeRTOS task: Parser");
        }
    #endif
    xTaskCreate(parserTask, "Parser", STACK_PARSER, NULL, PRIO_PARSER, &g_parserHandle);
}