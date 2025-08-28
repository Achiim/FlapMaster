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
#include "AreYouReadyLimiter.h"

enum TwinCommands {
    TWIN_NO_COMMAND        = 0,                                                 // no command
    TWIN_SHOW_FLAP         = 10,                                                // show Flap with this flap number
    TWIN_CALIBRATION       = 20,                                                // do calibration
    TWIN_STEP_MEASUREMENT  = 30,                                                // measure steps for one revolution
    TWIN_SPEED_MEASUREMENT = 40,                                                // measure speed in ms for one revolution
    TWIN_SENSOR_CHECK      = 50,                                                // check if hall sensor is working
    TWIN_NEXT_FLAP         = 60,                                                // move to next flap
    TWIN_PREV_FLAP         = 70,                                                // move to previous flap
    TWIN_NEXT_STEP         = 80,                                                // move to next step (25 steps) to adjust calibration
    TWIN_PREV_STEP         = 90,                                                // move to previous step (-25 steps) to adjust calibration
    TWIN_SET_OFFSET        = 100,                                               // save calibration offset in EEPROM
    TWIN_FACTORY_RESET     = 110,                                               // do complete factory reset of slave  I2C address = 0x55, no serialNumber, EEPROM
    TWIN_AVAILABILITY      = 120,                                               // this command is used to check if twin device is available
    TWIN_REGISTER          = 130,                                               // this command is used to register a twin device
    TWIN_NEW_ADDRESS       = 140                                                // this command is used to set the base address for the twin
};                                                                              // list of possible twin commands

enum ReportCommands {
    REPORT_NO_COMMAND    = 0,                                                   // no command
    REPORT_ALL           = 300,                                                 // all reports
    REPORT_TASKS_STATUS  = 310,                                                 // trace uptime and next countdown timer
    REPORT_MEMORY        = 320,                                                 // trace memory usage od ESP32
    REPORT_RTOS_TASKS    = 330,                                                 // trace RTOS Task List
    REPORT_STEPS_BY_FLAP = 340,                                                 // trace actual relative steps by flap
    REPORT_REGISTRY      = 350,                                                 // trace content of registry list
    REPORT_I2C_STATISTIC = 360,                                                 // trace i2c usage history
    REPORT_LIGA_TABLE    = 370                                                  // trace liga tabelle
};                                                                              // list of possible twin commands

// Command that will be accepted byTwin
struct TwinCommand {
    TwinCommands  twinCommand;                                                  // command to be performed by twin
    int           twinParameter;                                                // parameter for command
    QueueHandle_t responsQueue;                                                 // queue, where result shall be responded
};

// Command that will be accepted byRepoting
struct ReportCommand {
    ReportCommands repCommand;                                                  // command to be performed by reporting
    QueueHandle_t  responsQueue;                                                // queue, where result shall be responded
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
    void performAvailability();                                                 // check availability of twin device
    void performRegister();                                                     // register twin device
    void bootRelease();                                                         // release bootFlag an calibrate

    // Helper
    bool  readAllParameters(slaveParameter& p);                                 // Reads all parameters from the slave device.
    bool  readSerialNumber(uint32_t& outSerial);                                // Reads only the serial number from the slave device.
    bool  readOffset(uint16_t& outOffset);                                      // Reads the offset value from the slave device.
    bool  readFlaps(uint8_t& outFlaps);                                         // Reads the number of flaps from the slave device.
    bool  readSpeed(uint16_t& outSpeed);                                        // Reads the speed value from the slave device.
    bool  readSteps(uint16_t& outSteps);                                        // Reads the steps value from the slave device.
    bool  readSensorWorking(bool& outSensorWorking);                            // Reads the sensor working status from the slave device.
    bool  askSlaveAboutParameter(slaveParameter& parameter);                    // retrieve all slave parameter
    void  calculateStepsPerFlap();                                              // compute steps needed to move flap by flap
    bool  isSlaveReady();                                                       // check if slave is ready
    bool  getFullStateOfSlave();                                                // get slave state structure
    Key21 ir2Key21(uint64_t ircode);                                            // convert IR code to Key21

    void updateSlaveReadyInfo(uint8_t* data);                                   // take over Read Structure

    // ---------------------------
    // I2C command procedures
    void      i2cLongCommand(LongMessage mess);                                 // send long command to slave
    esp_err_t i2cMidCommand(MidMessage midCmd, I2Caddress slaveaddress, uint8_t* answer, int size); // send mid command to slave
    esp_err_t i2cShortCommand(ShortMessage ShortCommand, uint8_t* answer, int size); // send short command to slave

    // ---------------------------
    // RTOS queue procedures
    void readQueue();                                                           // read command from Twin queue
    void createQueue();                                                         // create queue for Twin commands
    bool sendQueue(TwinCommand twinCmd);                                        // send command to Twin queue
    int  stepsByFlap[MAXIMUM_FLAPS];                                            // steps needed to move flap by flap (Bresenham-artige Verteilung)

   private:
    // -------------------------------
    // private Variables
    int           _targetFlapNumber = -1;                                       // flap to be shown
    QueueHandle_t _twinQueue;                                                   // command entry queue

    // --- Per-instance state for AYR/ready polling ---
    bool     _inAYRwait            = false;                                     // true while AYR-based wait is running
    uint32_t _readyPollGateUntilMs = 0;                                         // next allowed millis() for external ready polls

    // -------------------------------
    // internal Helpers
    void systemHalt(const char* reason, int blinkCode);                         // system halt with reason and blink code
    void twinControl(TwinCommand twinCmd);                                      // handle Twin command
    void logAndRun(const char* message, std::function<void()> action);          // log message and run action
    void printSlaveReadyInfo();                                                 // trace output Read Structure
    void synchSlaveRegistry();                                                  // take over slave parameter to registry
    int  countStepsToMove(int from, int to);                                    // return steps to move fom "from" to "to"

    // ---------------------------
    // I2C Helper
    LongMessage i2cCommandParameter(i2cCommand command, u_int16_t parameter);   // prepare I2C LongCommand from paramter

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

    // Logging helpers – private to SlaveTwin
    void logHexBytes(const char* prefix, const uint8_t* buf, size_t n);
    void logInfo(const char* prefix, const String& value);
    void logInfoU32(const char* prefix, uint32_t v);
    void logInfoU16(const char* prefix, uint16_t v);
    void logInfoU8(const char* prefix, uint8_t v);
    void logErr(const char* msg);

    // AYR-Limiter helpers
    uint32_t               validMsPerRevolution() const;
    uint16_t               validStepsPerRevolution() const;
    uint32_t               stepsToMs(uint32_t steps) const;
    uint32_t               estimateAYRdurationMs(uint8_t cmd, uint16_t par) const;
    uint32_t               withSafety(uint32_t ms, uint8_t longCmd) const;
    bool                   waitUntilYouAreReady(uint8_t longCmd, uint16_t param_sent_to_slave, uint32_t timeout_ms);
    static inline uint16_t normalizeOffset(uint16_t off, uint16_t spr) {
        return (spr == 0) ? 0 : (off % spr);                                    // 0..spr-1
    }

    // AYR helper pipeline
    uint32_t computeEtaWithGuards(uint8_t longCmd, uint16_t param) const;
    uint32_t computeOvershoot(uint8_t longCmd) const;
    uint32_t planFirstPollAt(uint32_t eta_ms, uint8_t longCmd) const;
    void     stretchTimeoutForFirstWindow(uint32_t first_poll_at, uint32_t& timeout_ms) const;
    bool     quietWait(uint32_t t0, uint32_t first_poll_at, uint32_t timeout_ms);
    bool     tryFirstPoll(uint32_t t0, uint8_t longCmd, uint16_t param, uint32_t eta_ms, uint32_t& polls, bool& seenBusy, uint32_t& firstBusy_ms);
    bool     followUpPolling(uint32_t t0, uint32_t timeout_ms, uint32_t eta_ms, uint8_t longCmd, uint16_t param, uint32_t& polls, bool& seenBusy,
                             uint32_t& firstBusy_ms);

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
