// #####################################################################################################
//
//  ███████ ██       █████  ██    ██ ███████     ████████ ██     ██ ██ ███    ██
//  ██      ██      ██   ██ ██    ██ ██             ██    ██     ██ ██ ████   ██
//  ███████ ██      ███████ ██    ██ █████          ██    ██  █  ██ ██ ██ ██  ██
//       ██ ██      ██   ██  ██  ██  ██             ██    ██ ███ ██ ██ ██  ██ ██
//  ███████ ███████ ██   ██   ████   ███████        ██     ███ ███  ██ ██   ████
//
//
//   //// ################################################################################ by Achim ####
// Banner created:
// https://patorjk.com/software/taag/#p=display&c=c%2B%2B&f=ANSI%20Regular&t=SLAVE%20TWIN
//
/*
    Flap Slave Twin Definition
    --------------------------
    provides functionality on a flap module level to do:
    - Flap Drum calibration
    - check for Hall Sensor presence
    - show Flap number
    - move one Flap (next/prev)
    - move some Steps (next/prev) to set offset steps, if desired flap is not shown after calibration
    - save parameter to EEPROM on Flap module
    - step measurement
    - speed measurement
    - reset to factory settings
    - set new I2C address
    - avilability check
    - scan for new devices
    - remote control via IR

*/
#ifndef SlaveTwin_h
#define SlaveTwin_h

#include <Arduino.h>
#include "sstream"
#include "driver/i2c.h"
#include <FlapGlobal.h>
#include "TracePrint.h"
#include "RemoteControl.h"

enum TwinCommands {
    TWIN_NO_COMMAND,                                                            // no command
    TWIN_SHOW_FLAP,                                                             // show Flap with this flap number
    TWIN_CALIBRATION,                                                           // do calibration
    TWIN_STEP_MEASUREMENT,                                                      // measure steps for one revolution
    TWIN_SPEED_MEASUREMENT,                                                     // measure speed in ms for one revolution
    TWIN_SENSOR_CHECK,                                                          // check if hall sensor is working
    TWIN_NEXT_FLAP,                                                             // move to next flap
    TWIN_PREV_FLAP,                                                             // move to previous flap
    TWIN_NEXT_STEP,                                                             // move to next step (25 steps) to adjust calibration
    TWIN_PREV_STEP,                                                             // move to previous step (-25 steps) to adjust calibration
    TWIN_SET_OFFSET,                                                            // save calibration offset in EEPROM
    TWIN_RESET,                                                                 // do complete factory reset of slave  I2C address = 0x55, no serialNumber, EEPROM
    TWIN_AVAILABLE,                                                             // this command is used to check if twin is available
    TWIN_SCAN,                                                                  // this command is used to scan for twin devices
    TWIN_NEW_ADDRESS                                                            // this command is used to set the base address for the twin
};                                                                              // list of possible twin commands

// Command that will be accepted byTwin
struct TwinCommand {
    ClickType     type;                                                         // type of command SINGLE, DOUBLE, LONG
    TwinCommands  twinCommand;                                                  // command to be performed by twin
    int           twinParameter;                                                // parameter for command
    QueueHandle_t responsQueue;                                                 // queue, where result shall be responded
};

class SlaveTwin {
   public:
    int            _numberOfFlaps = 0;                                          // the number of flaps of each flap drum
    I2Caddress     _slaveAddress  = 0;                                          // I2C Address of the flap
    int            _flapNumber    = 0;                                          // actual flap that is displayed
    int            _adjustOffset  = 0;                                          // internal adjustment -> will be saved by save
    unsigned long  _lastTwinTime  = 0;                                          // last time a twin command was received
    uint64_t       _lastTwinCode  = 0;                                          // last received code from remote
    Key21          _lastKey       = Key21::UNKNOWN;                             // last pressed key
    slaveParameter _parameter;                                                  // parameter of Slave in EEPROM
    slaveStatus    _slaveReady;                                                 // status of slave

    // ----------------------------
    // Constructor
    SlaveTwin(int add);                                                         // Twin instance with I2C address

    // ----------------------------
    // Command functions
    void showFlap(int flapnumber);                                              // Show Flap with this char
    void calibration();                                                         // calibrate flap drum
    void stepMeasurement();                                                     // measure steps to be used for one revolution
    void speedMeasurement();                                                    // measure milisecends for one revolution
    void sensorCheck();                                                         // verify if hall sensor is working
    void nextFlap();                                                            // one flap forward
    void prevFlap();                                                            // one flap backward
    void nextSteps();                                                           // calibration adjustment +50 steps
    void prevSteps();                                                           // calibration adjustment -50 steps
    void setOffset();                                                           // save calibration ofset in slave EEPROM
    void reset();                                                               // do complete factory reset of slave  I2C address = 0x55, no serialNumber, EEPROM 0
    void setNewAddress(int address);                                            // set new address for the twin

    // Helper
    void  askSlaveAboutParameter(I2Caddress address, slaveParameter& parameter); // retrieve all slave parameter
    void  calculateStepsPerFlap();                                              // compute steps needed to move flap by flap
    bool  isSlaveReady();                                                       // check if slave is ready
    bool  getSlaveState();                                                      // get slave state structure
    Key21 ir2Key21(uint64_t ircode);                                            // convert IR code to Key21

    void updateSlaveReadyInfo(uint8_t* data);                                   // take over Read Structure

    // ---------------------------
    // I2C procedures
    void      i2cLongCommand(LongMessage mess);                                 // send long command to slave
    esp_err_t i2cMidCommand(MidMessage midCmd, I2Caddress slaveaddress, uint8_t* answer, int size); // send mid command to slave
    esp_err_t i2cShortCommand(ShortMessage ShortCommand, uint8_t* answer, int size); // send short command to slave

    // ---------------------------
    // RTOS queue procedures
    void readQueue();                                                           // read command from Twin queue
    void createQueue();                                                         // create queue for Twin commands
    void sendQueue(TwinCommand twinCmd);                                        // send command to Twin queue
    int  stepsByFlap[MAXIMUM_FLAPS];                                            // steps needed to move flap by flap (Bresenham-artige Verteilung)

   private:
    // -------------------------------
    // private Variables
    int           targetFlapNumber = -1;                                        // flap to be shown
    QueueHandle_t twinQueue;                                                    // command entry queue

    // -------------------------------
    // internal Helpers
    void systemHalt(const char* reason, int blinkCode);                         // system halt with reason and blink code
    void twinControl(TwinCommand twinCmd);                                      // handle Twin command
    void handleDoubleKey(TwinCommands cmd, int param);                          // handle double key command
    void handleSingleKey(TwinCommands cmd, int param);                          // handle single key command
    void logAndRun(const char* message, std::function<void()> action);          // log message and run action
    void printSlaveReadyInfo();                                                 // trace output Read Structure
    void synchSlaveRegistry(slaveParameter parameter);                          // take over slave parameter to registry
    bool waitUntilSlaveReady(uint32_t timeout_ms);                              // wait until slave is ready
    int  countStepsToMove(int from, int to);                                    // return steps to move fom "from" to "to"

    // ---------------------------
    // I2C Helper
    LongMessage i2cCommandParameter(uint8_t command, u_int16_t parameter);      // prepare I2C LongCommand from paramter

    // -------------------------------
    // internal i2c Helpers
    i2c_cmd_handle_t buildShortCommand(ShortMessage shortCmd, uint8_t* answer, int size); // build I2C command for short command
    i2c_cmd_handle_t buildMidCommand(MidMessage midCmd, I2Caddress slaveAddress, uint8_t* answer, int size);
    void             logShortRequest(ShortMessage cmd);                         // log sending short command
    void             logShortResponse(uint8_t* answer, int size);               // log response to short command
    void             logShortError(ShortMessage cmd, esp_err_t err);            // log errors for short command
    void             logMidRequest(MidMessage cmd, I2Caddress slaveAddress);    // Mid Request
    void             logMidResponse(uint8_t* answer, int size);                 // Mid Response
    void             logMidError(MidMessage cmd, esp_err_t err);                // Mid Error

    // -------------------------------
    // Twin trace
    template <typename... Args>
    void twinPrint(const Args&... args) {
        char buf[20];
        snprintf(buf, sizeof(buf), "[I2C TWIN 0x%02X  ] ", _slaveAddress);      // Prefix mit Adresse
        tracePrint(buf, args...);
    }
    template <typename... Args>
    void twinPrintln(const Args&... args) {
        char buf[20];
        snprintf(buf, sizeof(buf), "[I2C TWIN 0x%02X  ] ", _slaveAddress);      // Prefix mit Adresse
        tracePrintln(buf, args...);
    }

    // ----------------------------
};
#endif                                                                          // SlaveTwin_h
