/*
 * WIFE GATE  v0.6
 * MCU:   ATtiny1616
 * Core:  megaTinyCore (Spence Konde)
 *
 * Behavior:
 *   - Rising edge on PA3 (TRIG_IN) starts one HIGH pulse on PB1 (GATE_OUT).
 *   - Pulse length is set by the pot on PA4: 0 -> 10 ms, 1023 -> 5000 ms, linear between.
 *   - At trigger time, PA5 (RANDOM_ADD) is read with analogRead(). A random
 *     value from 0 up to and including that reading is picked, multiplied
 *     by 2, and added to the pulse length as extra milliseconds.
 *   - When a cycle's timer finishes, PB2 (EOC_OUT) fires a fixed 200 ms
 *     HIGH pulse ("end of cycle"). This always fires at the end of every
 *     cycle, including alt mode's silent (LOW) beats -- not just HIGH pulses.
 *   - Rising edges that arrive while GATE_OUT is HIGH are ignored, UNLESS
 *     PB4 (ALT_MODE_SW) is grounded (alt mode). In alt mode, every rising
 *     edge immediately restarts a new cycle (same duration math), and
 *     GATE_OUT alternates: odd edges produce a normal HIGH pulse, even
 *     edges stay LOW for that same duration (a skipped/silent beat).
 *   - Debounce: after an accepted edge, TRIG_IN must stay LOW for
 *     REARM_LOW_MS before another rising edge is accepted (applies in
 *     both modes).
 *   - Pot reading is smoothed with an exponential moving average.
 *   - Pulse length is latched when the pulse starts, so turning the pot
 *     mid-pulse does not change the pulse already in progress.
 *
 * Tools menu: attachInterrupt mode = "Enabled on all pins" (the default).
 */

// ---------- Pins ----------
#define GATE_OUT     PIN_PB1
#define TRIG_IN      PIN_PA3
#define LENGTH_POT   PIN_PA4
#define RANDOM_ADD   PIN_PA5
#define EOC_OUT      PIN_PB2
#define ALT_MODE_SW  PIN_PB4

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
bool     cycleActive  = false;         // true while a cycle's timer is running (HIGH pulse or silent beat)

// ---------- ISR ----------
void onTrigger() {
  bool altMode = (digitalReadFast(ALT_MODE_SW) == LOW);
  // ignore bounce always; ignore edges while the gate is HIGH unless alt mode is active
  if (armed && (altMode || !gateHigh)) {
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

// ---------- Setup ----------
void setup() {
  pinMode(GATE_OUT, OUTPUT);
  digitalWriteFast(GATE_OUT, LOW);

  pinMode(EOC_OUT, OUTPUT);
  digitalWriteFast(EOC_OUT, LOW);

  pinMode(TRIG_IN, INPUT);
  pinMode(ALT_MODE_SW, INPUT_PULLUP);

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

  // Start a pulse on a pending trigger
  if (triggerPending) {
    triggerPending = false;

    uint16_t randomAddRaw = analogRead(RANDOM_ADD);          // 0..1023
    uint16_t randomAdd    = random(0, (long)randomAddRaw + 1); // 0..randomAddRaw

    gateLengthMs = potToMs(smoothedPot()) + (uint32_t)randomAdd * 2;
    gateStartMs  = now;
    cycleActive  = true;

    if (digitalReadFast(ALT_MODE_SW) == LOW) {
      // Alt mode: every edge restarts the cycle immediately, alternating
      // between a HIGH pulse and a silent (LOW) beat of the same duration.
      altPulseOn = !altPulseOn;
      gateHigh   = altPulseOn;
      digitalWriteFast(GATE_OUT, altPulseOn ? HIGH : LOW);
    } else {
      gateHigh = true;
      digitalWriteFast(GATE_OUT, HIGH);
    }
  }

  // End the cycle when its time is up -- always fires EOC_OUT, whether this
  // cycle drove GATE_OUT HIGH or (alt mode's silent beats) left it LOW.
  if (cycleActive && (millis() - gateStartMs >= gateLengthMs)) {
    digitalWriteFast(GATE_OUT, LOW);
    triggerPending = false;            // discard anything that slipped in
    gateHigh       = false;
    cycleActive    = false;

    eocStartMs = now;
    eocHigh    = true;
    digitalWriteFast(EOC_OUT, HIGH);
  }

  // End the EOC pulse when its time is up
  if (eocHigh && (millis() - eocStartMs >= EOC_MS)) {
    digitalWriteFast(EOC_OUT, LOW);
    eocHigh = false;
  }
}
