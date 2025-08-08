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

    provides functionality on a flap module level to do

    - Flap Drum calibration
    - check for Hall Sensor presence
    - show Flap digt
    - move one Flap (next/prev)
    - move some Steps (next/prev) to set offset steps, if desired flap is not shown after calibration
    - save calibration to EEPROM on Flap module
    - step measurement
    - speed measurement

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
    TWIN_NO_COMMAND,
    TWIN_SHOW_FLAP,
    TWIN_CALIBRATION,
    TWIN_STEP_MEASUREMENT,
    TWIN_SPEED_MEASUREMENT,
    TWIN_SENSOR_CHECK,
    TWIN_NEXT_FLAP,
    TWIN_PREV_FLAP,
    TWIN_NEXT_STEP,
    TWIN_PREV_STEP,
    TWIN_SET_OFFSET,
    TWIN_RESET
};                                                                              // list of possible twin commands

// Command that Twin will accept
struct TwinCommand {
    TwinCommands  twinCommand;                                                  // command to be performed by twin
    int           twinParameter;                                                // parameter for command
    QueueHandle_t responsQueue;                                                 // queue, where result shall be responded
};

class SlaveTwin {
   public:
    int            numberOfFlaps = 0;                                           // the number of flaps of each flap drum
    int            slaveAddress  = 0;                                           // I2C Address of the flap
    int            flapNumber    = 0;                                           // actual flap that is displayed
    int            adjustOffset  = 0;                                           // internal adjustment -> will be saved by save
    unsigned long  lastTwinTime  = 0;
    uint64_t       lastTwinCode  = 0;
    Key21          lastKey       = Key21::UNKNOWN;
    slaveParameter parameter;                                                   // parameter of Slave in EEPROM
    slaveStatus    slaveReady;                                                  // status of slave

    // ----------------------------
    // Constructor
    SlaveTwin(int add);                                                         // Twin instance with I2C address

    // ----------------------------
    // Command functions
    void showFlap(char digit);                                                  // Show Flap with this char
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

    // Helper
    void  askSlaveAboutParameter(uint8_t address, slaveParameter& parameter);   // retrieve all slave parameter
    void  calculateStepsPerFlap();                                              // compute steps needed to move flap by flap
    bool  isSlaveReady();                                                       // check if slave is ready
    Key21 ir2Key21(uint64_t ircode);

    // ---------------------------
    // I2C procedures
    esp_err_t i2cShortCommand(ShortMessage ShortCommand, uint8_t* answer, int size); // send short command to slave

    // ---------------------------
    // RTOS queue procedures
    void readQueue();
    void createQueue();
    void sendQueue(ClickEvent receivedEvent);

    // 10 Flap-Modul
    int stepsByFlap[MAXIMUM_FLAPS] = {409, 409, 411, 409, 409,                  // there a 10 flaps: (4096/10=409 Rest 6)
                                      411, 409, 409, 411, 409};                 // 4096 steps (stepsPerRevolution)

    // clang-format off
    // 40 Flap-Modul
    // -------------
    /*
    int stepsByFlap[MAXIMUM_FLAPS] = {51,51,51,51,52,51,51,51,51,52,            // there are 40 flaps: here is defined how much steps
                                    51,51,51,51,52,51,51,51,51,52,              // are needed to rotate that flap (2048/40=51 Rest 8)
                                    51,51,51,51,52,51,51,51,51,52,              // Every 5th flap will do one step more to reach in sum
                                    51,51,51,51,52,51,51,51,51,52};             // 2048 steps (stepsPerRevolution)



    */
    // clang-format on

   private:
    // -------------------------------
    // private Variables
    int           targetFlapNumber = -1;                                        // flap to be shown
    QueueHandle_t twinQueue;                                                    // command entry queue

    // -------------------------------
    // internal Helpers
    void             systemHalt(const char* reason, int blinkCode);
    void             twinControl(ClickEvent event);
    void             handleDoubleKey(Key21 key);
    void             handleSingleKey(Key21 key);
    void             logAndRun(const char* message, std::function<void()> action);
    char             key21ToDigit(Key21 key);
    void             printSlaveReadyInfo();                                     // trace output Read Structure
    void             updateSlaveReadyInfo(uint8_t* data);                       // take over Read Structure
    i2c_cmd_handle_t buildShortCommand(ShortMessage shortCmd, uint8_t* answer, int size);
    void             logShortRequest(ShortMessage cmd);
    void             logShortResponse(uint8_t* answer, int size);
    void             logShortError(ShortMessage cmd, esp_err_t err);
    void             synchSlaveRegistry(slaveParameter parameter);
    bool             waitUntilSlaveReady(uint32_t timeout_ms);
    int              searchSign(char digit);                                    // return flapNumber of digit
    int              countStepsToMove(int from, int to);                        // return steps to move fom "from" to "to"

    // Twin trace
    template <typename... Args>
    void twinPrint(const Args&... args) {
        char buf[20];
        snprintf(buf, sizeof(buf), "[I2C TWIN 0x%02X  ] ", slaveAddress);       // Prefix mit Adresse
        tracePrint(buf, args...);
    }
    template <typename... Args>
    void twinPrintln(const Args&... args) {
        char buf[20];
        snprintf(buf, sizeof(buf), "[I2C TWIN 0x%02X  ] ", slaveAddress);       // Prefix mit Adresse
        tracePrintln(buf, args...);
    }

    // 40 Flap-Modul
    // -------------
    // String flapFont = "0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ+-?";               // this list represents the sequence
    // of digits on the flap drum
    //

    // 10 Flap-Modul
    // -------------
    String flapFont = "0123456789";                                             // this list represents the sequence of digits on the flap drum

    // ----------------------------
};
#endif                                                                          // SlaveTwin_h
