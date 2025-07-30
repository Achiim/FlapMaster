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
#include "SlaveTwin.h"
#include "MasterPrint.h"

// Global defines for RTOS task handles
TaskHandle_t g_remoteControlHandle = nullptr;                                   // Task handlers https://www.freertos.org/a00019.html#xTaskHandle
TaskHandle_t g_remoteParserHandle  = nullptr;
TaskHandle_t g_twinRegisterHandle  = nullptr;
TaskHandle_t g_reportingTaskHandle = nullptr;
TaskHandle_t g_statisticTaskHandle = nullptr;
TaskHandle_t g_twinHandle[numberOfTwins];

// Global defines for RTOS Queue handles
QueueHandle_t g_twinQueue[numberOfTwins];
QueueHandle_t g_reportingQueue = nullptr;
QueueHandle_t g_parserQueue    = nullptr;

// Global Objects for Tasks
RemoteControl   Control;                                                        // Remote Control with 21 keys
RemoteParser*   Parser         = nullptr;                                       // Remote Parser to filter 21 keys
FlapRegistry*   Register       = nullptr;                                       // Object for Registry Task
FlapStatistics* DataEvaluation = nullptr;                                       // Object for Statistics Task
FlapTask*       Master         = nullptr;

// --------------------------------------
void FlapTask::TwinControl(ClickEvent event, int mod) {
    if (event.type == CLICK_SINGLE) {
        handleSingleKey(event.key, mod);
    }
    if (event.type == CLICK_DOUBLE) {
        handleDoubleKey(event.key, mod);
    }
}

// --------------------------------------
void FlapTask::handleDoubleKey(Key21 key, int mod) {}

// --------------------------------------
void FlapTask::handleSingleKey(Key21 key, int mod) {
    switch (key) {
        case Key21::KEY_CH_MINUS:
            logAndRun(mod, "Send Step Measurement...", [=] { Twin[mod]->stepMeasurement(); });
            break;
        case Key21::KEY_CH:
            logAndRun(mod, "Send Calibration...", [=] { Twin[mod]->Calibrate(); });
            break;
        case Key21::KEY_CH_PLUS:
            logAndRun(mod, "Send Speed Measurement...", [=] { Twin[mod]->speedMeasurement(); });
            break;
        case Key21::KEY_PREV:
            logAndRun(mod, "Send Prev Steps...", [=] { Twin[mod]->prevSteps(); });
            break;
        case Key21::KEY_NEXT:
            logAndRun(mod, "Send Next Steps...", [=] { Twin[mod]->nextSteps(); });
            break;
        case Key21::KEY_PLAY_PAUSE:
            logAndRun(mod, "Send Save Offset...", [=] { Twin[mod]->setOffset(); });
            break;
        case Key21::KEY_VOL_MINUS:
            logAndRun(mod, "Send Previous Flap...", [=] { Twin[mod]->prevFlap(); });
            break;
        case Key21::KEY_VOL_PLUS:
            logAndRun(mod, "Send Next Flap...", [=] { Twin[mod]->nextFlap(); });
            break;
        case Key21::KEY_EQ:
            logAndRun(mod, "Send Sensor Check...", [=] { Twin[mod]->sensorCheck(); });
            break;
        case Key21::KEY_200_PLUS:
            logAndRun(mod, "Send RESET to Slave...", [=] { Twin[mod]->reset(); });
            break;

        case Key21::KEY_0:
        case Key21::KEY_1:
        case Key21::KEY_2:
        case Key21::KEY_3:
        case Key21::KEY_4:
        case Key21::KEY_5:
        case Key21::KEY_6:
        case Key21::KEY_7:
        case Key21::KEY_8:
        case Key21::KEY_9: {
            char        digit = keyToDigit(key);
            std::string msg   = "Send ";
            msg += digit;
            msg += "...";
            logAndRun(mod, msg.c_str(), [=] { Twin[mod]->showFlap(digit); });
            break;
        }

        default: {
            #ifdef MASTERVERBOSE
                {
                TraceScope trace;
                masterPrint("Unknown remote control key: ");
                Serial.println(static_cast<int>(key));
                }
            #endif
            break;
        }
    }
}

// --------------------------------------
void FlapTask::logAndRun(int mod, const char* message, std::function<void()> action) {
    #ifdef MASTERVERBOSE
        {
        TraceScope trace;
        Twin[mod]->twinPrintln(message);
        }
    #endif
    action();
}

char FlapTask::keyToDigit(Key21 key) {
    switch (key) {
        case Key21::KEY_0:
            return '0';
        case Key21::KEY_1:
            return '1';
        case Key21::KEY_2:
            return '2';
        case Key21::KEY_3:
            return '3';
        case Key21::KEY_4:
            return '4';
        case Key21::KEY_5:
            return '5';
        case Key21::KEY_6:
            return '6';
        case Key21::KEY_7:
            return '7';
        case Key21::KEY_8:
            return '8';
        case Key21::KEY_9:
            return '9';
        default:
            return 0;
    }
}

// --------------------------------------
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
        {
            TraceScope trace;                                                   // use semaphore to protect
            masterPrint("System halt reason: ");                                // regelmäßige
                                                  // Konsolenmeldung
            Serial.println(reason);
        }
        vTaskDelay(pdMS_TO_TICKS(5000));                                        // Delay for 5s
    }
}
