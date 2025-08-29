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
#include "SlaveTwin.h"
#include "Parser.h"
#include "FlapRegistry.h"
#include "RtosTasks.h"
#include "FlapTasks.h"

// ----------------------------------

/**
 * @brief Data Structure to register FlapDevices
 *
 * 1: I2C-Address
 *
 * 2: parameter (offset, slaveaddress, flaps, sensorworking, serialnumber, steps, speed)
 *
 * 3: position
 *
 * 4: bootFlag
 */
std::map<I2Caddress, I2CSlaveDevice*> g_slaveRegistry;

// -----------------------------------

/**
 * @brief Will be called cyclic by Registry Task with the help of a countdown timer
 * and will trigger the twins of all registered devices to ping them and:
 *
 * if device answers:
 *
 * - ask device about bootFlag
 *
 * - reset bootFlag
 *
 * - calibrate device
 *
 * if device does not answer:
 *
 * - de-register this device
 */
void FlapRegistry::availabilityCheck() {
    TwinCommand cmd{};                                                          // zero-init for all
    cmd.twinCommand   = TWIN_AVAILABILITY;
    cmd.twinParameter = 0;

    forEachRegisteredIdx([&](int idx, I2Caddress addr) {                        // all registered devices
        {
            #ifdef AVAILABILITYVERBOSE
                {
                TraceScope trace;
                registerPrint("send TWIN_AVAILABILITY to twin 0x");
                Serial.println(addr, HEX);
                }
            #endif
        }
        if (!Twin[idx]) {
            #ifdef ERRORVERBOSE
                {
                TraceScope trace;                                               // debug: trace scope
                registerPrintln("Twin does not exist");
                }
            #endif
            return;
        }
        bool rc = Twin[idx]->sendQueue(cmd);                                    // send to twin with registered device
        if (!rc) {
            #ifdef ERRORVERBOSE
                {
                TraceScope trace;                                               // debug: trace scope
                registerPrintln("send to Twin Queue failed");
                }
            #endif
        }
    });                                                                         // next registered device
}

// ----------------------------

/**
 * @brief erase slave from registry
 *
 * @param slaveAddress = address to be erased from registry
 *
 */
void FlapRegistry::deRegisterDevice(I2Caddress slaveAddress) {
    auto it = g_slaveRegistry.find(slaveAddress);
    if (it != g_slaveRegistry.end()) {                                          // only attempt erase if registry is not empty
        delete it->second;                                                      // release memory
        g_slaveRegistry.erase(it);                                              // remove entry for the given slave address (no-op if not present)
    }
}

// ----------------------------

/**
 * @brief messages to introduce deviceRegistry
 *
 */
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

/**
 * @brief messages to leave deviceRegistry
 *
 */
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

/**
 * @brief Query all expected addresses (from the fixed address pool)
 * and trigger (send TWIN_REGISTER to their Twin[n] queue)
 * not registered slaves to be registered by theis twin .
 */
void FlapRegistry::registerDevice() {
    deviceRegistryIntro();                                                      // announce what we're about to do

    const int slotCount = capacity();                                           // number of Twin slots (fixed, sequential pool)

    TwinCommand cmd{};                                                          // Prepare the command once; reuse for all unregistered addresses
    cmd.twinCommand   = TWIN_REGISTER;                                          // ask the device to register
    cmd.twinParameter = 0;                                                      // no parameter
    cmd.responsQueue  = nullptr;                                                // no response queue expected
    for (int i = 0; i < slotCount; ++i) {
        const I2Caddress addr = addressAt(i);                                   // mapping: addr = I2C_MINADR + i

        if (!isAddressRegistered(addr)) {                                       // Only act on addresses that are not yet present in the registry

        #ifdef REGISTRYVERBOSE
            {
            TraceScope trace;
            registerPrintln("probe/register: idx=%d addr=0x%02x (not registered)", i, addr);
            }
        #endif
            Twin[i]->sendQueue(cmd);                                            // send to twins entry queue
        }
    }
    deviceRegistryOutro();                                                      // wrap up logging
}

// ----------------------------

/**
 * @brief will be called by Twin as service function and
 *
 * 1: registers device as new, if not already present in registry
 *
 * 2: updates registry entry if device is known
 *
 * 3: update registry with parameter handed over
 *
 * @param address = address to be registered or updated
 * @param parameter = parameter to be used fpr update or register
 */
void FlapRegistry::updateRegistry(I2Caddress address, slaveParameter parameter) {
    int n = indexOfAddress(address);
    if (n < 0 || Twin[n] == nullptr)
        return;                                                                 // no Twin exists

    I2CSlaveDevice* device      = nullptr;                                      // generate new device
    bool            deviceIsNew = false;

    auto it = g_slaveRegistry.find(address);
    if (it != g_slaveRegistry.end() && it->second) {                            // is device allready registered?
        device = it->second;
    } else {
        deviceIsNew              = true;                                        // is't a new device
        device                   = new I2CSlaveDevice();
        g_slaveRegistry[address] = device;                                      // insert device in registry
    }

    // --- remember changes before overwriting them
    const auto oldSteps = device->parameter.steps;
    const auto oldFlaps = device->parameter.flaps;
    const auto oldOff   = device->parameter.offset;
    const auto oldSpeed = device->parameter.speed;
    const auto oldPos   = device->position;
    const auto oldBoot  = device->bootFlag;
    const auto oldSens  = device->parameter.sensorworking;

    // take over actual parameter and states
    device->parameter               = parameter;                                // speed/offset/steps/flaps
    device->position                = Twin[n]->_slaveReady.position;
    device->bootFlag                = Twin[n]->_slaveReady.bootFlag;
    device->parameter.sensorworking = Twin[n]->_slaveReady.sensorStatus;        // :contentReference[oaicite:3]{index=3}

    if (deviceIsNew) {
        Twin[n]->_parameter = parameter;                                        // wie bisher nur bei neuen Geräten
    }

    // changes of movement parameter
    const bool stepsChanged = (oldSteps != parameter.steps);
    const bool flapsChanged = (oldFlaps != parameter.flaps);
    if (stepsChanged || flapsChanged) {
        Twin[n]->_numberOfFlaps = parameter.flaps;
        Twin[n]->calculateStepsPerFlap();                                       // recalculate relative movement

        #ifdef SCANVERBOSE
            {
            TraceScope trace;
            printStepsByFlapLines(address, Twin[n]->stepsByFlap, parameter.flaps);
            }
        #endif
    }

    // other changes
    const bool offChanged   = (oldOff != parameter.offset);
    const bool speedChanged = (oldSpeed != parameter.speed);
    const bool bootChanged  = (oldBoot != Twin[n]->_slaveReady.bootFlag);
    const bool posChanged   = (oldPos != Twin[n]->_slaveReady.position);
    const bool sensChanged  = (oldSens != Twin[n]->_slaveReady.sensorStatus);

    if (offChanged) {
        #ifdef SCANVERBOSE
            {
            TraceScope trace;
            registerPrint("device 0x%02X offset changed to: ", address);
            Serial.println(device->parameter.offset);
            }
        #endif
    }

    if (speedChanged) {
        #ifdef SCANVERBOSE
            {
            TraceScope trace;
            registerPrint("device 0x%02X speed changed to: ", address);
            Serial.println(device->parameter.speed);
            }
        #endif
    }

    if (bootChanged) {
        #ifdef SCANVERBOSE
            {
            TraceScope trace;
            registerPrint("device 0x%02X bootFlag changed to: ", address);
            Serial.println(device->bootFlag);
            }
        #endif
    }

    if (posChanged) {
        #ifdef SCANVERBOSE
            {
            TraceScope trace;
            registerPrint("device 0x%02X position changed to: ", address);
            Serial.println(device->position);
            }
        #endif
    }

    if (sensChanged) {
        #ifdef SCANVERBOSE
            {
            TraceScope trace;
            registerPrint("device 0x%02X sensor status changed to: ", address);
            device->parameter.sensorworking ? Serial.println("working") : Serial.println("broken");
            }
        #endif
    }
}

// -----------------------------------------
/**
 * @brief print configuration steps steps by flap
 *
 * @param address device address
 * @param steps parameter steps
 * @param flaps parameter flaps
 * @param perLine print x steps per line
 *
 */
void FlapRegistry::printStepsByFlapLines(I2Caddress address, int* steps, int flaps, int perLine) {
    if (!steps || flaps <= 0) {
        return;
    }
    for (int i = 0; i < flaps; ++i) {
        if ((i % perLine) == 0) {                                               // begin with prefix line
            registerPrint("device 0x%02X Steps by Flap: ", address);
        }
        Serial.print(steps[i]);
        const bool endOfLine = ((i % perLine) == perLine - 1) || (i == flaps - 1);
        if (endOfLine) {
            Serial.println();                                                   // line break
        } else {
            Serial.print(", ");                                                 // separate steps
        }
    }
}

// -----------------------------------------

/**
 * @brief deliver number of Twins
 *
 * @return int number of twins
 *
 */
int FlapRegistry::capacity() const {
    return numberOfTwins;
}

// -----------------------------------------

/**
 * @brief number of registered devices
 *
 * @return int number of registered devices
 */
int FlapRegistry::size() const {
    return static_cast<int>(g_slaveRegistry.size());
}

// -----------------------------------------

/**
 * @brief is index valid based on number of Twins
 *
 * @param idx
 * @return true index is valid;
 * @return false index is invalid
 */
bool FlapRegistry::isValidIndex(int idx) const {
    return idx >= 0 && idx < numberOfTwins;
}

// -----------------------------------------

/**
 * @brief deliver address at index
 *
 * @param idx twin index
 * @return uint8_t addresss or 0 if invalid
 */
uint8_t FlapRegistry::addressAt(int idx) const {
    return isValidIndex(idx) ? g_slaveAddressPool[idx] : 0;
}

// -----------------------------------------

/**
 * @brief get index from address
 *
 * @param addr i2c adress
 * @return int index of Twin[n]
 */
int FlapRegistry::indexOfAddress(I2Caddress addr) const {                       // get index from address
    int idx = int(addr) - int(I2C_MINADR);
    return (idx >= 0 && idx < capacity()) ? idx : -1;
}

// -----------------------------------------

/**
 * @brief is address registered
 *
 * @param addr device address
 * @return true
 * @return false
 */
bool FlapRegistry::isAddressRegistered(I2Caddress addr) const {
    return g_slaveRegistry.find(addr) != g_slaveRegistry.end();
}

// -----------------------------------------

/**
 * @brief is there a Twin for that address
 *
 * @param idx
 * @return true
 * @return false
 */
bool FlapRegistry::isIndexRegistered(int idx) const {
    return isValidIndex(idx) && isAddressRegistered(addressAt(idx));
}

// -----------------------------------------

/**
 * @brief get next free ic2 address from registry
 *
 * @return I2Caddress or 0 = no more free address
 */
I2Caddress FlapRegistry::getNextFreeAddress() {
    I2Caddress nextAddress = findFreeAddress(I2C_MINADR, I2C_MINADR + capacity() - 1); // find address in range

    if (nextAddress != 0) {                                                     // valid address found
        return nextAddress;                                                     // deliver next free address
    }
    return 0;                                                                   // indicate no more free address
}

// ------------------------------

/**
 * @brief find free address in registry
 *
 * @param minAddr range within to be searched
 * @param maxAddr range within to be searched
 * @return I2Caddress = 0, if no address is free
 */
I2Caddress FlapRegistry::findFreeAddress(I2Caddress minAddr, I2Caddress maxAddr) { //  I²C Range for slaves
    for (I2Caddress addr = minAddr; addr <= maxAddr; ++addr) {
        if (g_slaveRegistry.find(addr) == g_slaveRegistry.end()) {
            return addr;                                                        // free address found
        }
    }
    return 0;                                                                   // no free addess found
}

// ---------------------------------

/**
 * @brief navigate to first registered device
 *
 * @return int twin index of first registered device
 */
int FlapRegistry::firstRegisteredIndex() const {
    const int n = capacity();
    for (int i = 0; i < n; ++i) {
        if (isIndexRegistered(i))
            return i;
    }
    return -1;
}

// ---------------------------------

/**
 * @brief navigate to next registered device
 *
 * @param start begin index of search
 * @param dir direction of search
 * @return int next index or -1 if no index is next
 */
int FlapRegistry::nextRegisteredIndex(int start, int dir) const {
    const int n = capacity();                                                   // number of twins
    if (n <= 0)
        return -1;
    if (dir == 0)
        dir = 1;

    int cur = (start < 0 || start >= n) ? ((dir > 0) ? -1 : 0) : start;         // define current depending on direction
    for (int steps = 0; steps < n; ++steps) {                                   // Wrap-around speps
        int next = cur + dir;
        if (next >= n)
            next -= n;
        if (next < 0)
            next += n;

        if (isIndexRegistered(next))
            return next;
        cur = next;
    }
    return -1;                                                                  // nobody registered
}

// ---------------------------------

/**
 * @brief send to TwinQueue n
 *
 * @param idx twin index
 * @param cmd TwinCommand to be send
 * @return true
 * @return false
 */
bool FlapRegistry::sendToIndex(int idx, const TwinCommand& cmd) const {
    if (!isIndexRegistered(idx)) {
        #ifdef ERRORVERBOSE
            {
            TraceScope trace;
            registerPrintln("sendToIndex: invalid/unregistered index=%d", idx);
            }
        #endif
        return false;
    }
    const uint8_t addr = addressAt(idx);
    Twin[idx]->sendQueue(cmd);
    #ifdef REGISTRYVERBOSE
        {
        TraceScope trace;
        registerPrintln("send Twin_Command: %s to Twin 0x%02x", Parser->twinCommandToString(cmd.twinCommand), addr);
        registerPrintln("send Twin_Parameter: %d to Twin 0x%02x", cmd.twinParameter, addr);
        }
    #endif
    return true;
}

// ---------------------------------

/**
 * @brief send to all TwinQueues
 *
 * @param cmd
 */
void FlapRegistry::sendToAll(const TwinCommand& cmd) const {
    forEachRegisteredIdx([&](int idx, I2Caddress addr) {
        Twin[idx]->sendQueue(cmd);
        #ifdef REGISTRYVERBOSE
            {
            TraceScope trace;
            registerPrintln("send Twin_Command: %s to Twin 0x%02x", Parser->twinCommandToString(cmd.twinCommand), addr);
            registerPrintln("send Twin_Parameter: %d to Twin 0x%02x", cmd.twinParameter, addr);
            }
        #endif
    });
}

// ---------------------------------

/**
 * @brief This function is used to repair devices that are out of the address pool.
 * Because no Twin is connected to out of pool addresses -> Twin[0] is used
 */
void FlapRegistry::repairOutOfPoolDevices() {
    I2Caddress nextFreeAddress = 0;
    if (getNextFreeAddress() == 0) {
        return;                                                                 // if no address is free, repair not possible
    }
    #ifdef SCANVERBOSE
        {
        TraceScope trace;
        registerPrintln("Repairing devices out of address pool...");
        }
    #endif

    for (I2Caddress ii = I2C_MINADR + numberOfTwins; ii <= I2C_MAXADR; ii++) {  // iterate over all address out of pool
        if (i2c_probe_device(ii) == ESP_OK) {                                   // is there someone out of range?
            nextFreeAddress = getNextFreeAddress();                             // get next free address for out of range slaves
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
/**
 * @brief register unregistered devices
 * because no Twin is connected to address 0x55 (unregistered) -> Twin[0] is used
 *
 */
void FlapRegistry::registerUnregistered() {
    I2Caddress nextFreeAddress = 0;
    nextFreeAddress            = getNextFreeAddress();                          // get next free address for new slaves

    if (nextFreeAddress == 0) {                                                 // address pool empty
        return;
    }

    if (nextFreeAddress >= I2C_MINADR && nextFreeAddress <= I2C_MAXADR) {       // if address is valide
        {
            TraceScope trace;                                                   // use semaphore to protect this block
            #ifdef SCANVERBOSE
                registerPrintln("Check for unregistered slaves to be registered");
                //    registerPrintln("Send TWIN_NEW_ADDRESS with free I2C Address: 0x");
                //    Serial.println(nextFreeAddress, HEX);
            #endif
        }

        TwinCommand twinCmd;
        twinCmd.twinCommand   = TWIN_NEW_ADDRESS;                               // set command to send base address
        twinCmd.twinParameter = nextFreeAddress;                                // set new base address
        Twin[0]->sendQueue(twinCmd);                                            // send command to Twin[0] to set base address
    }
}
