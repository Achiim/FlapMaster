// #################################################################################################################
//
//     █████  ██████  ███████     ██    ██  ██████  ██    ██     ██████  ███████  █████  ██████  ██    ██
//    ██   ██ ██   ██ ██           ██  ██  ██    ██ ██    ██     ██   ██ ██      ██   ██ ██   ██  ██  ██
//    ███████ ██████  █████         ████   ██    ██ ██    ██     ██████  █████   ███████ ██   ██   ████
//    ██   ██ ██   ██ ██             ██    ██    ██ ██    ██     ██   ██ ██      ██   ██ ██   ██    ██
//    ██   ██ ██   ██ ███████        ██     ██████   ██████      ██   ██ ███████ ██   ██ ██████     ██
//
//
// ################################################################################################## by Achim ####
// Banner created:
// https://patorjk.com/software/taag/#p=display&c=c%2B%2B&f=ANSI%20Regular&t=Are%20You%20Ready
//

#ifndef AreYouReadyLimiter_h
#define AreYouReadyLimiter_h

#include <Arduino.h>
#include <FlapGlobal.h>

// ================= READY limiter (ARE-YOU-READY rate limiting) =================
//
// After issuing a LONG command we stay quiet until the ETA (estimated time of arrival),
// then we open a short "fast-poll window" in which we probe readiness (ARE YOU READY)
// at a higher rate. This avoids hammering the i2c bus while still detecting completion
// of LONG command quickly.

// Length (in milliseconds) of the fast-poll window that starts at ETA.
// Within this window the master may send up to READY_WINDOW_MS / READY_POLL_MS
// AYR probes. If the slave is still busy after the window, we fall back to the
// coarse (rough) follow-up logic (or timeout).
static constexpr uint16_t READY_WINDOW_MS = 500;                                // duration of sending AYR shortCommands in ms

// Period (in milliseconds) between AYR probes inside the fast-poll window.
// Smaller → quicker detection but more I²C traffic; larger → fewer probes but
// potentially more latency. With 100 ms and a 500 ms window we attempt at most
// 5 polls per LONG. Typical safe range: 50–200 ms.
static constexpr uint16_t READY_POLL_MS = 100;                                  // interval of sending AYR shortCommands in ms (during READY_WINDOW)

// ===================== tuning / defaults (fallbacks) ==========================
//
// Used as per-slave parameters are known/validated and as guardrails.
// All times are milliseconds unless stated otherwise.

// Default time for one full revolution (fallback speed) used for ETA estimates
// and plausibility checks when the slave has not reported/calibrated speed yet.
// Example default ≈ 3000 ms (~3 s per revolution).
static constexpr uint32_t DEFAULT_REV_MS = DEFAULT_SPEED;

// Default number of steps per full revolution (fallback steps-per-rev) used
// for step↔time conversions and plausibility checks. Example default = 4096.
static constexpr uint16_t DEFAULT_STEPS_PER_REV = DEFAULT_STEPS;

// Minimum spacing (in ms) between successive global AYR probes across twins.
// Acts as a bus-level backoff to avoid back-to-back readiness polls that could
// congest the I²C bus when multiple devices complete around the same time.
static constexpr uint16_t GLOBAL_READY_POLL_GAP_MS = 120;

// =========================== plausibility checks ==============================
//
// Clamps and sanity bounds applied to parameters recovered from EEPROM or
// reported by slaves. Prevents wild values from breaking ETA math.

// Allowed range for revolution time. With DEFAULT_REV_MS ≈ 3000 ms these bounds
// are roughly 90–110% of default (i.e., quick vs. slow units). Adjust the ±
// slack if you change the default. Values outside are clamped/ignored.
static constexpr uint32_t REV_MS_MIN = (DEFAULT_REV_MS - 300);
static constexpr uint32_t REV_MS_MAX = (DEFAULT_REV_MS + 300);

// Allowed range for steps per revolution. With DEFAULT_STEPS_PER_REV = 4096,
// ±410 is roughly 90–110%. Values outside indicate bad calibration/data and
// are clamped/ignored to keep step↔time conversions stable.
static constexpr uint16_t STEPS_MIN = (DEFAULT_STEPS_PER_REV - 410);
static constexpr uint16_t STEPS_MAX = (DEFAULT_STEPS_PER_REV + 410);

// =================== AYR / measurement related tuning =========================
//
// Delay between consecutive step measurements during speed characterization
// (e.g., when performing SPEED_MEASURE). Gives the motor/mechanics time to
// settle and avoids oversampling the bus. Increase if the mechanism needs more
// time to stabilize between reads; decrease to speed up characterization.
static constexpr uint16_t STEP_MEASURE_DELAY_MS = 1000;
//
// ================= READY limiter (ARE-YOU-READY rate limiting) =================

#endif                                                                          // AreYouReadyLimiter_h
