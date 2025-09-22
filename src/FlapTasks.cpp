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

    Features:

    - destribution of IR Commands received from Remote Control

*/
#include "FlapStatistics.h"
#include "FlapTasks.h"
#include "FlapRegistry.h"
#include "MasterPrint.h"

// Global defines for RTOS task handles
TaskHandle_t g_remoteControlHandle = nullptr;                                   // Task handlers https://www.freertos.org/a00019.html#xTaskHandle
TaskHandle_t g_parserHandle        = nullptr;
TaskHandle_t g_ligaHandle          = nullptr;
TaskHandle_t g_registryHandle      = nullptr;
TaskHandle_t g_reportHandle        = nullptr;
TaskHandle_t g_statisticHandle     = nullptr;
TaskHandle_t g_twinHandle[numberOfTwins];

// Global defines for RTOS Queue handles
QueueHandle_t g_reportQueue = nullptr;
QueueHandle_t g_parserQueue = nullptr;

// Global Objects for Tasks
RemoteControl*  Control        = nullptr;                                       // Remote Control with 21 keys
LigaTable*      Liga           = nullptr;                                       // Object for Liga task
ParserClass*    Parser         = nullptr;                                       // Parser to filter 21 keys and convert to twin commands
FlapRegistry*   Register       = nullptr;                                       // Object for Registry Task
FlapStatistics* DataEvaluation = nullptr;                                       // Object for Statistics Task
FlapTask*       Master         = nullptr;

// Global Timer-Handles
TimerHandle_t regiScanTimer   = nullptr;
TimerHandle_t availCheckTimer = nullptr;
TimerHandle_t ligaScanTimer   = nullptr;

// Global registry scan mode
scanModes g_scanMode = NO_SCAN;                                                 // registry scan mode

// --------------------------------------
/**
 * @brief on error this function makes a system halt
 *
 * @param reason reason for system halt
 * @param blinkCode number of blink code to signalize reason
 */
void FlapTask::systemHalt(const char* reason, int blinkCode) {
    masterPrintln("===================================");
    masterPrintln("🛑 SYSTEM HALTED!");
    masterPrint("reason: ");
    Serial.println(reason);
    masterPrintln("===================================");

    #ifdef LED_BUILTIN
        const int ERROR_LED = LED_BUILTIN;                                      // oder eigene Fehler-LED
        pinMode(ERROR_LED, OUTPUT);
    #endif
    while (true) {
        #ifdef LED_BUILTIN
            if (blinkCode > 0) {
            for (int i = 0; i < blinkCode; ++i) {
            digitalWrite(ERROR_LED, HIGH);
            vTaskDelay(pdMS_TO_TICKS(200));
            digitalWrite(ERROR_LED, LOW);
            vTaskDelay(pdMS_TO_TICKS(200));
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            } else {                                                            // Ohne Blinkcode: LED dauerhaft an
            digitalWrite(ERROR_LED, HIGH);
            vTaskDelay(pdMS_TO_TICKS(5000));
            }
        #endif
        TraceScope trace;                                                       // use semaphore to protect
        {
            masterPrint("System halt reason: ");                                // regelmäßige Konsolenmeldung
            Serial.println(reason);
        }
        vTaskDelay(pdMS_TO_TICKS(5000));                                        // Delay for 5s
    }
}

// ----------------------------------

/**
 * @brief  liga Scan (dynamic countdown),
 * scans openLigaDB for changes
 *
 * @param xTimer associated timer
 */
void ligaScanCallback(TimerHandle_t xTimer) {}

// ----------------------------------

/**
 * @brief  Short Regisgtry Scan (SHORT_SCAN_COUNTDOWN), change to Long-Scan if all devices are available.
 * First makes device registry then registers unregistered devices.
 *
 * @param xTimer associated timer
 */
void regiScanCallback(TimerHandle_t xTimer) {
    #ifdef REGISTRYVERBOSE
        {
        TraceScope trace;
        Register->registerPrint("========== Registry I²C Scan = ");
        if (g_scanMode == SCAN_SHORT)
        Serial.print("SHORT");
        if (g_scanMode == SCAN_LONG)
        Serial.print("LONG");
        if (g_scanMode == SCAN_FAST)
        Serial.print("FAST");
        Serial.println(" ===========");
        }
    #endif
    Register->registerDevice();
}

// --- Availability-Check (AVAILABILITY_CHECK_COUNTDOWN),
// change back to Short-Scan, if devices are missing
/**
 * @brief Availability-Check (AVAILABILITY_CHECK_COUNTDOWN), change back to Short-Scan, if devices are missing
 *
 * @param xTimer associated timer
 */
void availCheckCallback(TimerHandle_t xTimer) {
    #ifdef AVAILABILITYVERBOSE
        {
        TraceScope trace;
        Register->registerPrintln("======= Device-Availability Check =============");
        }
    #endif

    Register->availabilityCheck();
    vTaskDelay(pdMS_TO_TICKS(200));                                             // short grace period
    Register->registerUnregistered();                                           // register devices that are known but not yet registered
    vTaskDelay(pdMS_TO_TICKS(200));                                             // short grace period
    Register->repairOutOfPoolDevices();                                         // reassign devices that are outside the address pool

    if (Register->size() >= Register->capacity()) {
        if (g_scanMode != SCAN_LONG) {
            xTimerChangePeriod(regiScanTimer, pdMS_TO_TICKS(LONG_SCAN_COUNTDOWN), 0);
            #ifdef MASTERVERBOSE
                {
                TraceScope trace;
                masterPrintln("all devices %d from %d registered- Registry is going back to long scan", Register->size(), Register->capacity());
                }
            #endif
        }
    } else {
        if (g_scanMode != SCAN_SHORT) {
            xTimerChangePeriod(regiScanTimer, pdMS_TO_TICKS(SHORT_SCAN_COUNTDOWN), 0);
            #ifdef MASTERVERBOSE
                {
                TraceScope trace;
                masterPrintln("missing devices %d from %d - Registry is going back to short scan", Register->capacity() - Register->size(),
                Register->capacity());
                }
            #endif
        }
    }
}
