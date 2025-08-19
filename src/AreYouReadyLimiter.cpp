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

#include "SlaveTwin.h"

// ----------------------------
/*
bool SlaveTwin::maybePollReady(bool& outReady) {
    // If an AYR wait is active, do not poll from outside.
    if (_inAYRwait) {
        outReady = false;
        return false;
    }

    const uint32_t now = millis();
    if (now < _readyPollGateUntilMs) {
        outReady = false;
        return false;                                                           // throttled: no bus access
    }

    // Perform the short "ARE YOU READY" transaction.
    // (Assumes lower layer handles the I2C mutex.)
    outReady = isSlaveReady();

    // Gate next external poll.
    _readyPollGateUntilMs = now + GLOBAL_READY_POLL_GAP_MS;
    return true;                                                                // we touched the bus
}
*/

// Für unterschiedliche Commands ggf. andere Safety-Kappen
uint32_t SlaveTwin::withSafety(uint32_t ms, uint8_t longCmd) const {
    uint32_t inflated;
    uint32_t min_ms;
    uint32_t max_ms;

    if (longCmd == MOVE) {
        // Für kleine Moves: Start-/Dispatch-Latenz (~250ms) mit abdecken
        const uint32_t start_overhead_ms = 300u;
        inflated                         = (ms * 130u) / 100u + start_overhead_ms; // +30% + 300ms
        min_ms                           = 800u;                                // statt 500ms
        max_ms                           = 5000u;
    } else if (longCmd == CALIBRATE) {
        inflated = (ms * 125u) / 100u;                                          // unverändert
        min_ms   = 500u;
        max_ms   = 30000u;
    } else {
        inflated = (ms * 125u) / 100u;
        min_ms   = 500u;
        max_ms   = 5000u;
    }

    if (inflated < min_ms)
        inflated = min_ms;
    if (inflated > max_ms)
        inflated = max_ms;
    return inflated;
}

// ----------------------------
// Wait until the slave becomes READY using ETA-based "quiet-then-fast" polling.
//
// - longCmd / param_sent_to_slave: the LONG command and its parameter that were
//   just sent; used for ETA estimation and command-specific tweaks.
// - timeout_ms: absolute time budget for this wait. The function never blocks
//   beyond this (it may locally stretch the budget just enough to complete the
//   first fast-poll window so we don't time out silently before polling).
//
// Returns:
//   true  -> READY observed within the (possibly locally stretched) budget
//   false -> timeout (no READY seen in time)
bool SlaveTwin::waitUntilYouAreReady(uint8_t longCmd, uint16_t param_sent_to_slave, uint32_t timeout_ms) {
    const uint32_t t0 = millis();                                               // start timestamp for elapsed-time accounting

    // 1) Compute ETA (can be adjusted by command-specific guards below).
    //    The ETA is when we expect the operation to be finished (or very close),
    //    so we can avoid hammering the bus before that point.
    uint32_t eta_ms = estimateAYRdurationMs(longCmd, param_sent_to_slave);

    // SENSOR_CHECK failsafe:
    // Some devices execute SENSOR_CHECK effectively slower than nominal step timing.
    // Ensure ETA is at least ~1.6x of the nominal step duration plus a small tail,
    // so our first poll lands just after completion instead of too early.
    if (longCmd == SENSOR_CHECK) {
        const uint32_t base_ms   = stepsToMs((uint32_t)param_sent_to_slave);    // nominal time for 'param' steps
        const uint32_t scaled_ms = (base_ms * 8u + 4u) / 5u;                    // ≈ 1.6x with rounding
        const uint32_t min_eta   = scaled_ms + 120u;                            // keep total lag ≤ ~300 ms
        if (eta_ms < min_eta)
            eta_ms = min_eta;
    }

    // Command-specific overshoot:
    // We intentionally poll a little after ETA to minimize the chance of seeing BUSY.
    // - MOVE: larger tail because motion-complete signaling can lag slightly.
    // - SENSOR_CHECK: medium tail to account for measured behavior.
    // - Others: small tail.
    const uint32_t overshoot_ms = (longCmd == MOVE) ? 280u : (longCmd == SENSOR_CHECK ? 200u : 20u);

    // Absolute time for the first poll attempt (quiet until then).
    const uint32_t first_poll_at = eta_ms + overshoot_ms;                       // goal: first poll hits READY

    // Ensure the external timeout doesn't expire before we even get to run the
    // first fast-poll window (which is READY_WINDOW_MS long with READY_POLL_MS cadence).
    // This prevents "quiet" timeouts just before we're about to poll.
    {
        const uint32_t min_timeout = first_poll_at + (uint32_t)READY_WINDOW_MS + (uint32_t)READY_POLL_MS; // small slack after window
        if (timeout_ms < min_timeout)
            timeout_ms = min_timeout;
    }

    // 2) Quiet phase: do nothing until just after ETA (plus overshoot).
    //    This keeps bus traffic low during the operation itself.
    while (true) {
        const uint32_t elapsed = millis() - t0;
        if (elapsed >= first_poll_at)
            break;                                                              // time to issue the first poll

        // If the overall budget is somehow exceeded during quiet wait, abort.
        if (elapsed > timeout_ms) {
            #ifdef AYRVERBOSE
                {
                TraceScope trace;
                twinPrint("AYR TIMEOUT (quiet) cmd=0x");
                Serial.print(longCmd, HEX);
                Serial.print(" param=");
                Serial.print(param_sent_to_slave);
                Serial.print(" budget_ms=");
                Serial.println(timeout_ms);
                }
            #endif
            return false;
        }

        // Sleep a tiny slice to yield the CPU (use RTOS ticks; ensure ≥1 tick).
        TickType_t ticks = pdMS_TO_TICKS(5);
        if (ticks == 0)
            ticks = 1;                                                          // at least one tick
        vTaskDelay(ticks);
    }

    // 3) First poll: if our ETA and overshoot were good, the slave should be READY now.
    uint32_t polls        = 0;                                                  // number of AYR probes we actually sent
    bool     seenBusy     = false;
    uint32_t firstBusy_ms = 0;                                                  // when we first observed BUSY (relative to t0)

    {
        const bool ready = isSlaveReady();
        polls++;
        if (ready) {
            #ifdef AYRVERBOSE
                {
                TraceScope     trace;
                const uint32_t elapsed = millis() - t0;
                twinPrint("AYR success cmd=0x");
                Serial.print(longCmd, HEX);
                Serial.print(" param=");
                Serial.print(param_sent_to_slave);
                Serial.print(" elapsed_ms=");
                Serial.print(elapsed);
                Serial.print(" eta_ms=");
                Serial.print(eta_ms);
                Serial.print(" polls=");
                Serial.println(polls);
                }
            #endif
            return true;                                                        // done on first poll
        }
        // Still busy — remember when we saw it and fall through to coarse follow-up.
        seenBusy     = true;
        firstBusy_ms = millis() - t0;
    }

    // 4) Coarse follow-up polling until timeout.
    //    We keep the probe count low: poll coarsely, then tighten cadence
    //    only in the very last fraction of the budget.
    const uint16_t coarse_interval_ms = 200u;                                   // main cadence while there is budget
    const uint16_t fine_window_ms     = 200u;                                   // final "sprint" near the deadline

    for (;;) {
        const uint32_t now     = millis();
        const uint32_t elapsed = now - t0;

        // Hard timeout: give up if we ran out of budget.
        if (elapsed > timeout_ms) {
            #ifdef AYRVERBOSE
                {
                TraceScope trace;
                twinPrint("AYR TIMEOUT cmd=0x");
                Serial.print(longCmd, HEX);
                Serial.print(" param=");
                Serial.print(param_sent_to_slave);
                Serial.print(" polls=");
                Serial.print(polls);
                Serial.print(" seenBusy=");
                Serial.print(seenBusy ? 1 : 0);
                Serial.print(" eta_ms=");
                Serial.println(eta_ms);
                }
            #endif
            return false;
        }

        // Choose a sleep interval: coarse most of the time, fine near deadline.
        const uint32_t remaining = timeout_ms - elapsed;
        const uint16_t sleep_ms  = (remaining <= fine_window_ms) ? 20u : coarse_interval_ms;
        delay(sleep_ms);                                                        // small blocking delay is fine here; frequency is low

        // Next poll attempt.
        const bool ready = isSlaveReady();
        polls++;
        if (ready) {
            #ifdef AYRVERBOSE
                {
                TraceScope     trace;
                const uint32_t elapsed2 = millis() - t0;
                twinPrint("AYR success cmd=0x");
                Serial.print(longCmd, HEX);
                Serial.print(" param=");
                Serial.print(param_sent_to_slave);
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
            return true;
        }
    }
}

// ---- plausibility-checked getters (use measured values if sane) ----------
uint32_t SlaveTwin::validMsPerRevolution() const {
    const uint32_t v = _parameter.speed ? _parameter.speed : DEFAULT_REV_MS;    // measured or default
    if (v < REV_MS_MIN || v > REV_MS_MAX)
        return DEFAULT_REV_MS;                                                  // clamp to default on out-of-range
    return v;
}

uint16_t SlaveTwin::validStepsPerRevolution() const {
    const uint16_t s = _parameter.steps ? _parameter.steps : DEFAULT_STEPS_PER_REV; // measured or default
    if (s < STEPS_MIN || s > STEPS_MAX)
        return DEFAULT_STEPS_PER_REV;                                           // clamp to default on out-of-range
    return s;
}

// Steps -> ms (rounded), integer math only
uint32_t SlaveTwin::stepsToMs(uint32_t steps) const {
    const uint32_t rev_ms = validMsPerRevolution();
    const uint16_t spr    = validStepsPerRevolution();
    const uint64_t num    = (uint64_t)steps * (uint64_t)rev_ms + (spr / 2);     // +0.5 for rounding
    return (spr ? (uint32_t)(num / spr) : rev_ms);                              // guard if spr==0
}

// -----------------------------
// Predict LONG duration in ms (command-specific)
// Returns command-specific ETA in ms (pure model; no window logic here)
// ---------------------------------------
// ETA-Schätzer für AYR-Limiter (Command-spezifisch)
// Zentrale ETA mit Semantik pro Command:
uint32_t SlaveTwin::estimateAYRdurationMs(uint8_t longCmd, uint16_t param) const {
    const uint16_t spr    = validStepsPerRevolution();
    const uint16_t offset = normalizeOffset(_parameter.offset, spr);

    // Base margin used by most commands (kept as-is)
    uint32_t margin_ms = (((uint32_t)READY_POLL_MS + 50u) > 150u) ? ((uint32_t)READY_POLL_MS + 50u) : 150u;
    if (margin_ms > 300u)
        margin_ms = 300u;

    uint32_t eta = 0;

    switch (longCmd) {
        case CALIBRATE: {
            // up to one revolution to find sensor + offset to logical zero
            const uint32_t t_search = stepsToMs((uint32_t)spr);
            const uint32_t t_offset = stepsToMs((uint32_t)offset);
            eta                     = t_search + t_offset + margin_ms;
            break;
        }
        case MOVE: {
            // param = relative steps
            const uint32_t base_ms = stepsToMs((uint32_t)param);
            eta                    = base_ms + margin_ms;
            break;
        }
        case SPEED_MEASURE: {
            // Measurement lasts about one revolution; slightly larger minimal margin for robustness
            const uint32_t rev_ms = validMsPerRevolution();
            uint32_t       sp_margin_ms =
                (((uint32_t)READY_POLL_MS + 50u) > 280u) ? ((uint32_t)READY_POLL_MS + 50u) : 280u; // bump min to ~280 ms (≤ 300 ms cap)
            if (sp_margin_ms > 300u)
                sp_margin_ms = 300u;

            eta = rev_ms + sp_margin_ms;
            break;
        }
        case SENSOR_CHECK: {
            // Runs exactly 'param' steps (counts detections, no early stop).
            // On some devices (e.g., 0x56) this is effectively slower than nominal → scale by ~1.6x.
            const uint32_t base_ms   = stepsToMs((uint32_t)param);              // nominal
            const uint32_t scaled_ms = (base_ms * 8u + 4u) / 5u;                // ≈ 1.6x with rounding

            // Small tail so the single AYR poll lands shortly AFTER "done" (stay ≤ 300 ms late).
            margin_ms = (READY_POLL_MS > 120u) ? (uint32_t)READY_POLL_MS : 120u;
            if (margin_ms > 300u)
                margin_ms = 300u;

            eta = scaled_ms + margin_ms;
        }
        default: {
            // Conservative fallback: one revolution + slightly larger minimal margin
            const uint32_t rev_ms = validMsPerRevolution();

            uint32_t def_margin_ms = (((uint32_t)READY_POLL_MS + 50u) > 200u) ? ((uint32_t)READY_POLL_MS + 50u) : 200u;
            if (def_margin_ms > 300u)
                def_margin_ms = 300u;

            eta = rev_ms + def_margin_ms;
            break;
        }
    }

    // Central clamp (safety)
    if (eta < 500u)
        eta = 500u;
    else if (eta > 60000u)
        eta = 60000u;

    return eta;
}
