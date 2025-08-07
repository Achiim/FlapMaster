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
#include <map>
#include <freertos/FreeRTOS.h>                                                  // Real Time OS
#include <freertos/timers.h>                                                    // Real Time OS time
#include "MasterPrint.h"
#include "TracePrint.h"
#include "FlapRegistry.h"
#include "RtosTasks.h"
#include "SlaveTwin.h"
#include "driver/i2c.h"
#include "i2cMaster.h"

// ----------------------------------
// Data Structure to register FlapDevices
std::map<uint8_t, I2CSlaveDevice*> g_slaveRegistry;

// -----------------------------------
// check if all registerd slaves are still available, if not deregister
void FlapRegistry::check_slave_availability() {
    for (auto it = g_slaveRegistry.begin(); it != g_slaveRegistry.end();) {
        const uint8_t addr = it->first;

        #ifdef REGISTRYVERBOSE
            {
            TraceScope trace;
            registerPrint("check availability of registered I²C-Slave 0x");
            Serial.println(addr, HEX);
            }
        #endif
        esp_err_t ret = i2c_probe_device(addr);                                 // send ping to device/slave
        if (ret != ESP_OK) {
            #ifdef REGISTRYVERBOSE
                {
                TraceScope trace;
                registerPrint("registered I²C-Slave not available -> will deregister it 0x");
                Serial.println(addr, HEX);
                }
            #endif
            delete it->second;
            it = g_slaveRegistry.erase(it);                                     // maybee don't delete at first unavailability
        } else {
            #ifdef REGISTRYVERBOSE
                {
                TraceScope trace;
                registerPrint("registered I2C-Slave is available: 0x");
                Serial.println(addr, HEX);
                }
            #endif
            int n = findTwinIndexByAddress(addr);                               // corresponding twin for new slave
            if (n >= 0)
                checkSlaveHasBooted(n, addr);
            ++it;                                                               // next registry entry
        }
    }
}

// ----------------------------
// erase slave from registry
void FlapRegistry::deregisterSlave(uint8_t slaveAddress) {
    if (!g_slaveRegistry.empty()) {
        g_slaveRegistry.erase(slaveAddress);
    }
}

// ----------------------------
// messages to introduce scan_i2c_bus
void FlapRegistry::intro_scan_i2c() {
    #ifdef MASTERVERBOSE
        {
        TraceScope trace;
        registerPrintln("Start I²C-Scan to register new slaves...");
        }
    #endif

    if (g_masterBooted) {
        #ifdef MASTERVERBOSE
            {
            TraceScope trace;
            registerPrintln("System-Reset detected → Master has restarted");
            registerPrintln("all slaves detected druring I²C-bus-scan will be calibrated...");
            }
        #endif
    }
}

// ----------------------------
// Helper: finde Twin-Index zu gegebener I2C-Adresse, oder -1 wenn keiner passt
int FlapRegistry::findTwinIndexByAddress(uint8_t addr) {
    for (int s = 0; s < numberOfTwins; ++s) {
        if (Twin[s] && Twin[s]->slaveAddress == addr) {
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
int FlapRegistry::scanForSlave(int poolIndex, uint8_t addr) {
    #ifdef MASTERVERBOSE
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
        #ifdef REGISTRYVERBOSE
            {
            TraceScope trace;
            registerPrint("Slave is present on I2C bus: 0x");
            Serial.println(addr, HEX);
            }
        #endif
    } else {
        #ifdef REGISTRYVERBOSE
            {
            TraceScope trace;
            registerPrint("No slave responded on I2C bus at address: 0x");
            Serial.println(addr, HEX);
            }
            return -1;
        #endif
    }

    int twinIndex = findTwinIndexByAddress(addr);
    #ifdef REGISTRYVERBOSE
        {
        TraceScope trace;
        registerPrint("Twin number resolved to: ");
        Serial.println(twinIndex);
        }
    #endif

    if (twinIndex < 0) {
        return -1;                                                              // no matching twin
    }

    // Registry lookup (for diagnostics)
    auto it = g_slaveRegistry.find(addr);
    if (it != g_slaveRegistry.end() && it->second != nullptr) {
        #ifdef REGISTRYVERBOSE
            {
            TraceScope trace;
            registerPrint("Slave already registered at address 0x");
            Serial.println(addr, HEX);
            }
        #endif
    }

    return twinIndex;
}

// ----------------------------
// messages to leave scan_i2c_bus
void FlapRegistry::outro_scan_i2c(int foundToCalibrate, int foundToRegister) {
    if (g_masterBooted) {
        g_masterBooted = false;                                                 // master boot is over, reset master has booted
        #ifdef MASTERVERBOSE
            {
            TraceScope trace;
            registerPrintln("Master-Setup complete -> number of calibrated devices: %d", foundToCalibrate);
            }
        #endif
    }

    #ifdef REGISTRYVERBOSE
        {
        TraceScope trace;
        registerPrintln("number of registered devices: %d", foundToRegister);
        registerPrintln("I²C-Scan complete.");
        }
    #endif
}
// ----------------------------
// error tracing for one slave on i2c bus
void FlapRegistry::error_scan_i2c(int n, uint8_t addr) {
    #ifdef REGISTRYVERBOSE
        {
        TraceScope trace;
        if (n == -1) {
        registerPrint("no salve twin task with addr 0x");
        Serial.println(addr, HEX);                                              // no twin
        for (int a = 0; a < numberOfTwins; a++) {
        registerPrint("Master I2C-Address Pool# %d: 0x", a);
        Serial.println(g_slaveAddressPool[a], HEX);                             // address pool
        }
        } else {
        if (n == -2) {
        registerPrint("salve not reachable with addr 0x");
        Serial.println(addr, HEX);                                              // no slave reachable
        }
        }
        }
    #endif
}

// ----------------------------
// Ask I2C Bus who is there, and register unknown slaves
void FlapRegistry::scan_i2c_bus() {
    int     foundToRegister  = 0;                                               // counter for registered slaves
    int     foundToCalibrate = 0;                                               // counter for calibrated slaves
    int     n                = -1;                                              // Twin[n]
    int     c                = 0;                                               // calibrations counted
    uint8_t addr;                                                               // slave addres

    slaveParameter parameter;                                                   // to ask slave about his parameters
    intro_scan_i2c();                                                           // inform about what is happening

    for (int i = 0; i < numberOfTwins; i++) {
        addr = g_slaveAddressPool[i];                                           // get address from address pool
        n    = scanForSlave(i, addr);                                           // scan for slave i on i2c bus and get Twin[n]
        if (n < 0) {
            error_scan_i2c(n, addr);                                            // trace scan error messages
        } else {
            foundToRegister++;
            Twin[n]->askSlaveAboutParameter(addr, parameter);                   // get actual parameter from
            c = updateSlaveRegistry(n, addr, parameter);                        // register slave
            foundToCalibrate += c;                                              // count calibrations
        }
    }

    outro_scan_i2c(foundToCalibrate, foundToRegister);                          // goodbye messages
}

// ------------------------------------------
// check if slave has booted and waits for calibration
//
bool FlapRegistry::checkSlaveHasBooted(int n, uint8_t address) {
    uint8_t ans = 0;

    #ifdef REGISTRYVERBOSE
        {
        TraceScope trace;
        registerPrint("check bootFlag status of slave 0x");
        Serial.println(address, HEX);
        }
    #endif
    Twin[n]->i2cShortCommand(CMD_GET_BOOT_FLAG, &ans, sizeof(ans));             // check if bootFlag of slave is set
    Twin[n]->slaveReady.bootFlag = ans;                                         // transfer to Twin[n]

    if (Twin[n]->slaveReady.bootFlag) {
        #ifdef MASTERVERBOSE
            {
            TraceScope trace;
            registerPrint("reset bootFlag on rebooted Slave 0x");
            Serial.println(address, HEX);
            }
        #endif

        uint8_t ans = 0;
        Twin[n]->i2cShortCommand(CMD_RESET_BOOT, &ans, sizeof(ans));            // send reset bootFlag to slave

        #ifdef MASTERVERBOSE
            {
            TraceScope trace;
            registerPrint("slave has rebooted calibrating now Slave 0x");
            Serial.println(address, HEX);
            }
        #endif

        i2cLongCommand(i2cCommandParameter(CALIBRATE, DEFAULT_STEPS),
                       address);                                                // Calibrate device because of reboot
        Twin[n]->flapNumber = 0;                                                // synchronize FlapPosition

        return true;                                                            // bootFlag resetted
    } else {
        #ifdef REGISTRYVERBOSE
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
int FlapRegistry::updateSlaveRegistry(int n, uint8_t address, slaveParameter parameter) {
    bool twinIsNew = false;                                                     // flag, if twin is new
    int  c         = 0;                                                         // number of calibrated devices
    auto it        = g_slaveRegistry.find(address);                             // search in registry
    if (it != g_slaveRegistry.end() && it->second != nullptr) {                 // fist check if device is registered
        twinIsNew                       = false;                                // twin is allready regisered
        I2CSlaveDevice* device          = it->second;
        device->parameter               = parameter;                            // update all parameter
        device->position                = Twin[n]->slaveReady.position;         // update Flap position
        device->parameter.sensorworking = Twin[n]->slaveReady.sensorStatus;     // update Sensor status
        #ifdef REGISTRYVERBOSE
            {
            TraceScope trace;
            registerPrint("Registry updated for known Slave 0x");
            Serial.print(address, HEX);
            Serial.print(" in position = ");
            Serial.print(device->position);
            Serial.print(" and sensor status = ");
            Serial.println(device->parameter.sensorworking);
            }
        #endif

    } else {
        // Device is new → register
        twinIsNew                          = true;                              // new twin detected
        I2CSlaveDevice* newDevice          = new I2CSlaveDevice();              // create new device
        newDevice->parameter               = parameter;                         // set all parameter
        newDevice->position                = Twin[n]->slaveReady.position;      // set flap position
        newDevice->parameter.sensorworking = Twin[n]->slaveReady.sensorStatus;  // set Sensor status

        #ifdef REGISTRYVERBOSE
            {
            TraceScope trace;
            registerPrint("Registered new Slave 0x");
            Serial.print(address, HEX);
            Serial.print(" in position = ");
            Serial.print(newDevice->position);
            Serial.print(" and sensor status = ");
            Serial.println(newDevice->parameter.sensorworking);
            }
        #endif
        g_slaveRegistry[address] = newDevice;                                   // register new device
    }

    checkSlaveHasBooted(n, address);                                            // slave comes again with reboot
    if (n >= 0 && twinIsNew) {                                                  // only if slave is ready
        {
            #ifdef MASTERVERBOSE                                                // take over parameter to twin
                {
                TraceScope trace;
                registerPrint("take over parameter before isSlaveReady() values from slave to his twin on master side 0x");
                Serial.println(address, HEX);
                registerPrint("number of Steps = %d for Slave 0x", parameter.steps);
                Serial.println(address, HEX);
                registerPrint("sensor status = %d for Slave 0x", parameter.sensorworking);
                Serial.println(address, HEX);
                }
                }
            #endif
        Twin[n]->parameter = parameter;                                         // update all twin parameter
        Twin[n]->isSlaveReady();

        #ifdef REGISTRYVERBOSE
            {
            TraceScope trace;
            registerPrint("take over parameter values from slave to his twin on master side 0x");
            Serial.println(address, HEX);
            registerPrint("number of Steps = %d for Slave 0x", parameter.steps);
            Serial.println(address, HEX);
            }
        #endif

        if (Twin[n]->numberOfFlaps != parameter.flaps ||
            Twin[n]->parameter.steps != parameter.steps) {                      // recompute steps by flaps based non new step measurement from slave

            #ifdef REGISTRYVERBOSE
                {
                TraceScope trace;
                registerPrint("number of Flaps = %d for Slave 0x", parameter.flaps);
                Serial.println(address, HEX);
                registerPrint("compute new Steps by Flap array for Slave 0x");
                Serial.println(address, HEX);
                }
            #endif
            Twin[n]->numberOfFlaps = parameter.flaps;
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

    #ifdef MASTERVERBOSE
        {
        TraceScope trace;
        registerPrint("master has rebooted: calibrating now Slave 0x");
        Serial.println(address, HEX);
        }
    #endif
        c++;                                                                    // counting calibrations
        i2cLongCommand(i2cCommandParameter(CALIBRATE, DEFAULT_STEPS),
                       address);                                                // Calibrate device because of reboot
        Twin[n]->flapNumber = 0;                                                // synchronize FlapPosition

        #ifdef MASTERVERBOSE
            {
            TraceScope trace;
            registerPrint("set internal adjustment step counter to %d for rebooted Slave 0x", Twin[n]->adjustOffset);
            Serial.println(address, HEX);
            }
        #endif
    }
    return c;                                                                   // number of calibrated devices
}

// -----------------------------------------
// get next free ic2 address from registry
uint8_t FlapRegistry::getNextAddress() {
    uint8_t nextAddress = findFreeAddress(I2C_MINADR, I2C_MINADR + numberOfTwins); // find address in range
    if (nextAddress > 0) {                                                      // valid address found
        return nextAddress;                                                     // deliver next free address
    } else {
        #ifdef MASTERVERBOSE
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
uint8_t FlapRegistry::findFreeAddress(uint8_t minAddr, uint8_t maxAddr) {       //  I²C Range
    for (uint8_t addr = minAddr; addr <= maxAddr; ++addr) {
        if (g_slaveRegistry.find(addr) == g_slaveRegistry.end()) {
            return addr;                                                        // freie Adresse gefunden
        }
    }
    return 0;                                                                   // alle vergeben
}

// ---------------------------------
// find number of registered devices
int FlapRegistry::numberOfRegisterdDevices() {
    return g_slaveRegistry.size();
}

// ------------------------------
// register unregistered devices
void FlapRegistry::registerUnregistered() {
    uint8_t nextFreeAddress = 0;
    if (i2c_probe_device(I2C_BASE_ADDRESS) == ESP_OK) {                         // is there someone new?
        nextFreeAddress = getNextAddress();                                     // get next free address for new slaves

        if (nextFreeAddress > 0) {                                              // if address is valide
            {
                TraceScope trace;                                               // use semaphore to protect this block
                #ifdef REGISTRYVERBOSE
                    registerPrint("Send to unregistered slave (with I2C_BASE_ADDRESS) next free I2C Address: 0x");
                    Serial.println(nextFreeAddress, HEX);
                #endif
            }

            MidMessage midCmd;
            midCmd.command   = CMD_NEW_ADDRESS;
            midCmd.paramByte = nextFreeAddress;
            uint8_t answer[4];
            i2cMidCommand(midCmd, I2C_BASE_ADDRESS, answer, sizeof(answer));
            #ifdef MASTERVERBOSE
                uint32_t sn;
                sn = ((uint32_t)answer[0]) | ((uint32_t)answer[1] << 8) | ((uint32_t)answer[2] << 16) | ((uint32_t)answer[3] << 24);
                registerPrint("new address 0x");
                Serial.println(nextFreeAddress, HEX);
                registerPrint("was received by device with serial number: ");
                Serial.println(formatSerialNumber(sn));
            #endif
        }
    } else {
        {
            TraceScope trace;                                                   // use semaphore to protect this block
            #ifdef REGISTRYVERBOSE
                registerPrintln("no new unregisered Slave (with I2C_BASE_ADDRESS) detected..."); // no answer to ping 0x55
            #endif
        }
    }
}
