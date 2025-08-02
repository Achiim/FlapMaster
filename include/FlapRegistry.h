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
    - check if registered slave is still available
    - list registry

*/
#include <Arduino.h>
#include <map>
#include <FlapGlobal.h>
#include "TracePrint.h"

#ifndef FlapRegistry_h
    #define FlapRegistry_h

// Content of Registry
struct I2CSlaveDevice {
    slaveParameter parameter;
    uint16_t       position;
};

// ----------------------------
extern std::map<uint8_t, I2CSlaveDevice*> g_slaveRegistry;

class FlapRegistry {
   public:
    // Registry trace
    template <typename... Args>
    void registerPrint(const Args&... args) {
        tracePrint("[FLAP - REGISTER] ", args...);
    }
    template <typename... Args>
    void registerPrintln(const Args&... args) {
        tracePrintln("[FLAP - REGISTER] ", args...);
    }

    // ----------------------------
    // public functions
    void    check_slave_availability();
    void    deregisterSlave(uint8_t slaveAddress);                              // deregister from registry
    void    scan_i2c_bus();                                                     // search for slave in I2C bus
    void    registerUnregistered();                                             // collect all unregistered slaves
    int     updateSlaveRegistry(int n, uint8_t address, slaveParameter parameter); // register slaves in registry
    int     numberOfRegisterdDevices();                                         // scan registry to evaluate number of registered devices
    uint8_t getNextAddress();                                                   // next free i2c address form slave registry

   private:
    // ----------------------------
    // privat functions
    uint8_t findFreeAddress(uint8_t minAddr, uint8_t maxAddr);                  // free address from register
    bool    checkSlaveHasBooted(int n, uint8_t addr);                           // handle slave has booted
    void    error_scan_i2c(int n, uint8_t addr);                                // error tracing during scan_i2c_bus
    void    intro_scan_i2c();                                                   // intro for scan_i2c_bus
    void    outro_scan_i2c(int foundToCalibrate, int foundToRegister);          // outro scan_i2c_bus
    int     scanForSlave(int i, uint8_t addrs);                                 // scan one slave
    int     findTwinIndexByAddress(uint8_t addr);                               // find Twin by slaveAddress
};
#endif                                                                          // FlapRegistry_h
