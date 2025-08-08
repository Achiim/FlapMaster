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
#include "FlapStatistics.h"
#include "FlapRegistry.h"

// Task Priorites
#define PRIO_TWIN 5                                                             // Twin Tasks 0-n
#define PRIO_REGISTRY 4                                                         // Registry Task
#define PRIO_REPORT 3                                                           // Reportimg Task
#define PRIO_REMOTE 2                                                           // Remote Control Task
#define PRIO_PARSER 3                                                           // Remote Parser Task
#define PRIO_STATISTICS 1                                                       // Statistics Task

// Task Stack sizes
#define STACK_TWIN 2536                                                         // Twin Tasks 0-n
#define STACK_REGISTRY 2536                                                     // Registry Task
#define STACK_REPORT 2048                                                       // Reportimg Task
#define STACK_REMOTE 2048                                                       // Remote Control Task
#define STACK_PARSER 2536                                                       // Remote Parser Task

#ifdef STATISTICVERBOSE
    #define STACK_STATISTICS 2048                                               // Statistics Task
#endif
#ifndef STATISTICVERBOSE
    #define STACK_STATISTICS 1024                                               // Statistics Task
#endif

// Task Countdown Timer
#define LONG_SCAN_COUNTDOWN 1000 * 60 * 20                                      // 20 minutes
#define SHORT_SCAN_COUNTDOWN 1000 * 15                                          // 15 seconds
#define AVAILABILITY_CHECK_COUNTDOWN 1000 * 60                                  // 1 minute

// Global variables for RTOS task handles https://www.freertos.org/a00019.html#xTaskHandle
extern TaskHandle_t g_remoteControlHandle;                                      // RTOS Task Handler
extern TaskHandle_t g_parserHandle;                                             // RTOS Task Handler
extern TaskHandle_t g_registryHandle;                                           // RTOS Task Handler
extern TaskHandle_t g_reportHandle;                                             // RTOS Task Handler
extern TaskHandle_t g_statisticHandle;                                          // RTOS Task Handler
extern TaskHandle_t g_twinHandle[numberOfTwins];                                // RTOS Task Handler

// Global variables for RTOS Queue handles
extern QueueHandle_t g_reportQueue;                                             // Queue for Report Task to receive remote control keys
extern QueueHandle_t g_parserQueue;                                             // Queue for remoteParser Task to receive remote control keys

// Global Objects for Tasks
extern RemoteControl   Control;                                                 // class to receice key from remote control
extern RemoteParser*   Parser;                                                  // Parser class to filter key from remote control
extern FlapRegistry*   Register;                                                // class for Registry Task
extern FlapStatistics* DataEvaluation;                                          // class to collect and evaluate operation statistics

// Global count down Timer-Handles
extern TimerHandle_t shortScanTimer;                                            // ic2 scan in short modus
extern TimerHandle_t longScanTimer;                                             // i2c scan in long modus
extern TimerHandle_t availCheckTimer;                                           // device availability check

// Global Timer-Callbacks
extern void shortScanCallback(TimerHandle_t xTimer);                            // execute short Time i2c bus scan
extern void longScanCallback(TimerHandle_t xTimer);                             // execute Long Time i2c bus scan
extern void availCheckCallback(TimerHandle_t xTimer);                           // execute Availability Check

class FlapTask {
   public:
    void TwinControl(ClickEvent receivedEvent, int mod);                        // overall control Twin with received remote keys
    void systemHalt(const char* reason    = "unknown error",
                    int         blinkCode = 0);                                 // fatal operation situations lead to systemHalt()

   private:
    void handleSingleKey(Key21 key, int mod);                                   // remote key single pressed handle
    void handleDoubleKey(Key21 key, int mod);                                   // remote key double pressed handle
    void logAndRun(int mod, const char* message, std::function<void()> action); // remote key execution
    char key21ToDigit(Key21 key);                                               // convert Key21 0...9 to digit 0...9
};

#endif                                                                          // FlapTasks_h
