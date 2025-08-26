// #################################################################################################################
//
//  ███████ ██       █████  ██████      ██████  ███████  ██████  ██ ███████ ████████ ██████  ██    ██
//  ██      ██      ██   ██ ██   ██     ██   ██ ██      ██       ██ ██         ██    ██   ██  ██  ██
//  █████   ██      ███████ ██████      ██████  █████   ██   ███ ██ ███████    ██    ██████    ████
//  ██      ██      ██   ██ ██          ██   ██ ██      ██    ██ ██      ██    ██    ██   ██    ██
//  ██      ███████ ██   ██ ██          ██   ██ ███████  ██████  ██ ███████    ██    ██   ██    ██
//
//
// ################################################################################################## by Achim ####
// Banner created:
// https://patorjk.com/software/taag/#p=display&c=c%2B%2B&f=ANSI%20Regular&t=flap%20Registry
//
/*

    Registration storage for Flap Displays

    Features:
    - register
    - deregister
    - i2c scan to check who is there
    - availability check if registered slave is still available
    - list registry
    - count registered devices
    - provide free address to be used for registration

*/
#include <Arduino.h>
#include <map>
#include <FlapGlobal.h>
#include "TracePrint.h"
#include <cstdint>
#include <climits>

#ifndef FlapRegistry_h
    #define FlapRegistry_h

// Content of Registry
struct I2CSlaveDevice {
    slaveParameter parameter;                                                   // parameter of device
    uint16_t       position;                                                    // position of device
    bool           bootFlag;                                                    // bootFlag of device
};

// ----------------------------
extern std::map<I2Caddress, I2CSlaveDevice*> g_slaveRegistry;

/**
 * @brief Flap Registry class
 * provides al functionality te register devices and the registry itself
 */
class FlapRegistry {
   public:
    // ----------------------------
    // public functions

    // registration
    void registerUnregistered();                                                // collect all unregistered slaves
    void repairOutOfPoolDevices();                                              // repair devices that are out of the address pool
    void updateRegistry(I2Caddress address, slaveParameter parameter);          // register slaves in registry
    void deRegisterDevice(I2Caddress slaveAddress);                             // deregister from registry
    void availabilityCheck();                                                   // check if slave is still available
    void registerDevice();                                                      // search for slave in I2C bus

    // registry access
    int        size() const;                                                    // number of registered devices (Registry-size)
    int        firstRegisteredIndex() const;                                    // -1, if no first
    int        nextRegisteredIndex(int start, int dir) const;                   // dir: +1 forward, -1 backward
    bool       isAddressRegistered(I2Caddress addr) const;                      // is address registered?
    bool       isIndexRegistered(int idx) const;                                // is index valid and registered?
    I2Caddress getNextFreeAddress();                                            // next free i2c address form slave registry

    // address pool access
    int        capacity() const;                                                // numberOfTwins
    int        indexOfAddress(I2Caddress addr) const;                           // get index from address
    bool       isValidIndex(int idx) const;                                     // verify twin index
    I2Caddress addressAt(int idx) const;                                        // 0 if invalid

    // API to twins
    bool sendToIndex(int idx, const TwinCommand& cmd) const;                    // sent to TwinQueue n
    void sendToAll(const TwinCommand& cmd) const;                               // send to all TwinQueues

    template <typename Fn>
    inline void forEachRegisteredIdx(Fn&& fn) const {                           // loop all registered devices
        for (int i = 0; i < capacity(); ++i) {
            I2Caddress addr = addressAt(i);                                     // addr -> device
            if (isAddressRegistered(addr))
                fn(i, addr);
        }
    }

    // public trace functions
    template <typename... Args>                                                 // Registry trace
    void registerPrint(const Args&... args) const {
        tracePrint("[FLAP - REGISTER] ", args...);
    }
    template <typename... Args>                                                 // Registry trace with new line
    void registerPrintln(const Args&... args) const {
        tracePrintln("[FLAP - REGISTER] ", args...);
    }

   private:
    // ----------------------------
    // privat functions
    void deviceRegistryIntro();                                                 // intro for scan_i2c_bus
    void deviceRegistryOutro();                                                 // outro scan_i2c_bus

    I2Caddress findFreeAddress(I2Caddress minAddr, I2Caddress maxAddr);         // free address from register

    void printStepsByFlapLines(I2Caddress address, int* steps, int flaps, int perLine = 10);
};
#endif                                                                          // FlapRegistry_h
