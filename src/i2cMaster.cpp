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

/**
 * @brief setup ics bus for master access
 * introduce PINS for SDA, SCL
 *
 * enable pullup's
 *
 * define bus frequence
 *
 * install bus driver
 */
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
    err = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (err != ESP_OK) {
        {
            TraceScope trace;                                                   // use semaphore to protect this block
            masterPrint("FATAL ERROR: i2c configuration failed: ");
            Serial.println(esp_err_to_name(err));
            Master->systemHalt("FATAL ERROR: i2c configuration failed.", 2);
        }
    }

    err = i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
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
// purpose
// - just memcpy
//
// parameter:
// mess =
// outBuffer =
//
/**
 * @brief convert LongMessage to byte buffer, that can be send to i2c bus
 *
 * @param mess structure form type LongMessage (size = 3 byte)
 * @param outBuffer pointer to 3 byte array to get converted ic2FlapMessage
 */
void prepareI2Cdata(LongMessage mess, uint8_t* outBuffer) {
    memcpy(outBuffer, &mess, sizeof(LongMessage));                              // transfer i2c message to send buffer
    #ifdef I2CMASTERVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        masterPrintln("prepare LongMessage to be sent");
        }
    #endif
}

// --------------------------

/**
 * @brief trace slave answer to STATUS
 *
 * @param twin
 */

void printSlaveReadyInfo(SlaveTwin* twin) {
    #ifdef MASTERVERBOSE
        {
        TraceScope trace;                                                       // use semaphore to protect this block
        masterPrint("_slaveReady.ready = ");
        Serial.println(twin->_slaveReady.ready ? "TRUE" : "FALSE");
        //
        masterPrint("_slaveReady.taskCode 0x");
        Serial.print(twin->_slaveReady.taskCode, HEX);
        Serial.print(twin->_slaveReady.ready ? " - ready with " : " - busy with ");
        Serial.println(getCommandName(twin->_slaveReady.taskCode));
        //
        masterPrint("_slaveReady.bootFlag = ");
        Serial.println(twin->_slaveReady.bootFlag ? "TRUE" : "FALSE");
        //
        masterPrint("_slaveReady.sensorStatus = ");
        Serial.println(twin->_slaveReady.sensorStatus ? "WORKING" : "BROKEN");
        //
        masterPrint("_slaveReady.position = ");
        Serial.println(twin->_slaveReady.position);
        //
        masterPrint("");
        Serial.println(twin->_slaveReady.ready ? "Slave is ready" : "Slave is busy, ignore command");
        }
    #endif
}

// --------------------------

/**
 * @brief semaphore protected ping
 *
 * @param address
 * @return esp_err_t
 */
esp_err_t i2c_probe_device(I2Caddress address) {
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
//

/**
 * @brief take Semaphore for I2C access
 *
 * @return true
 * @return false
 */
bool takeI2CSemaphore() {
    if (xSemaphoreTake(g_i2c_mutex, pdMS_TO_TICKS(50))) {
        #ifdef SEMAPHOREVERBOSE
            {
            TraceScope trace;                                                   // use semaphore to protect this block
            masterPrintln("Semaphore erfolgreich genommen (takeI2CSemaphore)");
            }
        #endif
        return true;                                                            // success taken semaphore
    } else {
        #ifdef SEMAPHOREVERBOSE
            {
            TraceScope trace;                                                   // use semaphore to protect this block
            masterPrintln("Semaphore nicht bekommen (takeI2CSemaphore)");
            }
        #endif
        return false;                                                           // no semaphore
    }
}

// --------------------------

/**
 * @brief give Semaphore to release I2C access
 *
 * @return true
 * @return false
 */
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

/**
 * @brief request ACK from slave (ping)
 *
 * @param address
 * @return esp_err_t
 */
esp_err_t pingI2Cslave(I2Caddress address) {
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
            Serial.print(address, HEX);
            Serial.print(" esp-error = ");
            Serial.println(esp_err_to_name(ret));
            }
            if (DataEvaluation && address != I2C_BASE_ADDRESS)
            DataEvaluation->increment(0, 0, 0, 1);                              // count I2C usage, 1 timeout
        #endif
    }
    return ret;
}
