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
#include "i2cMaster.h"
#include "FlapRegistry.h"
#include "RtosTasks.h"
#include "FlapTasks.h"

// ----------------------------------
// Data Structure to register FlapDevices
std::map<I2Caddress, I2CSlaveDevice*> g_slaveRegistry;

// -----------------------------------
// will be called cyclic by Registry Task with the help of a countdown timer
// function:
// - will trigger the twins of all registered devices to ping them and
// if device answers:
//   - ask device about bootFlag
//   - reset bootFlag
//   - calibrate device
//
// if device does not answer:
//   - deregister this device
//

void FlapRegistry::availabilityCheck() {
    for (auto it = g_slaveRegistry.begin(); it != g_slaveRegistry.end();) {     // iterate over all registered slaves
        const I2Caddress addr = it->first;                                      // get address of current slave

        #ifdef AVAILABILITYVERBOSE
            {
            TraceScope trace;                                                   // debug: trace scope
            registerPrint("check availability of registered I²C-Slave 0x");     // debug: print message
            Serial.println(addr, HEX);                                          // print address in hex
            }
        #endif

        #ifdef AVAILABILITYVERBOSE
            {
            TraceScope trace;                                                   // debug: trace scope
            registerPrint("send TWIN_AVAILABILITY to twin 0x");                 // debug: print message
            Serial.println(addr, HEX);                                          // print address in hex
            }
        #endif

        TwinCommand twinCmd;                                                    // create new TwinCommand object
        twinCmd.twinCommand   = TWIN_AVAILABILITY;                              // command = "availability check"
        twinCmd.twinParameter = 0;                                              // no parameter required
        twinCmd.responsQueue  = nullptr;                                        // no direct response requested
        int n                 = findTwinIndexByAddress(addr);                   // resolve twin index from slave address
        Twin[n]->sendQueue(twinCmd);                                            // trigger availability check on twin
                                                               // -> evaluation & deregistration
                                                               //    are handled inside the Twin
        ++it;                                                                   // continue with next registry entry
    }
}

// ----------------------------
// erase slave from registry
void FlapRegistry::deregisterSlave(I2Caddress slaveAddress) {
    auto it = g_slaveRegistry.find(slaveAddress);
    if (it != g_slaveRegistry.end()) {                                          // only attempt erase if registry is not empty
        delete it->second;                                                      // release memory
        g_slaveRegistry.erase(it);                                              // remove entry for the given slave address (no-op if not present)
    }
}

// ----------------------------
// Helper: find Twin-Index for I2C-Address, or -1 if not found
int FlapRegistry::findTwinIndexByAddress(I2Caddress addr) {
    for (int s = 0; s < numberOfTwins; ++s) {                                   // iterate over all available twins
        if (Twin[s] && Twin[s]->_slaveAddress == addr) {                        // check pointer validity and address match
            return s;                                                           // return matching twin index
        }
    }
    return -1;                                                                  // not found → caller must handle "no twin" case
}

// ------------------------------------
// Registry lookup (for diagnostics)
bool FlapRegistry::registered(I2Caddress addr) {
    auto it = g_slaveRegistry.find(addr);                                       // attempt to find address in registry
    if (it != g_slaveRegistry.end() && it->second != nullptr) {                 // ensure key exists and has a valid payload
    #ifdef SCANVERBOSE
        {
        TraceScope trace;                                                       // debug: scoped trace output
        registerPrint("Slave already registered at address 0x");                // debug message
        Serial.println(addr, HEX);                                              // print address in hex
        }
    #endif
        return true;                                                            // address is registered
    }
    return false;                                                               // address not present or invalid entry
}

// ----------------------------
// messages to introduce deviceRegistry
void FlapRegistry::deviceRegistryIntro() {
    #ifdef SCANVERBOSE
        {
        TraceScope trace;                                                       // debug: scoped trace output
        registerPrintln("Start I²C-Scan to register new slaves...");            // announce scan start
        }
    #endif

    if (g_masterBooted) {                                                       // master just rebooted? (boot flag set)
    #ifdef SCANVERBOSE
        {
        TraceScope trace;                                                       // debug: scoped trace output
        registerPrintln("System-Reset detected → Master has restarted");        // inform about master restart
        registerPrintln("all slaves detected during I²C-bus-scan will be calibrated..."); // note calibration policy
        }
    #endif
    }
}

// ----------------------------
// messages to leave deviceRegistry
void FlapRegistry::deviceRegistryOutro() {
    if (g_masterBooted) {
        g_masterBooted = false;                                                 // master boot sequence finished → clear boot flag
    }

    #ifdef SCANVERBOSE
        {
        TraceScope trace;                                                       // debug: scoped trace output
        registerPrintln("I²C-Scan complete.");                                  // announce scan completion
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

// ----------------------------
// purpose:
// will be called by Twin as service function
// - register device as new if not already present in registry
// - update registry entry if device is known
// - update registry with parameter handed over
// variables:
// address   = address of slave to be registered/updated
// parameter = parameter to be stored in registry for this slave
//
void FlapRegistry::updateSlaveRegistry(I2Caddress address, slaveParameter parameter) {
    int n = findTwinIndexByAddress(address);                                    // resolve twin index for this address
    if (n < 0 || Twin[n] == nullptr) {                                          // guard against invalid twin index/pointer
        {
            #ifdef SCANVERBOSE
                {
                TraceScope trace;                                               // debug: scoped trace
                registerPrint("No Twin found for Slave 0x");                    // cannot safely update without Twin
                Serial.println(address, HEX);
                }
            #endif
        }
        return;                                                                 // nothing to do without a valid Twin
    }

    I2CSlaveDevice* device      = nullptr;                                      // Prepare a pointer to the device object (either existing or newly created).
    bool            deviceIsNew = false;

    auto it = g_slaveRegistry.find(address);                                    // search registry for this address
    if (it != g_slaveRegistry.end() && it->second != nullptr) {                 // if device already exists in registry
        deviceIsNew = false;                                                    // known slave
        device      = it->second;                                               // retrieve existing device object

    } else {
        // Device is new → register
        deviceIsNew              = true;                                        // mark as new
        device                   = new I2CSlaveDevice();                        // create new slave object
        g_slaveRegistry[address] = device;                                      // add new device to registry
    }

    // Common assignments for both new and existing devices
    device->parameter               = parameter;                                // update parameter
    device->position                = Twin[n]->_slaveReady.position;            // update flap position
    device->bootFlag                = Twin[n]->_slaveReady.bootFlag;            // update boot flag
    device->parameter.sensorworking = Twin[n]->_slaveReady.sensorStatus;        // update sensor status
    const char* sensorStatus        = device->parameter.sensorworking ? "WORKING" : "BROKEN"; // derive status string

    #ifdef SCANVERBOSE
        {
        TraceScope trace;                                                       // debug: trace scope
        if (deviceIsNew) {
        registerPrint("Registered new Slave 0x");                               // unified logging
        } else {
        registerPrint("Registry updated for known Slave 0x");
        }
        Serial.print(address, HEX);                                             // print address
        Serial.print(" in position = ");
        Serial.print(device->position);
        Serial.print(" and sensor status = ");
        Serial.println(sensorStatus);
        }
    #endif

    if (deviceIsNew) {                                                          // only if device is new
        Twin[n]->_parameter = parameter;                                        // copy parameter from slave to twin

        #ifdef SCANVERBOSE
            {
            TraceScope trace;                                                   // debug: trace scope
            registerPrint("take over parameter values from slave to his twin on master side 0x");
            Serial.println(address, HEX);
            registerPrint("number of Steps = %d for Slave 0x", parameter.steps);
            Serial.println(address, HEX);
            }
        #endif

        // Decide if a recomputation of steps-per-flap is necessary.
        // Compare requested values (from 'parameter') with the Twin’s *current* values.
        // Note: For new slaves, _parameter may just have been set above; for known slaves,
        //       we intentionally compare against the Twin’s existing values (unchanged here).
        if (Twin[n]->_numberOfFlaps != parameter.flaps ||
            Twin[n]->_parameter.steps != parameter.steps) {                     // check if flaps or steps differ → recalc needed

            #ifdef SCANVERBOSE
                {
                TraceScope trace;                                               // debug: trace scope
                registerPrint("number of Flaps = %d for Slave 0x", parameter.flaps);
                Serial.println(address, HEX);
                registerPrint("compute new Steps by Flap array for Slave 0x");
                Serial.println(address, HEX);
                }
            #endif

            Twin[n]->_numberOfFlaps = parameter.flaps;                          // update flap count
            Twin[n]->calculateStepsPerFlap();                                   // recalc steps per flap

            #ifdef SCANVERBOSE
                {
                TraceScope trace;                                               // debug: trace scope
                registerPrint("Steps by Flap are: ");                           // log newly computed steps array
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
// because no Twin is connected to out of pool addresses -> Twin[0] is used
void FlapRegistry::repairOutOfPoolDevices() {
    I2Caddress nextFreeAddress = 0;
    #ifdef SCANVERBOSE
        {
        TraceScope trace;
        registerPrintln("Repairing devices out of address pool...");
        }
    #endif

    for (I2Caddress ii = I2C_MINADR + numberOfTwins; ii <= I2C_MAXADR; ii++) {  // iterate over all address out of pool
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
// because no Twin is connected to address 0x55 (unregistered) -> Twin[0] is used
void FlapRegistry::registerUnregistered() {
    I2Caddress nextFreeAddress = 0;
    nextFreeAddress            = getNextAddress();                              // get next free address for new slaves

    if (nextFreeAddress == -1) {                                                // address pool empty
        {
            TraceScope trace;                                                   // use semaphore to protect this block
            #ifdef SCANVERBOSE
                registerPrint("no mor address found in address pool");
                Serial.println(nextFreeAddress, HEX);
            #endif
        }
        return;
    }

    if (nextFreeAddress >= I2C_MINADR && nextFreeAddress <= I2C_MAXADR) {       // if address is valide
        {
            TraceScope trace;                                                   // use semaphore to protect this block
            #ifdef SCANVERBOSE
                registerPrint("Send to unregistered slave (with I2C_BASE_ADDRESS) next free I2C Address: 0x");
                Serial.println(nextFreeAddress, HEX);
            #endif
        }

        TwinCommand twinCmd;
        twinCmd.twinCommand   = TWIN_NEW_ADDRESS;                               // set command to send base address
        twinCmd.twinParameter = nextFreeAddress;                                // set new base address
        Twin[0]->sendQueue(twinCmd);                                            // send command to Twin[0] to set base address
        #ifdef SCANVERBOSE
            {
            TraceScope trace;                                                   // use semaphore to protect this block
            registerPrintln("send TwinCommand: %s to TWIN[0]", Parser->twinCommandToString(twinCmd.twinCommand));
            registerPrint("Unregistered slave will now be registered with I2C address: 0x");
            Serial.println(nextFreeAddress, HEX);
        #endif
        }
    }
}