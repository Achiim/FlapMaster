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
    slaveParameter parameter;
    uint16_t       position;
};

// ----------------------------
extern std::map<uint8_t, I2CSlaveDevice*> g_slaveRegistry;

class FlapRegistry {
   public:
    // ----------------------------
    // public functions
    void    registerUnregistered();                                             // collect all unregistered slaves
    int     updateSlaveRegistry(int n, uint8_t address, slaveParameter parameter); // register slaves in registry
    void    deregisterSlave(uint8_t slaveAddress);                              // deregister from registry
    void    check_slave_availability();                                         // check if slave is still available
    void    scan_i2c_bus();                                                     // search for slave in I2C bus
    int     numberOfRegisterdDevices();                                         // scan registry to evaluate number of registered devices
    uint8_t getNextAddress();                                                   // next free i2c address form slave registry

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
    bool    checkSlaveHasBooted(int n, uint8_t addr);                           // handle slave has booted
    void    error_scan_i2c(int n, uint8_t addr);                                // error tracing during scan_i2c_bus
    void    intro_scan_i2c();                                                   // intro for scan_i2c_bus
    void    outro_scan_i2c(int foundToCalibrate, int foundToRegister);          // outro scan_i2c_bus
    int     scanForSlave(int i, uint8_t addrs);                                 // scan one slave
    uint8_t findFreeAddress(uint8_t minAddr, uint8_t maxAddr);                  // free address from register
    int     findTwinIndexByAddress(uint8_t addr);                               // find Twin by slaveAddress
};
#endif                                                                          // FlapRegistry_h
