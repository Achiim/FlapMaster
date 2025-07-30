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
#include "RemoteParser.h"
#include "FlapStatistics.h"
#include "FlapRegistry.h"

// Task Priorites
#define PRIO_TWIN 5                                                             // Twin Tasks 0-n
#define PRIO_REGISTRY 4                                                         // Registry Task
#define PRIO_REPORTING 3                                                        // Reportimg Task
#define PRIO_REMOTE 2                                                           // Remote Control Task
#define PRIO_PARSER 3                                                           // Remote Parser Task
#define PRIO_STATISTICS 1                                                       // Statistics Task

// Task Stack sizes
#define STACK_TWIN 2536                                                         // Twin Tasks 0-n
#define STACK_REGISTRY 2536                                                     // Registry Task
#define STACK_REPORTING 2048                                                    // Reportimg Task
#define STACK_REMOTE 2048                                                       // Remote Control Task
#define STACK_PARSER 2536                                                       // Remote Parser Task

#ifdef STATISTICVERBOSE
    #define STACK_STATISTICS 2048                                               // Statistics Task
#endif
#ifndef STATISTICVERBOSE
    #define STACK_STATISTICS 1024                                               // Statistics Task
#endif

// Global variables for RTOS task handles
extern TaskHandle_t g_remoteControlHandle;                                      // Task handlers https://www.freertos.org/a00019.html#xTaskHandle
extern TaskHandle_t g_remoteParserHandle;                                       // RTOS Task Handler
extern TaskHandle_t g_twinRegisterHandle;                                       // RTOS Task Handler
extern TaskHandle_t g_reportingTaskHandle;                                      // RTOS Task Handler
extern TaskHandle_t g_statisticTaskHandle;                                      // RTOS Task Handler
extern TaskHandle_t g_twinHandle[numberOfTwins];                                // RTOS Task Handler

// Global variables for RTOS Queue handles
extern QueueHandle_t g_twinQueue[numberOfTwins];                                // Queue for Twin Tasks to receive remote control keys
extern QueueHandle_t g_reportingQueue;                                          // Queue for Reporting Task to receive remote control keys
extern QueueHandle_t g_parserQueue;                                             // Queue for remoteParser Task to receive remote control keys

// Global Objects for Tasks
extern RemoteControl   Control;                                                 // class to receice key from remote control
extern RemoteParser*   Parser;                                                  // Parser class to filter key from remote control
extern FlapRegistry*   Register;                                                // class for Registry Task
extern FlapStatistics* DataEvaluation;                                          // class to collect and evaluate operation statistics

class FlapTask {
   public:
    void TwinControl(ClickEvent receivedEvent, int mod);                        // overall control Twin with received remote keys
    void systemHalt(const char* reason    = "unknown error",
                    int         blinkCode = 0);                                 // fatal operation situations lead to systemHalt()

   private:
    void handleSingleKey(Key21 key, int mod);                                   // remote key single pressed handle
    void handleDoubleKey(Key21 key, int mod);                                   // remote key double pressed handle
    void logAndRun(int mod, const char* message, std::function<void()> action); // remote key execution
    char keyToDigit(Key21 key);                                                 // convert Key21 0...9 to digit 0...9
};

#endif                                                                          // FlapTasks_h
