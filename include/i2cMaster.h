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
/*

    I2C features on Master side

    Features:

    - provide Semaphore to controll access to I2C bus
    - I2C setup for Master
    - generate LongMessage, from Command and Parameter
    - write I2C command to slave
    - check if slave is ready to receive new command

*/
#ifndef i2cMaster_h
#define i2cMaster_h

#include "freertos/semphr.h"
#include <FlapGlobal.h>

#define I2C_MASTER_NUM I2C_NUM_0                                                // for IC2 scanner
#define I2C_MASTER_SCL_IO 22                                                    // SCL PIN
#define I2C_MASTER_SDA_IO 21                                                    // SDA PIN
#define I2C_MASTER_FREQ_HZ 350000                                               // I2C Semi Fast Mode 200 kHz (400 kHz fast)

// Master global variables
extern bool              g_masterBooted;                                        // global Flag if Master has fresh booted
extern SemaphoreHandle_t g_i2c_mutex;                                           // Semaphor to protect access to I2C Bus

// -------------------------------
void        i2csetup();                                                         // initialize I2C Bus for Master access
LongMessage i2cCommandParameter(uint8_t command, u_int16_t parameter);          // prepare I2C LongCommand from paramter
void        i2cLongCommand(LongMessage mess, uint8_t slaveAddress);             // Long Command to slave, do not wait for answer
int         check_slaveReady(uint8_t slaveAddress);                             // status check isf slave is ready/busy
void        updateSlaveReadyInfo(int n, uint8_t slaveAddress,
                                 uint8_t* data);                                // evaluate and remember slave answer to check_slaveReady()
void        printSlaveReadyInfo(SlaveTwin* twin);                               // trace printing slave answer to check_slaveReady()
bool        takeI2CSemaphore();                                                 // get a semaphore
bool        giveI2CSemaphore();                                                 // release a semaphore
esp_err_t   i2c_probe_device(uint8_t address);                                  // semaphore protected ping
esp_err_t   pingI2Cslave(u_int8_t address);                                     // just ping on I2C if slave is still online
// --------------------------------

#endif                                                                          // i2cMaster_h
