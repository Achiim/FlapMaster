// #################################################################################################################
//
//  ██████  ███████ ███    ███  ██████  ████████ ███████     ██████   █████  ██████  ███████ ███████ ██████
//  ██   ██ ██      ████  ████ ██    ██    ██    ██          ██   ██ ██   ██ ██   ██ ██      ██      ██   ██
//  ██████  █████   ██ ████ ██ ██    ██    ██    █████       ██████  ███████ ██████  ███████ █████   ██████
//  ██   ██ ██      ██  ██  ██ ██    ██    ██    ██          ██      ██   ██ ██   ██      ██ ██      ██   ██
//  ██   ██ ███████ ██      ██  ██████     ██    ███████     ██      ██   ██ ██   ██ ███████ ███████ ██   ██
//
// ################################################################################################## by Achim ####
// Banner created:
// https://patorjk.com/software/taag/#p=display&c=c%2B%2B&f=ANSI%20Regular&t=Remote%20PARSER
//

#include <Arduino.h>
#include "FlapTasks.h"
#include "RemoteControl.h"
#include "RemoteParser.h"

// ---------------------
// Constructor for RemoteParser
RemoteParser::RemoteParser() {
    _receivedEvent.key  = Key21::NONE;                                          // reset received key
    _receivedEvent.type = CLICK_NONE;                                           // reset received type
};

// ---------------------
// dispatch keystroke to all registered devices
void RemoteParser::dispatchToTwins() {
    #ifdef MASTERVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        parserPrintln("final decision ClickEvent.type: %s", Control.clickTypeToString(_receivedEvent.type));
        parserPrintln("final decision Received Key21: %s", Control.key21ToString(_receivedEvent.key));
        }
    #endif

    for (int m = 0; m < numberOfTwins; m++) {
        auto it = g_slaveRegistry.find(g_slaveAddressPool[m]);                  // search in registry
        if (it != g_slaveRegistry.end()) {                                      // if slave is registerd

            if (g_twinQueue[m] != nullptr) {                                    // if queue exists, send to all registered twin queues
                xQueueOverwrite(g_twinQueue[m], &_receivedEvent);
                #ifdef IRVERBOSE
                    {
                    TraceScope trace;                                           // use semaphore to protect this block
                    parserPrintln("send final decision ClickEvent.type: %s to Twin",
                    Control.clickTypeToString(receivedEvent.type));
                    parserPrintln("send final decision Received Key21: %s to Twin",
                    Control.key21ToString(receivedEvent.key));
                    }
                #endif

            } else {
                {
                    TraceScope trace;                                           // use semaphore to protect this block
                    #ifdef MASTERVERBOSE
                        parserPrintln("no slaveTwin available");
                    #endif
                }
            }
        }
    }
}

// ---------------------
// first analysis of keystroke and assume it is a SINGLE
ClickEvent RemoteParser::detect(Key21 receivedKey) {
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
void RemoteParser::analyseClickEvent() {
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
void RemoteParser::handleQueueMessage() {
    if (xQueueReceive(g_parserQueue, &_receivedValue, 0)) {
        _receivedKey = Control.ir2Key21(_receivedValue);                        // filter remote signal to reduce options
        if (_receivedKey != Key21::NONE && _receivedKey != Key21::UNKNOWN) {
            _receivedEvent = Parser->detect(_receivedKey);                      // valid key21 was pressed
            #ifdef MASTERVERBOSE
                {
                TraceScope trace;                                               // use semaphore to protect this block
                Parser->parserPrintln("from Queue ClickEvent.type: %s", Control.clickTypeToString(_receivedEvent.type));
                Parser->parserPrintln("from Received Key21: %s", Control.key21ToString(_receivedEvent.key));
                }
            #endif
        }
    }
}

// ---------------------
// if DOUBLE click not in time, declare it as SINGLE
ClickEvent RemoteParser::poll() {
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
