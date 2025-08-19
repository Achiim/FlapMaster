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

// --------------------------
// Wait until the slave becomes READY using ETA-based "quiet-then-fast" polling.
//
// parameter:
// - longCmd: the LONG command and
// - param_sent_to_slave: parameter that were
//   used for ETA estimation and command-specific tweaks.
//
// - timeout_ms: absolute time budget for this wait. The function never blocks
//   beyond this (it may locally stretch the budget just enough to complete the
//   first fast-poll window so we don't time out silently before polling).
//
// Returns:
//   true -> READY observed within the (possibly locally stretched) budget
//   false -> timeout (no READY seen in time)
// Waits until the slave reports READY using an ETA-driven "quiet then fast" polling scheme.
// - longCmd:              LONG command that has just been sent (affects ETA/overshoot).
// - param_sent_to_slave:  parameter that was sent alongside the LONG (affects ETA).
// - timeout_ms:           absolute time budget; we will not block beyond this (except we
//                         locally stretch just enough to complete the first fast window).
// Returns true if READY was observed in time; false on timeout.
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

// ------------------------------
// Predicts the duration (ETA) in milliseconds for a LONG command, with per-command semantics.
// This function returns a *pure* ETA used by the AYR limiter (no polling-window logic here).
// Inputs:
//   - longCmd : command identifier (e.g., CALIBRATE, MOVE, SENSOR_CHECK, ...)
//   - param   : command parameter (semantics depend on the command; e.g., steps)
// Output:
//   - ETA in milliseconds (clamped to a safe range) that the caller uses to schedule AYR polling.
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
// compute ETA including command-specific guards (SENSOR_CHECK scaling)
// Computes the ETA and enforces command-specific minimums so we do not poll too early.
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
// Command-specific overshoot (how far after ETA we issue the first poll)
// Returns the overshoot in ms we add to the ETA before the first AYR poll.
uint32_t SlaveTwin::computeOvershoot(uint8_t longCmd) const {
    return (longCmd == MOVE) ? 280u                                             // longer tail for MOVE
                             : (longCmd == SENSOR_CHECK ? 200u : 20u);          // medium tail for SENSOR_CHECK, small for others
}

// --------------------------------------
// 3) part of waitUntilYouAreReady()
// Plan absolute time for the first poll attempt
// Combines ETA and overshoot to choose the first-poll timestamp (relative to start).
uint32_t SlaveTwin::planFirstPollAt(uint32_t eta_ms, uint8_t longCmd) const {
    return eta_ms + computeOvershoot(longCmd);                                  // schedule first poll slightly after ETA
}

// 4) part of waitUntilYouAreReady()
// Guarantee timeout covers the first fast window (avoid "quiet" timeouts)
// Ensures the given timeout is large enough to reach and traverse the first fast-poll window.
void SlaveTwin::stretchTimeoutForFirstWindow(uint32_t first_poll_at, uint32_t& timeout_ms) const {
    const uint32_t min_timeout = first_poll_at                                  // need to reach the first-poll point
                                 + (uint32_t)READY_WINDOW_MS                    // plus the fast window length
                                 + (uint32_t)READY_POLL_MS;                     // plus a tiny slack (one poll period)
    if (timeout_ms < min_timeout)                                               // if external budget is too small
        timeout_ms = min_timeout;                                               // stretch locally to avoid "quiet" timeout
}

// 5) part of waitUntilYouAreReady()
// Quiet wait until it's time to poll (yields CPU using RTOS ticks)
// Sleeps in tiny slices until first-poll time or timeout; returns false if budget is exceeded.
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
// First poll; returns true if READY, else marks BUSY (+stats)
// Performs the very first AYR probe. On READY it logs (optional) and returns true; otherwise
// it records BUSY timing and returns false to let the caller continue with follow-up polling.
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
// Coarse follow-up polling until timeout (low AYR count, fine near deadline)
// Polls at a coarse cadence to keep AYR count low; near the deadline it tightens cadence.
// Returns true on READY, false if the overall timeout elapses.
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

        // Coarse cadence is low; blocking delay here is fine. If you prefer RTOS:
        // TickType_t t = pdMS_TO_TICKS(sleep_ms); if (t == 0) t = 1; vTaskDelay(t);
        delay(sleep_ms);                                                        // sleep between polls

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
// Returns ms per revolution using measured value if plausible; otherwise falls back to default.
uint32_t SlaveTwin::validMsPerRevolution() const {
    const uint32_t v = _parameter.speed ? _parameter.speed : DEFAULT_REV_MS;    // prefer measured, else default
    if (v < REV_MS_MIN || v > REV_MS_MAX)                                       // outside plausibility band?
        return DEFAULT_REV_MS;                                                  // fall back to default
    return v;                                                                   // use measured/plausible value
}

// -------------------------------
// Returns steps per revolution using measured value if plausible; otherwise falls back to default.
uint16_t SlaveTwin::validStepsPerRevolution() const {
    const uint16_t s = _parameter.steps ? _parameter.steps : DEFAULT_STEPS_PER_REV; // prefer measured, else default
    if (s < STEPS_MIN || s > STEPS_MAX)                                         // outside plausibility band?
        return DEFAULT_STEPS_PER_REV;                                           // fall back to default
    return s;                                                                   // use measured/plausible value
}

// -------------------------------
// Converts a step count to milliseconds using integer math (rounded).
uint32_t SlaveTwin::stepsToMs(uint32_t steps) const {
    const uint32_t rev_ms = validMsPerRevolution();                             // ms per full revolution (plausibility-checked)
    const uint16_t spr    = validStepsPerRevolution();                          // steps per full revolution (plausibility-checked)
    const uint64_t num    = (uint64_t)steps * (uint64_t)rev_ms + (spr / 2);     // numerator with +0.5*spr for rounding
    return (spr ? (uint32_t)(num / spr) : rev_ms);                              // divide by spr (or guard if spr==0)
}

// ----------------------------
// Adds a safety margin to an ETA and clamps it into sane bounds.
// The result is used as the *timeout budget* for a LONG command’s wait.
// - ms:      base estimate (ETA) for the command duration, in milliseconds
// - longCmd: command code (used to choose per-command inflation/caps)
// Returns:
//   A time budget in milliseconds: inflated by a percentage and/or fixed
//   overhead, then clamped to a [min..max] range depending on the command.
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
