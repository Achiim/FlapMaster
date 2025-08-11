// #####################################################################################################
//
//  ███████ ██       █████  ██    ██ ███████     ████████ ██     ██ ██ ███    ██
//  ██      ██      ██   ██ ██    ██ ██             ██    ██     ██ ██ ████   ██
//  ███████ ██      ███████ ██    ██ █████          ██    ██  █  ██ ██ ██ ██  ██
//       ██ ██      ██   ██  ██  ██  ██             ██    ██ ███ ██ ██ ██  ██ ██
//  ███████ ███████ ██   ██   ████   ███████        ██     ███ ███  ██ ██   ████
//
//
//   //// ################################################################################ by Achim ####
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
    - save calibration to EEPROM on Flap module
    - step measurement
    - speed measurement

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
// read entry queue
void SlaveTwin::readQueue() {
    TwinCommand twinCmd;
    if (twinQueue != nullptr) {                                                 // if queue exists
        if (xQueueReceive(twinQueue, &twinCmd, portMAX_DELAY)) {
            #ifdef TWINVERBOSE
                {
                TraceScope trace;                                               // use semaphore to protect this block
                twinPrintln("(readQueue) Twin Command received: %s", Parser->twinCommandToString(twinCmd.twinCommand));
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
    // twinQueue = xQueueCreate(1, sizeof(ClickEvent));                            // Create twin Queue
    twinQueue = xQueueCreate(1, sizeof(TwinCommand));                           // Create twin Queue
}

// ----------------------------
// send entry queue
void SlaveTwin::sendQueue(TwinCommand twinCmd) {
    if (twinQueue != nullptr) {                                                 // if queue exists, send to all registered twin queues
        xQueueOverwrite(twinQueue, &twinCmd);
        #ifdef TWINVERBOSE
            {
            TraceScope trace;                                                   // use semaphore to protect this block
            twinPrintln("send TwinCommand: %s to slave", Parser->twinCommandToString(twinCmd.twinCommand));
            }
        #endif
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
            logAndRun("Translate to I2C comands Step Measurement...", [=] { stepMeasurement(); });
            break;
        case TWIN_CALIBRATION:
            logAndRun("Translate to I2C comands Calibration...", [=] { calibration(); });
            break;
        case TWIN_SPEED_MEASUREMENT:
            logAndRun("Translate to I2C comands Speed Measurement...", [=] { speedMeasurement(); });
            break;
        case TWIN_PREV_STEP:
            logAndRun("Translate to I2C comands Prev Steps...", [=] { prevSteps(); });
            break;
        case TWIN_NEXT_STEP:
            logAndRun("Translate to I2C comands Next Steps...", [=] { nextSteps(); });
            break;
        case TWIN_SET_OFFSET:
            logAndRun("Translate to I2C comands Save Offset...", [=] { setOffset(); });
            break;
        case TWIN_PREV_FLAP:
            logAndRun("Translate to I2C comands Previous Flap...", [=] { prevFlap(); });
            break;
        case TWIN_NEXT_FLAP:
            logAndRun("Translate to I2C comands Next Flap...", [=] { nextFlap(); });
            break;
        case TWIN_SENSOR_CHECK:
            logAndRun("Translate to I2C comands Sensor Check...", [=] { sensorCheck(); });
            break;
        case TWIN_RESET:
            logAndRun("Translate to I2C comands RESET to Slave...", [=] { reset(); });
            break;
        case TWIN_NEW_ADDRESS:
            logAndRun("Translate to I2C comands NEW ADDRESS to Slave...", [=] { setNewAddress(param); });
            break;

        case TWIN_SHOW_FLAP: {
            std::string msg = "Translate to I2C comands ";
            msg += param;
            msg += "...";
            logAndRun(msg.c_str(), [=] { showFlap(param); });
            break;
        }

        default: {
            #ifdef ERRORVERBOSE
                {
                TraceScope trace;
                twinPrint("Unknown twin command: ");
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

// ---------------------------------------
// show selected digit
void SlaveTwin::showFlap(int digit) {
    //    targetFlapNumber = searchSign(digit);                                       // sign position in flapFont is number of targetFlap
    targetFlapNumber = digit;                                                   // sign position in flapFont is number of targetFlap
    if (targetFlapNumber < 0) {                                                 // search sign content of flapFont
        {
            #ifdef ERRORVERBOSE
                {
                TraceScope trace;                                               // use semaphore to protect this block
                twinPrint(" Flap unknown ...");
                Serial.println(digit);
                }
            #endif
        }
        return;
    }

    #ifdef TWINVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        twinPrint("showFlap: digit ");
        Serial.println(digit);
        twinPrint("showFlap: targetFlapNumber ");
        Serial.println(targetFlapNumber);
        }
    #endif

    int steps = countStepsToMove(_flapNumber, targetFlapNumber);                // get steps to move
    if (steps > 0) {
        i2cLongCommand(i2cCommandParameter(MOVE, steps));
        _flapNumber = targetFlapNumber;                                         // I assume it's okay
        if (!waitUntilSlaveReady(_parameter.speed)) {                           // wait for slave to get ready; maximum time of one revolutions
            {
                #ifdef ERRORVERBOSE
                    twinPrintln("showFlap failed or timed out on slave 0x%02X", _slaveAddress);
                #endif
                return;
            }
        }
        #ifdef TWINVERBOSE
            twinPrintln("request result of showFlap");
        #endif
        askSlaveAboutParameter(_slaveAddress, _parameter);                      // get result of showFlap
        synchSlaveRegistry(_parameter);                                         // take over position to registry
    }
}

// ----------------------------
void SlaveTwin::calibration() {
    if (_parameter.steps > 0) {                                                 // happend a step-messurement?
        i2cLongCommand(i2cCommandParameter(CALIBRATE, _parameter.steps));       // use result of measurement
    } else {
        i2cLongCommand(i2cCommandParameter(CALIBRATE, DEFAULT_STEPS));          // use default steps
    }
    _flapNumber = 0;                                                            // we stand at Zero after that
    if (!waitUntilSlaveReady(2 * _parameter.speed)) {                           // wait for slave to get ready; maximum time of 2 revolutions
        {
            #ifdef ERRORVERBOSE
                twinPrintln("Calibration failed or timed out on slave 0x%02X", _slaveAddress);
            #endif
            return;
        }
    }
    #ifdef TWINVERBOSE
        twinPrintln("request result of calibration");
    #endif
    askSlaveAboutParameter(_slaveAddress, _parameter);                          // get result of measurement
    synchSlaveRegistry(_parameter);                                             // take over measured value to registry
}

// ----------------------------
void SlaveTwin::stepMeasurement() {
    i2cLongCommand(i2cCommandParameter(STEP_MEASSURE, 0));
    _flapNumber = 0;                                                            // we stand at Zero after that
    if (!waitUntilSlaveReady(15 * _parameter.speed)) {                          // wait for slave to get ready; maximum time of 15 revolutions
        {
            #ifdef ERRORVERBOSE
                twinPrintln("Step meassurement failed or timed out on slave 0x%02X", _slaveAddress);
            #endif
            return;
        }
    }
    #ifdef TWINVERBOSE
        twinPrintln("request result of step measurement");
    #endif
    askSlaveAboutParameter(_slaveAddress, _parameter);                          // get result of measurement
    synchSlaveRegistry(_parameter);                                             // take over measured value to registry
}

// ----------------------------
void SlaveTwin::speedMeasurement() {
    uint16_t stepsToCheck = (_parameter.steps > 0) ? _parameter.steps : DEFAULT_STEPS; // has a step measurement been performed before?
    i2cLongCommand(i2cCommandParameter(SPEED_MEASSURE, _parameter.steps));
    if (!waitUntilSlaveReady(2 * _parameter.speed)) {                           // wait for slave to get ready; maximum time of 2 revolutions
        {
            #ifdef ERRORVERBOSE
                twinPrintln("Speed meassurement failed or timed out on slave 0x%02X", _slaveAddress);
            #endif
            return;
        }
    }
    #ifdef TWINVERBOSE
        twinPrintln("request result of speed measurement");
    #endif
    askSlaveAboutParameter(_slaveAddress, _parameter);                          // get result of measurement
    synchSlaveRegistry(_parameter);                                             // take over measured value to registry
}

// ----------------------------
void SlaveTwin::sensorCheck() {
    uint16_t stepsToCheck = (_parameter.steps > 0) ? _parameter.steps : DEFAULT_STEPS; // has a step measurement been performed before?
    i2cLongCommand(i2cCommandParameter(SENSOR_CHECK, _parameter.steps));        // do sensor check
    if (!waitUntilSlaveReady(2 * _parameter.speed)) {                           // wait for slave to get ready; maximum time of 2 revolutions
        {
            #ifdef ERRORVERBOSE
                twinPrintln("Sensor check failed or timed out on slave 0x%02X", _slaveAddress);
            #endif
            return;
        }
    }
    #ifdef TWINVERBOSE
        twinPrintln("request result of sensor check");
    #endif
    askSlaveAboutParameter(_slaveAddress, _parameter);                          // get result of sensor
    synchSlaveRegistry(_parameter);                                             // take over measured value to registry
}

// ----------------------------
void SlaveTwin::nextFlap() {
    if (_numberOfFlaps > 0) {
        targetFlapNumber = _flapNumber + 1;
        if (targetFlapNumber >= _numberOfFlaps)
            targetFlapNumber = 0;
        int steps = countStepsToMove(_flapNumber, targetFlapNumber);
        if (steps > 0) {
            i2cLongCommand(i2cCommandParameter(MOVE, steps));
            _flapNumber = targetFlapNumber;
            if (!waitUntilSlaveReady(_parameter.speed)) {                       // wait for slave to get ready; maximum time 1/2 revelution
                {
                    #ifdef ERRORVERBOSE
                        twinPrintln("Next Flap failed or timed out on slave 0x%02X", _slaveAddress);
                    #endif
                    return;
                }
            }
            #ifdef TWINVERBOSE
                twinPrintln("request result of next flap");
            #endif
            askSlaveAboutParameter(_slaveAddress, _parameter);                  // get result of next flap
            synchSlaveRegistry(_parameter);                                     // take over position to registry
        }
    } else {
        #ifdef ERRORVERBOSE
            twinPrintln("number of Flaps not set");
        #endif
    }
}

// ----------------------------
void SlaveTwin::prevFlap() {
    if (_numberOfFlaps > 0) {
        targetFlapNumber = _flapNumber - 1;
        if (targetFlapNumber < 0)
            targetFlapNumber = _numberOfFlaps - 1;
        int steps = countStepsToMove(_flapNumber, targetFlapNumber);
        if (steps > 0) {
            i2cLongCommand(i2cCommandParameter(MOVE, steps));
            _flapNumber = targetFlapNumber;
            if (!waitUntilSlaveReady(2 * _parameter.speed)) {                   // wait for slave to get ready; maximum time of2 revolutions
                {
                    #ifdef ERRORVERBOSE
                        twinPrintln("Previous Flap failed or timed out on slave 0x%02X", _slaveAddress);
                    #endif
                    return;
                }
            }
            #ifdef TWINVERBOSE
                twinPrintln("request result of previous flap");
            #endif
            askSlaveAboutParameter(_slaveAddress, _parameter);                  // get result of prev flap
            synchSlaveRegistry(_parameter);                                     // take over position to registry
        }
    } else {
        #ifdef ERRORVERBOSE
            twinPrintln("number of Flaps not set");
        #endif
    }
}

// ----------------------------
void SlaveTwin::nextSteps() {
    if (_parameter.offset + _adjustOffset + ADJUSTMENT_STEPS <= _parameter.steps) { // only as positiv, as to be stepped to one revolutoin
        i2cLongCommand(i2cCommandParameter(MOVE, ADJUSTMENT_STEPS));
        _adjustOffset += ADJUSTMENT_STEPS;
        twinPrintln("to be stored offset: %d (not yet stored)", _parameter.offset + _adjustOffset);
    }
}

// ----------------------------
void SlaveTwin::prevSteps() {
    if (_parameter.offset + _adjustOffset - ADJUSTMENT_STEPS >= 0) {            // only as negativ, as to be stepped back to zero
        _adjustOffset -= ADJUSTMENT_STEPS;
        twinPrintln("to be stored offset: %d (not yet stored)", _parameter.offset + _adjustOffset);
    }
}

// ----------------------------
void SlaveTwin::setOffset() {
    if (_parameter.offset + _adjustOffset >= 0 && _parameter.offset + _adjustOffset <= _parameter.steps) {
        _parameter.offset += _adjustOffset;                                     // add adjustment to stored offset
    }
    if (_parameter.offset < 0 || _parameter.offset > _parameter.steps)
        _parameter.offset = 0;                                                  // reset parameter.offset to zero
    _adjustOffset = 0;                                                          // reset adjustement to zero
    twinPrintln("reset adjustment offset to 0");
    twinPrintln("save: offset = %d | ms/Rev = %d | St/Rev = %d", _parameter.offset, _parameter.speed, _parameter.steps);

    synchSlaveRegistry(_parameter);                                             // take over new offset to registry
    i2cLongCommand(i2cCommandParameter(SET_OFFSET, _parameter.offset));
}

// ----------------------------
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

    // take semaphore
    takeI2CSemaphore();

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

    // give semaphore
    giveI2CSemaphore();

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
    takeI2CSemaphore();

    logShortRequest(shortCmd);

    i2c_cmd_handle_t cmd = buildShortCommand(shortCmd, answer, size);
    ret                  = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(500));
    i2c_cmd_link_delete(cmd);

    giveI2CSemaphore();
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
void SlaveTwin::askSlaveAboutParameter(I2Caddress address, slaveParameter& parameter) {
    // ask Flap about actual parameter
    uint8_t ser[4] = {0, 0, 0, 0};
    if (i2cShortCommand(CMD_GET_SERIAL, ser, sizeof(ser)) == ESP_OK) {          // get serial number
        {
            #ifdef TWINVERBOSE
                {
                TraceScope trace;                                               // use semaphore to protect this block
                twinPrint("slave answered for CMD_GET_SERIAL: ");
                Serial.print("0x");
                Serial.print(ser[0], HEX);
                Serial.print(" 0x");
                Serial.print(ser[1], HEX);
                Serial.print(" 0x");
                Serial.print(ser[2], HEX);
                Serial.print(" 0x");
                Serial.println(ser[3], HEX);
                }
            #endif
        }
        // compute serialNumber from byte to integer
        parameter.serialnumber = ((uint32_t)ser[0]) | ((uint32_t)ser[1] << 8) | ((uint32_t)ser[2] << 16) | ((uint32_t)ser[3] << 24);
    } else {
        {
            #ifdef ERRORVERBOSE
                TraceScope trace;                                               // use semaphore to protect this block
                twinPrintln("No answer to CMD_GET_SERIAL, slave is not responing, ignore command.");
            #endif
        }
    }

    #ifdef TWINVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        twinPrint("slave answered for parameter.serialnumber = ");
        Serial.println(formatSerialNumber(parameter.serialnumber));
        }
    #endif

    uint8_t off[2] = {0, 0};
    if (i2cShortCommand(CMD_GET_OFFSET, off, sizeof(off)) == ESP_OK) {          // get offset of flap drum
        {
            #ifdef TWINVERBOSE
                {
                TraceScope trace;                                               // use semaphore to protect this block
                twinPrint("slave answered for CMD_GET_OFFSET: ");
                Serial.print("0x");
                Serial.print(off[0], HEX);
                Serial.print(" 0x");
                Serial.println(off[1], HEX);
                }
            #endif
            parameter.offset = 0x100 * off[1] + off[0];                         // compute offset from byte to integer
        }
    } else {
        {
            #ifdef ERRORVERBOSE
                TraceScope trace;                                               // use semaphore to protect this block
                twinPrintln("No answer to CMD_GET_OFFSET, slave is not responing, ignore command.");
            #endif
        }
    }

    #ifdef TWINVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        twinPrint("slave answered for parameter.offset = ");
        Serial.println(parameter.offset);
        }
    #endif

    uint8_t flaps = 0;
    if (i2cShortCommand(CMD_GET_FLAPS, &flaps, sizeof(flaps)) == ESP_OK) {      // get number of flaps
        parameter.flaps = flaps;
        #ifdef TWINVERBOSE
            {
            TraceScope trace;                                                   // use semaphore to protect this block
            twinPrint("slave answered for parameter.flaps = ");
            Serial.println(parameter.flaps);
            }
        #endif
    } else {
        {
            #ifdef ERRORVERBOSE
                TraceScope trace;                                               // use semaphore to protect this block
                twinPrintln("No answer to CMD_GET_FLAPS, slave is not responing, ignore command.");
            #endif
        }
    }

    uint8_t speed[2] = {0, 0};
    if (i2cShortCommand(CMD_GET_SPEED, speed, sizeof(speed)) == ESP_OK) {       // get speed of flap drum
        parameter.speed = speed[1] * 0x100 + speed[0];
        #ifdef TWINVERBOSE
            {
            TraceScope trace;                                                   // use semaphore to protect this block
            twinPrint("slave answered for parameter.speed = ");
            Serial.println(parameter.speed);
            }
        #endif
    } else {
        {
            #ifdef ERRORVERBOSE
                TraceScope trace;                                               // use semaphore to protect this block
                twinPrintln("No answer to CMD_GET_SPEED, slave is not responing, ignore command.");
            #endif
        }
    }

    uint8_t steps[2] = {0, 0};
    if (i2cShortCommand(CMD_GET_STEPS, steps, sizeof(steps)) == ESP_OK) {       // get steps of flap drum
        parameter.steps = steps[1] * 0x100 + steps[0];
        #ifdef TWINVERBOSE
            {
            TraceScope trace;                                                   // use semaphore to protect this block
            twinPrint("slave answered for parameter.steps = ");
            Serial.println(parameter.steps);
            }
        #endif
    } else {
        #ifdef ERRORVERBOSE
            TraceScope trace;                                                   // use semaphore to protect this block
            twinPrint("No answer to CMD_GET_STEPS, slave is not responing, ignore command. ");
            Serial.println(parameter.steps);
        #endif
    }

    uint8_t sensor = 0;
    if (i2cShortCommand(CMD_GET_SENSOR, &sensor, sizeof(sensor)) == ESP_OK) {   // get sensor working status
        parameter.sensorworking = sensor;
        #ifdef TWINVERBOSE
            {
            TraceScope trace;                                                   // use semaphore to protect this block
            twinPrint("slave answered for parameter.sensorworking = ");
            Serial.println(parameter.sensorworking);
            }
        #endif
    } else {
        {
            #ifdef ERRORVERBOSE
                TraceScope trace;                                               // use semaphore to protect this block
                twinPrintln("No answer to CMD_GET_SENSOR, slave is not responing, ignore command.");
            #endif
        }
    }
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

    key = Control.decodeIR(ircode);
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
            Serial.println(Control.key21ToString(key));                         // make it visable
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
    #ifdef TWINVERBOSE
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
// - read data structure slaveReady (structure slaveStatus .read, .taskCode, ...)
// return:
// false  = busy
// true  = ready
// Twin will be updated: slaveRead.ready, .taskCode, .bootFlag, .sensorStatus and .position
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

    return _slaveReady.ready;                                                   // return Twin[n]
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
// - updates Register, if device is known
// - don't register new device
// - updates Registry with parameter handed over as parameter
// variable:
// parameter = parameter to be stored in registry for slave
//
void SlaveTwin::synchSlaveRegistry(slaveParameter parameter) {
    int  oldSteps = parameter.steps;                                            // remember old value of steps per revolution
    int  c        = 0;                                                          // number of calibrated devices
    auto it       = g_slaveRegistry.find(_slaveAddress);                        // search in registry
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
        if (device->parameter.steps != parameter.steps) {
            device->parameter.steps = parameter.steps;                          // update steps per revolution
            #ifdef TWINVERBOSE
                {
                TraceScope trace;
                twinPrint("Device steps per revolution in registry updated of slave 0x%02X to:", _slaveAddress);
                Serial.println(parameter.steps);
                }
            #endif

            calculateStepsPerFlap();                                            // steps per rev. has changed recalculate stepsPerFlap

            #ifdef TWINVERBOSE
                {
                TraceScope trace;
                twinPrint("Steps by Flap are: ");
                for (int i = 0; i < parameter.flaps; ++i) {
                Serial.print(stepsByFlap[i]);
                if (i < parameter.flaps - 1)
                Serial.print(", ");
                }
                Serial.println();
                }
            #endif
        }

        if (device->parameter.speed != parameter.speed) {
            device->parameter.speed = parameter.speed;                          // update speed (time per revolution)
            #ifdef TWINVERBOSE
                {
                TraceScope trace;
                twinPrintln("Device speed in registry updated of slave 0x%02X to: %d", _slaveAddress, parameter.speed);
                }
            #endif
        }

        if (device->parameter.offset != parameter.offset) {
            device->parameter.offset = parameter.offset;                        // update offset
            #ifdef TWINVERBOSE
                {
                TraceScope trace;
                twinPrintln("Device offset in registry updated of slave 0x%02X to: %d", _slaveAddress, parameter.offset);
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
    }
}

// ---------------------------
// Wait until the slave reports ready.
// First: wait one half revolution (parameter.speed ms) without polling.
// Then: poll every 300ms until overall timeout_ms elapses.
// Returns true if the slave became ready within timeout, false otherwise.
//
bool SlaveTwin::waitUntilSlaveReady(uint32_t timeout_ms) {
    uint32_t       start       = millis();
    const uint32_t rotation_ms = _parameter.speed / 2;                          // duration of one half revolution in ms

    // initial wait for one rotation, capped by remaining timeout
    uint32_t elapsed = static_cast<uint32_t>(millis() - start);
    if (elapsed >= timeout_ms) {
        return false;                                                           // no time left
    }
    uint32_t remaining_before_first = timeout_ms - elapsed;
    uint32_t first_wait             = (rotation_ms < remaining_before_first) ? rotation_ms : remaining_before_first;
    vTaskDelay(pdMS_TO_TICKS(first_wait));

    // poll every 500ms until timeout
    const uint32_t poll_interval = 500;
    while (static_cast<uint32_t>(millis() - start) < timeout_ms) {
        if (isSlaveReady()) {
            getFullStateOfSlave();                                              // update slave state
            return true;                                                        // slave is ready
        }

        uint32_t elapsed_now = static_cast<uint32_t>(millis() - start);
        if (elapsed_now >= timeout_ms)
            break;
        uint32_t remaining = timeout_ms - elapsed_now;
        uint32_t delay_ms  = (poll_interval < remaining) ? poll_interval : remaining;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }

    return false;                                                               // timed out waiting for ready
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
