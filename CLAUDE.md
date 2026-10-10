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
| GATE_OUT   | PB1 | Gate output, HIGH = 5 V                     |
| TRIG_IN    | PA3 | Trigger input, rising edge, via interrupt   |
| LENGTH_POT | PA4 | Pot, analogRead 0..1023 sets gate length    |

## Current behavior (v0.2, tested and working on hardware)
- Rising edge on TRIG_IN starts one HIGH pulse on GATE_OUT.
- Pulse length: linear map, pot 0 -> 10 ms, pot 1023 -> 5000 ms.
- Pulse length is latched at trigger time.
- Edges arriving while GATE_OUT is HIGH are ignored.
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
