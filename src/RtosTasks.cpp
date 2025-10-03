// #################################################################################################################
//
//  ██████  ████████  ██████  ███████     ████████  █████  ███████ ██   ██
//  ██   ██    ██    ██    ██ ██             ██    ██   ██ ██      ██  ██
//  ██████     ██    ██    ██ ███████        ██    ███████ ███████ █████
//  ██   ██    ██    ██    ██      ██        ██    ██   ██      ██ ██  ██
//  ██   ██    ██     ██████  ███████        ██    ██   ██ ███████ ██   ██
//
//
// ################################################################################################## by Achim ####
// Banner created:
// https://patorjk.com/software/taag/#p=display&c=c%2B%2B&f=ANSI%20Regular&t=RTOS%20TASK
//

#include <freertos/FreeRTOS.h>                                                  // Real Time OS
#include <freertos/task.h>
#include <FlapGlobal.h>
#include <cstdio>
#include <WebServer.h>
#include <SPIFFS.h>
#include <WiFi.h>
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
//     __      __   _    ___
//     \ \    / /__| |__/ __| ___ _ ___ _____ _ _
//      \ \/\/ / -_) '_ \__ \/ -_) '_\ V / -_) '_|
//       \_/\_/\___|_.__/___/\___|_|  \_/\___|_|
//
// Banner created:
// https://patorjk.com/software/taag/#p=display&c=c%2B%2B&f=Small&t=WebServer
/*
 */
void flapServerTask(void* pvParameters) {
    if (!Store->available())                                                    ///< check if file system is available
        return;
    if (!connectToWifi())                                                       ///< Ensure WiFi/TLS preconditions for OpenLigaDB are met
        return;

    vTaskDelay(pdMS_TO_TICKS(300));                                             ///< Short delay before time configuration
    configureTime();

    server.on("/", []() {
        Serial.println("[FLAP - SERVER  ] Web Client opened");
        server.send(200,                                                        // HTTP Status 200 OK
                    "text/html; charset=UTF-8",                                 // MIME-Typ
                    "<html><body><h1>Hallo Achim!</h1><p>Deine ESP32-Webseite läuft.</p></body></html>");
    });

    server.on("/status", []() {
        Serial.println("[FLAP - SERVER  ] Flap Display Task Status requested");
        sendStatusHtmlStream("/R01.json");
    });

    // Endpunkt 1: JSON pur
    server.on("/1", []() {
        File file = SPIFFS.open("/R01.json", FILE_READ);
        if (!file) {
            server.sendHeader("Access-Control-Allow-Origin", "*");
            server.send(500, "application/json", "{\"error\":\"Datei nicht gefunden\"}");
            return;
        }
        String jsonString = file.readString();
        file.close();

        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.send(200, "application/json; charset=UTF-8", jsonString);
    });

    server.begin();
    Serial.print("[FLAP - SERVER  ] Flap Liga Display WebServer address: ");
    Serial.println(WiFi.localIP());

    const TickType_t oneDay   = 24 * 60 * 60 * 1000 / portTICK_PERIOD_MS;       // background loop only for time-Sync
    TickType_t       lastSync = xTaskGetTickCount();

    while (true) {
        server.handleClient();                                                  // poll web client for requests
        if (xTaskGetTickCount() - lastSync > oneDay) {
            struct tm timeinfo;
            if (getLocalTime(&timeinfo)) {
                Serial.printf("date/time re-synchronized: %s\n", asctime(&timeinfo));
            }
            lastSync = xTaskGetTickCount();
        }

        vTaskDelay(pdMS_TO_TICKS(1000));                                        // relaxed sleep/poll mode
    }
}

// ----------------------------
//      _    _
//     | |  (_)__ _ __ _
//     | |__| / _` / _` |
//     |____|_\__, \__,_|
//            |___/
// Banner created:
// https://patorjk.com/software/taag/#p=display&c=c%2B%2B&f=Small&t=Liga
/**
/**
/**
 * @brief Main FreeRTOS task to manage periodic Bundesliga updates.
 *
 * This task continuously:
 *  - polls OpenLigaDB for table changes,
 *  - updates the internal snapshots,
 *  - detects leader changes,
 *  - queries live matches and new goal events,
 *  - triggers reporting and visualization hooks.
 *
 * Design:
 *  - Uses a one-shot FreeRTOS timer (`ligaScanTimer`) to keep a precise schedule.
 *  - Period between polls is dynamically adjusted via LigaTable::decidePollMs().
 *  - All network interactions are routed through LigaTable helpers.
 *
 * @param pvParameters Unused (FreeRTOS task prototype requirement).
 */

void ligaTask(void* pvParameters) {
    activeLeague   = League::BL1;
    ligaSeason     = 0;
    ligaMatchday   = 0;
    isSomeThingNew = false;

    snap[snapshotIndex].clear();
    snap[snapshotIndex ^ 1].clear();

    if (!initLigaTask()) {
        #ifdef ERRORVERBOSE
            {
            TraceScope trace;
            masterPrintln("LigaTask not correct initialized");
            }
        #endif

        while (true) {
            vTaskDelay(pdMS_TO_TICKS(30000));
        }
    }

    #ifdef LIGAVERBOSE
        {
        TraceScope trace;
        Liga->ligaPrintln("LigaTask successfully started");
        }
    #endif

    currentPollMode = POLL_MODE_ONCE;                                           // we start with ONCE cycle

    while (true) {
        selectPollCycle(currentPollMode);                                       // setzt activeCycle + activeCycleLength

        for (size_t i = 0; i < activeCycleLength; ++i) {
            currentPollScope = activeCycle[i];
            processPollScope(currentPollScope);
        }

        nextPollMode = determineNextPollMode();

        if (currentPollMode != nextPollMode) {
            Liga->ligaPrintln("PollMode changed from %s to %s", pollModeToString(currentPollMode), pollModeToString(nextPollMode));
        }

        currentPollMode = nextPollMode;
        isSomeThingNew  = false;

        pollManagerDynamicWait    = getPollDelay(currentPollMode);
        pollManagerStartOfWaiting = millis();

        uint32_t notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(pollManagerDynamicWait));
        if (notified > 0) {
            Liga->ligaPrintln("LigaTask woken up by notification");
        }
    }
}

// ----------------------------
//      ___               _        ___         _           _
//     | _ \___ _ __  ___| |_ ___ / __|___ _ _| |_ _ _ ___| |
//     |   / -_) '  \/ _ \  _/ -_) (__/ _ \ ' \  _| '_/ _ \ |
//     |_|_\___|_|_|_\___/\__\___|\___\___/_||_\__|_| \___/_|
//
// Banner created:
// https://patorjk.com/software/taag/#p=display&c=c%2B%2B&f=Small&t=RemoteControl
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
//      ___
//     | _ \__ _ _ _ ___ ___ _ _
//     |  _/ _` | '_(_-</ -_) '_|
//     |_| \__,_|_| /__/\___|_|

// Banner created:
// https://patorjk.com/software/taag/#p=display&c=c%2B%2B&f=Small&t=Parser
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
//      ___          _    _
//     | _ \___ __ _(_)__| |_ _ _ _  _
//     |   / -_) _` | (_-<  _| '_| || |
//     |_|_\___\__, |_/__/\__|_|  \_, |
//             |___/              |__/

// Banner created:
// https://patorjk.com/software/taag/#p=display&c=c%2B%2B&f=Small&t=Registry
/**
 * @brief freeRTOS Task Registry
 *
 * @param pvParameters
 */
void twinRegister(void* pvParameters) {
    #ifdef DISABLEREGISTRY
        {
        TraceScope trace;
        masterPrintln("Registry is disabled, no scan and no registry");
        vTaskSuspend(nullptr);                                                  // suspend task permanently if registry is disabled
        return;
        }
    #endif

    Register = new FlapRegistry();

    Register->registerDevice();                                                 // initial full scan for known devices
    vTaskDelay(pdMS_TO_TICKS(200));                                             // short grace period
    Register->registerUnregistered();                                           // register devices that are known but not yet registered
    vTaskDelay(pdMS_TO_TICKS(200));                                             // short grace period
    Register->repairOutOfPoolDevices();                                         // reassign devices that are outside the address pool

    // --- create timers for cyclic tasks ---
    regiScanTimer   = xTimerCreate("RegiScan", pdMS_TO_TICKS(SHORT_SCAN_COUNTDOWN), pdTRUE, nullptr, regiScanCallback);
    availCheckTimer = xTimerCreate("AvailChk", pdMS_TO_TICKS(AVAILABILITY_CHECK_COUNTDOWN), pdTRUE, nullptr, availCheckCallback);

    // --- FAST MODE immediately after boot ---
    const TickType_t bootWindowMs = pdMS_TO_TICKS(BOOT_WINDOW);                 // maximum 30s fast mode (boot window)
    const TickType_t fastShort    = pdMS_TO_TICKS(FAST_SCAN_COUNTDOWN);         // fast short-scan every 2s

    // configure timers for fast mode
    xTimerChangePeriod(regiScanTimer, fastShort, 0);                            // fast short-scan

    // start timers now
    xTimerStart(regiScanTimer, 0);                                              // start scan
    xTimerStart(availCheckTimer, 0);                                            // start availability check

    TickType_t startTick = xTaskGetTickCount();                                 // remember start time of fast mode

    if (Register->size() < Register->capacity()) {
        g_scanMode = SCAN_FAST;                                                 // actual scan mode
        #ifdef MASTERVERBOSE
            {
            TraceScope trace;
            Register->registerPrintln("Registry starts in fast scan");
            }
        #endif

        // --- loop until either boot window expires or all expected devices are registered ---
        while ((xTaskGetTickCount() - startTick) < bootWindowMs && (Register->size() < Register->capacity())) {
            vTaskDelay(pdMS_TO_TICKS(500));                                     // wait a bit before checking again
        }
    }

    // --- switch back to normal scan intervals ---
    if (Register->size() < Register->capacity()) {
        g_scanMode = SCAN_SHORT;                                                // actual scan mode
        xTimerChangePeriod(regiScanTimer, pdMS_TO_TICKS(SHORT_SCAN_COUNTDOWN), 0);
        #ifdef MASTERVERBOSE
            {
            TraceScope trace;
            Register->registerPrintln("Registry switches to short scan");
            }
        #endif
    } else {
        g_scanMode = SCAN_LONG;                                                 // actual scan mode
        xTimerChangePeriod(regiScanTimer, pdMS_TO_TICKS(LONG_SCAN_COUNTDOWN), 0);
        #ifdef MASTERVERBOSE
            {
            TraceScope trace;
            Register->registerPrintln("Registry switches to long scan");
            }
        #endif
    }
    xTimerChangePeriod(availCheckTimer, pdMS_TO_TICKS(AVAILABILITY_CHECK_COUNTDOWN), 0);

    // --- hand over to timers, suspend this task ---
    vTaskSuspend(nullptr);
}

// ----------------------------
//      _____        _
//     |_   _|_ __ _(_)_ _
//       | | \ V  V / | ' \ 
//       |_|  \_/\_/|_|_||_|

// Banner created:
// https://patorjk.com/software/taag/#p=display&c=c%2B%2B&f=Small&t=Twin
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
//      ___ _        _   _    _   _
//     / __| |_ __ _| |_(_)__| |_(_)__
//     \__ \  _/ _` |  _| (_-<  _| / _|
//     |___/\__\__,_|\__|_/__/\__|_\__|
//

// Banner created:
// https://patorjk.com/software/taag/#p=display&c=c%2B%2B&f=Small&t=Statistic
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
//      ___                   _   _
//     | _ \___ _ __  ___ _ _| |_(_)_ _  __ _
//     |   / -_) '_ \/ _ \ '_|  _| | ' \/ _` |
//     |_|_\___| .__/\___/_|  \__|_|_||_\__, |
//             |_|                      |___/
// Banner created:
// https://patorjk.com/software/taag/#p=display&c=c%2B%2B&f=Small&t=Reporting
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
            {
                TraceScope trace;                                               // protect report by semaphore

                Reports->reportPrintln("======== Flap Master Report ========"); // Report Header

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
                    Reports->reportI2CStatistic();                              // show I2C usage
                if (receivedCmd == REPORT_LIGA_TABLE)
                    Reports->reportLigaTable();                                 // show liga tabelle
                if (receivedCmd == REPORT_POLL_STATUS)
                    Reports->reportPollStatus();                                // show poll status

                Reports->reportPrintln("====== Flap Master Report End ======"); // Report Footer

            }                                                                   // release semaphore protection
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}
