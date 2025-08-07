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
    numberOfFlaps           = 0;                                                // unknown, will be set by device
    slaveAddress            = add;                                              // take over from funktion call
    parameter.flaps         = 0;                                                // init all parameter
    parameter.offset        = 0;
    parameter.sensorworking = false;
    parameter.serialnumber  = 0;
    parameter.slaveaddress  = add;
    parameter.speed         = 0;
    parameter.steps         = 0;
    adjustOffset            = 0;                                                // init set offset variable for adjusting calibration
    slaveReady.bootFlag     = false;
    slaveReady.position     = 0;
    slaveReady.ready        = false;
    slaveReady.sensorStatus = false;
    slaveReady.taskCode     = NO_COMMAND;
    twinQueue               = nullptr;
}

// ----------------------------
// read entry queue
void SlaveTwin::readQueue() {
    ClickEvent receivedEvent;
    if (twinQueue != nullptr) {                                                 // if queue exists
        if (xQueueReceive(twinQueue, &receivedEvent, portMAX_DELAY)) {
            if (receivedEvent.key != Key21::NONE) {
                twinControl(receivedEvent);                                     // send corresponding Flap-Command to device
            }
        }
    }
}

// ----------------------------
// create entry queue
void SlaveTwin::createQueue() {
    twinQueue = xQueueCreate(1, sizeof(ClickEvent));                            // Create twin Queue
}

// ----------------------------
// send entry queue
void SlaveTwin::sendQueue(ClickEvent receivedEvent) {
    if (twinQueue != nullptr) {                                                 // if queue exists, send to all registered twin queues
        xQueueOverwrite(twinQueue, &receivedEvent);
        #ifdef TWINVERBOSE
            {
            TraceScope trace;                                                   // use semaphore to protect this block
            twinPrintln("send ClickEvent.type: %s to slave", Control.clickTypeToString(receivedEvent.type));
            twinPrintln("send Received Key21: %s to slave", Control.key21ToString(receivedEvent.key));
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
void SlaveTwin::twinControl(ClickEvent event) {
    if (event.type == CLICK_SINGLE) {
        handleSingleKey(event.key);
    }
    if (event.type == CLICK_DOUBLE) {
        handleDoubleKey(event.key);
    }
}

// --------------------------------------
void SlaveTwin::handleDoubleKey(Key21 key) {}

// --------------------------------------
void SlaveTwin::handleSingleKey(Key21 key) {
    switch (key) {
        case Key21::KEY_CH_MINUS:
            logAndRun("Send Step Measurement...", [=] { stepMeasurement(); });
            break;
        case Key21::KEY_CH:
            logAndRun("Send Calibration...", [=] { Calibrate(); });
            break;
        case Key21::KEY_CH_PLUS:
            logAndRun("Send Speed Measurement...", [=] { speedMeasurement(); });
            break;
        case Key21::KEY_PREV:
            logAndRun("Send Prev Steps...", [=] { prevSteps(); });
            break;
        case Key21::KEY_NEXT:
            logAndRun("Send Next Steps...", [=] { nextSteps(); });
            break;
        case Key21::KEY_PLAY_PAUSE:
            logAndRun("Send Save Offset...", [=] { setOffset(); });
            break;
        case Key21::KEY_VOL_MINUS:
            logAndRun("Send Previous Flap...", [=] { prevFlap(); });
            break;
        case Key21::KEY_VOL_PLUS:
            logAndRun("Send Next Flap...", [=] { nextFlap(); });
            break;
        case Key21::KEY_EQ:
            logAndRun("Send Sensor Check...", [=] { sensorCheck(); });
            break;
        case Key21::KEY_200_PLUS:
            logAndRun("Send RESET to Slave...", [=] { reset(); });
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
            char        digit = key21ToDigit(key);
            std::string msg   = "Send ";
            msg += digit;
            msg += "...";
            logAndRun(msg.c_str(), [=] { showFlap(digit); });
            break;
        }

        default: {
            #ifdef ERRORVERBOSE
                {
                TraceScope trace;
                twinPrint("Unknown remote control key: ");
                Serial.println(static_cast<int>(key));
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

char SlaveTwin::key21ToDigit(Key21 key) {
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

// ----------------------------
// show selected digit
void SlaveTwin::showFlap(char digit) {
    targetFlapNumber = searchSign(digit);                                       // sign position in flapFont is number of targetFlap
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

    int steps = countStepsToMove(flapNumber, targetFlapNumber);                 // get steps to move
    if (steps > 0) {
        if (isSlaveReady()) {
            i2cLongCommand(i2cCommandParameter(MOVE, steps), slaveAddress);
            flapNumber = targetFlapNumber;                                      // I assume it's okay
            if (!waitUntilSlaveReady(parameter.speed)) {                        // wait for slave to get ready; maximum time of one revolutions
                {
                    #ifdef ERRORVERBOSE
                        twinPrintln("showFlap failed or timed out on slave 0x%02X", slaveAddress);
                    #endif
                    return;
                }
            }
            #ifdef TWINVERBOSE
                twinPrintln("request result of showFlap");
            #endif
            askSlaveAboutParameter(slaveAddress, parameter);                    // get result of showFlap
            synchSlaveRegistry(parameter);                                      // take over position to registry
        }
    }
}

// ----------------------------
void SlaveTwin::Calibrate() {
    if (isSlaveReady()) {
        if (parameter.steps > 0) {                                              // happend a step-messurement?
            i2cLongCommand(i2cCommandParameter(CALIBRATE, parameter.steps),
                           slaveAddress);                                       // use result of measurement
        } else {
            i2cLongCommand(i2cCommandParameter(CALIBRATE, DEFAULT_STEPS), slaveAddress); // use default steps
        }
        flapNumber = 0;                                                         // we stand at Zero after that
        if (!waitUntilSlaveReady(2 * parameter.speed)) {                        // wait for slave to get ready; maximum time of 2 revolutions
            {
                #ifdef ERRORVERBOSE
                    twinPrintln("Calibration failed or timed out on slave 0x%02X", slaveAddress);
                #endif
                return;
            }
        }
        #ifdef TWINVERBOSE
            twinPrintln("request result of calibration");
        #endif
        askSlaveAboutParameter(slaveAddress, parameter);                        // get result of measurement
        synchSlaveRegistry(parameter);                                          // take over measured value to registry
    }
}

// ----------------------------
void SlaveTwin::stepMeasurement() {
    if (!isSlaveReady())
        return;                                                                 // slave not ready, ignore
    i2cLongCommand(i2cCommandParameter(STEP_MEASSURE, 0), slaveAddress);
    flapNumber = 0;                                                             // we stand at Zero after that
    if (!waitUntilSlaveReady(15 * parameter.speed)) {                           // wait for slave to get ready; maximum time of 15 revolutions
        {
            #ifdef ERRORVERBOSE
                twinPrintln("Step meassurement failed or timed out on slave 0x%02X", slaveAddress);
            #endif
            return;
        }
    }
    #ifdef TWINVERBOSE
        twinPrintln("request result of step measurement");
    #endif
    askSlaveAboutParameter(slaveAddress, parameter);                            // get result of measurement
    synchSlaveRegistry(parameter);                                              // take over measured value to registry
}

// ----------------------------
void SlaveTwin::speedMeasurement() {
    if (!isSlaveReady())
        return;                                                                 // slave not ready, ignore
    uint16_t stepsToCheck = (parameter.steps > 0) ? parameter.steps : DEFAULT_STEPS; // has a step measurement been performed before?
    i2cLongCommand(i2cCommandParameter(SPEED_MEASSURE, parameter.steps), slaveAddress);
    if (!waitUntilSlaveReady(2 * parameter.speed)) {                            // wait for slave to get ready; maximum time of 2 revolutions
        {
            #ifdef ERRORVERBOSE
                twinPrintln("Speed meassurement failed or timed out on slave 0x%02X", slaveAddress);
            #endif
            return;
        }
    }
    #ifdef TWINVERBOSE
        twinPrintln("request result of speed measurement");
    #endif
    askSlaveAboutParameter(slaveAddress, parameter);                            // get result of measurement
    synchSlaveRegistry(parameter);                                              // take over measured value to registry
}

// ----------------------------
void SlaveTwin::sensorCheck() {
    if (!isSlaveReady())
        return;                                                                 // slave not ready, ignore

    uint16_t stepsToCheck = (parameter.steps > 0) ? parameter.steps : DEFAULT_STEPS; // has a step measurement been performed before?
    i2cLongCommand(i2cCommandParameter(SENSOR_CHECK, parameter.steps), slaveAddress); // do sensor check
    if (!waitUntilSlaveReady(2 * parameter.speed)) {                            // wait for slave to get ready; maximum time of 2 revolutions
        {
            #ifdef ERRORVERBOSE
                twinPrintln("Sensor check failed or timed out on slave 0x%02X", slaveAddress);
            #endif
            return;
        }
    }
    #ifdef TWINVERBOSE
        twinPrintln("request result of sensor check");
    #endif
    askSlaveAboutParameter(slaveAddress, parameter);                            // get result of sensor
    synchSlaveRegistry(parameter);                                              // take over measured value to registry
}

// ----------------------------
void SlaveTwin::nextFlap() {
    if (numberOfFlaps > 0) {
        targetFlapNumber = flapNumber + 1;
        if (targetFlapNumber >= numberOfFlaps)
            targetFlapNumber = 0;
        int steps = countStepsToMove(flapNumber, targetFlapNumber);
        if (steps > 0) {
            if (isSlaveReady()) {
                i2cLongCommand(i2cCommandParameter(MOVE, steps), slaveAddress);
                flapNumber = targetFlapNumber;
                if (!waitUntilSlaveReady(parameter.speed)) {                    // wait for slave to get ready; maximum time 1/2 revelution
                    {
                        #ifdef ERRORVERBOSE
                            twinPrintln("Next Flap failed or timed out on slave 0x%02X", slaveAddress);
                        #endif
                        return;
                    }
                }
                #ifdef TWINVERBOSE
                    twinPrintln("request result of next flap");
                #endif
                askSlaveAboutParameter(slaveAddress, parameter);                // get result of next flap
                synchSlaveRegistry(parameter);                                  // take over position to registry
            }
        }
    } else {
        #ifdef ERRORVERBOSE
            twinPrintln("number of Flaps not set");
        #endif
    }
}

// ----------------------------
void SlaveTwin::prevFlap() {
    if (numberOfFlaps > 0) {
        targetFlapNumber = flapNumber - 1;
        if (targetFlapNumber < 0)
            targetFlapNumber = numberOfFlaps - 1;
        int steps = countStepsToMove(flapNumber, targetFlapNumber);
        if (steps > 0) {
            if (isSlaveReady()) {
                i2cLongCommand(i2cCommandParameter(MOVE, steps), slaveAddress);
                flapNumber = targetFlapNumber;
                if (!waitUntilSlaveReady(2 * parameter.speed)) {                // wait for slave to get ready; maximum time of2 revolutions
                    {
                        #ifdef ERRORVERBOSE
                            twinPrintln("Previous Flap failed or timed out on slave 0x%02X", slaveAddress);
                        #endif
                        return;
                    }
                }
                #ifdef TWINVERBOSE
                    twinPrintln("request result of previous flap");
                #endif
                askSlaveAboutParameter(slaveAddress, parameter);                // get result of prev flap
                synchSlaveRegistry(parameter);                                  // take over position to registry
            }
        }
    } else {
        #ifdef ERRORVERBOSE
            twinPrintln("number of Flaps not set");
        #endif
    }
}

// ----------------------------
void SlaveTwin::nextSteps() {
    if (isSlaveReady()) {
        if (parameter.offset + adjustOffset + ADJUSTMENT_STEPS <= parameter.steps) { // only as positiv, as to be stepped to one revolutoin
            i2cLongCommand(i2cCommandParameter(MOVE, ADJUSTMENT_STEPS), slaveAddress);
            adjustOffset += ADJUSTMENT_STEPS;
            twinPrintln("to be stored offset: %d (not yet stored)", parameter.offset + adjustOffset);
        }
    }
}

// ----------------------------
void SlaveTwin::prevSteps() {
    if (parameter.offset + adjustOffset - ADJUSTMENT_STEPS >= 0) {              // only as negativ, as to be stepped back to zero
        adjustOffset -= ADJUSTMENT_STEPS;
        twinPrintln("to be stored offset: %d (not yet stored)", parameter.offset + adjustOffset);
    }
}

// ----------------------------
void SlaveTwin::setOffset() {
    if (parameter.offset + adjustOffset >= 0 && parameter.offset + adjustOffset <= parameter.steps) {
        parameter.offset += adjustOffset;                                       // add adjustment to stored offset
    }
    if (parameter.offset < 0 || parameter.offset > parameter.steps)
        parameter.offset = 0;                                                   // reset parameter.offset to zero
    adjustOffset = 0;                                                           // reset adjustement to zero
    twinPrintln("reset adjustment offset to 0");
    twinPrintln("save: offset = %d | ms/Rev = %d | St/Rev = %d", parameter.offset, parameter.speed, parameter.steps);
    if (isSlaveReady()) {
        synchSlaveRegistry(parameter);                                          // take over new offset to registry
        i2cLongCommand(i2cCommandParameter(SET_OFFSET, parameter.offset), slaveAddress);
    }
}

// ----------------------------
void SlaveTwin::reset() {
    twinPrint("send reset to 0x:  ");
    Serial.println(slaveAddress, HEX);
    if (isSlaveReady()) {
        Register->deregisterSlave(slaveAddress);                                // delete slave from registry, this address is free now
        i2cLongCommand(i2cCommandParameter(RESET, 0), slaveAddress);            // send reset to slave
    }
}

// ----------------------------
int SlaveTwin::searchSign(char digit) {
    if (numberOfFlaps > 0) {
        for (int i = 0; i < numberOfFlaps; i++) {
            if (digit == flapFont[i])
                return i;                                                       // digit in flaps found
        }
        return -1;                                                              // digit not found
    } else {
        #ifdef ERRORVERBOSE
            twinPrintln("number of Flaps not set");
        #endif
        return -1;                                                              // digit not found
    }
}

// ----------------------------
int SlaveTwin::countStepsToMove(int from, int to) {
    if (numberOfFlaps > 0) {
        int sum = 0;
        int pos = from;
        while (pos != to) {
            sum += stepsByFlap[pos];                                            // count steps to go from flap t0 flap
            pos = (pos + 1) % numberOfFlaps;
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
// purpose: send I2C short command, only one byte
esp_err_t SlaveTwin::i2cShortCommand(ShortMessage shortCmd, uint8_t* answer, int size) {
    esp_err_t ret = ESP_FAIL;
    takeI2CSemaphore();

    logShortRequest(shortCmd);

    i2c_cmd_handle_t cmd = buildShortCommand(shortCmd, answer, size);
    ret                  = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(200));
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

    if (i2c_master_write_byte(cmd, (slaveAddress << 1) | I2C_MASTER_WRITE, true) != ESP_OK)
        systemHalt("FATAL ERROR: write address failed", 3);

    i2c_master_write_byte(cmd, shortCmd, true);

    i2c_master_start(cmd);                                                      // repeated start
    i2c_master_write_byte(cmd, (slaveAddress << 1) | I2C_MASTER_READ, true);
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
        Serial.println(slaveAddress, HEX);
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
void SlaveTwin::askSlaveAboutParameter(uint8_t address, slaveParameter& parameter) {
    // ask Flap about actual parameter
    uint8_t ser[4] = {0, 0, 0, 0};
    i2cShortCommand(CMD_GET_SERIAL, ser, sizeof(ser));
    // compute serialNumber from byte to integer
    parameter.serialnumber = ((uint32_t)ser[0]) | ((uint32_t)ser[1] << 8) | ((uint32_t)ser[2] << 16) | ((uint32_t)ser[3] << 24);

    #ifdef REGISTRYVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        twinPrint("slave answered for parameter.serialnumber = ");
        Serial.println(formatSerialNumber(parameter.serialnumber));
        }
    #endif

    uint8_t off[2] = {0, 0};
    i2cShortCommand(CMD_GET_OFFSET, off, sizeof(off));
    parameter.offset = 0x100 * off[1] + off[0];                                 // compute offset from byte to integer

    #ifdef REGISTRYVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        twinPrint("slave answered for parameter.offset = ");
        Serial.println(parameter.offset);
        }
    #endif

    uint8_t flaps = 0;
    i2cShortCommand(CMD_GET_FLAPS, &flaps, sizeof(flaps));                      // get number of flaps
    parameter.flaps = flaps;
    #ifdef REGISTRYVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        twinPrint("slave answered for parameter.flaps = ");
        Serial.println(parameter.flaps);
        }
    #endif

    uint8_t speed[2] = {0, 0};
    i2cShortCommand(CMD_GET_SPEED, speed, sizeof(speed));                       // get speed of flap drum
    parameter.speed = speed[1] * 0x100 + speed[0];
    #ifdef REGISTRYVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        twinPrint("slave answered for parameter.speed = ");
        Serial.println(parameter.speed);
        }
    #endif

    uint8_t steps[2] = {0, 0};
    i2cShortCommand(CMD_GET_STEPS, steps, sizeof(steps));                       // get steps of flap drum
    parameter.steps = steps[1] * 0x100 + steps[0];
    #ifdef REGISTRYVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        twinPrint("slave answered for parameter.steps = ");
        Serial.println(parameter.steps);
        }
    #endif

    uint8_t sensor = 0;
    i2cShortCommand(CMD_GET_SENSOR, &sensor, sizeof(sensor));                   // get sensor working status
    parameter.sensorworking = sensor;
    #ifdef REGISTRYVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        twinPrint("slave answered for parameter.sensorworking = ");
        Serial.println(parameter.sensorworking);
        }
    #endif
}

// ----------------------------
// compute steps needed to move flap by flap (Bresenham-artige Verteilung)
void SlaveTwin::calculateStepsPerFlap() {
    for (int i = 0; i < parameter.flaps; ++i) {
        int start      = (i * parameter.steps) / parameter.flaps;
        int end        = ((i + 1) * parameter.steps) / parameter.flaps;
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
    if (ircode == lastTwinCode && (now - lastTwinTime) < 100) {
        return Key21::NONE;
    }

    key = Control.decodeIR(ircode);
    if ((int)key < (int)Key21::NONE || (int)key > (int)Key21::UNKNOWN) {
        lastTwinTime = now;
        lastTwinCode = ircode;
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
        lastTwinTime = now;
        lastTwinCode = ircode;
        return key;
    }
    lastTwinTime = now;
    lastTwinCode = ircode;
    return Key21::NONE;                                                         // filter unknown to NONE
}

// ----------------------------
// purpose: check if i2c slave is ready or busy
// - access to i2c bus is protected by semaphore
// - read data structure slaveReady (structure slaveStatus .read, .taskCode, ...)
//
//
// return:
// false  = busy
// true  = ready
// Twin will update: slaveRead.ready, .taskCode, .bootFlag, .sensorStatus and .position
//
bool SlaveTwin::isSlaveReady() {
    uint8_t data[6] = {0, 0, 0, 0, 0, 0};                                       // structure to receive answer for STATE
    #ifdef READYBUSYVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        twinPrint("readyness/busyness check of slave 0x");
        Serial.println(slaveAddress, HEX);
        }
    #endif

    if (i2cShortCommand(CMD_GET_STATE, data, sizeof(data)) != ESP_OK) {         // send  request to Slave
    #ifdef ERRORVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        twinPrint("(check_slaveReady) shortCommand STATE failed to: 0x");
        Serial.println(slaveAddress, HEX);
        }
    #endif
        return false;                                                           // twin not connected
    }

    updateSlaveReadyInfo(data);                                                 // Update Slave-Status
    #ifdef READYBUSYVERBOSE
        printSlaveReadyInfo();
    #endif

    return slaveReady.ready;                                                    // return Twin[n]
}

// --------------------------
//
void SlaveTwin::updateSlaveReadyInfo(uint8_t* data) {
    slaveReady.ready        = data[0];
    slaveReady.taskCode     = data[1];
    slaveReady.bootFlag     = data[2];
    slaveReady.sensorStatus = data[3];
    slaveReady.position     = data[5] * 0x100 + data[4];                        // MSB + LSB
    /*
        auto it = g_slaveRegistry.find(slaveAddress);                           // search in registry
        if (it != g_slaveRegistry.end() && it->second != nullptr) {
            I2CSlaveDevice* device = it->second;
            if (device->position != slaveReady.position) {
                device->position = slaveReady.position;                         // update Flap position in registry
                #ifdef MASTERVERBOSE
                    TraceScope trace;
                    {
                    twinPrintln("Updated position for address 0x%02X → %d", slaveAddress, slaveReady.position);
                    }
                #endif
            }
       }
    */
}

// --------------------------
//
void SlaveTwin::printSlaveReadyInfo() {
    #ifdef READYBUSYVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        twinPrint("slaveReady.ready = ");
        Serial.println(slaveReady.ready ? "TRUE" : "FALSE");
        //
        twinPrint("slaveReady.taskCode 0x");
        Serial.print(slaveReady.taskCode, HEX);
        Serial.print(slaveReady.ready ? " - ready with " : " - busy with ");
        Serial.println(getCommandName(slaveReady.taskCode));
        //
        twinPrint("slaveReady.bootFlag = ");
        Serial.println(slaveReady.bootFlag ? "TRUE" : "FALSE");
        //
        twinPrint("slaveReady.sensorStatus = ");
        Serial.println(slaveReady.sensorStatus ? "WORKING" : "BROKEN");
        //
        twinPrint("slaveReady.position = ");
        Serial.println(slaveReady.position);
        //
        twinPrint("");
        Serial.println(slaveReady.ready ? "Slave is ready" : "Slave is busy and ignored your command");
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
    auto it       = g_slaveRegistry.find(slaveAddress);                         // search in registry
    if (it != g_slaveRegistry.end() && it->second != nullptr) {                 // fist check if device is registered

        I2CSlaveDevice* device = it->second;

        if (device->position != slaveReady.position) {
            device->position = slaveReady.position;                             // update Flap position
            #ifdef TWINVERBOSE
                {
                TraceScope trace;
                twinPrint("Device position in registry updated of slave 0x%02X to: ", slaveAddress);
                Serial.println(slaveReady.position);
                }
            #endif
        }
        if (device->parameter.steps != parameter.steps) {
            device->parameter.steps = parameter.steps;                          // update steps per revolution
            #ifdef TWINVERBOSE
                {
                TraceScope trace;
                twinPrint("Device steps per revolution in registry updated of slave 0x%02X to:", slaveAddress);
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
                twinPrintln("Device speed in registry updated of slave 0x%02X to: %d", slaveAddress, parameter.speed);
                }
            #endif
        }

        if (device->parameter.offset != parameter.offset) {
            device->parameter.offset = parameter.offset;                        // update offset
            #ifdef TWINVERBOSE
                {
                TraceScope trace;
                twinPrintln("Device offset in registry updated of slave 0x%02X to: %d", slaveAddress, parameter.offset);
                }
            #endif
        }

        if (device->parameter.sensorworking != slaveReady.sensorStatus) {
            device->parameter.sensorworking = slaveReady.sensorStatus;          // update Sensor status
            #ifdef TWINVERBOSE
                {
                TraceScope trace;
                twinPrintln("Device sensor status in registry updated of slave 0x%02X to: %d", slaveAddress, slaveReady.sensorStatus);
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
    const uint32_t rotation_ms = parameter.speed / 2;                           // duration of one half revolution in ms

    // initial wait for one rotation, capped by remaining timeout
    uint32_t elapsed = static_cast<uint32_t>(millis() - start);
    if (elapsed >= timeout_ms) {
        return false;                                                           // no time left
    }
    uint32_t remaining_before_first = timeout_ms - elapsed;
    uint32_t first_wait             = (rotation_ms < remaining_before_first) ? rotation_ms : remaining_before_first;
    vTaskDelay(pdMS_TO_TICKS(first_wait));

    // poll every 300ms until timeout
    const uint32_t poll_interval = 300;
    while (static_cast<uint32_t>(millis() - start) < timeout_ms) {
        if (isSlaveReady()) {
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
