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
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <FlapGlobal.h>
#include "RemoteControl.h"
#include "MasterPrint.h"
#include "FlapTasks.h"

IRrecv         irController(IR_RECEIVER_PIN);
decode_results results;

// ---------------------
// Constructor for RemoteControl
RemoteControl::RemoteControl() {
    _lastKey   = Key21::UNKNOWN;                                                // no key pressed on remote control
    _actualKey = Key21::NONE;                                                   // no key pressed on remote control
};

// ---------------------
void RemoteControl::getRemote() {
    getKey();
};

// ---------------------
void RemoteControl::getKey() {
    if (irController.decode(&results)) {
        _lastGetKeyCode = results.value;
        if (_lastGetKeyCode != 0xFF9867) {                                      // is it Key100_PLUS

            if (g_parserQueue != nullptr) {                                     // if queue exists
                xQueueOverwrite(g_parserQueue,
                                &_lastGetKeyCode);                              // send all other keys to all registered twin queues
            }
        } else {
            if (g_reportingQueue != nullptr) {                                  // if queue exists
                xQueueOverwrite(g_reportingQueue, &_lastGetKeyCode);            // send Key100_PLUS only to status task queue
            }
        }
        irController.resume();                                                  // prepare next ir receive
    }
}

// ----------------------------
// filters repetation and double sendings
Key21 RemoteControl::ir2Key21(uint64_t ircode) {
    Key21    key;
    uint32_t now = millis();

    if (ircode == 0xFFFFFFFFFFFFFFFF)                                           // Filter repeat signal FFFFF...FFF
        return Key21::NONE;

    if (ircode == _lastCode && (now - _lastTime) < DEBOUNCE_CONTROL_REMOTE) {   // Filter debouncing
        return Key21::NONE;
    }

    key = Control.decodeIR(ircode);                                             // convert to Key21
    if ((int)key < (int)Key21::NONE || (int)key > (int)Key21::UNKNOWN) {
        _lastTime = now;                                                        // remember presstime
        _lastCode = ircode;                                                     // rember this pressed code as last one
        return Key21::NONE;
    }

    if (key != Key21::UNKNOWN && key != Key21::NONE) {
        #ifdef IRVERBOSE
            {
            TraceScope trace;                                                   // use semaphore to protect this block
            controlPrint("RemoteControl::key recognized: ");
            Serial.print((int)key);
            Serial.print(" (0x");
            Serial.print(ircode, HEX);
            Serial.print(") - ");
            Serial.println(Control.key21ToString(key));                         // make it visable
            }
        #endif
        _lastTime = now;
        _lastCode = ircode;
        return key;                                                             // this is the valide key21::key
    }
    _lastTime = now;
    _lastCode = ircode;
    return Key21::NONE;                                                         // filter unknown to NONE
}

// ---------------------
const char* RemoteControl::clickTypeToString(ClickType type) {
    switch (type) {
        case CLICK_SINGLE:
            return "SINGLE";
        case CLICK_DOUBLE:
            return "DOUBLE";
        case CLICK_LONG:
            return "LONG";
        default:
            return "NONE";
    }
}

// ----------------------------
// convert raw key code to Key21::key
Key21 RemoteControl::decodeIR(uint32_t code) {
    switch (code) {
        case 0xFFA25D:
            return Key21::KEY_CH_MINUS;
        case 0xFF629D:
            return Key21::KEY_CH;
        case 0xFFE21D:
            return Key21::KEY_CH_PLUS;

        case 0xFF22DD:
            return Key21::KEY_PREV;
        case 0xFF02FD:
            return Key21::KEY_NEXT;
        case 0xFFC23D:
            return Key21::KEY_PLAY_PAUSE;

        case 0xFFE01F:
            return Key21::KEY_VOL_MINUS;
        case 0xFFA857:
            return Key21::KEY_VOL_PLUS;
        case 0xFF906F:
            return Key21::KEY_EQ;

        case 0xFF6897:
            return Key21::KEY_0;
        case 0xFF9867:
            return Key21::KEY_100_PLUS;
        case 0xFFB04F:
            return Key21::KEY_200_PLUS;

        case 0xFF30CF:
            return Key21::KEY_1;
        case 0xFF18E7:
            return Key21::KEY_2;
        case 0xFF7A85:
            return Key21::KEY_3;

        case 0xFF10EF:
            return Key21::KEY_4;
        case 0xFF38C7:
            return Key21::KEY_5;
        case 0xFF5AA5:
            return Key21::KEY_6;

        case 0xFF42BD:
            return Key21::KEY_7;
        case 0xFF4AB5:
            return Key21::KEY_8;
        case 0xFF52AD:
            return Key21::KEY_9;

        // ... add more key assignments if needed ...
        default:
            return Key21::UNKNOWN;
    }
}

// -----------------------------------
// make key 21 readable
const char* RemoteControl::key21ToString(Key21 key) {
    switch (key) {
        case Key21::KEY_CH_MINUS:
            return "CH-";
        case Key21::KEY_CH:
            return "CH";
        case Key21::KEY_CH_PLUS:
            return "CH+";

        case Key21::KEY_PREV:
            return "PREV";
        case Key21::KEY_NEXT:
            return "NEXT";
        case Key21::KEY_PLAY_PAUSE:
            return "PLAY/PAUSE";

        case Key21::KEY_VOL_MINUS:
            return "VOL-";
        case Key21::KEY_VOL_PLUS:
            return "VOL+";
        case Key21::KEY_EQ:
            return "EQ";

        case Key21::KEY_0:
            return "0";
        case Key21::KEY_100_PLUS:
            return "100+";
        case Key21::KEY_200_PLUS:
            return "200+";

        case Key21::KEY_1:
            return "1";
        case Key21::KEY_2:
            return "2";
        case Key21::KEY_3:
            return "3";

        case Key21::KEY_4:
            return "4";
        case Key21::KEY_5:
            return "5";
        case Key21::KEY_6:
            return "6";

        case Key21::KEY_7:
            return "7";
        case Key21::KEY_8:
            return "8";
        case Key21::KEY_9:
            return "9";

        case Key21::UNKNOWN:
            return "[UNKNOWN]";
        case Key21::NONE:
            return "[NONE]";

        default:
            return "[INVALID]";
    }
}