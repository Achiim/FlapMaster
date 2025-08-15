// #################################################################################################################
//
//  ██████   █████  ██████  ███████ ███████ ██████
//  ██   ██ ██   ██ ██   ██ ██      ██      ██   ██
//  ██████  ███████ ██████  ███████ █████   ██████
//  ██      ██   ██ ██   ██      ██ ██      ██   ██
//  ██      ██   ██ ██   ██ ███████ ███████ ██   ██
//
// ################################################################################################## by Achim ####
// Banner created:
// https://patorjk.com/software/taag/#p=display&c=c%2B%2B&f=ANSI%20Regular&t=PARSER
//

#include <Arduino.h>
#include "Parser.h"
#include "FlapTasks.h"
#include "SlaveTwin.h"
#include "RemoteControl.h"

// ---------------------
// Constructor for Parser
ParserClass::ParserClass() {
    TwinCommand _mappedCommand;

    _receivedEvent.key           = Key21::NONE;                                 // reset received key
    _receivedEvent.type          = CLICK_NONE;                                  // reset received type
    _mappedCommand.twinCommand   = TWIN_NO_COMMAND;                             // init with no command
    _mappedCommand.twinParameter = 0;                                           // no parameter
    _mappedCommand.responsQueue  = nullptr;                                     // no respons queue
};

// ---------------------
// dispatch keystroke to all registered devices
void ParserClass::dispatchToTwins() {
    #ifdef PARSERVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        parserPrintln("final decision ClickEvent.type: %s", Control->clickTypeToString(_receivedEvent.type));
        parserPrintln("final decision Received Key21: %s", Control->key21ToString(_receivedEvent.key));
        }
    #endif

    _mappedCommand = mapEvent2Command(_receivedEvent);                          // map ClickEvent to TwinCommand

    #ifdef PARSERVERBOSE
        {
        TraceScope trace;
        parserPrint("mapping key21: ");
        Serial.print(Control->key21ToString(_receivedEvent.key));
        Serial.print(" to twinCommand: ");
        Serial.println(twinCommandToString(_mappedCommand.twinCommand));
        }
    #endif

    for (int m = 0; m < numberOfTwins; m++) {
        auto it = g_slaveRegistry.find(g_slaveAddressPool[m]);                  // search in registry
        if (it != g_slaveRegistry.end()) {                                      // if slave is registerd
            Twin[m]->sendQueue(_mappedCommand);                                 // send mapped command to twin
            #ifdef PARSERVERBOSE
                {
                TraceScope trace;                                               // use semaphore to protect this block
                parserPrintln("send Twin_Command: %s to Twin 0x%02x", twinCommandToString(_mappedCommand.twinCommand), g_slaveAddressPool[m]);
                parserPrintln("send Twin_Parameter: %d to Twin 0x%02x", _mappedCommand.twinParameter, g_slaveAddressPool[m]);
                }
            #endif
        } else {
            #ifdef ERRORVERBOSE
                {
                TraceScope trace;                                               // use semaphore to protect this block
                parserPrintln("Twin 0x%02x not registered", g_slaveAddressPool[m]);
                }
            #endif
        }
    }
}

// ---------------------
// first analysis of keystroke and assume it is a SINGLE
ClickEvent ParserClass::detect(Key21 receivedKey) {
    unsigned long now = millis();

    if (receivedKey == _lastKey) {
        if (_waitingForSecondClick && (now - _lastClickTime <= DOUBLE_CLICK_THRESHOLD)) {
            // Doppel-Klick erkannt
            _waitingForSecondClick  = false;
            _pendingKey             = Key21::NONE;
            _singleClickPendingTime = 0;
            _repeatCount            = 0;
            return {receivedKey, CLICK_DOUBLE};                                 // DOUBLE Click decision
        }

        if (now - _lastReceiveTime < REPEAT_WINDOW) {
            _repeatCount++;
            if (_repeatCount >= 3 && (now - _lastClickTime >= LONG_PRESS_THRESHOLD)) {
                _waitingForSecondClick  = false;
                _pendingKey             = Key21::NONE;
                _singleClickPendingTime = 0;
                _repeatCount            = 0;
                return {receivedKey, CLICK_LONG};                               // LONG Click decision
            }
        } else {
            _repeatCount = 0;
        }
    }

    // possible Single-Click – as fist assumption
    _lastKey         = receivedKey;
    _lastClickTime   = now;
    _lastReceiveTime = now;

    _pendingKey             = receivedKey;                                      // waiting for same key to be pressed again
    _singleClickPendingTime = now;
    _waitingForSecondClick  = true;

    return {receivedKey, CLICK_NONE};                                           // ClickEvent.type not now decided
}

// ---------------------
// update the first decision about ClickEvent
void ParserClass::analyseClickEvent() {
    ClickEvent evt;
    if (Parser->_waitingForSecondClick) {
        evt = poll();                                                           // analyse further key presses
        if (evt.type != CLICK_NONE) {
            _receivedEvent = evt;                                               // overwrite first assumption about Event
        }
    }
}

// ---------------------
// read Parser input from Queue and filter
void ParserClass::handleQueueMessage() {
    if (xQueueReceive(g_parserQueue, &_receivedValue, 0)) {
        _receivedKey = Control->ircodeToKey21(_receivedValue);                  // filter remote signal to reduce options
        if (_receivedKey != Key21::NONE && _receivedKey != Key21::UNKNOWN) {
            _receivedEvent = Parser->detect(_receivedKey);                      // valid key21 was pressed
            #ifdef PARSERVERBOSE
                {
                TraceScope trace;                                               // use semaphore to protect this block
                Parser->parserPrintln("from Queue ClickEvent.type: %s", Control->clickTypeToString(_receivedEvent.type));
                Parser->parserPrintln("from Received Key21: %s", Control->key21ToString(_receivedEvent.key));
                }
            #endif
        }
    }
}

// ---------------------
// if DOUBLE click not in time, declare it as SINGLE
ClickEvent ParserClass::poll() {
    /////////only for debugging///////////////////////////////////////////
    // #ifdef IRVERBOSE
    //     TraceScope trace;
    //     parserPrint("Poll(): ");
    //     Serial.print("waiting = ");
    //     Serial.print(waitingForSecondClick);
    //     Serial.print(", pendingKey = ");
    //     Serial.print((int)pendingKey);
    //     Serial.print(", elapsed = ");
    //     Serial.println(millis() - singleClickPendingTime);
    // #endif
    /////////////////////////////////////////////////////////////////////
    if (_waitingForSecondClick && _pendingKey != Key21::NONE) {
        unsigned long elapsed = millis() - _singleClickPendingTime;
        if (elapsed > DOUBLE_CLICK_THRESHOLD) {                                 // threshold is over
        #ifdef IRVERBOSE
            parserPrintln("→ SINGLE-Click detected because no second Click with same key");
        #endif
            ClickEvent evt          = {_pendingKey, CLICK_SINGLE};              // SINGLE Click decision
            _pendingKey             = Key21::NONE;                              // reset pending key
            _waitingForSecondClick  = false;                                    // reset
            _singleClickPendingTime = 0;                                        // reset
            _repeatCount            = 0;                                        // reset
            return evt;                                                         // provide decision
        }
    }
    return {Key21::NONE, CLICK_NONE};                                           // no new Event
}

// -------------------------
// map event to command
TwinCommand ParserClass::mapEvent2Command(ClickEvent event) {
    TwinCommand cmd;
    cmd.twinCommand   = TWIN_NO_COMMAND;
    cmd.twinParameter = 0;
    cmd.responsQueue  = nullptr;

    switch (event.key) {
        case Key21::KEY_CH_MINUS:
            cmd.twinCommand = TWIN_STEP_MEASUREMENT;
            return cmd;
            break;
        case Key21::KEY_CH:
            cmd.twinCommand = TWIN_CALIBRATION;
            return cmd;
            break;
        case Key21::KEY_CH_PLUS:
            cmd.twinCommand = TWIN_SPEED_MEASUREMENT;
            return cmd;
            break;
        case Key21::KEY_PREV:
            cmd.twinCommand = TWIN_PREV_STEP;
            return cmd;
            break;
        case Key21::KEY_NEXT:
            cmd.twinCommand = TWIN_NEXT_STEP;
            return cmd;
            break;
        case Key21::KEY_PLAY_PAUSE:
            cmd.twinCommand = TWIN_SET_OFFSET;
            return cmd;
            break;
        case Key21::KEY_VOL_MINUS:
            cmd.twinCommand = TWIN_PREV_FLAP;
            return cmd;
            break;
        case Key21::KEY_VOL_PLUS:
            cmd.twinCommand = TWIN_NEXT_FLAP;
            return cmd;
            break;
        case Key21::KEY_EQ:
            cmd.twinCommand = TWIN_SENSOR_CHECK;
            return cmd;
            break;
        case Key21::KEY_200_PLUS:
            cmd.twinCommand = TWIN_RESET;
            return cmd;
            break;
        case Key21::KEY_0: {
            cmd.twinCommand   = TWIN_SHOW_FLAP;
            cmd.twinParameter = 0;
            return cmd;
            break;
        }
        case Key21::KEY_1: {
            cmd.twinCommand   = TWIN_SHOW_FLAP;
            cmd.twinParameter = 1;
            return cmd;
            break;
        }
        case Key21::KEY_2: {
            cmd.twinCommand   = TWIN_SHOW_FLAP;
            cmd.twinParameter = 2;
            return cmd;
            break;
        }
        case Key21::KEY_3: {
            cmd.twinCommand   = TWIN_SHOW_FLAP;
            cmd.twinParameter = 3;
            return cmd;
            break;
        }
        case Key21::KEY_4: {
            cmd.twinCommand   = TWIN_SHOW_FLAP;
            cmd.twinParameter = 4;
            return cmd;
            break;
        }
        case Key21::KEY_5: {
            cmd.twinCommand   = TWIN_SHOW_FLAP;
            cmd.twinParameter = 5;
            return cmd;
            break;
        }
        case Key21::KEY_6: {
            cmd.twinCommand   = TWIN_SHOW_FLAP;
            cmd.twinParameter = 6;
            return cmd;
            break;
        }
        case Key21::KEY_7: {
            cmd.twinCommand   = TWIN_SHOW_FLAP;
            cmd.twinParameter = 7;
            return cmd;
            break;
        }
        case Key21::KEY_8: {
            cmd.twinCommand   = TWIN_SHOW_FLAP;
            cmd.twinParameter = 8;
            return cmd;
            break;
        }
        case Key21::KEY_9: {
            cmd.twinCommand   = TWIN_SHOW_FLAP;
            cmd.twinParameter = 9;
            return cmd;
            break;
        }
        default: {
            #ifdef ERRORVERBOSE
                {
                TraceScope trace;
                parserPrint("Unknown key21: ");
                Serial.println(static_cast<int>(event.key));
                }
            #endif
            cmd.twinCommand = TWIN_NO_COMMAND;
            return cmd;
            break;
        }
    }
}

// Wandelt einen TwinCommands-Wert in einen lesbaren String um
const char* ParserClass::twinCommandToString(TwinCommands cmd) {
    switch (cmd) {
        case TWIN_NO_COMMAND:
            return "TWIN_NO_COMMAND";
        case TWIN_SHOW_FLAP:
            return "TWIN_SHOW_FLAP";
        case TWIN_CALIBRATION:
            return "TWIN_CALIBRATION";
        case TWIN_STEP_MEASUREMENT:
            return "TWIN_STEP_MEASUREMENT";
        case TWIN_SPEED_MEASUREMENT:
            return "TWIN_SPEED_MEASUREMENT";
        case TWIN_SENSOR_CHECK:
            return "TWIN_SENSOR_CHECK";
        case TWIN_NEXT_FLAP:
            return "TWIN_NEXT_FLAP";
        case TWIN_PREV_FLAP:
            return "TWIN_PREV_FLAP";
        case TWIN_NEXT_STEP:
            return "TWIN_NEXT_STEP";
        case TWIN_PREV_STEP:
            return "TWIN_PREV_STEP";
        case TWIN_SET_OFFSET:
            return "TWIN_SET_OFFSET";
        case TWIN_RESET:
            return "TWIN_RESET";
        default:
            return "UNKNOWN_TWIN_COMMAND";
    }
}