// #################################################################################################################
//
//  ██ ██████   ██████     ███    ███  █████  ███████ ████████ ███████ ██████
//  ██      ██ ██          ████  ████ ██   ██ ██         ██    ██      ██   ██
//  ██  █████  ██          ██ ████ ██ ███████ ███████    ██    █████   ██████
//  ██ ██      ██          ██  ██  ██ ██   ██      ██    ██    ██      ██   ██
//  ██ ███████  ██████     ██      ██ ██   ██ ███████    ██    ███████ ██   ██
//

// ################################################################################################## by Achim ####
// Banner created:
// https://patorjk.com/software/taag/#p=display&c=c%2B%2B&f=ANSI%20Regular&t=I2C%20Master
//
#include <Arduino.h>
#include <FlapGlobal.h>
#include "driver/i2c.h"
#include "MasterPrint.h"
#include "i2cFlap.h"
#include "i2cMaster.h"
#include "FlapTasks.h"
#include "FlapRegistry.h"
#include "SlaveTwin.h"
#include "RtosTasks.h"
#include "FlapStatistics.h"

#define I2C_MASTER_NUM I2C_NUM_0                                                // for IC2 scanner
bool              g_masterBooted = true;                                        // true, until first i2c_scan_bus
SemaphoreHandle_t g_i2c_mutex    = nullptr;                                     // Semaphor to protect access to I2C Bus

// -------------------------
// purpose: setup ics bus for master access
// - introduce PINS for SDA, SCL
// - enable pullup's
// - define bus frequence
// - install bus driver
void i2csetup() {
    if (I2C_MASTER_SDA_IO == -1 || I2C_MASTER_SCL_IO == -1 || I2C_MASTER_SDA_IO == I2C_MASTER_SCL_IO) {
        {
            TraceScope trace;                                                   // use semaphore to protect this block
            Master->systemHalt("FATAL ERROR: Invalid I2C pin configuration!", 1);
        }
    }

    i2c_config_t conf = {.mode          = I2C_MODE_MASTER,
                         .sda_io_num    = I2C_MASTER_SDA_IO,
                         .scl_io_num    = I2C_MASTER_SCL_IO,
                         .sda_pullup_en = GPIO_PULLUP_ENABLE,
                         .scl_pullup_en = GPIO_PULLUP_ENABLE,
                         .master{.clk_speed = I2C_MASTER_FREQ_HZ}};

    esp_err_t err;
    err = i2c_param_config(I2C_NUM_0, &conf);
    if (err != ESP_OK) {
        {
            TraceScope trace;                                                   // use semaphore to protect this block
            masterPrint("FATAL ERROR: i2c configuration failed: ");
            Serial.println(esp_err_to_name(err));
            Master->systemHalt("FATAL ERROR: i2c configuration failed.", 2);
        }
    }

    err = i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0);
    if (err != ESP_OK) {
        {
            TraceScope trace;                                                   // use semaphore to protect this block
            masterPrint("FATAL ERROR: i2c_driver_install failed: ");
            Serial.println(esp_err_to_name(err));
            Master->systemHalt("FATAL ERROR: Driver install failed.", 4);
        }
    }

    // generate Semaphor to protect access to I2C Bus
    g_i2c_mutex = xSemaphoreCreateMutex();
    if (g_i2c_mutex == NULL) {
        {
            TraceScope trace;                                                   // use semaphore to protect this block
            masterPrintln("FATAL ERROR: Failed to create I2C mutex!");
            Master->systemHalt("FATAL ERROR: Failed to create I2C mutex!", 3);
        }
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
LongMessage i2cCommandParameter(uint8_t command, u_int16_t parameter) {
    LongMessage mess = {0x00, 0x00, 0x00};                                      // initial message
    mess.command     = command;                                                 // get i2c command
    mess.lowByte     = parameter & 0xFF;                                        // get i2c parameter lower byte part
    mess.highByte    = (parameter >> 8) & 0xFF;                                 // get i2c parameter higher byte part

    return mess;
}

// ----------------------------
// purpose convert LongMessage to byte buffer, that can be send to i2c bus
// - just memcpy
//
// parameter:
// mess = structure form type LongMessage (size = 3 byte)
// outBuffer = pointer to 3 byte array to get converted ic2FlapMessage
//
void prepareI2Cdata(LongMessage mess, uint8_t slaveAddress, uint8_t* outBuffer) {
    memcpy(outBuffer, &mess, sizeof(LongMessage));                              // transfer i2c message to send buffer
    #ifdef I2CMASTERVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        masterPrint("prepare I2C-LongCommand (prepareI2Cdata) to be sent to slaveAddress 0x");
        Serial.print(slaveAddress, HEX);
        Serial.print(" : 0x");
        for (size_t i = 0; i < sizeof(LongMessage); ++i) {
        Serial.printf("%02X ", outBuffer[i]);
        }
        Serial.println();
        }
    #endif
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
void i2cLongCommand(LongMessage mess, uint8_t slaveAddress) {
    uint8_t data[sizeof(LongMessage)];
    prepareI2Cdata(mess, slaveAddress, data);
    esp_err_t error = ESP_FAIL;

    // take semaphore
    takeI2CSemaphore();

    #ifdef I2CMASTERVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        masterPrint("send LongCommand (i2cLongCommand): 0x");
        Serial.print(data[0], HEX);
        Serial.print(" - ");
        Serial.println(getCommandName(data[0]));
        }
    #endif

    // construct command chain
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();                               // create link to command chain
    i2c_master_start(cmd);                                                      // set i2c start condition
    i2c_master_write_byte(cmd, (slaveAddress << 1) | I2C_MASTER_WRITE, true);   // set i2c address of flap module
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
            masterPrint("ACK from Slave 0x");
            Serial.print(slaveAddress, HEX);
            Serial.println(" received.");
            }
        #endif
    } else {
        #ifdef I2CMASTERVERBOSE
            {
            TraceScope trace;                                                   // use semaphore to protect this block
            masterPrint("Error while sending to 0x");
            Serial.print(slaveAddress, HEX);
            Serial.print(". Errorcode: ");
            Serial.println(esp_err_to_name(error));
            }
        #endif
        if (DataEvaluation)
            DataEvaluation->increment(0, 0, 0, 1);                              // count I2C usage, 1 timeout
    }
}

// ----------------------------
// purpose: send I2C mid command, only one byte
//
esp_err_t i2cMidCommand(MidMessage midCmd, uint8_t slaveAddress, uint8_t* answer, int size) {
    esp_err_t ret = ESP_FAIL;
    takeI2CSemaphore();

    logMidRequest(midCmd, slaveAddress);

    i2c_cmd_handle_t cmd = buildMidCommand(midCmd, slaveAddress, answer, size);
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
i2c_cmd_handle_t buildMidCommand(MidMessage midCmd, uint8_t slaveAddress, uint8_t* answer, int size) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    if (i2c_master_start(cmd) != ESP_OK)
        Master->systemHalt("FATAL ERROR: I2C start failed", 2);

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
void logMidRequest(MidMessage cmd, uint8_t slaveAddress) {
    #ifdef I2CMASTERVERBOSE
        TraceScope trace;
        masterPrint("Send midCommand: 0x");
        Serial.print(cmd.command, HEX);
        Serial.print(" - ");
        Serial.print(getCommandName(cmd.command));
        Serial.print(" to slave 0x");
        Serial.println(slaveAddress, HEX);
    #endif
}

// ---------------------------
// helper to log response to mid command
void logMidResponse(uint8_t* answer, int size) {
    #ifdef I2CMASTERVERBOSE
        TraceScope trace;
        masterPrint("Got answer for midCommand from slave:");
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
void logMidError(MidMessage cmd, esp_err_t err) {
    #ifdef MASTERVERBOSE
        TraceScope trace;
        masterPrint("No answer to midCommand: 0x");
        Serial.print(cmd.command, HEX);
        Serial.print(" - ");
        Serial.println(getCommandName(cmd.command));
        masterPrint("ESP ERROR: ");
        Serial.println(esp_err_to_name(err));
        masterPrintln("Slave is not connected, ignore command.");
    #endif
}

// ----------------------------
// purpose: check if i2c slave is ready or busy
// - access to i2c bus is protected by semaphore
// - read data structure slaveReady (structure slaveStatus .read, .taskCode, .error, .eta)
//
// parameter:
// slaveAddress = i2c address of slave that shall be checked
//
// return:
// n   = Twin Number
// -1  = no Twin found
// -2  = device not connected
// Twin with slaveAddress is update slaveRead.read, .taskCode, .bootFlag, .sensorStatus and .position
//
int check_slaveReady(uint8_t slaveAddress) {
    uint8_t data[6] = {0, 0, 0, 0, 0, 0};
    #ifdef READYBUSYVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        masterPrint("readyness/busyness check of slave 0x");
        Serial.println(slaveAddress, HEX);
        }
    #endif
    bool found = false;
    int  a     = 0;
    while (a < numberOfTwins) {                                                 // is slaveAddress valide (content of adress pool)
        if (g_slaveAddressPool[a] == slaveAddress) {
            found = true;
            break;
        }
        a++;
    }

    if (!found || Twin[a] == nullptr) {                                         // is there a valide Twin[] Object available that can communicte with slave
    #ifdef READYBUSYVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        masterPrint("corresponding twin object not found or not created for slave: 0x");
        Serial.println(slaveAddress, HEX);
        }
    #endif
        return -1;                                                              // return no Twin found
    }

    #ifdef READYBUSYVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        masterPrint("corresponding twin found: Twin[%d] fits to slave: 0x", a);
        Serial.println(slaveAddress, HEX);
        masterPrint("sending now STATE request to slave: 0x");
        Serial.println(slaveAddress, HEX);
        }
    #endif

    if (Twin[a]->i2cShortCommand(CMD_GET_STATE, data, sizeof(data)) != ESP_OK) { // send  request to Slave
    #ifdef READYBUSYVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        masterPrint("(check_slaveReady) shortCommand STATE failed to: 0x");
        Serial.println(slaveAddress, HEX);
        }
    #endif
        return -2;                                                              // twin not connected
    }
    updateSlaveReadyInfo(a, slaveAddress, data);                                // Update Slave-Status
    #ifdef READYBUSYVERBOSE
        printSlaveReadyInfo(Twin[a]);
    #endif

    return a;                                                                   // return Twin[n]
}

// --------------------------
// update position to registry
void updateSlaveReadyInfo(int n, uint8_t address, uint8_t* data) {
    if (data == nullptr || n < 0 || n >= numberOfTwins || Twin[n] == nullptr) {
        masterPrintln("Invalid input for updateSlaveReadyInfo: Twin[%d] or data is null", n);
        return;
    }

    // update twin status
    Twin[n]->slaveReady.ready        = data[0];
    Twin[n]->slaveReady.taskCode     = data[1];
    Twin[n]->slaveReady.bootFlag     = data[2];
    Twin[n]->slaveReady.sensorStatus = data[3];
    Twin[n]->slaveReady.position     = data[5] * 0x100 + data[4];               // MSB + LSB

    // update crrresponding regisry entry
    auto it = g_slaveRegistry.find(address);                                    // search in registry
    if (it != g_slaveRegistry.end() && it->second != nullptr) {
        I2CSlaveDevice* device = it->second;
        if (device->position != Twin[n]->slaveReady.position) {
            device->position = Twin[n]->slaveReady.position;                    // update Flap position in registry
            #ifdef MASTERVERBOSE
                TraceScope trace;
                {
                Twin[n]->twinPrintln("Updated position for address 0x%02X → %d", address, Twin[n]->slaveReady.position);
                }
            #endif
        }
    }
}

// --------------------------
// trace slave answer to STATUS
void printSlaveReadyInfo(SlaveTwin* twin) {
    #ifdef MASTERVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        masterPrint("slaveReady.ready = ");
        Serial.println(twin->slaveReady.ready ? "TRUE" : "FALSE");
        //
        masterPrint("slaveReady.taskCode 0x");
        Serial.print(twin->slaveReady.taskCode, HEX);
        Serial.print(twin->slaveReady.ready ? " - ready with " : " - busy with ");
        Serial.println(getCommandName(twin->slaveReady.taskCode));
        //
        masterPrint("slaveReady.bootFlag = ");
        Serial.println(twin->slaveReady.bootFlag ? "TRUE" : "FALSE");
        //
        masterPrint("slaveReady.sensorStatus = ");
        Serial.println(twin->slaveReady.sensorStatus ? "WORKING" : "BROKEN");
        //
        masterPrint("slaveReady.position = ");
        Serial.println(twin->slaveReady.position);
        //
        masterPrint("");
        Serial.println(twin->slaveReady.ready ? "Slave is ready" : "Slave is busy, ignore command");
        }
    #endif
}

// --------------------------
// semaphore protected ping
esp_err_t i2c_probe_device(uint8_t address) {
    if (takeI2CSemaphore()) {
        esp_err_t ret = pingI2Cslave(address);
        giveI2CSemaphore();
        return ret;
    } else {
        #ifdef I2CMASTERVERBOSE
            {
            TraceScope trace;                                                   // use semaphore to protect this block
            masterPrintln("request for I²C access failed (i2c_probe_device)");
            }
        #endif

        return ESP_FAIL;                                                        // get semaphore error
    }
}

// --------------------------
// take Semaphore
bool takeI2CSemaphore() {
    int retry = 5;
    while (retry > 0) {
        if (xSemaphoreTake(g_i2c_mutex, pdMS_TO_TICKS(200))) {
            #ifdef SEMAPHOREVERBOSE
                {
                TraceScope trace;                                               // use semaphore to protect this block
                masterPrintln("Semaphore erfolgreich genommen (takeI2CSemaphore)");
                }
            #endif
            return true;
        } else {
            #ifdef SEMAPHOREVERBOSE
                {
                TraceScope trace;                                               // use semaphore to protect this block
                masterPrintln("Semaphore nicht bekommen (takeI2CSemaphore)");
                }
            #endif
            vTaskDelay(100 / portTICK_PERIOD_MS);
            retry--;
        }
    }
    return false;
}

// --------------------------
// give Semaphore
bool giveI2CSemaphore() {
    if (xSemaphoreGive(g_i2c_mutex)) {
        #ifdef SEMAPHOREVERBOSE
            {
            TraceScope trace;                                                   // use semaphore to protect this block
            masterPrintln("Semaphore erfolgreich freigegeben (giveI2CSemaphore)");
            }
        #endif
        return true;
    } else {
        #ifdef SEMAPHOREVERBOSE
            {
            TraceScope trace;                                                   // use semaphore to protect this block
            masterPrintln("Semaphore Freigabe gescheitert (giveI2CSemaphore)");
            }
        #endif
        return false;
    }
}

// --------------------------
// request ACK from slave
esp_err_t pingI2Cslave(u_int8_t address) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();                               // I2C command buffer
    i2c_master_start(cmd);                                                      // Set start signal
    i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE,
                          true);                                                // I2C Write mode and ACK expected from slave
    i2c_master_stop(cmd);                                                       // Set stop signal
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(100)); // send command buffer to slave
    i2c_cmd_link_delete(cmd);                                                   // delete command buffer

    if (DataEvaluation)
        DataEvaluation->increment(1, 0);                                        // 1 access, 0 byte data, 0 read

    if (ret == ESP_OK) {
        #ifdef PINGVERBOSE
            {
            TraceScope trace;                                                   // use semaphore to protect this block
            masterPrint("ping successful (pingI2Cslave) with 0x");
            Serial.println(address, HEX);
            }
        #endif
    } else {
        #ifdef PINGVERBOSE
            {
            TraceScope trace;                                                   // use semaphore to protect this block
            masterPrint("ping failed (pingI2Cslave) with 0x");
            Serial.println(address, HEX);
            }
            if (DataEvaluation && address != I2C_BASE_ADDRESS)
            DataEvaluation->increment(0, 0, 0, 1);                              // count I2C usage, 1 timeout
        #endif
    }
    return ret;
}
