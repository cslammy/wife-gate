# WIFE GATE

A Eurorack trigger-to-gate module. A rising edge at the trigger input
produces a gate pulse at the output, with the pulse length set by a panel
pot, a per-trigger random add-on, and a handful of switch-selectable
behaviors (retrigger/alternate mode, a triple HIGH/LOW/HIGH mode, output
invert, duration halving/doubling, and a hard reset).

Firmware is a single Arduino sketch: `WIFE_GATE.ino`.

## Hardware

- **MCU:** ATtiny1616 (tinyAVR 1-series), running on its 20 MHz internal
  oscillator.
- **Core:** [megaTinyCore](https://github.com/SpenceKonde/megaTinyCore)
  (Spence Konde).
- **Programmer:** SerialUPDI, via the UPDI pin (PA0). PA0 is not available
  as a general-purpose I/O pin.
- **Board FQBN:**
  `megaTinyCore:megaavr:atxy6:chip=1616,clock=20internal`

All of the switch inputs listed below are wired as a simple switch to
ground; the firmware enables the ATtiny1616's internal pull-up on each one,
so no external pull-up resistor is needed. Each switch's normal (open)
position reads HIGH and is the "off"/default behavior; closing it to ground
reads LOW and turns the feature on.

## Pinout

| Signal           | Pin | Direction | Function                                                        |
|------------------|-----|-----------|-------------------------------------------------------------------------|
| `GATE_OUT`       | PB1 | Output    | Gate output. HIGH = 5 V (see `INVERT_SW`).                              |
| `TRIG_IN`        | PA3 | Input     | Trigger input. A rising edge starts a new cycle.                        |
| `LENGTH_POT`     | PA4 | Input     | Panel pot (analog). Sets the base gate length.                          |
| `RANDOM_ADD`     | PA5 | Input     | Panel pot/CV (analog). Sets the max random length add-on.               |
| `EOC_OUT`        | PB2 | Output    | End-of-cycle output. Fires a fixed 200 ms HIGH pulse when a cycle ends.  |
| `ALT_MODE_SW`    | PB4 | Input     | Switch to GND. LOW = alternate/retrigger mode.                          |
| `RESET_IN`       | PB5 | Input     | Switch to GND. LOW = force `GATE_OUT` LOW and hold it there.            |
| `TRIPLE_MODE_SW` | PC3 | Input     | Switch to GND. LOW = triple-phase (HIGH/LOW/HIGH) mode.                 |
| `INVERT_SW`      | PC2 | Input     | Switch to GND. LOW = invert `GATE_OUT`'s output polarity.               |
| `HALF_SW`        | PC1 | Input     | Switch to GND. LOW = halve the computed gate duration.                  |
| `DOUBLE_SW`      | PC0 | Input     | Switch to GND. LOW = double the computed gate duration.                 |
| *(UPDI)*         | PA0 | —         | Programming pin. Not available as GPIO.                                 |

## How it works

### Basic operation

A rising edge at `TRIG_IN` starts a cycle: `GATE_OUT` goes HIGH for a
computed duration, then returns LOW. While a cycle is in progress, further
edges at `TRIG_IN` are ignored (except in alt mode, see below). The pot
reading used to compute the duration is smoothed with an exponential
moving average, and the duration is latched the moment the cycle starts —
turning the pot mid-pulse doesn't affect the pulse already running.

### Duration

The base duration is a linear map of `LENGTH_POT`:

- Pot fully CCW (0) → 10 ms
- Pot fully CW (1023) → 5000 ms

At the same moment, `RANDOM_ADD` is read and a random value from 0 up to
and including that reading is chosen, multiplied by 2, and added on top —
e.g. a `RANDOM_ADD` reading of 122 can add up to 244 ms. This combined
value (pot length + random add-on) is the duration used for the rest of
the cycle, and is itself then scaled by `HALF_SW`/`DOUBLE_SW` (see below)
before anything else uses it.

### End-of-cycle output (`EOC_OUT`)

`EOC_OUT` fires a fixed 200 ms HIGH pulse whenever a cycle finishes — a
single HIGH pulse, an alt-mode silent beat, the full HIGH/LOW/HIGH sequence
in triple mode, or a cycle cut short by `RESET_IN`. It is non-blocking and
timed independently of `GATE_OUT`. In alt mode it additionally flashes the
moment each new cycle *starts* (see below). The 200 ms pulse width itself
is never scaled by `HALF_SW`/`DOUBLE_SW`.

### Alt mode (`ALT_MODE_SW`, PB4)

When grounded, every rising edge at `TRIG_IN` immediately starts a new
cycle — it does not wait for the current one to finish, and cancels
whatever was in progress. `GATE_OUT` alternates each edge between a normal
HIGH pulse (1st, 3rd, 5th, ... edges) and a silent beat that holds LOW for
the same duration (2nd, 4th, 6th, ... edges), so the module effectively
divides incoming triggers by two. `EOC_OUT` flashes both at the start of
each new cycle and again when its duration finishes.

### Triple mode (`TRIPLE_MODE_SW`, PC3)

When grounded at the moment a trigger is accepted, the duration is computed
once as usual and the cycle runs three back-to-back phases of that same
duration: `GATE_OUT` goes HIGH, then LOW, then HIGH again. `EOC_OUT` only
fires once, after the third phase finishes — not between phases. New edges
at `TRIG_IN` are ignored for the whole three-phase sequence. Triple mode
takes priority over alt mode if both switches are grounded at once.

### Invert (`INVERT_SW`, PC2)

When grounded, the physical level written to `GATE_OUT` is flipped at
every transition — idle level, pulses, alt mode's silent beats, triple
mode's phases, and `RESET_IN`'s forced-LOW are all inverted. All of the
internal timing and mode logic is computed exactly the same; only the
final `GATE_OUT` pin write is affected.

### Duration scaling (`HALF_SW` PC1, `DOUBLE_SW` PC0)

At the moment a trigger is accepted, the computed duration (pot length +
random add-on) is scaled before it's used for anything else:

| `HALF_SW` | `DOUBLE_SW` | Result            |
|-----------|-------------|--------------------|
| open      | open        | unchanged (x1)     |
| grounded  | open        | halved (x0.5)      |
| open      | grounded    | doubled (x2)       |
| grounded  | grounded    | tripled (x3)       |

Because this scales the single shared duration value before mode-specific
logic runs, it applies uniformly to a normal pulse, alt mode's HIGH/LOW
beat, and all three triple-mode phases alike.

### Reset (`RESET_IN`, PB5)

While grounded, `GATE_OUT` is forced LOW immediately and any cycle in
progress — including mid-sequence in triple mode — is cancelled. If a
cycle was actually running at that moment, `EOC_OUT` still fires its 200 ms
pulse, since the cycle did end, just early. `GATE_OUT` then stays LOW, even
after `RESET_IN` floats back HIGH, until the next rising edge at
`TRIG_IN` — at which point normal (or alt/triple mode) operation resumes.
Any edges at `TRIG_IN` that occur while `RESET_IN` is grounded are
discarded.

### Debounce

An accepted trigger edge disarms the input; it only re-arms once `TRIG_IN`
has been continuously LOW for 5 ms. This applies in every mode and prevents
double-triggering at short gate lengths.

## Building

```sh
arduino-cli compile --fqbn megaTinyCore:megaavr:atxy6:chip=1616,clock=20internal .

**READ MORE:** https://audiodiwhy.blogspot.com/2026/10/ai-relationships-modular-synthesis-and.html
```

Flash with a SerialUPDI programmer connected to PA0, using the Tools menu
setting `attachInterrupt mode = "Enabled on all pins"` (the default).
