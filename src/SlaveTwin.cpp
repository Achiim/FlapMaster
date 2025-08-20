// #####################################################################################################
//
//  ███████ ██       █████  ██    ██ ███████     ████████ ██     ██ ██ ███    ██
//  ██      ██      ██   ██ ██    ██ ██             ██    ██     ██ ██ ████   ██
//  ███████ ██      ███████ ██    ██ █████          ██    ██  █  ██ ██ ██ ██  ██
//       ██ ██      ██   ██  ██  ██  ██             ██    ██ ███ ██ ██ ██  ██ ██
//  ███████ ███████ ██   ██   ████   ███████        ██     ███ ███  ██ ██   ████
//
// ################################################################################ by Achim ####
// Banner created:
// https://patorjk.com/software/taag/#p=display&c=c%2B%2B&f=ANSI%20Regular&t=SLAVE%20TWIN
//
/*

    Flap Slave Twin Definition

    provides functionality on a flap module level to do

    - Flap Drum calibration
    - check for Hall Sensor presence
    - show Flap digt
    - move one Flap (next/prev)
    - move some Steps (next/prev) to set offset steps, if desired flap is not shown after calibration
    - save calibration to EEPROM of Flap module
    - step measurement
    - speed measurement
    - ARE YOU READY request limitation, to avoid heavy i2c bus load

*/

#include <Arduino.h>
#include <freertos/FreeRTOS.h>                                                  // Real Time OS
#include <freertos/task.h>
#include "driver/i2c.h"
#include <FlapGlobal.h>
#include "i2cFlap.h"
#include "i2cMaster.h"
#include "SlaveTwin.h"
#include "FlapTasks.h"

// ----------------------------
// generate for each slave one twin
SlaveTwin* Twin[numberOfTwins];

// ---------------------------------
// Constructor
SlaveTwin::SlaveTwin(int add) {
    _numberOfFlaps           = 0;                                               // unknown, will be set by device
    _slaveAddress            = add;                                             // take over from funktion call
    _parameter.flaps         = 0;                                               // init all parameter
    _parameter.offset        = 0;
    _parameter.sensorworking = false;
    _parameter.serialnumber  = 0;
    _parameter.slaveaddress  = add;
    _parameter.speed         = 0;
    _parameter.steps         = 0;
    _adjustOffset            = 0;                                               // init set offset variable for adjusting calibration
    _slaveReady.bootFlag     = false;
    _slaveReady.position     = 0;
    _slaveReady.ready        = false;
    _slaveReady.sensorStatus = false;
    _slaveReady.taskCode     = NO_COMMAND;
    twinQueue                = nullptr;

    #ifdef TWINVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        twinPrintln("Twin Object created 0x%02x", _slaveAddress);
        }
    #endif
}

// ----------------------------
// read from entry queue
void SlaveTwin::readQueue() {
    TwinCommand twinCmd;
    if (twinQueue != nullptr) {                                                 // if queue exists
        if (xQueueReceive(twinQueue, &twinCmd, portMAX_DELAY)) {
            #ifdef TWINVERBOSE
                {
                TraceScope trace;                                               // use semaphore to protect this block
                twinPrintln("Twin-Command received: %s", Parser->twinCommandToString(twinCmd.twinCommand));
                }
            #endif
            if (twinCmd.twinCommand != TWIN_NO_COMMAND) {
                twinControl(twinCmd);                                           // send corresponding Flap-Command to device
            }
        }
    }
}

// ----------------------------
// create entry queue
void SlaveTwin::createQueue() {
    twinQueue = xQueueCreate(1, sizeof(TwinCommand));                           // Create twin Queue
    if (twinQueue == nullptr) {
        #ifdef TWINVERBOSE
            {
            TraceScope trace;                                                   // use semaphore to protect this block
            twinPrintln("crearing of Twin--Entry-Queue failed");
            }
        #endif
    }
}

// ----------------------------
// send into entry queue
void SlaveTwin::sendQueue(TwinCommand twinCmd) {
    if (twinQueue != nullptr) {                                                 // if queue exists, send to all registered twin queues
        xQueueOverwrite(twinQueue, &twinCmd);
    } else {
        {
            TraceScope trace;                                                   // use semaphore to protect this block
            #ifdef ERRORVERBOSE
                twinPrintln("no slaveTwin available");
            #endif
        }
    }
}

// --------------------------------------
void SlaveTwin::twinControl(TwinCommand twinCmd) {
    TwinCommands cmd   = twinCmd.twinCommand;                                   // get command
    int          param = twinCmd.twinParameter;                                 // get parameter
    switch (cmd) {
        case TWIN_STEP_MEASUREMENT:
            logAndRun("Translate to I2C command Step Measurement...", [=] { stepMeasurement(); });
            break;
        case TWIN_CALIBRATION:
            logAndRun("Translate to I2C command Calibration...", [=] { calibration(); });
            break;
        case TWIN_SPEED_MEASUREMENT:
            logAndRun("Translate to I2C command Speed Measurement...", [=] { speedMeasurement(); });
            break;
        case TWIN_PREV_STEP:
            logAndRun("Translate to I2C command Prev Steps...", [=] { prevSteps(); });
            break;
        case TWIN_NEXT_STEP:
            logAndRun("Translate to I2C command Next Steps...", [=] { nextSteps(); });
            break;
        case TWIN_SET_OFFSET:
            logAndRun("Translate to I2C command Save Offset...", [=] { setOffset(); });
            break;
        case TWIN_PREV_FLAP:
            logAndRun("Translate to I2C command Previous Flap...", [=] { prevFlap(); });
            break;
        case TWIN_NEXT_FLAP:
            logAndRun("Translate to I2C command Next Flap...", [=] { nextFlap(); });
            break;
        case TWIN_SENSOR_CHECK:
            logAndRun("Translate to I2C command Sensor Check...", [=] { sensorCheck(); });
            break;
        case TWIN_RESET:
            logAndRun("Translate to I2C command RESET to Slave...", [=] { reset(); });
            break;
        case TWIN_NEW_ADDRESS:
            logAndRun("Translate to I2C command NEW ADDRESS to Slave...", [=] { setNewAddress(param); });
            break;
        case TWIN_AVAILABILITY:
            logAndRun("Translate to performAvailability ...", [=] { performAvailability(); });
            break;
        case TWIN_SHOW_FLAP: {
            std::string msg = "Translate to I2C command Show Flap / MOVE ";
            msg += param;
            msg += "...";
            logAndRun(msg.c_str(), [=] { showFlap(param); });
            break;
        }

        default: {
            #ifdef ERRORVERBOSE
                {
                TraceScope trace;
                twinPrint("Unknown Twin-Command: ");
                Serial.println(static_cast<int>(cmd));
                }
            #endif
            break;
        }
    }
}

// --------------------------------------
void SlaveTwin::logAndRun(const char* message, std::function<void()> action) {
    #ifdef TWINVERBOSE
        {
        TraceScope trace;
        twinPrintln(message);
        }
    #endif
    action();
}

// --------------------------------------------
// --------- SHOW_FLAP / MOVE -----------------
// --------------------------------------------
// show selected digit
void SlaveTwin::showFlap(int digit) {
    targetFlapNumber = digit;                                                   // remember requested target flap
    if (targetFlapNumber < 0 || targetFlapNumber >= _parameter.flaps) {         // validate range (flaps are 0..flaps-1)
    #ifdef ERRORVERBOSE
        {
        TraceScope trace;
        twinPrint("Flap unknown ...");
        Serial.println(digit);
        }
    #endif
        return;                                                                 // ignore invalid request
    }

    #ifdef TWINVERBOSE
        {
        TraceScope trace;
        twinPrint("showFlap: digit ");
        Serial.println(digit);
        twinPrint("showFlap: targetFlapNumber ");
        Serial.println(targetFlapNumber);
        }
    #endif

    const int steps_i = countStepsToMove(_flapNumber, targetFlapNumber);        // signed delta (can be <= 0)
    if (steps_i <= 0)
        return;                                                                 // nothing to do

    const uint16_t steps = (steps_i > 0xFFFF) ? 0xFFFF                          // clamp to 16-bit, because we use uint16_t for I2C command
                                              : static_cast<uint16_t>(steps_i);
    i2cLongCommand(i2cCommandParameter(MOVE, steps));                           // send LONG command to slave
    _flapNumber = targetFlapNumber;                                             // optimistic local update (no rollback by design)

    const uint32_t ayr_ms     = estimateAYRdurationMs(MOVE, steps);             // estimate duration for MOVE based on speed/steps
    const uint32_t timeout_ms = withSafety(ayr_ms, MOVE);                       // add safety margin (e.g. +25%, min/max caps)

    if (!waitUntilYouAreReady(MOVE, steps, timeout_ms)) {                       // quiet-until-AYR, then fast-poll ARE_YOU_READY
        {
            #ifdef ERRORVERBOSE
                {
                TraceScope trace;
                twinPrintln("showFlap failed or timed out on slave 0x%02X", _slaveAddress);
                }
            #endif
        }
        return;                                                                 // no registry sync on failure
    }

    #ifdef TWINVERBOSE
        {
        TraceScope trace;
        twinPrintln("request result of showFlap");
        }
    #endif
    getFullStateOfSlave();                                                      // get result of move
    synchSlaveRegistry();                                                       // update registry with confirmed state
}

// --------------------------------------------
// --------- CALIBRATIION ---------------------
// --------------------------------------------
void SlaveTwin::calibration() {
    const uint16_t steps_to_use = validStepsPerRevolution();                    // plausibilization of steps
    const uint16_t off_norm     = normalizeOffset(_parameter.offset, steps_to_use);
    const uint32_t steps_to_est = static_cast<uint32_t>(steps_to_use) + off_norm;

    i2cLongCommand(i2cCommandParameter(CALIBRATE, steps_to_use));
    _flapNumber = 0;                                                            // we reached Zero (flap 0)

    const uint32_t eta_ms     = estimateAYRdurationMs(CALIBRATE, steps_to_est); // etimate duration of CALIBRATE
    const uint32_t timeout_ms = withSafety(eta_ms, CALIBRATE);

    #ifdef TWINVERBOSE
        {
        TraceScope     trace;
        const uint32_t rev_ms = (_parameter.speed ? _parameter.speed : 1);
        const uint32_t sps    = (_parameter.steps * 1000u) / rev_ms;
        twinPrint("CALIBRATE spr=");
        Serial.print(spr);
        twinPrint(" off_norm=");
        Serial.print(off_norm);
        twinPrint(" steps_to_est=");
        Serial.print(steps_to_est);
        twinPrint(" sps≈");
        Serial.print(sps);
        twinPrint(" eta_ms=");
        Serial.print(eta_ms);
        twinPrint(" timeout_ms=");
        Serial.println(timeout_ms);
        }
    #endif

    if (!waitUntilYouAreReady(CALIBRATE, steps_to_est, timeout_ms)) {
        #ifdef ERRORVERBOSE
            {
            TraceScope trace;
            twinPrintln("Calibration failed or timed out on slave 0x%02X", _slaveAddress);
            }
        #endif
        return;
    }

    #ifdef TWINVERBOSE
        twinPrintln("request result of calibration");
    #endif

    readOffset(_parameter.offset);                                              // get offset to be secure
    getFullStateOfSlave();                                                      // get result of calibration
    synchSlaveRegistry();                                                       // take over confirmed values to registry
}

// --------------------------------------------
// --------- STEP MEASUREMENT -----------------
// --------------------------------------------
void SlaveTwin::stepMeasurement() {
    i2cLongCommand(i2cCommandParameter(STEP_MEASURE, 0));                       // STEP_MEASURE does not limit the steps, so we use 0
    _flapNumber = 0;                                                            // we stand at Zero after that

    const uint32_t ayr_ms     = estimateAYRdurationMs(STEP_MEASURE, 0);         // estimate duration for STEP MEASURE based on hall sensor
    const uint32_t timeout_ms = withSafety(ayr_ms, STEP_MEASURE);               // z. B. +25% min 500 ms max 5 s

    if (!waitUntilYouAreReady(STEP_MEASURE, 0, timeout_ms)) {                   // wait for slave to get ready;
        {
            {
                #ifdef ERRORVERBOSE
                    {
                    TraceScope trace;
                    twinPrintln("Step measurement failed or timed out on slave 0x%02X", _slaveAddress);
                    }
                #endif
                return;
            }
        }
    }
    #ifdef TWINVERBOSE
        twinPrintln("request result of step measurement");
    #endif

    readSteps(_parameter.steps);                                                // read new steps from slave
    getFullStateOfSlave();                                                      // get result of step measurement
    synchSlaveRegistry();                                                       // take over measured value to registry
}

// --------------------------------------------
// -------- SPEED MEASUREMENT -----------------
// --------------------------------------------
void SlaveTwin::speedMeasurement() {
    const uint16_t steps_to_use = validStepsPerRevolution();                    // 1) validate max. steps for speed measurement

    // Korrektur (angekündigt): konsistent den validierten Wert senden
    i2cLongCommand(i2cCommandParameter(SPEED_MEASURE, steps_to_use));

    const uint32_t ayr_ms     = estimateAYRdurationMs(SPEED_MEASURE, steps_to_use);
    const uint32_t timeout_ms = withSafety(ayr_ms, SPEED_MEASURE);

    if (!waitUntilYouAreReady(SPEED_MEASURE, steps_to_use, timeout_ms)) {
        #ifdef ERRORVERBOSE
            {
            TraceScope trace;
            twinPrintln("Speed measurement failed or timed out on slave 0x%02X", _slaveAddress);
            }
        #endif
        return;
    }
    #ifdef TWINVERBOSE
        twinPrintln("request result of speed measurement");
    #endif
    readSpeed(_parameter.speed);
    getFullStateOfSlave();
    synchSlaveRegistry();
}

// --------------------------------------------
// ------------ SENSOR CHECK ------------------
// --------------------------------------------
void SlaveTwin::sensorCheck() {
    uint16_t stepsToCheck = (_parameter.steps > 0) ? _parameter.steps : DEFAULT_STEPS; // has a step measurement been performed before?

    const uint32_t ayr_ms     = estimateAYRdurationMs(SENSOR_CHECK, stepsToCheck); // estimate duration for SPEED MEASURE based on hall sensor
    const uint32_t timeout_ms = withSafety(ayr_ms, SENSOR_CHECK);               // z. B. +25% min 500 ms max 5 s

    i2cLongCommand(i2cCommandParameter(SENSOR_CHECK, stepsToCheck));            // do sensor check
    if (!waitUntilYouAreReady(SENSOR_CHECK, stepsToCheck, timeout_ms)) {
        {
            #ifdef ERRORVERBOSE
                {
                TraceScope trace;
                twinPrintln("Sensor check failed or timed out on slave 0x%02X", _slaveAddress);
                }
            #endif
            return;
        }
        #ifdef TWINVERBOSE
            twinPrintln("request result of sensor check");
        #endif
    }
    getFullStateOfSlave();                                                      // get result of sensor check
    synchSlaveRegistry();                                                       // take over measured value to registry
}

// --------------------------------------------
// -------------- NEXT FLAP -------------------
// --------------------------------------------
void SlaveTwin::nextFlap() {
    if (_numberOfFlaps > 0) {
        targetFlapNumber = _flapNumber + 1;
        if (targetFlapNumber >= _numberOfFlaps)
            targetFlapNumber = 0;
        int steps = countStepsToMove(_flapNumber, targetFlapNumber);
        if (steps > 0) {
            i2cLongCommand(i2cCommandParameter(MOVE, steps));
            _flapNumber = targetFlapNumber;

            const uint32_t ayr_ms     = estimateAYRdurationMs(MOVE, steps);     // estimate duration for MOVE based on speed/steps
            const uint32_t timeout_ms = withSafety(ayr_ms, MOVE);               // add safety margin (e.g. +25%, min/max caps)

            if (!waitUntilYouAreReady(MOVE, steps, timeout_ms)) {               // quiet-until-AYR, then fast-poll ARE_YOU_READY
                {
                    #ifdef ERRORVERBOSE
                        {
                        TraceScope trace;
                        twinPrintln("nextFlap failed or timed out on slave 0x%02X", _slaveAddress);
                        }
                    #endif
                    return;                                                     // no registry sync on failure
                }
            }

            #ifdef TWINVERBOSE
                twinPrintln("request result of nextFlap");
            #endif
            getFullStateOfSlave();                                              // get result of move
            synchSlaveRegistry();                                               // update registry with confirmed state
        }
    } else {
        #ifdef ERRORVERBOSE
            twinPrintln("number of Flaps not set");
        #endif
    }
}

// --------------------------------------------
// -------------- PREV FLAP -------------------
// --------------------------------------------
void SlaveTwin::prevFlap() {
    if (_numberOfFlaps > 0) {
        targetFlapNumber = _flapNumber - 1;
        if (targetFlapNumber < 0)
            targetFlapNumber = _numberOfFlaps - 1;
        int steps = countStepsToMove(_flapNumber, targetFlapNumber);
        if (steps > 0) {
            i2cLongCommand(i2cCommandParameter(MOVE, steps));
            _flapNumber = targetFlapNumber;

            const uint32_t ayr_ms     = estimateAYRdurationMs(MOVE, steps);     // estimate duration for MOVE based on speed/steps
            const uint32_t timeout_ms = withSafety(ayr_ms, MOVE);               // add safety margin (e.g. +25%, min/max caps)

            if (!waitUntilYouAreReady(MOVE, steps, timeout_ms)) {               // quiet-until-AYR, then fast-poll ARE_YOU_READY
                {
                    #ifdef ERRORVERBOSE
                        {
                        TraceScope trace;
                        twinPrintln("prevFlap failed or timed out on slave 0x%02X", _slaveAddress);
                        }
                    #endif
                    return;                                                     // no registry sync on failure
                }
            }
            #ifdef TWINVERBOSE
                twinPrintln("request result of prevFlap");
            #endif
            getFullStateOfSlave();                                              // get result of move
            synchSlaveRegistry();                                               // update registry with confirmed state
        }
    } else {
        #ifdef ERRORVERBOSE
            twinPrintln("number of Flaps not set");
        #endif
    }
}

// --------------------------------------------
// ------- NEXT STEPS / ADJUSTMENT-------------
// --------------------------------------------
void SlaveTwin::nextSteps() {
    if (_parameter.offset + _adjustOffset + ADJUSTMENT_STEPS <= _parameter.steps) { // only as positiv, as to be stepped to one revolutoin
        i2cLongCommand(i2cCommandParameter(MOVE, ADJUSTMENT_STEPS));

        const uint32_t ayr_ms     = estimateAYRdurationMs(MOVE, ADJUSTMENT_STEPS); // estimate duration for MOVE based on speed/steps
        const uint32_t timeout_ms = withSafety(ayr_ms, MOVE);                   // add safety margin (e.g. +25%, min/max caps)
        if (!waitUntilYouAreReady(MOVE, ADJUSTMENT_STEPS, timeout_ms)) {        // quiet-until-AYR, then fast-poll ARE_YOU_READY
            {
                #ifdef ERRORVERBOSE
                    {
                    TraceScope trace;
                    twinPrintln("adjustment failed or timed out on slave 0x%02X", _slaveAddress);
                    }
                #endif
                return;                                                         // no registry sync on failure
            }
        }
        #ifdef TWINVERBOSE
            twinPrintln("request result of adjustment");
        #endif

        _adjustOffset += ADJUSTMENT_STEPS;
        twinPrintln("to be stored offset: %d (not yet stored)", _parameter.offset + _adjustOffset);

        getFullStateOfSlave();                                                  // get result of move
        synchSlaveRegistry();                                                   // update registry with confirmed state
    }
}

// --------------------------------------------
// ------- PREV STEPS / ADJUSTMENT-------------
// --------------------------------------------
void SlaveTwin::prevSteps() {
    if (_parameter.offset + _adjustOffset - ADJUSTMENT_STEPS >= 0) {            // only as negativ, as to be stepped back to zero
        _adjustOffset -= ADJUSTMENT_STEPS;
        twinPrintln("to be stored offset: %d (not yet stored)", _parameter.offset + _adjustOffset);
        synchSlaveRegistry();                                                   // update registry with confirmed state
    }
}

// --------------------------------------------
// -------------- SAVE OFFSET -----------------
// --------------------------------------------
void SlaveTwin::setOffset() {
    if (_parameter.offset + _adjustOffset >= 0 && _parameter.offset + _adjustOffset <= _parameter.steps) {
        _parameter.offset += _adjustOffset;                                     // add adjustment to stored offset
    }
    if (_parameter.offset < 0 || _parameter.offset > _parameter.steps)
        _parameter.offset = 0;                                                  // reset parameter.offset to zero
    _adjustOffset = 0;                                                          // reset adjustement to zero
    twinPrintln("reset adjustment offset to 0");
    twinPrintln("save: offset = %d | ms/Rev = %d | St/Rev = %d", _parameter.offset, _parameter.speed, _parameter.steps);

    i2cLongCommand(i2cCommandParameter(SET_OFFSET, _parameter.offset));
    const uint32_t ayr_ms     = estimateAYRdurationMs(MOVE, ADJUSTMENT_STEPS);  // estimate duration for MOVE based on speed/steps
    const uint32_t timeout_ms = withSafety(ayr_ms, MOVE);                       // add safety margin (e.g. +25%, min/max caps)
    if (!waitUntilYouAreReady(SET_OFFSET, ADJUSTMENT_STEPS, timeout_ms)) {      // quiet-until-AYR, then fast-poll ARE_YOU_READY
        {
            #ifdef ERRORVERBOSE
                {
                TraceScope trace;
                twinPrintln("save parameter failed or timed out on slave 0x%02X", _slaveAddress);
                }
            #endif
            return;                                                             // no registry sync on failure
        }
    }
    #ifdef TWINVERBOSE
        twinPrintln("request result of adjustment");
    #endif
    synchSlaveRegistry();                                                       // take over new offset to registry
}

// --------------------------------------------
// ----------------- RESET --------------------
// --------------------------------------------
void SlaveTwin::reset() {
    twinPrint("send reset to 0x:  ");
    Serial.println(_slaveAddress, HEX);
    Register->deregisterSlave(_slaveAddress);                                   // delete slave from registry, this address is free now
    i2cLongCommand(i2cCommandParameter(RESET, 0));                              // send reset to slave
}

// ----------------------------
int SlaveTwin::countStepsToMove(int from, int to) {
    if (_numberOfFlaps > 0) {
        int sum = 0;
        int pos = from;
        while (pos != to) {
            sum += stepsByFlap[pos];                                            // count steps to go from flap t0 flap
            pos = (pos + 1) % _numberOfFlaps;
        }
        return sum;
    } else {
        #ifdef MASTERVERBOSE
            twinPrintln("number of Flaps not set");
        #endif
        return 0;                                                               // nothing to move
    }
}
// ----------------------------
// purpose: write FlapMessage to i2c slave
// - access to i2c bus is semaphore protected
// - increment i2c statistics
//
// parameter:
// mess = message to be send (3byte structure: 1byte command, 2byte parameter)
// slaveAddress = i2c address of slave
//
void SlaveTwin::i2cLongCommand(LongMessage mess) {
    uint8_t data[sizeof(LongMessage)];
    prepareI2Cdata(mess, _slaveAddress, data);
    esp_err_t error = ESP_FAIL;

    takeI2CSemaphore();                                                         // take semaphore

    #ifdef I2CMASTERVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        twinPrint("send LongCommand (i2cLongCommand): 0x");
        Serial.print(data[0], HEX);
        Serial.print(" - ");
        Serial.println(getCommandName(data[0]));
        }
    #endif

    // construct command chain
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();                               // create link to command chain
    i2c_master_start(cmd);                                                      // set i2c start condition
    i2c_master_write_byte(cmd, (_slaveAddress << 1) | I2C_MASTER_WRITE, true);  // set i2c address of flap module
    i2c_master_write(cmd, data, sizeof(LongMessage), true);                     // send buffer to slave
    i2c_master_stop(cmd);                                                       // set i2c stop condition
    error = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(1));             // send command chain
    i2c_cmd_link_delete(cmd);                                                   // delete command chain

    giveI2CSemaphore();                                                         // give semaphore

    if (DataEvaluation)
        DataEvaluation->increment(1, sizeof(LongMessage));                      // count I2C usage, 1 Access, 3 byte data

    if (error == ESP_OK) {
        #ifdef I2CMASTERVERBOSE
            {
            TraceScope trace;                                                   // use semaphore to protect this block
            twinPrint("ACK from Slave 0x");
            Serial.print(_slaveAddress, HEX);
            Serial.println(" received.");
            }
        #endif
    } else {
        #ifdef I2CMASTERVERBOSE
            {
            TraceScope trace;                                                   // use semaphore to protect this block
            twinPrint("Error while sending to 0x");
            Serial.print(_slaveAddress, HEX);
            Serial.print(". Errorcode: ");
            Serial.println(esp_err_to_name(error));
            }
        #endif
        if (DataEvaluation)
            DataEvaluation->increment(0, 0, 0, 1);                              // count I2C usage, 1 timeout
    }
}
// ----------------------------
// purpose: convert command and parameter to LongMessage
// - change byte sequence of parameter, so slave can understand
//
// parameter:
// command = one byte command
// parameter = two byte integer
//
// return:
// LongMessage stucture to be send to i2c
//
LongMessage SlaveTwin::i2cCommandParameter(uint8_t command, u_int16_t parameter) {
    LongMessage mess = {0x00, 0x00, 0x00};                                      // initial message
    mess.command     = command;                                                 // get i2c command
    mess.lowByte     = parameter & 0xFF;                                        // get i2c parameter lower byte part
    mess.highByte    = (parameter >> 8) & 0xFF;                                 // get i2c parameter higher byte part

    return mess;
}

// ----------------------------
// purpose: send I2C short command, only one byte
esp_err_t SlaveTwin::i2cShortCommand(ShortMessage shortCmd, uint8_t* answer, int size) {
    esp_err_t ret = ESP_FAIL;

    logShortRequest(shortCmd);
    i2c_cmd_handle_t cmd = buildShortCommand(shortCmd, answer, size);
    takeI2CSemaphore();
    ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(30));
    giveI2CSemaphore();
    i2c_cmd_link_delete(cmd);

    if (DataEvaluation)
        DataEvaluation->increment(2, 1, 0);                                     // 2 accesses, 1 byte sent

    if (ret == ESP_OK) {
        logShortResponse(answer, size);
        if (DataEvaluation)
            DataEvaluation->increment(0, 0, size);                              // x bytes read
    } else {
        logShortError(shortCmd, ret);
        if (DataEvaluation)
            DataEvaluation->increment(0, 0, 0, 1);                              // timeout
    }

    return ret;
}

// ------------------------------------------
// helper: to build short command
i2c_cmd_handle_t SlaveTwin::buildShortCommand(ShortMessage shortCmd, uint8_t* answer, int size) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    if (i2c_master_start(cmd) != ESP_OK)
        systemHalt("FATAL ERROR: I2C start failed", 3);

    if (i2c_master_write_byte(cmd, (_slaveAddress << 1) | I2C_MASTER_WRITE, true) != ESP_OK)
        systemHalt("FATAL ERROR: write address failed", 3);

    i2c_master_write_byte(cmd, shortCmd, true);

    i2c_master_start(cmd);                                                      // repeated start
    i2c_master_write_byte(cmd, (_slaveAddress << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, answer, size, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);

    return cmd;
}

// ---------------------------
// helper to log sending short command
void SlaveTwin::logShortRequest(ShortMessage cmd) {
    #ifdef I2CMASTERVERBOSE
        TraceScope trace;
        twinPrint("Send shortCommand: 0x");
        Serial.print(cmd, HEX);
        Serial.print(" - ");
        Serial.print(getCommandName(cmd));
        Serial.print(" to slave 0x");
        Serial.println(_slaveAddress, HEX);
    #endif
}

// ---------------------------
// helper to log response to short command
void SlaveTwin::logShortResponse(uint8_t* answer, int size) {
    #ifdef I2CMASTERVERBOSE
        TraceScope trace;
        twinPrint("Got answer for shortCommand from slave:");
        for (int i = 0; i < size; i++) {
        Serial.print(" [");
        Serial.print(i);
        Serial.print("] = 0x");
        Serial.print(answer[i], HEX);
        }
        Serial.println();
    #endif
}

// ---------------------------
// helper to log errors for short command
void SlaveTwin::logShortError(ShortMessage cmd, esp_err_t err) {
    #ifdef ERRORVERBOSE
        TraceScope trace;
        twinPrint("No answer to shortCommand: 0x");
        Serial.print(cmd, HEX);
        Serial.print(" - ");
        Serial.println(getCommandName(cmd));
        twinPrint("ESP ERROR: ");
        Serial.println(esp_err_to_name(err));
        twinPrintln("Slave is not connected, ignore command.");
    #endif
}

// ----------------------------
// purpose: send I2C mid command, only one byte
//
esp_err_t SlaveTwin::i2cMidCommand(MidMessage midCmd, I2Caddress slaveaddress, uint8_t* answer, int size) {
    esp_err_t ret = ESP_FAIL;
    takeI2CSemaphore();

    logMidRequest(midCmd, slaveaddress);

    i2c_cmd_handle_t cmd = buildMidCommand(midCmd, slaveaddress, answer, size);
    ret                  = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(200));
    i2c_cmd_link_delete(cmd);

    giveI2CSemaphore();
    if (DataEvaluation)
        DataEvaluation->increment(2, 1, 0);                                     // 2 accesses, 1 byte sent

    if (ret == ESP_OK) {
        logMidResponse(answer, size);
        if (DataEvaluation)
            DataEvaluation->increment(0, 0, size);                              // x bytes read
    } else {
        logMidError(midCmd, ret);
        if (DataEvaluation)
            DataEvaluation->increment(0, 0, 0, 1);                              // timeout
    }

    return ret;
}

// ------------------------------------------
// helper: to build mid command
i2c_cmd_handle_t SlaveTwin::buildMidCommand(MidMessage midCmd, I2Caddress slaveAddress, uint8_t* answer, int size) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    if (i2c_master_start(cmd) != ESP_OK)
        Master->systemHalt("FATAL ERROR: I2C midCommand start failed", 2);

    if (i2c_master_write_byte(cmd, (slaveAddress << 1) | I2C_MASTER_WRITE, true) != ESP_OK)
        Master->systemHalt("FATAL ERROR: write address failed", 2);

    uint8_t data[2];
    data[0] = midCmd.command;
    data[1] = midCmd.paramByte;
    i2c_master_write(cmd, data, sizeof(MidMessage), true);                      // send buffer to slave

    i2c_master_start(cmd);                                                      // repeated start
    i2c_master_write_byte(cmd, (slaveAddress << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, answer, size, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);

    return cmd;
}

// ---------------------------
// helper to log sending mid command
void SlaveTwin::logMidRequest(MidMessage cmd, I2Caddress slaveAddress) {
    #ifdef I2CMASTERVERBOSE
        TraceScope trace;
        twinPrint("Send midCommand: 0x");
        Serial.print(cmd.command, HEX);
        Serial.print(" - ");
        Serial.print(getCommandName(cmd.command));
        Serial.print(" to slave 0x");
        Serial.println(slaveAddress, HEX);
    #endif
}

// ---------------------------
// helper to log response to mid command
void SlaveTwin::logMidResponse(uint8_t* answer, int size) {
    #ifdef I2CMASTERVERBOSE
        TraceScope trace;
        twinPrint("Got answer for midCommand from slave:");
        for (int i = 0; i < size; i++) {
        Serial.print(" [");
        Serial.print(i);
        Serial.print("] = 0x");
        Serial.print(answer[i], HEX);
        }
        Serial.println();
    #endif
}

// ---------------------------
// helper to log errors for mid command
void SlaveTwin::logMidError(MidMessage cmd, esp_err_t err) {
    #ifdef TWINVERBOSE
        TraceScope trace;
        twinPrint("No answer to midCommand: 0x");
        Serial.print(cmd.command, HEX);
        Serial.print(" - ");
        Serial.println(getCommandName(cmd.command));
        twinPrint("ESP ERROR: ");
        Serial.println(esp_err_to_name(err));
        twinPrintln("Slave is not connected, ignore command.");
    #endif
}

void SlaveTwin::setNewAddress(int address) {
    MidMessage midCmd;
    midCmd.command   = CMD_NEW_ADDRESS;
    midCmd.paramByte = address;                                                 // take over new address from command
    uint8_t answer[4];
    i2cMidCommand(midCmd, I2C_BASE_ADDRESS, answer, sizeof(answer));

    #ifdef TWINVERBOSE
        uint32_t sn;
        sn = ((uint32_t)answer[0]) | ((uint32_t)answer[1] << 8) | ((uint32_t)answer[2] << 16) | ((uint32_t)answer[3] << 24);
        twinPrint("new address 0x");
        Serial.println(address, HEX);
        twinPrint("was received by device with serial number: ");
        Serial.println(formatSerialNumber(sn));
    #endif
}

void SlaveTwin::performAvailability() {
    uint8_t   ans = 0;
    esp_err_t ret = i2c_probe_device(_slaveAddress);                            // send ping to device/slave
    if (ret != ESP_OK) {
        #ifdef AVAILABILITYVERBOSE
            {
            TraceScope trace;
            twinPrint("I²C-Slave is not available -> will deregister it 0x");
            Serial.println(_slaveAddress, HEX);
            }
        #endif
        Register->deregisterSlave(_slaveAddress);                               // delete slave from registry
        return;
    }

    #ifdef AVAILABILITYVERBOSE
        {
        TraceScope trace;
        twinPrint("I2C-Slave is available: 0x");
        Serial.println(_slaveAddress, HEX);
        }
    #endif

    if (i2cShortCommand(CMD_GET_BOOT_FLAG, &ans, sizeof(ans)) == ESP_OK) {      // request bootFlag
        _slaveReady.bootFlag = ans;

        if (_slaveReady.bootFlag) {                                             // check if bootFlag of slave is set
            {
                #ifdef AVAILABILITYVERBOSE
                    {
                    TraceScope trace;
                    twinPrint("reset bootFlag on rebooted Slave 0x");
                    Serial.println(_slaveAddress, HEX);
                    }
                #endif
            }
            if (i2cShortCommand(CMD_RESET_BOOT, &ans, sizeof(ans)) == ESP_OK) { // send reset bootFlag to slave
                {
                    #ifdef AVAILABILITYVERBOSE
                        {
                        TraceScope trace;
                        twinPrint("slave has rebooted; calibrating now Slave 0x");
                        Serial.println(_slaveAddress, HEX);
                        }
                    #endif
                }
                calibration();
            }
        }
    }
}

// -----------------------------------------
// ask slave about his parameters stored in EEPROM
// purpose:
// - request all parameter stored in EEPROM from slave by shortCommand
// - update structure parameter
//
// variable:
// - in:  address = address of slave to be registerd/updated
// - out: parameter = parameter that shall be updated by this function
//

// ----------------------------------------------------------
// Central reusable logging helpers (only active with TWINVERBOSE/ERRORVERBOSE)

void SlaveTwin::logHexBytes(const char* prefix, const uint8_t* buf, size_t n) {
    #ifdef TWINVERBOSE
        TraceScope trace;
        twinPrint(prefix);
        for (size_t i = 0; i < n; ++i) {
        Serial.print(i ? " 0x" : "0x");
        Serial.print(buf[i], HEX);
        }
        Serial.println();
    #endif
}

void SlaveTwin::logInfo(const char* prefix, const String& value) {
    #ifdef TWINVERBOSE
        TraceScope trace;
        twinPrint(prefix);
        Serial.println(value);
    #endif
}

void SlaveTwin::logInfoU32(const char* prefix, uint32_t v) {
    #ifdef TWINVERBOSE
        TraceScope trace;
        twinPrint(prefix);
        Serial.println(v);
    #endif
}

void SlaveTwin::logInfoU16(const char* prefix, uint16_t v) {
    #ifdef TWINVERBOSE
        TraceScope trace;
        twinPrint(prefix);
        Serial.println(v);
    #endif
}

void SlaveTwin::logInfoU8(const char* prefix, uint8_t v) {
    #ifdef TWINVERBOSE
        TraceScope trace;
        twinPrint(prefix);
        Serial.println(v);
    #endif
}

void SlaveTwin::logErr(const char* msg) {
    #ifdef ERRORVERBOSE
        TraceScope trace;
        twinPrintln(msg);
    #endif
}

// ----------------------------------------------------------
// Individual read functions – fully encapsulated
// Each function:
//  - sends the corresponding I2C command
//  - converts the received bytes into the target type
//  - writes directly into the provided variable
//  - logs success (TWINVERBOSE) and error (ERRORVERBOSE)
//  - returns true if successful, false otherwise

bool SlaveTwin::readSerialNumber(uint32_t& outSerial) {
    uint8_t ser[4] = {0, 0, 0, 0};
    if (i2cShortCommand(CMD_GET_SERIAL, ser, sizeof(ser)) == ESP_OK) {
        // logHexBytes("slave answered for CMD_GET_SERIAL: ", ser, sizeof(ser));
        outSerial = (uint32_t)ser[0] | ((uint32_t)ser[1] << 8) | ((uint32_t)ser[2] << 16) | ((uint32_t)ser[3] << 24);
        logInfo("slave answered for parameter.serialnumber = ", formatSerialNumber(outSerial));
        return true;
    }
    logErr("No answer to CMD_GET_SERIAL, slave is not responding, ignore command.");
    return false;
}

bool SlaveTwin::readOffset(uint16_t& outOffset) {
    uint8_t off[2] = {0, 0};
    if (i2cShortCommand(CMD_GET_OFFSET, off, sizeof(off)) == ESP_OK) {
        // logHexBytes("slave answered for CMD_GET_OFFSET: ", off, sizeof(off));
        outOffset = (uint16_t)((uint16_t)off[1] << 8 | off[0]);
        logInfoU16("slave answered for parameter.offset = ", outOffset);
        return true;
    }
    logErr("No answer to CMD_GET_OFFSET, slave is not responding, ignore command.");
    return false;
}

bool SlaveTwin::readFlaps(uint8_t& outFlaps) {
    uint8_t flaps = 0;
    if (i2cShortCommand(CMD_GET_FLAPS, &flaps, sizeof(flaps)) == ESP_OK) {
        outFlaps = flaps;
        logInfoU8("slave answered for parameter.flaps = ", outFlaps);
        return true;
    }
    logErr("No answer to CMD_GET_FLAPS, slave is not responding, ignore command.");
    return false;
}

bool SlaveTwin::readSpeed(uint16_t& outSpeed) {
    uint8_t speed[2] = {0, 0};
    if (i2cShortCommand(CMD_GET_SPEED, speed, sizeof(speed)) == ESP_OK) {
        outSpeed = (uint16_t)((uint16_t)speed[1] << 8 | speed[0]);
        logInfoU16("slave answered for parameter.speed = ", outSpeed);
        return true;
    }
    logErr("No answer to CMD_GET_SPEED, slave is not responding, ignore command.");
    return false;
}

bool SlaveTwin::readSteps(uint16_t& outSteps) {
    uint8_t steps[2] = {0, 0};
    if (i2cShortCommand(CMD_GET_STEPS, steps, sizeof(steps)) == ESP_OK) {
        outSteps = (uint16_t)((uint16_t)steps[1] << 8 | steps[0]);
        logInfoU16("slave answered for parameter.steps = ", outSteps);
        return true;
    }
    logErr("No answer to CMD_GET_STEPS, slave is not responding, ignore command.");
    return false;
}

bool SlaveTwin::readSensorWorking(bool& outSensorWorking) {
    uint8_t sensor = 0;
    if (i2cShortCommand(CMD_GET_SENSOR, &sensor, sizeof(sensor)) == ESP_OK) {
        outSensorWorking = sensor;
        logInfoU8("slave answered for parameter.sensorworking = ", outSensorWorking);
        return true;
    }
    logErr("No answer to CMD_GET_SENSOR, slave is not responding, ignore command.");
    return false;
}

// ----------------------------------------------------------
// Combined function without address parameter, uses individual readers.
// Return value: true if *all* reads were successful.
bool SlaveTwin::readAllParameters(slaveParameter& p) {
    bool ok = true;
    ok &= readSerialNumber(p.serialnumber);
    ok &= readOffset(p.offset);
    ok &= readFlaps(p.flaps);
    ok &= readSpeed(p.speed);
    ok &= readSteps(p.steps);
    ok &= readSensorWorking(p.sensorworking);
    return ok;
}

// ----------------------------------------------------------
// askSlaveAboutParameter now without address; only calls the combined function.
bool SlaveTwin::askSlaveAboutParameter(slaveParameter& parameter) {
    return readAllParameters(parameter);
}

// ----------------------------
// compute steps needed to move flap by flap (Bresenham-artige Verteilung)
void SlaveTwin::calculateStepsPerFlap() {
    for (int i = 0; i < _parameter.flaps; ++i) {
        int start      = (i * _parameter.steps) / _parameter.flaps;
        int end        = ((i + 1) * _parameter.steps) / _parameter.flaps;
        stepsByFlap[i] = end - start;
    }
}

// ----------------------------
// filters repetation and double sendings
Key21 SlaveTwin::ir2Key21(uint64_t ircode) {
    Key21    key;
    uint32_t now = millis();

    // Wiederholungsmuster direkt raus
    if (ircode == 0xFFFFFFFFFFFFFFFF)
        return Key21::NONE;

    // Wiederholung echten Codes in kurzer Zeit
    if (ircode == _lastTwinCode && (now - _lastTwinTime) < 100) {
        return Key21::NONE;
    }

    key = Control->decodeIR(ircode);
    if ((int)key < (int)Key21::NONE || (int)key > (int)Key21::UNKNOWN) {
        _lastTwinTime = now;
        _lastTwinCode = ircode;
        return Key21::NONE;
    }

    if (key != Key21::UNKNOWN && key != Key21::NONE) {
        #ifdef IRVERBOSE
            {
            TraceScope trace;                                                   // use semaphore to protect this block
            twinPrint("key recognized: ");
            Serial.print((int)key);
            Serial.print(" (0x");
            Serial.print(ircode, HEX);
            Serial.print(") - ");
            Serial.println(Control->key21ToString(key));                        // make it visable
            }
        #endif
        _lastTwinTime = now;
        _lastTwinCode = ircode;
        return key;
    }
    _lastTwinTime = now;
    _lastTwinCode = ircode;
    return Key21::NONE;                                                         // filter unknown to NONE
}

// ----------------------------
// purpose: check if i2c slave is ready or busy
// - access to i2c bus is protected by semaphore
//
// return:
// false  = busy
// true  = ready
//
bool SlaveTwin::isSlaveReady() {
    uint8_t data[1] = {0};                                                      // to receive answer
    #ifdef READYBUSYVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        twinPrint("readyness/busyness check of slave 0x");
        Serial.println(_slaveAddress, HEX);
        }
    #endif

    if (i2cShortCommand(CMD_ARE_YOU_READY, data, sizeof(data)) != ESP_OK) {     // send  request to Slave
        {
            #ifdef ERRORVERBOSE
                {
                TraceScope trace;                                               // use semaphore to protect this block
                twinPrint("(isSlaveReady) shortCommand STATE failed to: 0x");
                Serial.println(_slaveAddress, HEX);
                }
            #endif
        }
        return false;                                                           // twin not connected
    }

    _slaveReady.ready = data[0];                                                // update slaveReady structure
    #ifdef READYBUSYVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        twinPrint("(isSlaveReady) shortCommand STATE answer from slave 0x");
        Serial.print(_slaveAddress, HEX);
        Serial.print(" = ");
        Serial.println(_slaveReady.ready ? "READY" : "BUSY");
        }
    #endif
    return _slaveReady.ready;                                                   // return ready/busy state of slave
}

// ----------------------------
// purpose: get slave state  data structure
// - will ask slave about .ready, .taskCode, .bootFlag, .sensorStatus and .position
// return:
//          false  = busy
//          true  = ready
//          Twin will be updated: _slaveReady.ready, .taskCode, .bootFlag, .sensorStatus and .position
//
bool SlaveTwin::getFullStateOfSlave() {
    uint8_t data[6] = {0, 0, 0, 0, 0, 0};                                       // structure to receive answer for STATE
    #ifdef READYBUSYVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        twinPrint("get full STATE of slave 0x");
        Serial.println(_slaveAddress, HEX);
        }
    #endif

    if (i2cShortCommand(CMD_GET_STATE, data, sizeof(data)) != ESP_OK) {         // send  request to Slave
    #ifdef ERRORVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        twinPrint("(getFullStateOfSlave) shortCommand GET_STATE failed to: 0x");
        Serial.println(_slaveAddress, HEX);
        }
    #endif
        return false;                                                           // twin not connected
    }

    updateSlaveReadyInfo(data);                                                 // Update Slave-Status
    #ifdef READYBUSYVERBOSE
        printSlaveReadyInfo();
    #endif

    return _slaveReady.ready;                                                   // return read state
}

// --------------------------
//
void SlaveTwin::updateSlaveReadyInfo(uint8_t* data) {
    _slaveReady.ready        = data[0];
    _slaveReady.taskCode     = data[1];
    _slaveReady.bootFlag     = data[2];
    _slaveReady.sensorStatus = data[3];
    _slaveReady.position     = data[5] * 0x100 + data[4];                       // MSB + LSB
}

// --------------------------
//
void SlaveTwin::printSlaveReadyInfo() {
    #ifdef READYBUSYVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        twinPrint("_slaveReady.ready = ");
        Serial.println(_slaveReady.ready ? "TRUE" : "FALSE");
        //
        twinPrint("_slaveReady.taskCode 0x");
        Serial.print(_slaveReady.taskCode, HEX);
        Serial.print(_slaveReady.ready ? " - ready with " : " - busy with ");
        Serial.println(getCommandName(_slaveReady.taskCode));
        //
        twinPrint("_slaveReady.bootFlag = ");
        Serial.println(_slaveReady.bootFlag ? "TRUE" : "FALSE");
        //
        twinPrint("_slaveReady.sensorStatus = ");
        Serial.println(_slaveReady.sensorStatus ? "WORKING" : "BROKEN");
        //
        twinPrint("_slaveReady.position = ");
        Serial.println(_slaveReady.position);
        //
        twinPrint("");
        Serial.println(_slaveReady.ready ? "Slave is ready" : "Slave is busy and ignored your command");
        }
    #endif
}

// ----------------------------
// purpose:
// - updates Register, if device is allready registerd
// - don't register new device
// - updates Registry with Twin's _parameter
//
void SlaveTwin::synchSlaveRegistry() {
    auto it = g_slaveRegistry.find(_slaveAddress);                              // search in registry
    if (it != g_slaveRegistry.end() && it->second != nullptr) {                 // fist check if device is registered

        I2CSlaveDevice* device = it->second;

        if (device->position != _slaveReady.position) {
            device->position = _slaveReady.position;                            // update Flap position
            #ifdef TWINVERBOSE
                {
                TraceScope trace;
                twinPrint("Device position in registry updated of slave 0x%02X to: ", _slaveAddress);
                Serial.println(_slaveReady.position);
                }
            #endif
        }
        if (device->parameter.steps != _parameter.steps) {
            device->parameter.steps = _parameter.steps;                         // update steps per revolution
            #ifdef TWINVERBOSE
                {
                TraceScope trace;
                twinPrint("Device steps per revolution in registry updated of slave 0x%02X to:", _slaveAddress);
                Serial.println(_parameter.steps);
                }
            #endif

            calculateStepsPerFlap();                                            // steps per rev. has changed recalculate stepsPerFlap

            #ifdef TWINVERBOSE
                {
                TraceScope trace;
                twinPrint("Steps by Flap are: ");
                for (int i = 0; i < _parameter.flaps; ++i) {
                Serial.print(stepsByFlap[i]);
                if (i < _parameter.flaps - 1)
                Serial.print(", ");
                }
                Serial.println();
                }
            #endif
        }

        if (device->parameter.speed != _parameter.speed) {
            device->parameter.speed = _parameter.speed;                         // update speed (time per revolution)
            #ifdef TWINVERBOSE
                {
                TraceScope trace;
                twinPrintln("Device speed in registry updated of slave 0x%02X to: %d", _slaveAddress, _parameter.speed);
                }
            #endif
        }

        if (device->parameter.offset != _parameter.offset) {
            device->parameter.offset = _parameter.offset;                       // update offset
            #ifdef TWINVERBOSE
                {
                TraceScope trace;
                twinPrintln("Device offset in registry updated of slave 0x%02X to: %d", _slaveAddress, _parameter.offset);
                }
            #endif
        }

        if (device->parameter.sensorworking != _slaveReady.sensorStatus) {
            device->parameter.sensorworking = _slaveReady.sensorStatus;         // update Sensor status
            #ifdef TWINVERBOSE
                {
                TraceScope trace;
                twinPrintln("Device sensor status in registry updated of slave 0x%02X to: %d", _slaveAddress, _slaveReady.sensorStatus);
                }
            #endif
        }
        if (device->bootFlag != _slaveReady.bootFlag) {
            device->bootFlag = _slaveReady.bootFlag;                            // update bootFlag
            #ifdef TWINVERBOSE
                {
                TraceScope trace;
                twinPrint("Device bootFlag in registry updated of slave 0x%02X to: ", _slaveAddress);
                Serial.println(_slaveReady.bootFlag);
                }
            #endif
        }
    }
}

// --------------------------------------
void SlaveTwin::systemHalt(const char* reason, int blinkCode) {
    twinPrintln("===================================");
    twinPrintln("🛑 SYSTEM HALTED!");
    twinPrint("reason: ");
    Serial.println(reason);
    twinPrintln("===================================");

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
            twinPrint("System halt reason: ");                                  // regelmäßige
                                                // Konsolenmeldung
            Serial.println(reason);
        }
        vTaskDelay(pdMS_TO_TICKS(5000));                                        // Delay for 5s
    }
}
