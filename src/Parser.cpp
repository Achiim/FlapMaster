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
#include "FlapRegistry.h"
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
    _ds.mode                     = MODE_BROADCAST;                              // default mode, send to all
    _ds.currentIndex             = -1;                                          // no selection
};

// ---------------------

/**
 * @brief dispatch keystroke by reporting device
 *
 */
void ParserClass::dispatchToOther() {
    #ifdef PARSERVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        parserPrintln("final decision ClickEvent.type: %s", Control->clickTypeToString(_receivedEvent.type));
        parserPrintln("final decision Received Key21: %s", Control->key21ToString(_receivedEvent.key));
        }
    #endif
    #ifdef PARSERVERBOSE
        {
        TraceScope trace;
        parserPrint("send mapping key21 to reporting: ");
        Serial.println(Control->key21ToString(_receivedEvent.key));
        }
    #endif
    _mappedReport = mapEvent2Report(_receivedEvent);                            // map ClickEvent to ReportCommand

    if (_mappedReport.repCommand != REPORT_NO_COMMAND) {
        if (g_reportQueue != nullptr) {                                         // if queue exists
            xQueueOverwrite(g_reportQueue, &_mappedReport);                     // send Reporting Command to reporting task
        }
    } else {
        mapEvent2Parser(_receivedEvent);                                        // map ClickEvent to ParserCommand
    }
}

// ---------------------

/**
 * @brief dispatch keystroke to all registered devices
 *
 */
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

    if (_ds.mode == MODE_BROADCAST && _mappedCommand.twinCommand == TWIN_FACTORY_RESET) {
        #ifdef ERRORVERBOSE
            {
            TraceScope trace;                                                   // use semaphore to protect this block
            parserPrintln("TWIN_FACTORY_RESET not supported in MODE_BROADCAST");
            }
        #endif
        return;
    }

    // UNICAST
    if (_ds.mode == MODE_UNICAST) {
        if (!Register->sendToIndex(_ds.currentIndex, _mappedCommand)) {
            #ifdef ERRORVERBOSE
                {
                TraceScope trace;
                parserPrintln("UNICAST: send failed (index=%d)", _ds.currentIndex);
                }
            #endif
        }
        return;
    }

    // BROADCAST
    Register->sendToAll(_mappedCommand);                                        // send to all registered devices
}

// ---------------------

/**
 * @brief first analysis of keystroke and assume it is a SINGLE
 *
 * @param receivedKey
 * @return ClickEvent
 */
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

/**
 * @brief update the first decision about ClickEvent
 *
 */
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

/**
 * @brief read Parser input from Queue and filter
 *
 */
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

/**
 * @brief if DOUBLE click not in time, declare it as SINGLE
 *
 * @return ClickEvent
 */
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

/**
 * @brief map event to TwinCommand
 *
 * @param event
 * @return TwinCommand
 */
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
        case Key21::KEY_100_PLUS:
            cmd.twinCommand = TWIN_NO_COMMAND;
            return cmd;
            break;
        case Key21::KEY_200_PLUS:
            cmd.twinCommand = TWIN_FACTORY_RESET;
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

// -------------------------
//
/**
 * @brief map event to ReportCommand
 *
 * @param event
 * @return ReportCommand
 */
ReportCommand ParserClass::mapEvent2Report(ClickEvent event) {
    ReportCommand cmd;
    cmd.repCommand = REPORT_NO_COMMAND;

    cmd.responsQueue = nullptr;

    switch (event.key) {
        case Key21::KEY_CH_MINUS:
            cmd.repCommand = REPORT_NO_COMMAND;
            return cmd;
            break;
        case Key21::KEY_CH:
            cmd.repCommand = REPORT_NO_COMMAND;
            return cmd;
            break;
        case Key21::KEY_CH_PLUS:
            cmd.repCommand = REPORT_NO_COMMAND;
            return cmd;
            break;
        case Key21::KEY_PREV:
            cmd.repCommand = REPORT_NO_COMMAND;
            return cmd;
            break;
        case Key21::KEY_NEXT:
            cmd.repCommand = REPORT_NO_COMMAND;
            return cmd;
            break;
        case Key21::KEY_PLAY_PAUSE:
            cmd.repCommand = REPORT_NO_COMMAND;
            return cmd;
            break;
        case Key21::KEY_VOL_MINUS:
            cmd.repCommand = REPORT_NO_COMMAND;
            return cmd;
            break;
        case Key21::KEY_VOL_PLUS:
            cmd.repCommand = REPORT_NO_COMMAND;
            return cmd;
            break;
        case Key21::KEY_EQ:
            cmd.repCommand = REPORT_NO_COMMAND;
            return cmd;
            break;
        case Key21::KEY_100_PLUS:
            cmd.repCommand = REPORT_NO_COMMAND;
            return cmd;
            break;
        case Key21::KEY_200_PLUS:
            cmd.repCommand = REPORT_NO_COMMAND;
            return cmd;
            break;
        case Key21::KEY_0: {
            cmd.repCommand = REPORT_NO_COMMAND;
            return cmd;
            break;
        }
        case Key21::KEY_1: {
            cmd.repCommand = REPORT_TASKS_STATUS;
            return cmd;
            break;
        }
        case Key21::KEY_2: {
            cmd.repCommand = REPORT_MEMORY;
            return cmd;
            break;
        }
        case Key21::KEY_3: {
            cmd.repCommand = REPORT_STEPS_BY_FLAP;
            return cmd;
            break;
        }
        case Key21::KEY_4: {
            cmd.repCommand = REPORT_RTOS_TASKS;
            return cmd;
            break;
        }
        case Key21::KEY_5: {
            cmd.repCommand = REPORT_I2C_STATISTIC;
            return cmd;
            break;
        }
        case Key21::KEY_6: {
            cmd.repCommand = REPORT_REGISTRY;
            return cmd;
            break;
        }
        case Key21::KEY_7: {
            cmd.repCommand = REPORT_LIGA_TABLE;
            return cmd;
            break;
        }
        case Key21::KEY_8: {
            cmd.repCommand = REPORT_POLL_STATUS;
            return cmd;
            break;
        }
        case Key21::KEY_9: {
            cmd.repCommand = REPORT_NO_COMMAND;
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
            cmd.repCommand = REPORT_NO_COMMAND;
            return cmd;
            break;
        }
    }
}
// -------------------------
//
/**
 * @brief map event to ReportCommand
 *
 * @param event
 * @return ReportCommand
 */
void ParserClass::mapEvent2Parser(ClickEvent event) {
    switch (event.key) {
        case Key21::KEY_CH_MINUS:
            selectPrevTwin();                                                   // previous twin in UNICAST mode
            break;
        case Key21::KEY_CH:
            toggleBroadcastMode();                                              // toggle MODE_BROADCAST / MODE UNICAST
            break;
        case Key21::KEY_CH_PLUS:
            selectNextTwin();                                                   // next twin in UNICAST mode
            break;
        case Key21::KEY_EQ:
            toggleLeague();                                                     // toggle league BL1 / BL2 / DFB
            break;
        default: {
            #ifdef ERRORVERBOSE
                {
                TraceScope trace;
                parserPrint("Unknown key21: ");
                Serial.println(static_cast<int>(event.key));
                }
            #endif
            break;
        }
    }
}

// -----------------------------

/**
 * @brief toggle league BL1 / BL2
 *
 */
void ParserClass::toggleLeague() {
    if (activeLeague == League::BL1) {
        activeLeague = League::BL2;
        ligaMaxTeams = LIGA2_MAX_TEAMS;
    } else if (activeLeague == League::BL2) {
        activeLeague = League::BL3;
        ligaMaxTeams = LIGA3_MAX_TEAMS;
    } else if (activeLeague == League::BL3) {
        activeLeague = League::BL1;
        ligaMaxTeams = LIGA1_MAX_TEAMS;
    }

    #ifdef MASTERVERBOSE
        {
        TraceScope trace;
        parserPrintln("switched to League %s", leagueName(activeLeague));
        }
    #endif

    snap[snapshotIndex].clear();                                                // clear actual snapshot
    snap[snapshotIndex ^ 1].clear();                                            // clear other snapshot
    ligaSeason                   = 0;                                           // reset actual Season
    ligaMatchday                 = 0;                                           // reset actual Matchday
    currentLastChangeOfMatchday  = "";                                          // openLigaDB Matchday change state
    previousLastChangeOfMatchday = "";                                          // openLigaDB Matchday change state
    currentMatchdayChanged       = false;                                       // no chances
    nextKickoffChanged           = false;                                       // no chances
    nextKickoffFarAway           = true;                                        // assume far away
    currentNextKickoffTime       = 0;                                           // reset
    previousNextKickoffTime      = 0;                                           // reset
    diffSecondsUntilKickoff      = 0;                                           // reset
    nextKickoffString            = "";                                          // reset
    matchIsLive                  = false;                                       // reset live match detection
    ligaConnectionRefused        = false;                                       // reset connection refused
    currentPollMode              = PollMode::POLL_MODE_ONCE;                    // start with NONE cycle
    xTaskNotifyGive(g_ligaHandle);                                              // wake ligaTask to perform liga changes emmediately
};

// -----------------------------

/**
 * @brief toggle broadcast mode MODE_BROADCAST / MODE UNICAST
 *
 */
void ParserClass::toggleBroadcastMode() {
    _ds.mode = (_ds.mode == MODE_BROADCAST) ? MODE_UNICAST : MODE_BROADCAST;
    if (_ds.mode == MODE_UNICAST && !Register->isIndexRegistered(_ds.currentIndex)) {
        _ds.currentIndex = Register->firstRegisteredIndex();
    }

    if (_ds.mode == MODE_UNICAST) {
        #ifdef MASTERVERBOSE
            {
            TraceScope trace;
            parserPrint("switched to MODE_UNICAST, send to Twin[n]  n= ");
            Serial.print(_ds.currentIndex);
            Serial.print(" with address 0x");
            Serial.println(g_slaveAddressPool[_ds.currentIndex], HEX);
            }
        #endif
    } else {
        #ifdef MASTERVERBOSE
            {
            TraceScope trace;
            parserPrintln("switched to MODE_BROADCAST");
            }
        #endif
    }
};

// -----------------------------

/**
 * @brief select next Twin to communicate with in UNICAST mode
 *
 */
void ParserClass::selectNextTwin() {
    _ds.currentIndex = Register->nextRegisteredIndex(_ds.currentIndex, +1);
    #ifdef MASTERVERBOSE
        {
        TraceScope trace;
        parserPrint("selected Twin index: ");
        Serial.println(_ds.currentIndex);
        }
    #endif
}

// -----------------------------

/**
 * @brief select previous Twin to communicate with in UNICAST mode
 *
 */
void ParserClass::selectPrevTwin() {
    _ds.currentIndex = Register->nextRegisteredIndex(_ds.currentIndex, -1);
    #ifdef MASTERVERBOSE
        {
        TraceScope trace;
        parserPrint("selected Twin index: ");
        Serial.println(_ds.currentIndex);
        }
    #endif
}

//
/**
 * @brief change TwinCommand value to readable String
 *
 * @param cmd
 * @return const char*
 */
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
        case TWIN_FACTORY_RESET:
            return "TWIN_RESET";
        case TWIN_AVAILABILITY:
            return "TWIN_AVAILABILITY";
        case TWIN_REGISTER:
            return "TWIN_REGISTER";
        case TWIN_NEW_ADDRESS:
            return "TWIN_NEW_ADDRESS";
        default:
            return "UNKNOWN_TWIN_COMMAND";
    }
}