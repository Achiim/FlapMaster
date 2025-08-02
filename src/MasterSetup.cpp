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
#include "RtosTasks.h"
#include "FlapStatistics.h"
#include "MasterSetup.h"

void masterIntroduction() {
    Serial.begin(115200);
    Serial.print("\n\n\n");
    Serial.print("              =========================\n");
    Serial.print("              I²C-MASTER - FLAP DISPLAY\n");
    Serial.print("              ================(c)=2025=\n");
    Serial.print("\n\n\n");
}
// ---------------------------
void masterI2Csetup() {
    #ifdef MASTERVERBOSE
        masterPrintln("introduce as I2C Master to I2C bus");
    #endif
    i2csetup();                                                                 // Register as Master to I2C bus
}

// ---------------------------
void masterAddressPool() {
    #ifdef MASTERVERBOSE
        masterPrintln("generate I2C Address Pool");
    #endif
    if (!initAddressPool()) {                                                   // Register as Master to I2C bus
        masterPrintln("ERROR: I2C Address Pool too big, not supported");
    } else {
        #ifdef MASTERVERBOSE
            masterPrint("I2C Address Pool generated 0x");
            Serial.print(g_slaveAddressPool[0], HEX);
            Serial.print(" - 0x");
            Serial.println(g_slaveAddressPool[numberOfTwins - 1], HEX);
        #endif
    }
}

// ---------------------------
void masterRemoteControl() {
    #ifdef MASTERVERBOSE
        masterPrintln("create remote control object for Key21 control");
    #endif
    // Create IR Receiver
    irController.enableIRIn();                                                  // start remote control receiver
}

// ---------------------------
void masterSlaveControlObject() {
    #ifdef MASTERVERBOSE
        masterPrintln("create Master control object, to control TWINs");
    #endif
    Master = new FlapTask();                                                    // create Master control object, to control TWINs

    #ifdef MASTERVERBOSE
        masterPrintln("create Slave TWIN objects and command queues");
    #endif
    // create Master control object, to control TWINs
    for (int m = 0; m < numberOfTwins; m++) {
        Twin[m] = new SlaveTwin(g_slaveAddressPool[m]);                         // create twins
    }
}

// ---------------------------
void masterStartRtosTasks() {
    createStatisticTask();                                                      // create statistics task
    createReportingTask();                                                      // Create reporting tasks
    createTwinTasks();                                                          // Create twin tasks
    createRemoteControlTask();                                                  // Create remote control
    createRemoteParserTask();                                                   // create remote creator task
    createRegisterTwinsTask();                                                  // Create Register Twins task
}

// ---------------------------
void masterOutrodution() {
    #ifdef MASTERVERBOSE
        masterPrintln("I²C Master is up and running...");
    #endif
}

// ---------------------------
void createTwinTasks() {
    #ifdef MASTERVERBOSE
        masterPrintln("start freeRTOS task: SlaveTwin 0..%d", numberOfTwins - 1);
    #endif

    int twinNumber[numberOfTwins];                                              // storage for Twin Number
    for (int i = 0; i < numberOfTwins; ++i) {
        twinNumber[i] = i;                                                      // Nummer 1–18
        char taskName[16];
        snprintf(taskName, sizeof(taskName), "SlaveTwin-%02d", i + 1);
        #ifdef MEMORYVERBOSE
            masterPrint("starting freeRTOS task: ");
            Serial.println(taskName);
            uint32_t heepBefore = ESP.getFreeHeap();
            masterPrint("free heap before start of task: ");
            Serial.println(heepBefore);
        #endif
        xTaskCreate(slaveTwinTask, taskName, STACK_TWIN, &twinNumber[i], PRIO_TWIN, &g_twinHandle[i]);
        vTaskDelay(200 / portTICK_PERIOD_MS);                                   // start next twin task with some delay
        #ifdef MEMORYVERBOSE
            uint32_t heepAfter = ESP.getFreeHeap();
            masterPrint("free heap after start of task: ");
            Serial.println(heepAfter);
            masterPrint("used heap by task (in bytes): ");
            Serial.println(heepBefore - heepAfter);
        #endif
    }
}

// ---------------------------
void createRemoteControlTask() {
    #ifdef MASTERVERBOSE
        masterPrintln("start freeRTOS task: remote control receiver for Key21 control");
    #endif
    xTaskCreate(remoteControl, "RemoteControl", STACK_REMOTE, NULL, PRIO_REMOTE, &g_remoteControlHandle);
}

// ---------------------------
void createStatisticTask() {
    #ifdef MASTERVERBOSE
        masterPrintln("start freeRTOS task: StatisticTask");
    #endif
    xTaskCreate(statisticTask, "StatisticTask", STACK_STATISTICS, NULL, PRIO_STATISTICS, &g_statisticTaskHandle);
}

// ---------------------------
void createReportingTask() {
    #ifdef MASTERVERBOSE
        masterPrintln("start freeRTOS task: ReportingTask and corresponding queue");
    #endif
    xTaskCreate(reportingTask, "ReportingTask", STACK_REPORTING, NULL, PRIO_REPORTING, &g_reportingTaskHandle);
}

// ---------------------------
void createRegisterTwinsTask() {
    #ifdef MASTERVERBOSE
        masterPrintln("start freeRTOS task: TwinRegister");
    #endif
    xTaskCreate(twinRegister, "TwinRegister", STACK_REGISTRY, NULL, PRIO_REGISTRY, &g_twinRegisterHandle);
    vTaskDelay(500 / portTICK_PERIOD_MS);                                       // give task some time for registering new devices
}

// ---------------------------
void createRemoteParserTask() {
    #ifdef MASTERVERBOSE
        masterPrintln("start freeRTOS task: RemoteParser");
    #endif
    xTaskCreate(remoteParser, "RemoteParser", STACK_PARSER, NULL, PRIO_PARSER, &g_remoteParserHandle);
    vTaskDelay(500 / portTICK_PERIOD_MS);                                       // give task some time for registering new devices
}