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
#include <FlapGlobal.h>
#include "TracePrint.h"
#include "RemoteControl.h"

class SlaveTwin {
   public:
    int            numberOfFlaps = 0;                                           // the number of flaps of each flap drum
    int            slaveAddress  = 0;                                           // I2C Address of the flap
    int            flapNumber    = 0;                                           // actual flap that is displayed
    u_int16_t      adjustOffset  = 0;                                           // internal adjustment -> will be saved by save
    slaveParameter parameter;                                                   // parameter of Slave in EEPROM
    slaveStatus    slaveReady;                                                  // status of slave
    unsigned long  lastTwinTime = 0;
    uint64_t       lastTwinCode = 0;
    Key21          lastKey      = Key21::UNKNOWN;

    SlaveTwin(int add);                                                         // Twin instance with I2C address

    // ----------------------------
    void  showFlap(char digit);                                                 // Show Flap with this char
    void  Calibrate();                                                          // calibrate flap drum
    void  stepMeasurement();                                                    // measure steps to be used for one revolution
    void  speedMeasurement();                                                   // measure milisecends for one revolution
    void  sensorCheck();                                                        // verify if hall sensor is working
    void  nextFlap();                                                           // one flap forward
    void  prevFlap();                                                           // one flap backward
    void  nextSteps();                                                          // calibration adjustment +50 steps
    void  prevSteps();                                                          // calibration adjustment -50 steps
    void  setOffset();                                                          // save calibration ofset in slave EEPROM
    void  reset();                                                              // do complete factory reset of slave  I2C address = 0x55, no serialNumber, EEPROM 0
    void  askSlaveAboutParameter(uint8_t address, slaveParameter& parameter);   // retrieve all slave parameter
    void  initStepsByFlap();                                                    // compute steps needed to move flap by flap
    Key21 ir2Key21(uint64_t ircode);

    // Registry trace
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

    esp_err_t i2cShortCommand(ShortMessage ShortCommand, uint8_t* answer, int size); // send short command to slave

    // ----------------------------
    template <typename T>
    void appendToStream(std::ostringstream& stream, T value) {
        stream << value;
    }
    /*
        // ----------------------------
        template <typename T, typename... Args>
        void appendToStream(std::ostringstream& stream, T value, Args... args) {
            stream << value;
            appendToStream(stream, args...);
        }
    */
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
    // 40 Flap-Modul
    // -------------
    // String flapFont = "0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ+-?";               // this list represents the sequence
    // of digits on the flap drum
    //

    // 10 Flap-Modul
    // -------------
    String flapFont = "0123456789";                                             // this list represents the sequence of digits on the flap drum

    int targetFlapNumber = -1;                                                  // flap to be shown

    // ----------------------------
    int searchSign(char digit);                                                 // return flapNumber of digit
    int countStepsToMove(int from, int to);                                     // return steps to move fom "from" to "to"
};
#endif                                                                          // SlaveTwin_h
