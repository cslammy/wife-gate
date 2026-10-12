# WIFE GATE

Eurorack trigger-to-gate module firmware. Single Arduino sketch: `WIFE_GATE.ino`.

## Target
- MCU: ATtiny1616 (tinyAVR 1-series), 20 MHz internal clock
- Core: megaTinyCore (Spence Konde). NOT ATmegaCore / MegaCoreX / MiniCore.
- Programmer: SerialUPDI. PA0 is UPDI and is not available as GPIO.
- Board FQBN for arduino-cli: `megaTinyCore:megaavr:atxy6:chip=1616,clock=20internal`
- Use megaTinyCore pin names (`PIN_PA4`, `PIN_PB1`), not bare Arduino pin numbers.
- `digitalWriteFast` / `digitalReadFast` only take compile-time-constant pins.

## Pins
| Name       | Pin | Function                                   |
|------------|-----|--------------------------------------------|
| GATE_OUT     | PB1 | Gate output, HIGH = 5 V                          |
| TRIG_IN      | PA3 | Trigger input, rising edge, via interrupt        |
| LENGTH_POT   | PA4 | Pot, analogRead 0..1023 sets gate length         |
| RANDOM_ADD   | PA5 | analogRead 0..1023 sets max random add-on        |
| EOC_OUT      | PB2 | End-of-cycle output, fixed 200 ms HIGH pulse     |
| ALT_MODE_SW  | PB4 | Switch to GND, INPUT_PULLUP. LOW = alt mode      |
| RESET        | PB5 | Switch to GND, INPUT_PULLUP. LOW = force gate low|
| TRIPLE_MODE_SW | PC3 | Switch to GND, INPUT_PULLUP. LOW = triple mode |

## Current behavior (v1.0, pending hardware test)
- Rising edge on TRIG_IN starts one HIGH pulse on GATE_OUT.
- Pulse length: linear map, pot 0 -> 10 ms, pot 1023 -> 5000 ms.
- At trigger time, RANDOM_ADD is read with analogRead(). A random value from
  0 up to and including that reading is chosen, multiplied by 2, and added
  to the pulse length as extra milliseconds (e.g. reading of 122 -> up to
  244 extra ms).
- Pulse length (including the random add-on) is latched at trigger time.
- When a cycle's timer finishes, EOC_OUT fires a fixed 200 ms HIGH pulse
  ("end of cycle"). This is non-blocking and runs independently of the
  trigger/gate logic, and always fires at the end of every cycle -- including
  alt mode's silent (LOW) beats, not just HIGH pulses.
- ALT_MODE_SW normally reads HIGH (internal pull-up) and operation is as
  above. When grounded (alt mode):
  - Edges are never ignored while GATE_OUT is HIGH -- every rising edge
    immediately restarts a new cycle (duration recomputed fresh each time,
    same pot + random*2 math), canceling whatever cycle was in progress.
  - GATE_OUT alternates each edge: odd edges (1st, 3rd, ...) produce a
    normal HIGH pulse for the new duration; even edges (2nd, 4th, ...)
    hold GATE_OUT LOW for that same duration (a silent/skipped beat).
  - In alt mode, EOC_OUT also flashes immediately when each new cycle
    starts (on every rising edge), in addition to its normal flash when
    the duration runs its course. Normal mode is unaffected -- EOC_OUT
    still only flashes when the duration completes.
  - The REARM_LOW_MS debounce still applies in both modes.
- Edges arriving while a cycle is in progress are ignored (normal and
  triple mode); alt mode is the only exception (see above).
- TRIPLE_MODE_SW normally reads HIGH (internal pull-up). If it's grounded at
  the moment a trigger is accepted, the duration is computed once as usual
  (pot + random*2) and the cycle runs three back-to-back phases of that same
  duration: GATE_OUT goes HIGH, then LOW, then HIGH again. EOC_OUT does not
  fire between phases -- only once, after the third phase finishes. Takes
  priority over alt mode if both switches are grounded at the same trigger.
  The pending trigger that starts the sequence is latched the normal way; new
  edges are ignored for the whole three-phase duration (alt mode's retrigger
  behavior does not apply here).
- RESET normally reads HIGH (internal pull-up). While grounded, GATE_OUT is
  forced LOW immediately and any cycle in progress is cancelled, including
  mid-sequence in triple mode. If a cycle was actually running at that
  moment, EOC_OUT still fires its 200 ms pulse (the cycle ended, just early,
  rather than from timing out). GATE_OUT stays LOW -- even after RESET
  floats high again -- until the next rising edge on TRIG_IN, at which point
  normal (or alt/triple mode) operation resumes. Edges on TRIG_IN that occur
  while RESET is grounded are discarded.
- Pot is smoothed with an EMA (accumulator form, `SMOOTH_SHIFT` = 4, sampled every 2 ms).
- Debounce: an accepted edge disarms the input; it re-arms only after TRIG_IN
  has been continuously LOW for `REARM_LOW_MS` (5 ms). Fixed double-triggering
  at short gate lengths.
- Timing uses `millis()`; loop never blocks.

## Workflow
- Charlie adds features a few at a time, tests on hardware, then asks for more.
  Keep changes small and focused on the request.
- Don't refactor working code unless asked.
- Bump the version in the header comment and update "Current behavior" above
  when behavior changes.
- If arduino-cli is available, compile with the FQBN above before handing back.
- Commit only after Charlie confirms a change works on hardware.
