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

class SlaveTwin {
   public:
    int            numberOfFlaps = 0;                                           // the number of flaps of each flap drum
    int            slaveAddress  = 0;                                           // I2C Address of the flap
    int            flapNumber    = 0;                                           // actual flap that is displayed
    int            adjustOffset  = 0;                                           // internal adjustment -> will be saved by save
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
    void  calculateStepsPerFlap();                                              // compute steps needed to move flap by flap
    bool  isSlaveReady();                                                       // check if slave is ready
    Key21 ir2Key21(uint64_t ircode);

    void readQueue();
    void createQueue();
    void sendQueue(ClickEvent receivedEvent);

    esp_err_t i2cShortCommand(ShortMessage ShortCommand, uint8_t* answer, int size); // send short command to slave

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


#include <cstddef>

struct Club {
    const char* code;
    const char* name;
};

static constexpr Club Liga[] = {
    // 1. Bundesliga
    { "FCB", "FC Bayern München" },
    { "BVB", "Borussia Dortmund" },
    { "LEV", "Bayer 04 Leverkusen" },
    { "BMG", "Borussia Mönchengladbach" },
    { "SGE", "Eintracht Frankfurt" },
    { "FCA", "FC Augsburg" },
    { "KOE", "1. FC Köln" },
    { "SCF", "SC Freiburg" },
    { "HSV", "Hamburger SV" },
    { "HDH", "1. FC Heidenheim" },
    { "TSG", "TSG Hoffenheim" },
    { "M05", "1. FSV Mainz 05" },
    { "RBL", "RB Leipzig" },
    { "STP", "FC St. Pauli" },
    { "FCU", "Union Berlin" },
    { "VFB", "VfB Stuttgart" },
    { "WOB", "VfL Wolfsburg" },
    { "SVW", "SV Werder Bremen" },

    // 2. Bundesliga
    { "DSC", "Arminia Bielefeld" },
    { "D98", "SV Darmstadt 98" },
    { "SGD", "Dynamo Dresden" },
    { "EBS", "Eintracht Braunschweig" },
    { "ELV", "SV Elversberg" },
    { "FCM", "1. FC Magdeburg" },
    { "F95", "Fortuna Düsseldorf" },
    { "SGF", "SpVgg Greuther Fürth" },
    { "H96", "Hannover 96" },
    { "BSC", "Hertha BSC" },
    { "KSV", "Holstein Kiel" },
    { "FCK", "1. FC Kaiserslautern" },
    { "KSC", "Karlsruher SC" },
    { "FCN", "1. FC Nürnberg" },
    { "MUN", "Preußen Münster" },
    { "PAD", "SC Paderborn 07" },
    { "S04", "FC Schalke 04" },
    { "BOC", "VfL Bochum" }
};

static constexpr std::size_t numLiga =
    sizeof(Liga) / sizeof(Liga[0]);


static constexpr std::size_t numLiga =
    sizeof(Liga) / sizeof(Liga[0]);

// Liefert den Index im Array Liga[] zum Vereins-Kürzel,
// oder -1, wenn das Kürzel nicht gefunden wird.
constexpr int findClubIndex(std::string_view code) {
    for (std::size_t i = 0; i < numLiga; ++i) {
        if (code == Liga[i].code)
            return static_cast<int>(i);
    }
    return -1;
}

// Liefert das Vereins-Kürzel an der Stelle `index`,
// oder eine leere string_view, falls `index` ungültig ist.
constexpr std::string_view clubCodeAt(std::size_t index) {
    return index < numLiga
        ? std::string_view{Liga[index].code}
        : std::string_view{};
}

    */
    // clang-format on

   private:
    QueueHandle_t twinQueue;
    void          systemHalt(const char* reason, int blinkCode);
    void          twinControl(ClickEvent event);
    void          handleDoubleKey(Key21 key);
    void          handleSingleKey(Key21 key);
    void          logAndRun(const char* message, std::function<void()> action);
    char          key21ToDigit(Key21 key);

    void             printSlaveReadyInfo();                                     // trace output Read Structure
    void             updateSlaveReadyInfo(uint8_t* data);                       // take over Read Structure
    i2c_cmd_handle_t buildShortCommand(ShortMessage shortCmd, uint8_t* answer, int size);
    void             logShortRequest(ShortMessage cmd);
    void             logShortResponse(uint8_t* answer, int size);
    void             logShortError(ShortMessage cmd, esp_err_t err);
    void             synchSlaveRegistry(slaveParameter parameter);
    bool             waitUntilSlaveReady(uint32_t timeout_ms);

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
