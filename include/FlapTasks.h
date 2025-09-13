// #################################################################################################################
//  ███████ ██       █████  ██████      ████████  █████  ███████ ██   ██ ███████
//  ██      ██      ██   ██ ██   ██        ██    ██   ██ ██      ██  ██  ██
//  █████   ██      ███████ ██████         ██    ███████ ███████ █████   ███████
//  ██      ██      ██   ██ ██             ██    ██   ██      ██ ██  ██       ██
//  ██      ███████ ██   ██ ██             ██    ██   ██ ███████ ██   ██ ███████
//
//
// ################################################################################################## by Achim ####
// Banner created:
// https://patorjk.com/software/taag/#p=display&c=c%2B%2B&f=ANSI%20Regular&t=flap%20Tasks
//
/*

    Real time Tasks for Flap Twin

*/
#ifndef FlapTasks_h
#define FlapTasks_h

#include <FlapGlobal.h>
#include "RemoteControl.h"
#include "Parser.h"
#include "Liga.h"
#include "FlapStatistics.h"
#include "FlapRegistry.h"

// Task Priorites
#define PRIO_LIGA 6                                                             // Liga task
#define PRIO_TWIN 5                                                             // Twin Tasks 0-n
#define PRIO_REGISTRY 4                                                         // Registry Task
#define PRIO_REPORT 3                                                           // Reportimg Task
#define PRIO_REMOTE 2                                                           // Remote Control Task
#define PRIO_PARSER 3                                                           // Remote Parser Task
#define PRIO_STATISTICS 1                                                       // Statistics Task

// Task Stack sizes
#define STACK_LIGA 5.5 * 1024                                                   // Liga Task (18kB)
#define STACK_TWIN 1.5 * 1024                                                   // Twin Tasks 0-n (6kB per Task)
#define STACK_REGISTRY 2 * 1024                                                 // Registry Task (8 kB)
#define STACK_REPORT 2 * 1024                                                   // Reporting Task (8 kB)
#define STACK_REMOTE 2 * 1024                                                   // Remote Control Task (8 kB)
#define STACK_PARSER 2 * 1024                                                   // Remote Parser Task (8 kB)

#ifdef STATISTICVERBOSE
    #define STACK_STATISTICS 2 * 1024                                           // Statistics Task
#endif
#ifndef STATISTICVERBOSE
    #define STACK_STATISTICS 1 * 1024                                           // Statistics Task (4 kB)
#endif

// Task Countdown Timer
#define LONG_SCAN_COUNTDOWN 1000UL * 60UL * 20UL                                // 20 minutes
#define SHORT_SCAN_COUNTDOWN 1000UL * 90UL                                      // 90 seconds
#define FAST_SCAN_COUNTDOWN 1000UL * 2UL                                        // 2 seconds
#define AVAILABILITY_CHECK_COUNTDOWN (1000UL * (8UL * 60UL + 7UL))              // 8:07 minutes 7 seconds
#define FAST_AVAI_COUNTDOWN 1000UL * 1UL                                        // 1 second
#define BOOT_WINDOW 1000UL * 30UL                                               // 30 seconds duration of fast mode

// Global variables for RTOS task handles https://www.freertos.org/a00019.html#xTaskHandle
extern TaskHandle_t g_remoteControlHandle;                                      // RTOS Task Handler
extern TaskHandle_t g_ligaHandle;                                               // RTOS Task Handler
extern TaskHandle_t g_parserHandle;                                             // RTOS Task Handler
extern TaskHandle_t g_registryHandle;                                           // RTOS Task Handler
extern TaskHandle_t g_reportHandle;                                             // RTOS Task Handler
extern TaskHandle_t g_statisticHandle;                                          // RTOS Task Handler
extern TaskHandle_t g_twinHandle[numberOfTwins];                                // RTOS Task Handler

// Global variables for RTOS Queue handles
extern QueueHandle_t g_reportQueue;                                             // Queue for Report Task to receive remote control keys
extern QueueHandle_t g_parserQueue;                                             // Queue for remoteParser Task to receive remote control keys

// Global Objects for Tasks
extern RemoteControl*  Control;                                                 // class to receice key from remote control
extern ParserClass*    Parser;                                                  // Parser class to filter key from remote control
extern FlapRegistry*   Register;                                                // class for Registry Task
extern FlapStatistics* DataEvaluation;                                          // class to collect and evaluate operation statistics
extern LigaTable*      Liga;                                                    // class for Bundesliga

// Global count down Timer-Handles
extern TimerHandle_t regiScanTimer;                                             // registry ic2 scan
extern TimerHandle_t availCheckTimer;                                           // device availability check
extern TimerHandle_t ligaScanTimer;                                             // openLigaDB scan

// Global Task-Handles
extern TaskHandle_t ligaTaskHandle;                                             // task handle for liga scanner

// Global Timer-Callbacks
extern void regiScanCallback(TimerHandle_t xTimer);                             // execute short Time i2c bus scan
extern void availCheckCallback(TimerHandle_t xTimer);                           // execute Availability Check
extern void ligaScanCallback(TimerHandle_t xTimer);                             // notify liga scan check

// Global Scan modes
enum scanModes { SCAN_FAST, SCAN_SHORT, SCAN_LONG, NO_SCAN };
extern scanModes g_scanMode;                                                    // registry scan mode

class FlapTask {
   public:
    void systemHalt(const char* reason    = "unknown error",
                    int         blinkCode = 0);                                 // fatal operation situations lead to systemHalt()

   private:
};

#endif                                                                          // FlapTasks_h
