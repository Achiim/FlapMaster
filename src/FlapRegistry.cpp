// #################################################################################################################
//
//  ███████ ██       █████  ██████      ██████  ███████  ██████  ██ ███████ ████████ ██████  ██    ██
//  ██      ██      ██   ██ ██   ██     ██   ██ ██      ██       ██ ██         ██    ██   ██  ██  ██
//  █████   ██      ███████ ██████      ██████  █████   ██   ███ ██ ███████    ██    ██████    ████
//  ██      ██      ██   ██ ██          ██   ██ ██      ██    ██ ██      ██    ██    ██   ██    ██
//  ██      ███████ ██   ██ ██          ██   ██ ███████  ██████  ██ ███████    ██    ██   ██    ██
//
//
// ##################################################################################################
// by Achim #### Banner created:
// https://patorjk.com/software/taag/#p=display&c=c%2B%2B&f=ANSI%20Regular&t=flap%20Registry
//
#include <Arduino.h>
#include <FlapGlobal.h>
#include "driver/i2c.h"
#include <map>
#include <freertos/FreeRTOS.h>                                                  // Real Time OS
#include <freertos/timers.h>                                                    // Real Time OS time
#include "MasterPrint.h"
#include "FlapRegistry.h"
#include "RtosTasks.h"
#include "FlapTasks.h"
#include "SlaveTwin.h"
#include "i2cMaster.h"

// ----------------------------------
// Data Structure to register FlapDevices
std::map<I2Caddress, I2CSlaveDevice*> g_slaveRegistry;

// -----------------------------------
// check if all registered slaves are still available, if not -> deregister
// will be called cyclic by Registry Task with the help of a countdown timer
// function:
// - will ping registered all devices
//
// if device answers:
//   - ask device about bootFlag
//   - reset bootFlag
//   - calibrate device
//
// if device does not answer:
//   - deregister this device
//
void FlapRegistry::availabilityCheck() {
    for (auto it = g_slaveRegistry.begin(); it != g_slaveRegistry.end();) {
        const I2Caddress addr = it->first;

        #ifdef AVAILABILITYVERBOSE
            {
            TraceScope trace;
            registerPrint("check availability of registered I²C-Slave 0x");
            Serial.println(addr, HEX);
            }
        #endif
        // esp_err_t ret = i2c_probe_device(addr);                                 // send ping to device/slave

        #ifdef AVAILABILITYVERBOSE
            {
            TraceScope trace;
            registerPrint("send TWIN_AVAILABILITY to twin 0x");
            Serial.println(addr, HEX);
            }
        #endif

        TwinCommand twinCmd;
        twinCmd.twinCommand   = TWIN_AVAILABILITY;                              // set command to check availablility
        twinCmd.twinParameter = 0;                                              // no parameter
        twinCmd.responsQueue  = nullptr;                                        // no response requested
        int n                 = findTwinIndexByAddress(addr);                   // get twin index from address
        Twin[n]->sendQueue(twinCmd);                                            // send command to Twin[n] to calibrate
        ++it;                                                                   // next registry entry
    }
}

// ----------------------------
// erase slave from registry
void FlapRegistry::deregisterSlave(I2Caddress slaveAddress) {
    if (!g_slaveRegistry.empty()) {
        g_slaveRegistry.erase(slaveAddress);
    }
}

// ----------------------------
// Helper: finde Twin-Index zu gegebener I2C-Adresse, oder -1 wenn keiner passt
int FlapRegistry::findTwinIndexByAddress(I2Caddress addr) {
    for (int s = 0; s < numberOfTwins; ++s) {
        if (Twin[s] && Twin[s]->_slaveAddress == addr) {
            return s;
        }
    }
    return -1;
}

// ----------------------------
// Scan one slave on I2C bus.
// Returns:
//   >=0 : index of corresponding twin
//   -1  : slave not found / not ready / twin missing
int FlapRegistry::scanForSlave(int poolIndex, I2Caddress addr) {
    #ifdef SCANVERBOSE
        {
        TraceScope trace;
        registerPrint("Scanning address pool entry #");
        Serial.print(poolIndex);
        Serial.print(": slave 0x");
        Serial.println(addr, HEX);
        }
    #endif

    esp_err_t pingResult = i2c_probe_device(addr);                              // Ping slave and wait for ACK
    if (pingResult == ESP_OK) {
        #ifdef SCANVERBOSE
            {
            TraceScope trace;
            registerPrint("Slave is present on I2C bus: 0x");
            Serial.println(addr, HEX);
            }
        #endif
    } else {
        #ifdef SCANVERBOSE
            {
            TraceScope trace;
            registerPrint("No slave responded on I2C bus at address: 0x");
            Serial.println(addr, HEX);
            }
            return -1;
        #endif
    }

    int twinIndex = findTwinIndexByAddress(addr);
    #ifdef SCANVERBOSE
        {
        TraceScope trace;
        registerPrint("Twin number resolved to: ");
        Serial.println(twinIndex);
        }
    #endif

    if (twinIndex < 0) {
        return -1;                                                              // no matching twin
    }
    return twinIndex;
}

// ------------------------------------
// Registry lookup (for diagnostics)
bool FlapRegistry::registered(I2Caddress addr) {
    auto it = g_slaveRegistry.find(addr);
    if (it != g_slaveRegistry.end() && it->second != nullptr) {
        #ifdef SCANVERBOSE
            {
            TraceScope trace;
            registerPrint("Slave already registered at address 0x");
            Serial.println(addr, HEX);
            }
        #endif
        return true;
    }
    return false;
}

// ----------------------------
// messages to introduce scan_i2c_bus
void FlapRegistry::deviceRegistryIntro() {
    #ifdef SCANVERBOSE
        {
        TraceScope trace;
        registerPrintln("Start I²C-Scan to register new slaves...");
        }
    #endif

    if (g_masterBooted) {
        #ifdef SCANVERBOSE
            {
            TraceScope trace;
            registerPrintln("System-Reset detected → Master has restarted");
            registerPrintln("all slaves detected during I²C-bus-scan will be calibrated...");
            }
        #endif
    }
}

// ----------------------------
// messages to leave scan_i2c_bus
void FlapRegistry::deviceRegistryOutro() {
    if (g_masterBooted) {
        g_masterBooted = false;                                                 // master boot is over, reset master has booted
    }

    #ifdef SCANVERBOSE
        {
        TraceScope trace;
        registerPrintln("I²C-Scan complete.");
        }
    #endif
}

// ----------------------------
// Ask I2C Bus who is there, and register unknown slaves
void FlapRegistry::deviceRegistry() {
    I2Caddress addr;                                                            // slave addres
    deviceRegistryIntro();                                                      // inform about what is happening
    for (int i = 0; i < numberOfTwins; i++) {
        addr = g_slaveAddressPool[i];                                           // get address from address pool
        if (!registered(addr)) {
            TwinCommand twinCmd;
            twinCmd.twinCommand   = TWIN_REGISTER;                              // set command register twin device
            twinCmd.twinParameter = 0;                                          // no parameter
            twinCmd.responsQueue  = nullptr;                                    // no response requested
            int n                 = findTwinIndexByAddress(addr);               // get twin index from address
            Twin[n]->sendQueue(twinCmd);                                        // send command to Twin[n] to calibrate
        }
    }
    deviceRegistryOutro();                                                      // goodbye messages
}

// ------------------------------------------
// check if slave has booted and waits for calibration
//
bool FlapRegistry::checkSlaveHasBooted(int n, I2Caddress address) {
    #ifdef SCANVERBOSE
        {
        TraceScope trace;
        registerPrint("check bootFlag status of slave 0x");
        Serial.println(address, HEX);
        }
    #endif
    uint8_t ans = 0;
    Twin[n]->i2cShortCommand(CMD_GET_BOOT_FLAG, &ans, sizeof(ans));             // check if bootFlag of slave is set
    Twin[n]->_slaveReady.bootFlag = ans;                                        // transfer to Twin[n]

    if (Twin[n]->_slaveReady.bootFlag) {
        #ifdef SCANVERBOSE
            {
            TraceScope trace;
            registerPrint("reset bootFlag on rebooted Slave 0x");
            Serial.println(address, HEX);
            }
        #endif

        uint8_t ans = 0;
        Twin[n]->i2cShortCommand(CMD_RESET_BOOT, &ans, sizeof(ans));            // send reset bootFlag to slave

        #ifdef SCANVERBOSE
            {
            TraceScope trace;
            registerPrint("slave has rebooted; calibrating now Slave 0x");
            Serial.println(address, HEX);
            }
        #endif

        TwinCommand twinCmd;
        twinCmd.twinCommand = TWIN_CALIBRATION;                                 // set command to calibrate

        #ifdef SCANVERBOSE
            {
            TraceScope trace;                                                   // use semaphore to protect this block
            registerPrintln("send TwinCommand: %s to TWIN", Parser->twinCommandToString(twinCmd.twinCommand));
            }
        #endif

        Twin[n]->sendQueue(twinCmd);                                            // send command to Twin[n] to calibrate
        vTaskDelay(pdMS_TO_TICKS(4000));                                        // Delay for 4s
        return true;                                                            // bootFlag resetted and calibration started
    } else {
        #ifdef SCANVERBOSE
            {
            TraceScope trace;
            registerPrint("bootFlag is allready resetted for Slave  0x");
            Serial.println(address, HEX);
            }
        #endif
        return false;                                                           // bootFlag allready resetted
    }
}

// ----------------------------
// purpose:
// - register device as new if not allready content of registry
// - updates Register, if device is known
// - updates Registry with parameter handed over as parameter
// variable:
// address = address of slave to be registerd/updated
// parameter = parameter to be stored in registry for slave
//
// return = number of calibrated devices
//
int FlapRegistry::updateSlaveRegistry(int n, I2Caddress address, slaveParameter parameter) {
    bool slaveIsNew         = false;                                            // flag, if slave is new
    int  calibrationCounter = 0;                                                // number of calibrated devices
    auto it                 = g_slaveRegistry.find(address);                    // search in registry
    if (it != g_slaveRegistry.end() && it->second != nullptr) {                 // fist check if device is registered
        slaveIsNew                      = false;                                // twin is allready regisered
        I2CSlaveDevice* device          = it->second;
        device->parameter               = parameter;                            // update all parameter
        device->position                = Twin[n]->_slaveReady.position;        // update Flap position
        device->bootFlag                = Twin[n]->_slaveReady.bootFlag;        // update bootFlag
        device->parameter.sensorworking = Twin[n]->_slaveReady.sensorStatus;    // update Sensor status
        const char* sensorStatus        = device->parameter.sensorworking ? "WORKING" : "BROKEN";

        #ifdef SCANVERBOSE
            {
            TraceScope trace;
            registerPrint("Registry updated for known Slave 0x");
            Serial.print(address, HEX);
            Serial.print(" in position = ");
            Serial.print(device->position);
            Serial.print(" and sensor status = ");
            Serial.println(sensorStatus);
            }
        #endif

    } else {
        // Device is new → register
        slaveIsNew                         = true;                              // new slave detected
        I2CSlaveDevice* newDevice          = new I2CSlaveDevice();              // create new device
        newDevice->parameter               = parameter;                         // set all parameter
        newDevice->bootFlag                = Twin[n]->_slaveReady.bootFlag;     // set bootFlag
        newDevice->position                = Twin[n]->_slaveReady.position;     // set flap position
        newDevice->parameter.sensorworking = Twin[n]->_slaveReady.sensorStatus; // set Sensor status
        const char* sensorStatus           = newDevice->parameter.sensorworking ? "WORKING" : "BROKEN";

        #ifdef SCANVERBOSE
            {
            TraceScope trace;
            registerPrint("Registered new Slave 0x");
            Serial.print(address, HEX);
            Serial.print(" in position = ");
            Serial.print(newDevice->position);
            Serial.print(" and sensor status = ");
            Serial.println(sensorStatus);
            }
        #endif
        g_slaveRegistry[address] = newDevice;                                   // register new device
    }

    checkSlaveHasBooted(n, address);                                            // slave comes again with reboot
    if (n >= 0 && slaveIsNew) {                                                 // only if slave is ready
        Twin[n]->_parameter = parameter;                                        // update all twin parameter

        #ifdef REGISTRYVERBOSE
            {
            TraceScope trace;
            registerPrint("take over parameter values from slave to his twin on master side 0x");
            Serial.println(address, HEX);
            registerPrint("number of Steps = %d for Slave 0x", parameter.steps);
            Serial.println(address, HEX);
            }
        #endif

        if (Twin[n]->_numberOfFlaps != parameter.flaps ||
            Twin[n]->_parameter.steps != parameter.steps) {                     // recompute steps by flaps based non new step measurement from slave

            #ifdef REGISTRYVERBOSE
                {
                TraceScope trace;
                registerPrint("number of Flaps = %d for Slave 0x", parameter.flaps);
                Serial.println(address, HEX);
                registerPrint("compute new Steps by Flap array for Slave 0x");
                Serial.println(address, HEX);
                }
            #endif
            Twin[n]->_numberOfFlaps = parameter.flaps;
            Twin[n]->calculateStepsPerFlap();                                   // number of flaps or steps per rev. has changed recalculate

            #ifdef REGISTRYVERBOSE
                {
                TraceScope trace;
                registerPrint("Steps by Flap are: ");
                for (int i = 0; i < parameter.flaps; ++i) {
                Serial.print(Twin[n]->stepsByFlap[i]);
                if (i < parameter.flaps - 1)
                Serial.print(", ");
                }
                Serial.println();
                }
            #endif
        }
    }

    if (g_masterBooted) {                                                       // if Master booted
        {
            #ifdef REGISTRYVERBOSE
                {
                TraceScope trace;
                registerPrint("master has rebooted: calibrating now Slave 0x");
                Serial.println(address, HEX);
                }
            #endif
        }
        calibrationCounter++;                                                   // counting calibrations
        TwinCommand twinCmd;
        twinCmd.twinCommand = TWIN_CALIBRATION;                                 // set command to calibrate
        Twin[n]->sendQueue(twinCmd);
        #ifdef REGISTRYVERBOSE
            {
            TraceScope trace;                                                   // use semaphore to protect this block
            registerPrintln("send TwinCommand: %s to TWIN", Parser->twinCommandToString(twinCmd.twinCommand));
            }
        #endif

        #ifdef REGISTRYVERBOSE
            {
            TraceScope trace;
            registerPrint("set internal adjustment step counter to %d for rebooted Slave 0x", Twin[n]->_adjustOffset);
            Serial.println(address, HEX);
            }
        #endif
    }
    return calibrationCounter;                                                  // number of calibrated devices
}

// -----------------------------------------
// get next free ic2 address from registry
I2Caddress FlapRegistry::getNextAddress() {
    I2Caddress nextAddress = findFreeAddress(I2C_MINADR, I2C_MINADR + numberOfTwins - 1); // find address in range
    if (nextAddress > 0) {                                                      // valid address found
        return nextAddress;                                                     // deliver next free address
    } else {
        #ifdef ERRORVERBOSE
            {
            TraceScope trace;
            registerPrintln("no more I2C addresses available");
            }
        #endif
        return -1;                                                              // indicate no more free address
    }
}

// ------------------------------
// find free address in registry
// return = one free address between minAddr to maxAddr
// return = 0, if no address is free
I2Caddress FlapRegistry::findFreeAddress(I2Caddress minAddr, I2Caddress maxAddr) { //  I²C Range for slaves
    for (I2Caddress addr = minAddr; addr <= maxAddr; ++addr) {
        if (g_slaveRegistry.find(addr) == g_slaveRegistry.end()) {
            return addr;                                                        // free address found
        }
    }
    return 0;                                                                   // no free addess found
}

// ---------------------------------
// find number of registered devices
int FlapRegistry::numberOfRegisterdDevices() {
    return g_slaveRegistry.size();
}

// ---------------------------------
// repairOutOfPoolDevices
// This function is used to repair devices that are out of the address pool.
void FlapRegistry::repairOutOfPoolDevices() {
    I2Caddress nextFreeAddress = 0;
    #ifdef SCANVERBOSE
        {
        TraceScope trace;
        registerPrintln("Repairing devices out of address pool...");
        }
    #endif

    for (I2Caddress ii = I2C_MINADR + numberOfTwins; ii <= I2C_MINADR + numberOfTwins; ii++) { // iterate over all address out of pool
        if (i2c_probe_device(ii) == ESP_OK) {                                   // is there someone out of range?
            nextFreeAddress = getNextAddress();                                 // get next free address for out of range slaves
            if (nextFreeAddress >= I2C_MINADR && nextFreeAddress <= I2C_MAXADR) { // if address is valide
                {
                    #ifdef SCANVERBOSE
                        {
                        TraceScope trace;
                        registerPrint("Slave 0x");
                        Serial.print(ii, HEX);
                        Serial.println(" is out of address pool, repairing...");
                        }
                    #endif
                }
                MidMessage midCmd;                                              // take over new address from command
                midCmd.command   = CMD_NEW_ADDRESS;                             // set command to send new address
                midCmd.paramByte = nextFreeAddress;                             // set new address
                uint8_t answer[4];
                Twin[0]->i2cMidCommand(midCmd, ii, answer, sizeof(answer));     // send command to Twin[0] to set new address

                {
                    TraceScope trace;                                           // use semaphore to protect this block
                    #ifdef SCANVERBOSE
                        registerPrint("Out of range slave will now be reset to I2C address: 0x");
                        Serial.println(nextFreeAddress, HEX);
                    #endif
                }
            }
        }
    }
}

// ------------------------------
// register unregistered devices
void FlapRegistry::registerUnregistered() {
    I2Caddress nextFreeAddress = 0;
    if (i2c_probe_device(I2C_BASE_ADDRESS) == ESP_OK) {                         // is there someone new?
        nextFreeAddress = getNextAddress();                                     // get next free address for new slaves

        if (nextFreeAddress >= I2C_MINADR && nextFreeAddress <= I2C_MAXADR) {   // if address is valide
            {
                TraceScope trace;                                               // use semaphore to protect this block
                #ifdef SCANVERBOSE
                    registerPrint("Send to unregistered slave (with I2C_BASE_ADDRESS) next free I2C Address: 0x");
                    Serial.println(nextFreeAddress, HEX);
                #endif
            }

            TwinCommand twinCmd;
            twinCmd.twinCommand   = TWIN_NEW_ADDRESS;                           // set command to send base address
            twinCmd.twinParameter = nextFreeAddress;                            // set new base address
            Twin[0]->sendQueue(twinCmd);                                        // send command to Twin[0] to set base address
            #ifdef REGISTRYVERBOSE
                {
                TraceScope trace;                                               // use semaphore to protect this block
                registerPrintln("send TwinCommand: %s to TWIN[0]", Parser->twinCommandToString(twinCmd.twinCommand));
                }
            #endif
            {
                TraceScope trace;                                               // use semaphore to protect this block
                #ifdef SCANVERBOSE
                    registerPrint("Unregistered slave will now be registered with I2C address: 0x");
                    Serial.println(nextFreeAddress, HEX);
                #endif
            }
        }
    }
}