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

class FlapRegistry {
   public:
    // ----------------------------
    // public functions
    void registerUnregistered();                                                // collect all unregistered slaves
    void repairOutOfPoolDevices();                                              // repair devices that are out of the address pool
    void updateSlaveRegistry(I2Caddress address, slaveParameter parameter);     // register slaves in registry
    void deregisterSlave(I2Caddress slaveAddress);                              // deregister from registry
    void availabilityCheck();                                                   // check if slave is still available
    void deviceRegistry();                                                      // search for slave in I2C bus
    int  numberOfRegisterdDevices();                                            // scan registry to evaluate number of registered devices
    int  findTwinIndexByAddress(I2Caddress addr);                               // find Twin by slaveAddress

    I2Caddress getNextAddress();                                                // next free i2c address form slave registry

    // public trace functions
    template <typename... Args>                                                 // Registry trace
    void registerPrint(const Args&... args) {
        tracePrint("[FLAP - REGISTER] ", args...);
    }
    template <typename... Args>                                                 // Registry trace with new line
    void registerPrintln(const Args&... args) {
        tracePrintln("[FLAP - REGISTER] ", args...);
    }

   private:
    // ----------------------------
    // privat functions
    bool checkSlaveHasBooted(int n, I2Caddress addr);                           // handle slave has booted
    void deviceRegistryIntro();                                                 // intro for scan_i2c_bus
    void deviceRegistryOutro();                                                 // outro scan_i2c_bus
    bool registered(I2Caddress addr);

    I2Caddress findFreeAddress(I2Caddress minAddr, I2Caddress maxAddr);         // free address from register
};
#endif                                                                          // FlapRegistry_h
