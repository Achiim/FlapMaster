// ###################################################################################################
//
//  ███████ ██       █████  ██████      ███    ███  █████  ███████ ████████ ███████ ██████
//  ██      ██      ██   ██ ██   ██     ████  ████ ██   ██ ██         ██    ██      ██   ██
//  █████   ██      ███████ ██████      ██ ████ ██ ███████ ███████    ██    █████   ██████
//  ██      ██      ██   ██ ██          ██  ██  ██ ██   ██      ██    ██    ██      ██   ██
//  ██      ███████ ██   ██ ██          ██      ██ ██   ██ ███████    ██    ███████ ██   ██
//
//
// ################################################################################################## by Achim ####
// Banner created:
// https://patorjk.com/software/taag/#p=display&c=c%2B%2B&f=ANSI%20Regular&t=FLAP%20MASTER
/*
  Split Flap Display Master with remote control

  Feature:
    - controls as I2C Master all connected flap devices (I2C slave devices)
    - variable number of flap devices
    - startup protocol to enable plup&play in connecting new flap devices on the fly
    - robust and failure tollerant
    - I2C statistics with history to be able to analyse I2C traffic
    - flap device registration
    - task status reporting
*/

#include <Arduino.h>
#include <FlapGlobal.h>
#include "i2cMaster.h"
#include "MasterPrint.h"
#include "MasterSetup.h"
#include "Liga.h"
#include "esp_bt.h"

/**
 * @brief General setup to start ESP32
 *
 */
void setup() {
    traceSemaphore = xSemaphoreCreateMutex();                                   // Semaphore for trace messages
    g_masterBooted = true;                                                      // true, until first scan_i2c_bus

    masterIntroduction();                                                       // Wellcome to the world
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) {
        Serial.println("Bluetooth ist aktiv!");
    } else {
        Serial.println("Bluetooth ist deaktiviert.");
        esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);
    }
    masterAddressPool();                                                        // define I2C addresses
    masterI2Csetup();                                                           // introduce me as I2C Master
    masterRemoteControl();                                                      // generate remote control object
    masterSlaveControlObject();                                                 // generate control objects
    masterStartRtosTasks();                                                     // start RTOS tasks to do the master job
    masterOutrodution();                                                        // finish message of startup
}

/**
 * @brief General Loop for ESP32
 * empty, because everything will be done in background
 *
 */
void loop() {}                                                                  // do everything in background by tasks
