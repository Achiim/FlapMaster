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

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "SlaveTwin.h"

//--------------------------------

/**
 * @brief Waits until the slave is READY using an ETA-driven polling scheme.
 *
 * @param longCmd Command that was sent (affects ETA/overshoot).
 * @param param_sent_to_slave Parameter sent alongside the command (affects ETA).
 * @param timeout_ms Maximum wait time in ms (may be stretched slightly for first poll window).
 *
 * @return true if READY observed in time.
 * @return false if timeout occurred.
 */
bool SlaveTwin::waitUntilYouAreReady(uint8_t longCmd, uint16_t param_sent_to_slave, uint32_t timeout_ms) {
    const uint32_t t0           = millis();                                     // anchor start time for all elapsed/budget checks
    uint32_t       polls        = 0;                                            // AYR probe counter
    bool           seenBusy     = false;                                        // whether we ever observed BUSY
    uint32_t       firstBusy_ms = 0;                                            // timestamp of first BUSY observation (relative to t0)

    // Pipeline of functionality:
    // compute ETA (+guards)
    // → plan first poll
    // → ensure budget
    // → quiet wait
    // → poll(s)

    const uint32_t eta_ms        = computeEtaWithGuards(longCmd, param_sent_to_slave); // estimate finish time, incl. command-specific guards
    const uint32_t first_poll_at = planFirstPollAt(eta_ms, longCmd);            // pick first-poll time slightly after ETA
    stretchTimeoutForFirstWindow(first_poll_at, timeout_ms);                    // ensure timeout covers the first fast-poll window

    if (!quietWait(t0, first_poll_at, timeout_ms))                              // stay silent until first-poll moment (or timeout)
        return false;                                                           // gave up before first poll → timeout

    if (tryFirstPoll(t0, longCmd, param_sent_to_slave, eta_ms, polls, seenBusy, firstBusy_ms)) // first probe (expected to hit READY)
        return true;                                                            // READY on first try

    return followUpPolling(t0, timeout_ms, eta_ms, longCmd, param_sent_to_slave, polls, seenBusy, firstBusy_ms); // coarse follow-up loop
}

/**
 * @brief Estimates duration (ETA) of a LONG command in ms.
 *
 * @param longCmd Command identifier (e.g. CALIBRATE, MOVE).
 * @param param Command parameter (meaning depends on command).
 *
 * @return ETA in milliseconds (safe range).
 */
uint32_t SlaveTwin::estimateAYRdurationMs(uint8_t longCmd, uint16_t param) const {
    const uint16_t spr    = validStepsPerRevolution();                          // read plausible steps-per-rev (fallback to default if out-of-range)
    const uint16_t offset = normalizeOffset(_parameter.offset, spr);            // normalize stored offset into [0..spr)

    // Base margin used by most commands; ensures the first poll lands just after completion.
    uint32_t margin_ms = (((uint32_t)READY_POLL_MS + 50u) > 150u)               // choose max(READY_POLL_MS+50, 150) …
                             ? ((uint32_t)READY_POLL_MS + 50u)
                             : 150u;                                            // … to avoid polling too early
    if (margin_ms > 300u)                                                       // cap the margin to ≤ 300 ms
        margin_ms = 300u;                                                       // enforce the cap

    uint32_t eta = 0;                                                           // accumulator for the final ETA

    switch (longCmd) {                                                          // select command-specific ETA model
        case CALIBRATE: {                                                       // CALIBRATE: search sensor (≤1 rev) + move to logical zero
            const uint32_t t_search = stepsToMs((uint32_t)spr);                 // time for up to one full revolution (sensor search)
            const uint32_t t_offset = stepsToMs((uint32_t)offset);              // time to move from physical to logical zero (offset)
            eta                     = t_search + t_offset + margin_ms;          // ETA = search + offset + small tail
            break;                                                              // done with CALIBRATE
        }
        case MOVE: {                                                            // MOVE: param is the relative number of steps to move
            const uint32_t base_ms = stepsToMs((uint32_t)param);                // convert steps → time using current speed model
            eta                    = base_ms + margin_ms;                       // ETA = motion time + small tail
            break;                                                              // done with MOVE
        }
        case SPEED_MEASURE: {                                                   // SPEED_MEASURE: typically ~one revolution worth of motion
            const uint32_t rev_ms       = validMsPerRevolution();               // get plausible ms per revolution
            uint32_t       sp_margin_ms = (((uint32_t)READY_POLL_MS + 50u) > 280u) // slightly larger minimum tail for robustness …
                                              ? ((uint32_t)READY_POLL_MS + 50u)
                                              : 280u;                           // … but still within the 300 ms cap
            if (sp_margin_ms > 300u)                                            // enforce global ≤ 300 ms tail
                sp_margin_ms = 300u;                                            // apply cap

            eta = rev_ms + sp_margin_ms;                                        // ETA = one revolution + robust tail
            break;                                                              // done with SPEED_MEASURE
        }
        case SENSOR_CHECK: {                                                    // SENSOR_CHECK: runs exactly 'param' steps; counts detections
            const uint32_t base_ms   = stepsToMs((uint32_t)param);              // nominal time for 'param' steps
            const uint32_t scaled_ms = (base_ms * 8u + 4u) / 5u;                // empirically slower on some units → scale ≈ 1.6x (rounded)

            margin_ms = (READY_POLL_MS > 120u) ? (uint32_t)READY_POLL_MS : 120u; // ensure a small tail so first poll hits after "done"
            if (margin_ms > 300u)                                               // cap tail to ≤ 300 ms
                margin_ms = 300u;                                               // apply cap

            eta = scaled_ms + margin_ms;                                        // ETA = scaled motion time + tail
            // NOTE: no 'break' here → this will FALL THROUGH to 'default' if left as-is.
            // If you do NOT intend fall-through, add 'break;' above.
        }
        case STEP_MEASURE: {                                                    // 3 forward measurements
            const uint16_t off_steps = (param != 0) ? param : offset;           // treat param as offset_steps if provided
            const uint32_t rev_ms    = validMsPerRevolution();                  // ms per revolution

            // per measurement ≈ 2.45 × rev + offset_steps + 1 s pause
            // 2.45 ≈ 49/20 (integer math with rounding).
            const uint32_t t_search_like = (rev_ms * 49u + 10u) / 20u;          // ≈ 2.45 × rev
            const uint32_t t_offset      = stepsToMs((uint32_t)off_steps);      // offset segment
            const uint32_t t_pause       = (uint32_t)STEP_MEASURE_DELAY_MS;     // 1000 ms
            const uint32_t per_meas      = t_search_like + t_offset + t_pause;  // one measurement

            uint32_t tail_ms = (((uint32_t)READY_POLL_MS + 50u) > 120u) ? ((uint32_t)READY_POLL_MS + 50u) : 120u; // modest tail
            if (tail_ms > 300u)
                tail_ms = 300u;                                                 // ≤ 300 ms

            eta = 3u * per_meas + tail_ms;                                      // 3 measurements + final tail
            break;
        }
        default: {                                                              // Fallback for unknown/other commands
            const uint32_t rev_ms = validMsPerRevolution();                     // use one revolution as a conservative baseline

            uint32_t def_margin_ms = (((uint32_t)READY_POLL_MS + 50u) > 200u)   // slightly larger minimum tail for the generic case
                                         ? ((uint32_t)READY_POLL_MS + 50u)
                                         : 200u;                                // pick max(READY_POLL_MS+50, 200)
            if (def_margin_ms > 300u)                                           // cap to ≤ 300 ms
                def_margin_ms = 300u;                                           // apply cap

            eta = rev_ms + def_margin_ms;                                       // ETA = one revolution + generic tail
            break;                                                              // done with default
        }
    }

    // Central safety clamp: keep the ETA within sane bounds for scheduling.
    if (eta < 500u)                                                             // too small → risks premature polling / jitter sensitivity
        eta = 500u;                                                             // lift to minimum ETA
    else if (eta > 60000u)                                                      // too large → not realistic for a single LONG op here
        eta = 60000u;                                                           // clamp to maximum ETA

    return eta;                                                                 // return the command-specific ETA (ms)
}

// --------------------------
// 1) part of waitUntilYouAreReady()
/**
 * @brief Computes ETA with command-specific guards.
 *
 * @param longCmd Command identifier.
 * @param param Command parameter.
 * @return Adjusted ETA in milliseconds.
 */
uint32_t SlaveTwin::computeEtaWithGuards(uint8_t longCmd, uint16_t param) const {
    uint32_t eta_ms = estimateAYRdurationMs(longCmd, param);                    // base ETA from current tuning

    if (longCmd == SENSOR_CHECK) {                                              // SENSOR_CHECK tends to run slower on some devices
        // Ensure ETA is not unrealistically short: ~1.6x nominal + small tail
        const uint32_t base_ms   = stepsToMs((uint32_t)param);                  // nominal time for exactly 'param' steps
        const uint32_t scaled_ms = (base_ms * 8u + 4u) / 5u;                    // scale by ≈1.6 (integer rounding)
        const uint32_t min_eta   = scaled_ms + 120u;                            // add a small tail to keep post-finish lag ≤ ~300 ms
        if (eta_ms < min_eta)                                                   // if base ETA is too short
            eta_ms = min_eta;                                                   // lift it to a safe minimum
    }
    return eta_ms;                                                              // guarded ETA
}

// --------------------------------------
// 2) part of waitUntilYouAreReady()
/**
 * @brief Computes overshoot (ms after ETA before first poll).
 *
 * @param longCmd Command identifier.
 * @return Overshoot in milliseconds.
 */
uint32_t SlaveTwin::computeOvershoot(uint8_t longCmd) const {
    return (longCmd == MOVE)           ? 280u                                   // longer tail for MOVE
           : (longCmd == SENSOR_CHECK) ? 200u                                   // medium tail for SENSOR_CHECK, small for others
           : (longCmd == STEP_MEASURE) ? 120u                                   // tuned for ~28 s runtime
                                       : 20u;
}

// --------------------------------------
// 3) part of waitUntilYouAreReady()
/**
 * @brief Calculates first poll time (ETA + overshoot).
 *
 * @param eta_ms Estimated time to completion.
 * @param longCmd Command identifier.
 * @return First poll timestamp in ms.
 */
uint32_t SlaveTwin::planFirstPollAt(uint32_t eta_ms, uint8_t longCmd) const {
    return eta_ms + computeOvershoot(longCmd);                                  // schedule first poll slightly after ETA
}

// 4) part of waitUntilYouAreReady()
/**
 * @brief Ensures timeout is long enough for first poll window.
 *
 * @param first_poll_at First poll timestamp.
 * @param timeout_ms Reference to timeout budget.
 */
void SlaveTwin::stretchTimeoutForFirstWindow(uint32_t first_poll_at, uint32_t& timeout_ms) const {
    const uint32_t min_timeout = first_poll_at                                  // need to reach the first-poll point
                                 + (uint32_t)READY_WINDOW_MS                    // plus the fast window length
                                 + (uint32_t)READY_POLL_MS;                     // plus a tiny slack (one poll period)
    if (timeout_ms < min_timeout)                                               // if external budget is too small
        timeout_ms = min_timeout;                                               // stretch locally to avoid "quiet" timeout
}

// 5) part of waitUntilYouAreReady()
/**
 * @brief Waits quietly until first poll time or timeout.
 *
 * @param t0 Start timestamp.
 * @param first_poll_at First poll timestamp.
 * @param timeout_ms Timeout in ms.
 * @return true if first poll reached, false if timed out.
 */
bool SlaveTwin::quietWait(uint32_t t0, uint32_t first_poll_at, uint32_t timeout_ms) {
    for (;;) {                                                                  // loop until we reach the first-poll moment
        const uint32_t elapsed = millis() - t0;                                 // how long we have waited so far
        if (elapsed >= first_poll_at)                                           // first-poll time reached?
            return true;                                                        // yes → proceed to polling
        if (elapsed > timeout_ms) {                                             // exceeded budget while waiting?
        #ifdef AYRVERBOSE
            {
            TraceScope trace;                                                   // serialize log output
            twinPrintln("AYR TIMEOUT (quiet)");                                 // diagnostic: timed out before first poll
            }
        #endif
            return false;                                                       // signal timeout
        }
        TickType_t ticks = pdMS_TO_TICKS(5);                                    // convert 5 ms to RTOS ticks
        if (ticks == 0)                                                         // tick rate may be coarser than 1 ms
            ticks = 1;                                                          // ensure we yield at least one tick
        vTaskDelay(ticks);                                                      // yield CPU cooperatively
    }
}

// 6) part of waitUntilYouAreReady()
/**
 * @brief Executes first poll and checks if READY.
 *
 * @param t0 Start timestamp.
 * @param longCmd Command code.
 * @param param Command parameter.
 * @param eta_ms Estimated completion time.
 * @param polls Probe counter (incremented).
 * @param seenBusy Flag set if BUSY observed.
 * @param firstBusy_ms Time of first BUSY.
 * @return true if READY found, false otherwise.
 */
bool SlaveTwin::tryFirstPoll(uint32_t t0, uint8_t longCmd, uint16_t param, uint32_t eta_ms, uint32_t& polls, bool& seenBusy, uint32_t& firstBusy_ms) {
    const bool ready = isSlaveReady();                                          // issue AYR probe (I²C short command)
    polls++;                                                                    // count this probe
    if (ready) {                                                                // hit READY immediately?
    #ifdef AYRVERBOSE
        {
        TraceScope     trace;                                                   // serialize log output
        const uint32_t elapsed = millis() - t0;                                 // time-to-ready
        twinPrint("AYR success cmd=0x");                                        // structured verbose logging
        Serial.print(longCmd, HEX);
        Serial.print(" param=");
        Serial.print(param);
        Serial.print(" elapsed_ms=");
        Serial.print(elapsed);
        Serial.print(" eta_ms=");
        Serial.print(eta_ms);
        Serial.print(" polls=");
        Serial.println(polls);
        }
    #endif
        return true;                                                            // done on first poll
    }
    seenBusy     = true;                                                        // record that we saw BUSY at least once
    firstBusy_ms = millis() - t0;                                               // remember when BUSY was first observed
    return false;                                                               // continue with follow-up polling
}

// 7) part of waitUntilYouAreReady()
/**
 * @brief Continues polling until READY or timeout.
 *
 * @param t0 Start timestamp.
 * @param timeout_ms Maximum wait time.
 * @param eta_ms Estimated completion time.
 * @param longCmd Command code.
 * @param param Command parameter.
 * @param polls Probe counter.
 * @param seenBusy True if BUSY seen.
 * @param firstBusy_ms Time of first BUSY.
 * @return true if READY seen, false if timeout.
 */
bool SlaveTwin::followUpPolling(uint32_t t0, uint32_t timeout_ms, uint32_t eta_ms, uint8_t longCmd, uint16_t param, uint32_t& polls, bool& seenBusy,
                                uint32_t& firstBusy_ms) {
    const uint16_t coarse_interval_ms = 200u;                                   // main cadence between polls
    const uint16_t fine_window_ms     = 200u;                                   // final window for finer cadence

    for (;;) {                                                                  // continue until READY or timeout
        const uint32_t now     = millis();                                      // current timestamp
        const uint32_t elapsed = now - t0;                                      // elapsed since start

        if (elapsed > timeout_ms) {                                             // out of budget?
        #ifdef AYRVERBOSE
            {
            TraceScope trace;                                                   // serialize log output
            twinPrint("AYR TIMEOUT cmd=0x");                                    // diagnostic detail on timeout
            Serial.print(longCmd, HEX);
            Serial.print(" param=");
            Serial.print(param);
            Serial.print(" polls=");
            Serial.print(polls);
            Serial.print(" seenBusy=");
            Serial.print(seenBusy ? 1 : 0);
            Serial.print(" eta_ms=");
            Serial.println(eta_ms);
            }
        #endif
            return false;                                                       // give up on timeout
        }

        const uint32_t remaining = timeout_ms - elapsed;                        // remaining budget
        const uint16_t sleep_ms  = (remaining <= fine_window_ms) ? 20u          // use fine cadence near deadline
                                                                 : coarse_interval_ms; // coarse otherwise

        // Coarse cadence is low;
        TickType_t t = pdMS_TO_TICKS(sleep_ms);
        if (t == 0)
            t = 1;
        vTaskDelay(t);                                                          // sleep between polls

        const bool ready = isSlaveReady();                                      // next AYR probe
        polls++;                                                                // count this probe
        if (ready) {                                                            // READY observed?
        #ifdef AYRVERBOSE
            {
            TraceScope     trace;                                               // serialize log output
            const uint32_t elapsed2 = millis() - t0;                            // final elapsed time
            twinPrint("AYR success cmd=0x");                                    // structured verbose logging
            Serial.print(longCmd, HEX);
            Serial.print(" param=");
            Serial.print(param);
            Serial.print(" elapsed_ms=");
            Serial.print(elapsed2);
            Serial.print(" eta_ms=");
            Serial.print(eta_ms);
            Serial.print(" polls=");
            Serial.print(polls);
            Serial.print(" seenBusy=");
            Serial.print(seenBusy ? 1 : 0);
            Serial.print(" firstBusy_ms=");
            Serial.println(firstBusy_ms);
            }
        #endif
            return true;                                                        // done after follow-up poll
        }
    }
}

// -------------------------------
// ---- plausibility-checked getters (use measured values if sane) ----------
/**
 * @brief Returns ms per revolution (measured if valid, else default).
 *
 * @return Milliseconds per revolution.
 */
uint32_t SlaveTwin::validMsPerRevolution() const {
    const uint32_t v = _parameter.speed ? _parameter.speed : DEFAULT_REV_MS;    // prefer measured, else default
    if (v < REV_MS_MIN || v > REV_MS_MAX)                                       // outside plausibility band?
        return DEFAULT_REV_MS;                                                  // fall back to default
    return v;                                                                   // use measured/plausible value
}

// -------------------------------
/**
 * @brief Returns steps per revolution (measured if valid, else default).
 *
 * @return Steps per revolution.
 */
uint16_t SlaveTwin::validStepsPerRevolution() const {
    const uint16_t s = _parameter.steps ? _parameter.steps : DEFAULT_STEPS_PER_REV; // prefer measured, else default
    if (s < STEPS_MIN || s > STEPS_MAX)                                         // outside plausibility band?
        return DEFAULT_STEPS_PER_REV;                                           // fall back to default
    return s;                                                                   // use measured/plausible value
}

// -------------------------------
/**
 * @brief Converts steps to milliseconds.
 *
 * @param steps Step count.
 * @return Equivalent time in milliseconds.
 */
uint32_t SlaveTwin::stepsToMs(uint32_t steps) const {
    const uint32_t rev_ms = validMsPerRevolution();                             // ms per full revolution (plausibility-checked)
    const uint16_t spr    = validStepsPerRevolution();                          // steps per full revolution (plausibility-checked)
    const uint64_t num    = (uint64_t)steps * (uint64_t)rev_ms + (spr / 2);     // numerator with +0.5*spr for rounding
    return (spr ? (uint32_t)(num / spr) : rev_ms);                              // divide by spr (or guard if spr==0)
}

// ----------------------------

/**
 * @brief Inflates ETA with margin and clamps to valid range.
 * Used as timeout budget for LONG commands.
 *
 * @param ms Base ETA in ms.
 * @param longCmd Command code.
 * @return Time budget in ms.
 */
uint32_t SlaveTwin::withSafety(uint32_t ms, uint8_t longCmd) const {
    uint32_t inflated;
    uint32_t min_ms;
    uint32_t max_ms;

    if (longCmd == MOVE) {
        // MOVE:
        // Small moves are dominated by start/dispatch latency (controller, ISR,
        // mechanics spin-up). Add a fixed overhead so even very small ETAs
        // don’t time out early, then a percentage to absorb variance.
        //
        // Inflation:  +30%  of ms
        // Overhead:   +300 ms (startup/dispatch latency)
        // Clamp:      [800 ms .. 5 s]
        const uint32_t start_overhead_ms = 300u;
        inflated                         = (ms * 130u) / 100u + start_overhead_ms; // +30% + 300 ms
        min_ms                           = 800u;                                // larger floor than default to cover tiny moves
        max_ms                           = 5000u;                               // keep MOVE budgets tight
    } else if (longCmd == CALIBRATE) {
        // CALIBRATE:
        // Can be substantially longer (sensor search + offset travel).
        // Use a moderate percentage and allow a much higher ceiling.
        //
        // Inflation:  +25%
        // Clamp:      [500 ms .. 30 s]
        inflated = (ms * 125u) / 100u;                                          // +25%
        min_ms   = 500u;
        max_ms   = 30000u;
    } else if (longCmd == STEP_MEASURE) {
        inflated = (ms * 120u) / 100u;                                          // +20% across 3 cycles
        min_ms   = 10000u;                                                      // at least 10 s
        max_ms   = 60000u;                                                      // up to 60 s
    } else {
        // Default for other LONG commands:
        // Mild inflation and a compact clamp range.
        //
        // Inflation:  +25%
        // Clamp:      [500 ms .. 5 s]
        inflated = (ms * 125u) / 100u;                                          // +25%
        min_ms   = 500u;
        max_ms   = 5000u;
    }

    // Apply clamps to prevent unrealistic budgets (too small or excessively large).
    if (inflated < min_ms)
        inflated = min_ms;
    if (inflated > max_ms)
        inflated = max_ms;

    return inflated;
}
