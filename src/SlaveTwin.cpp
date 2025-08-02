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
#include "RtosTasks.h"
#include "FlapRegistry.h"
#include "SlaveTwin.h"
#include "FlapTasks.h"
#include "RemoteControl.h"

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
}

SlaveTwin* Twin[numberOfTwins];

// ----------------------------
void SlaveTwin::showFlap(char digit) {
    #ifdef MASTERVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        twinPrint("showFlap: digit ");
        Serial.println(digit);
        }
    #endif

    targetFlapNumber = searchSign(digit);                                       // sign position in flapFont is number of targetFlap

    #ifdef I2CMASTERVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        twinPrint("showFlap: targetFlapNumber ");
        Serial.println(targetFlapNumber);
        }
    #endif

    if (targetFlapNumber < 0) {                                                 // search sign content of flapFont
    #ifdef I2CMASTERVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        twinPrint(" Flap unknown ...");
        Serial.println(digit);
        }
    #endif
    } else {
        int steps = countStepsToMove(flapNumber, targetFlapNumber);
        if (steps > 0) {
            int n = check_slaveReady(slaveAddress);
            if (n > -1) {
                i2cLongCommand(i2cCommandParameter(MOVE, steps), slaveAddress);
                flapNumber = targetFlapNumber;                                  // I assume it's okay

                bool moveOver = false;
                while (!moveOver) {
                    vTaskDelay(pdMS_TO_TICKS(400));                             // Delay for 400ms
                    check_slaveReady(slaveAddress);                             // request new position from slave
                    moveOver = slaveReady.ready;
                }
            }
        }
    }
}

// ----------------------------
void SlaveTwin::Calibrate() {
    if (check_slaveReady(slaveAddress) > -1) {
        if (parameter.steps > 0)                                                // happend a step-messurement?
            i2cLongCommand(i2cCommandParameter(CALIBRATE, parameter.steps),
                           slaveAddress);                                       // use result of measurement
        else
            i2cLongCommand(i2cCommandParameter(CALIBRATE, DEFAULT_STEPS), slaveAddress); // use default steps

        flapNumber = 0;
    }
}

// ----------------------------
void SlaveTwin::stepMeasurement() {
    int n = check_slaveReady(slaveAddress);                                     // check if slave is ready
    if (n < 0)
        return;                                                                 // slave not ready, ignore
    i2cLongCommand(i2cCommandParameter(STEP_MEASSURE, 0), slaveAddress);
    flapNumber = 0;                                                             // we stand at Zero after that

    int r = 5;
    while (check_slaveReady(slaveAddress) < 0 && r-- > 0) {                     // waiting for step meassurement is done
        vTaskDelay(710 / portTICK_PERIOD_MS);                                   // Delay for 710 milliseconds
    }
    if (r <= 0) {
        #ifdef MASTERVERBOSE
            twinPrintln("Step meassurement failed or timed out on slave 0x%02X", slaveAddress);
        #endif
        return;
    }
    Register->updateSlaveRegistry(n, slaveAddress, parameter);                  // take over measured value to registry
    #ifdef MASTERVERBOSE
        twinPrintln("Device steps per revolution in registry updated of slave 0x%02X to: %d", slaveAddress, parameter.steps);
    #endif
}

// ----------------------------
void SlaveTwin::speedMeasurement() {
    int n = check_slaveReady(slaveAddress);                                     // check if slave is ready
    if (n < 0)
        return;                                                                 // slave not ready, ignore
    uint16_t stepsToCheck = (parameter.steps > 0) ? parameter.steps : DEFAULT_STEPS; // has a step measurement been performed before?
    i2cLongCommand(i2cCommandParameter(SPEED_MEASSURE, parameter.steps), slaveAddress);

    int r = 5;
    while (check_slaveReady(slaveAddress) < 0 && r-- > 0) {                     // waiting for speed meassurement is done
        vTaskDelay(710 / portTICK_PERIOD_MS);                                   // Delay for 710 milliseconds
    }
    if (r <= 0) {
        #ifdef MASTERVERBOSE
            twinPrintln("Speed meassurement failed or timed out on slave 0x%02X", slaveAddress);
        #endif
        return;
    }
    Register->updateSlaveRegistry(n, slaveAddress, parameter);                  // take over measured value to registry
    #ifdef MASTERVERBOSE
        twinPrintln("Device speed in registry updated of slave 0x%02X to: %d", slaveAddress, parameter.speed);
    #endif
}

// ----------------------------
void SlaveTwin::sensorCheck() {
    int n = check_slaveReady(slaveAddress);                                     // check if slave is ready
    if (n < 0)
        return;                                                                 // slave not ready, ignore
    uint16_t stepsToCheck = (parameter.steps > 0) ? parameter.steps : DEFAULT_STEPS; // has a step measurement been performed before?
    i2cLongCommand(i2cCommandParameter(SENSOR_CHECK, parameter.steps), slaveAddress); // do sensor check

    int r = 5;
    while (check_slaveReady(slaveAddress) < 0 && r-- > 0) {                     // waiting for sensor check is done
        vTaskDelay(710 / portTICK_PERIOD_MS);                                   // Delay for 710 milliseconds
    }
    if (r <= 0) {
        #ifdef MASTERVERBOSE
            twinPrintln("Sensor check failed or timed out on slave 0x%02X", slaveAddress);
        #endif
        return;
    }
    Register->updateSlaveRegistry(n, slaveAddress, parameter);                  // take over measured value to registry
    #ifdef MASTERVERBOSE
        twinPrintln("Sensor status in registry updated of slave 0x%02X to: %d", slaveAddress, slaveReady.sensorStatus);
    #endif
}

// ----------------------------
void SlaveTwin::nextFlap() {
    if (numberOfFlaps > 0) {
        targetFlapNumber = flapNumber + 1;
        if (targetFlapNumber >= numberOfFlaps)
            targetFlapNumber = 0;
        int steps = countStepsToMove(flapNumber, targetFlapNumber);
        if (steps > 0) {
            if (check_slaveReady(slaveAddress) > -1) {
                i2cLongCommand(i2cCommandParameter(MOVE, steps), slaveAddress);
                flapNumber    = targetFlapNumber;
                bool moveOver = false;
                while (!moveOver) {
                    vTaskDelay(pdMS_TO_TICKS(200));                             // Delay for 400ms
                    check_slaveReady(slaveAddress);                             // request new position from slave
                    moveOver = slaveReady.ready;
                }
            }
        }
    } else {
        #ifdef MASTERVERBOSE
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
            if (check_slaveReady(slaveAddress) > -1) {
                i2cLongCommand(i2cCommandParameter(MOVE, steps), slaveAddress);
                flapNumber    = targetFlapNumber;
                bool moveOver = false;
                while (!moveOver) {
                    vTaskDelay(pdMS_TO_TICKS(1000));                            // Delay for 1000ms
                    check_slaveReady(slaveAddress);                             // request new position from slave
                    moveOver = slaveReady.ready;
                }
            }
        }
    } else {
        #ifdef MASTERVERBOSE
            twinPrintln("number of Flaps not set");
        #endif
    }
}

// ----------------------------
void SlaveTwin::nextSteps() {
    if (check_slaveReady(slaveAddress) > -1) {
        i2cLongCommand(i2cCommandParameter(MOVE, ADJUSTMENT_STEPS), slaveAddress);
        adjustOffset += ADJUSTMENT_STEPS;
        twinPrintln("actual stored offset = %d + adjustment steps = %d: %d", parameter.offset, adjustOffset, parameter.offset + adjustOffset);
    }
}

// ----------------------------
void SlaveTwin::prevSteps() {
    if (adjustOffset >= ADJUSTMENT_STEPS)
        adjustOffset -= ADJUSTMENT_STEPS;
    twinPrintln("actual stored offset = %d + adjustment steps = %d: %d", parameter.offset, adjustOffset, parameter.offset + adjustOffset);
}

// ----------------------------
void SlaveTwin::setOffset() {
    parameter.offset += adjustOffset;                                           // add adjustment to stored offset
    adjustOffset = 0;                                                           // reset adjustement to zero
    twinPrintln("save: offset = %d | ms/Rev = %d | St/Rev = %d", parameter.offset, parameter.speed, parameter.steps);
    int n = check_slaveReady(slaveAddress);
    if (n > -1) {
        Register->updateSlaveRegistry(n, slaveAddress, parameter);              // take over new offset to registry
        i2cLongCommand(i2cCommandParameter(SET_OFFSET, parameter.offset), slaveAddress);
    }
}

// ----------------------------
void SlaveTwin::reset() {
    twinPrint("send reset to 0x:  ");
    Serial.println(slaveAddress, HEX);
    if (check_slaveReady(slaveAddress) > -1) {
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
        #ifdef MASTERVERBOSE
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
//
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
        Master->systemHalt("FATAL ERROR: I2C start failed", 3);

    if (i2c_master_write_byte(cmd, (slaveAddress << 1) | I2C_MASTER_WRITE, true) != ESP_OK)
        Master->systemHalt("FATAL ERROR: write address failed", 3);

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
    #ifdef MASTERVERBOSE
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
void SlaveTwin::initStepsByFlap() {
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
