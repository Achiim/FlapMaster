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
#include "SlaveTwin.h"
#include "FlapTasks.h"
#include "i2cMaster.h"
#include "FlapRegistry.h"
#include "MasterPrint.h"
#include "FlapReporting.h"
#include "FlapStatistics.h"
#include "Parser.h"
#include "Liga.h"
#include "LigaHelper.h"
#include "RemoteControl.h"
#include "RtosTasks.h"
// ----------------------------
//      _    _
//     | |  (_)__ _ __ _
//     | |__| / _` / _` |
//     |____|_\__, \__,_|
//            |___/
// Banner created:
// https://patorjk.com/software/taag/#p=display&c=c%2B%2B&f=Small&t=Liga
/**
 * @brief Liga FreeRTOS task: fetch/commit table, goals, next match; then wait.
 *
 * Core idea:
 *  1) Do the work (HTTP/JSON/etc.) first.
 *  2) Compute the dynamic period from Liga->decidePollMs().
 *  3) Subtract the work time to get the "remaining wait".
 *  4) Set the one-shot timer to that remaining wait (for countdown visibility).
 *  5) vTaskDelay() for exactly the same ticks (perfectly in sync with the timer).
 *
 * @param pvParameters Unused.
 */
void ligaTask(void* pvParameters) {
    int season   = 0;                                                           // actual season will be determined
    int matchday = 0;                                                           // actual matchday will be determined

    ReportCommand repCmd;                                                       // command to b e send to reporting task
    repCmd.repCommand   = REPORT_LIGA_TABLE;
    repCmd.responsQueue = nullptr;

    LiveGoalEvent       evs[maxGoalsPerMatchday];                               // bubber for live goals
    static LigaSnapshot snap;                                                   // snapshot off bundesliga table
    Liga = new LigaTable();                                                     // Construct the LigaTable object
    Liga->connect();                                                            // connect to openLigaDb

    ligaScanTimer = xTimerCreate("LigaScan",                                    // Using one-shot avoids repeated callbacks while the task is asleep.
                                 /*initial period*/ 1,                          // will be changed before each wait
                                 /*autoReload*/ pdFALSE,
                                 /*timerID*/ nullptr,
                                 /*callback*/ ligaScanCallback);
    configASSERT(ligaScanTimer != nullptr);
    xTimerStart(ligaScanTimer, pdMS_TO_TICKS(2));                               // Start it with a dummy short period so getNextLigaScanRemainingMs() is valid immediately.

    while (true) {
        #ifdef LIGAVERBOSE
            {
            TraceScope trace;
            Liga->ligaPrintln("========== Liga Scan ============== ");
            }
        #endif
        TickType_t t0 = xTaskGetTickCount();                                    // Work phase starts: remember start tick to compute remaining wait later

        if (Liga->pollLastChange(activeLeague, season, matchday)) {             // are there changes at openLigaDb??
            Liga->openLigaDBHealth();                                           // health check openLigaDB
            snap.clear();                                                       // reset snapshot buffer
            Liga->fetchTable(snap);                                             // pull latest table data (HTTP/JSON)
            Liga->commit(snap);                                                 // atomic flip to front buffer

            // TODO: Visualisierung / Animation neue Tabelle

            if (g_reportQueue)                                                  // Trigger a report of actual table
                xQueueOverwrite(g_reportQueue, &repCmd);

            // save snapshots for compare
            const LigaSnapshot& oldSnap = Liga->previousSnapshot();
            const LigaSnapshot& newSnap = Liga->activeSnapshot();
            const LigaRow*      oldL    = nullptr;
            const LigaRow*      newL    = nullptr;

            if (Liga->detectLeaderChange(oldSnap, newSnap, &oldL, &newL)) {
                // TODO: Visualisierung / Animation neuer Spitzenreiter auslösen
            }

            // Live-Gate detection
            LiveGoalEvent liveBuf[12];
            int           liveN = Liga->collectLiveMatches(activeLeague, liveBuf, 12); // are there some live matches
            if (liveN != 0) {
                for (int i = 0; i < liveN; ++i) {
                    LiveGoalEvent evBuf[8];                                     // expect less many new goals per live game
                    int           evN = Liga->fetchGoalsForLiveMatch(liveBuf[i].matchID, s_lastChange, evBuf, 8);
                    for (int j = 0; j < evN; ++j) {
                        const auto& e = evBuf[j];

                        // TODO: Visualisierung neue Treffer
                        // flapAnimateForTeam(e.scoredFor); ...
                    }
                }
            }
        }
        uint32_t nextMs = Liga->decidePollMs();                                 // Decide the *nominal* period in ms for the next scan
        if (nextMs < 1)
            nextMs = 1;

            #ifdef LIGAVERBOSE
                {
                TraceScope trace;
                Liga->ligaPrintln("========== Liga Scan End ========== ");
                }
            #endif

        TickType_t period = pdMS_TO_TICKS(nextMs);                              // Convert to ticks and compute the *remaining* wait (period - work time)
        if (period == 0)
            period = 1;                                                         // ensure at least 1 tick

        TickType_t elapsed = xTaskGetTickCount() - t0;                          // work duration in ticks
        TickType_t wait    = (elapsed < period) ? (period - elapsed) : 1;

        configASSERT(xTimerChangePeriod(ligaScanTimer, wait, pdMS_TO_TICKS(2)) ==
                     pdPASS);                                                   // xTimerChangePeriod() resets the one-shot timer to expire after 'wait' ticks.

        vTaskDelay(wait);                                                       // Sleep for exactly the same remaining ticks (perfect sync with timer)
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
