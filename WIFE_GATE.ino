/*
 * WIFE GATE  v1.1
 * MCU:   ATtiny1616
 * Core:  megaTinyCore (Spence Konde)
 *
 * Behavior:
 *   - If PC2 (INVERT_SW) is grounded, the physical level written to
 *     GATE_OUT is inverted (HIGH<->LOW) at every transition. No other
 *     logic changes -- timing, modes, and EOC_OUT are all unaffected.
 *   - Rising edge on PA3 (TRIG_IN) starts one HIGH pulse on PB1 (GATE_OUT).
 *   - Pulse length is set by the pot on PA4: 0 -> 10 ms, 1023 -> 5000 ms, linear between.
 *   - At trigger time, PA5 (RANDOM_ADD) is read with analogRead(). A random
 *     value from 0 up to and including that reading is picked, multiplied
 *     by 2, and added to the pulse length as extra milliseconds.
 *   - When a cycle's timer finishes, PB2 (EOC_OUT) fires a fixed 200 ms
 *     HIGH pulse ("end of cycle"). This always fires at the end of every
 *     cycle, including alt mode's silent (LOW) beats -- not just HIGH pulses.
 *   - Rising edges that arrive while a cycle is in progress are ignored,
 *     UNLESS PB4 (ALT_MODE_SW) is grounded (alt mode). In alt mode, every
 *     rising edge immediately restarts a new cycle (same duration math),
 *     and GATE_OUT alternates: odd edges produce a normal HIGH pulse, even
 *     edges stay LOW for that same duration (a skipped/silent beat). In alt
 *     mode, EOC_OUT also flashes immediately when each new cycle starts, in
 *     addition to its normal flash when the duration runs its course.
 *   - If PC3 (TRIPLE_MODE_SW) is grounded at trigger time, the duration is
 *     computed once as usual and the cycle runs three phases back to back,
 *     each for that same duration: HIGH, then LOW, then HIGH. EOC_OUT only
 *     fires once, after all three phases finish. Takes priority over alt
 *     mode if both switches are grounded at once.
 *   - PB5 (RESET) grounded forces GATE_OUT LOW immediately and cancels any
 *     cycle in progress (including mid-sequence in triple mode). If a cycle
 *     was actually running, EOC_OUT still fires its 200 ms pulse (the cycle
 *     ended, just early). GATE_OUT stays LOW -- even after RESET floats
 *     high again -- until the next rising edge on TRIG_IN, which resumes
 *     normal operation.
 *   - Debounce: after an accepted edge, TRIG_IN must stay LOW for
 *     REARM_LOW_MS before another rising edge is accepted (applies in
 *     all modes).
 *   - Pot reading is smoothed with an exponential moving average.
 *   - Pulse length is latched when the pulse starts, so turning the pot
 *     mid-pulse does not change the pulse already in progress.
 *
 * Tools menu: attachInterrupt mode = "Enabled on all pins" (the default).
 */

// ---------- Pins ----------
#define GATE_OUT        PIN_PB1
#define TRIG_IN         PIN_PA3
#define LENGTH_POT      PIN_PA4
#define RANDOM_ADD      PIN_PA5
#define EOC_OUT         PIN_PB2
#define ALT_MODE_SW     PIN_PB4
#define RESET_IN        PIN_PB5
#define TRIPLE_MODE_SW  PIN_PC3
#define INVERT_SW       PIN_PC2

// ---------- Settings ----------
const uint32_t MIN_MS        = 10;     // pulse length at pot fully CCW
const uint32_t MAX_MS        = 5000;   // pulse length at pot fully CW
const uint8_t  ADC_SAMPLE_MS = 2;      // pot sample interval
const uint8_t  SMOOTH_SHIFT  = 4;      // EMA weight = 1/16 (raise to 5 or 6 for more smoothing)
const uint8_t  REARM_LOW_MS  = 5;      // input must stay LOW this long before a new edge is accepted
const uint32_t EOC_MS        = 200;    // EOC_OUT pulse length

// ---------- State ----------
volatile bool triggerPending = false;  // set by ISR on a valid rising edge
volatile bool gateHigh       = false;  // true while GATE_OUT is HIGH
volatile bool armed          = true;   // false after an accepted edge until input has been LOW long enough

uint32_t inputLowSinceMs = 0;          // when TRIG_IN was first seen LOW during re-arm
bool     inputWasLow     = false;

uint32_t potAccum     = 0;             // EMA accumulator = average << SMOOTH_SHIFT
uint32_t lastSampleMs = 0;
uint32_t gateStartMs  = 0;
uint32_t gateLengthMs = MIN_MS;

bool     eocHigh      = false;         // true while EOC_OUT is HIGH
uint32_t eocStartMs   = 0;

bool     altPulseOn   = false;         // alternates each edge while in alt mode
bool     cycleActive  = false;         // true while a cycle's timer is running (any phase)

bool     resetLatched = false;         // true from RESET going LOW until the next accepted edge after it releases

bool     tripleMode   = false;         // true while executing a PC3 triple-phase (HIGH/LOW/HIGH) cycle
uint8_t  triplePhase  = 0;             // 1, 2, or 3 -- which phase of the triple sequence is active

// ---------- ISR ----------
void onTrigger() {
  bool altMode = (digitalReadFast(ALT_MODE_SW) == LOW);
  // ignore bounce always; ignore edges while a cycle is in progress unless alt mode is active
  if (armed && (altMode || !cycleActive)) {
    armed          = false;
    triggerPending = true;
  }
}

// ---------- Helpers ----------
uint16_t smoothedPot() {
  return (uint16_t)(potAccum >> SMOOTH_SHIFT);   // 0..1023
}

void samplePot() {
  uint16_t raw = analogRead(LENGTH_POT);
  potAccum = potAccum - (potAccum >> SMOOTH_SHIFT) + raw;
}

uint32_t potToMs(uint16_t pot) {
  return MIN_MS + ((uint32_t)pot * (MAX_MS - MIN_MS)) / 1023UL;
}

// Writes the logical gate level to GATE_OUT, inverting the physical pin
// level when INVERT_SW is grounded. All other logic stays in terms of the
// logical (non-inverted) level.
void writeGateOut(bool high) {
  bool invert = (digitalReadFast(INVERT_SW) == LOW);
  digitalWriteFast(GATE_OUT, (high != invert) ? HIGH : LOW);
}

// ---------- Setup ----------
void setup() {
  pinMode(GATE_OUT, OUTPUT);
  pinMode(INVERT_SW, INPUT_PULLUP);
  writeGateOut(false);

  pinMode(EOC_OUT, OUTPUT);
  digitalWriteFast(EOC_OUT, LOW);

  pinMode(TRIG_IN, INPUT);
  pinMode(ALT_MODE_SW, INPUT_PULLUP);
  pinMode(RESET_IN, INPUT_PULLUP);
  pinMode(TRIPLE_MODE_SW, INPUT_PULLUP);

  // Seed the average with a real reading so the first pulse is correct
  potAccum = (uint32_t)analogRead(LENGTH_POT) << SMOOTH_SHIFT;

  randomSeed(micros());

  attachInterrupt(digitalPinToInterrupt(TRIG_IN), onTrigger, RISING);
}

// ---------- Loop ----------
void loop() {
  uint32_t now = millis();

  // Smooth the pot at a fixed rate
  if (now - lastSampleMs >= ADC_SAMPLE_MS) {
    lastSampleMs = now;
    samplePot();
  }

  // RESET: force GATE_OUT LOW and cancel any cycle in progress while grounded.
  // Stays latched (GATE_OUT held LOW) after RESET releases until the next
  // accepted trigger edge, handled below.
  if (digitalReadFast(RESET_IN) == LOW) {
    if (cycleActive) {                 // a cycle was cut short -- it still ended, so fire EOC
      eocStartMs = now;
      eocHigh    = true;
      digitalWriteFast(EOC_OUT, HIGH);
    }
    resetLatched   = true;
    gateHigh       = false;
    cycleActive    = false;
    tripleMode     = false;
    triplePhase    = 0;
    triggerPending = false;            // discard edges seen while RESET is grounded
    writeGateOut(false);
  }

  // Re-arm only after TRIG_IN has been continuously LOW for REARM_LOW_MS
  if (!armed) {
    if (digitalReadFast(TRIG_IN) == LOW) {
      if (!inputWasLow) {
        inputWasLow     = true;
        inputLowSinceMs = now;
      } else if (now - inputLowSinceMs >= REARM_LOW_MS) {
        inputWasLow = false;
        armed       = true;
      }
    } else {
      inputWasLow = false;             // any HIGH (bounce) restarts the LOW timer
    }
  }

  // Start a pulse on a pending trigger. Edges seen while RESET is grounded
  // are discarded above, so any triggerPending here is a genuine edge --
  // either normal operation, or the first edge after RESET released.
  if (triggerPending) {
    triggerPending = false;
    resetLatched   = false;

    uint16_t randomAddRaw = analogRead(RANDOM_ADD);          // 0..1023
    uint16_t randomAdd    = random(0, (long)randomAddRaw + 1); // 0..randomAddRaw

    gateLengthMs = potToMs(smoothedPot()) + (uint32_t)randomAdd * 2;
    gateStartMs  = now;
    cycleActive  = true;

    if (digitalReadFast(TRIPLE_MODE_SW) == LOW) {
      // Triple mode: one cycle, three back-to-back phases of the same
      // duration -- HIGH, then LOW, then HIGH. EOC_OUT waits for all three.
      tripleMode  = true;
      triplePhase = 1;
      gateHigh    = true;
      writeGateOut(true);
    } else if (digitalReadFast(ALT_MODE_SW) == LOW) {
      // Alt mode: every edge restarts the cycle immediately, alternating
      // between a HIGH pulse and a silent (LOW) beat of the same duration.
      tripleMode = false;
      altPulseOn = !altPulseOn;
      gateHigh   = altPulseOn;
      writeGateOut(altPulseOn);

      // Alt mode also flashes EOC_OUT right when each new cycle starts,
      // in addition to the normal end-of-duration flash below.
      eocStartMs = now;
      eocHigh    = true;
      digitalWriteFast(EOC_OUT, HIGH);
    } else {
      tripleMode = false;
      gateHigh   = true;
      writeGateOut(true);
    }
  }

  // End the current phase when its time is up.
  if (cycleActive && (millis() - gateStartMs >= gateLengthMs)) {
    if (tripleMode && triplePhase < 3) {
      // Advance to the next phase of the triple sequence (HIGH -> LOW -> HIGH),
      // reusing the same duration. No EOC_OUT yet -- the cycle isn't done.
      triplePhase++;
      gateStartMs = now;
      gateHigh    = (triplePhase != 2);  // phase 1 = HIGH, phase 2 = LOW, phase 3 = HIGH
      writeGateOut(gateHigh);
    } else {
      // Cycle complete -- fires EOC_OUT whether this was a single HIGH pulse,
      // an alt-mode silent (LOW) beat, or the last phase of a triple sequence.
      writeGateOut(false);
      triggerPending = false;          // discard anything that slipped in
      gateHigh       = false;
      cycleActive    = false;
      tripleMode     = false;
      triplePhase    = 0;

      eocStartMs = now;
      eocHigh    = true;
      digitalWriteFast(EOC_OUT, HIGH);
    }
  }

  // End the EOC pulse when its time is up
  if (eocHigh && (millis() - eocStartMs >= EOC_MS)) {
    digitalWriteFast(EOC_OUT, LOW);
    eocHigh = false;
  }
}
