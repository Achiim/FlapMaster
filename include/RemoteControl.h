// #################################################################################################################
//
//  ██████  ███████ ███    ███  ██████  ████████ ███████      ██████  ██████  ███    ██ ████████ ██████   ██████  ██
//  ██   ██ ██      ████  ████ ██    ██    ██    ██          ██      ██    ██ ████   ██    ██    ██   ██ ██    ██ ██
//  ██████  █████   ██ ████ ██ ██    ██    ██    █████       ██      ██    ██ ██ ██  ██    ██    ██████  ██    ██ ██
//  ██   ██ ██      ██  ██  ██ ██    ██    ██    ██          ██      ██    ██ ██  ██ ██    ██    ██   ██ ██    ██ ██
//  ██   ██ ███████ ██      ██  ██████     ██    ███████      ██████  ██████  ██   ████    ██    ██   ██  ██████ ███████
//
// ################################################################################################## by Achim ####
// Banner created:
// https://patorjk.com/software/taag/#p=display&c=c%2B%2B&f=ANSI%20Regular&t=Remote%20Control
//
/*

    Remote Conrol Definition

    21-key Infrared Sender

    Features:
    - delivers label of pressed key
    - detection of repeated key presses
    - returns repeatCount

*/
#ifndef RemoteControl_h
#define RemoteControl_h

// Remote Control with 21 keys                                        // remote control
// #include <DIYables_IRcontroller.h>                                 // DIYables_IRcontroller library
#include <Arduino.h>
#include <IRrecv.h>
#include <FlapGlobal.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "TracePrint.h"

#define IR_RECEIVER_PIN 14                                                      // The ESP32 pin GPIO14 connected to IR controller
#define DEBOUNCE_CONTROL_REMOTE 50                                              // ms
#define REPEAT_WINDOW 200                                                       // ms
#define DOUBLE_CLICK_THRESHOLD 300                                              // ms
#define LONG_PRESS_THRESHOLD 900                                                // ms

enum class Key21 : uint8_t {                                                    // list of known key21:keys
    NONE           = 0,
    KEY_CH_MINUS   = 69,
    KEY_CH         = 70,
    KEY_CH_PLUS    = 71,
    KEY_PREV       = 68,
    KEY_NEXT       = 64,
    KEY_PLAY_PAUSE = 67,
    KEY_VOL_MINUS  = 7,
    KEY_VOL_PLUS   = 21,
    KEY_EQ         = 9,
    KEY_100_PLUS   = 25,
    KEY_200_PLUS   = 13,
    KEY_0          = 22,
    KEY_1          = 12,
    KEY_2          = 24,
    KEY_3          = 94,
    KEY_4          = 8,
    KEY_5          = 28,
    KEY_6          = 90,
    KEY_7          = 66,
    KEY_8          = 82,
    KEY_9          = 74,
    UNKNOWN        = 255
};

enum ClickType { CLICK_NONE, CLICK_SINGLE, CLICK_DOUBLE, CLICK_LONG };          // list of possible Click types

// event from remote control
struct ClickEvent {
    Key21     key;                                                              // key pressed
    ClickType type;                                                             // press type: SINGLE, DOUBLE, LONG
};

extern IRrecv irController;                                                     // original class of installed library to receive IR

class RemoteControl {                                                           // class to evaluate received remote control commands
   public:
    unsigned long _lastTime       = 0;
    uint64_t      _lastGetKeyCode = 0;
    uint64_t      _lastCode       = 0;
    Key21         _lastKey        = Key21::UNKNOWN;
    Key21         _actualKey      = Key21::NONE;

    // ---------------------
    // Constructor for RemoteControl
    RemoteControl();

    // ---------------------
    // public functions
    void        getRemote();                                                    // get original raw data from IR receiver
    Key21       decodeIR(uint32_t code);                                        // decode IR raw data to Key21 code
    Key21       ir2Key21(uint64_t ircode);                                      // filter raw key codes and convert to key21
    const char* clickTypeToString(ClickType type);                              // get Text to ClickType
    const char* key21ToString(Key21 key);                                       // make readable Key21 Text for tracing

    // Remote Control trace
    template <typename... Args>
    void controlPrint(const Args&... args) {                                    // standard controlPrint
        tracePrint("[FLAP - CONTROL ] ", args...);
    }

    template <typename... Args>
    void controlPrintln(const Args&... args) {                                  // standard controlPrint with new line
        tracePrintln("[FLAP - CONTROL ] ", args...);
    }

   private:
    // ---------------------
    // private functions
    void getKey();                                                              // direct contact to irConroller to receive raw data
};

#endif                                                                          // RemoteControl_h
