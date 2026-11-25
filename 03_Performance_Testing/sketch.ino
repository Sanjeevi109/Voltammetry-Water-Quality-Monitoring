// Derived from integrated v4.0; separate build, Wokwi regression pending.
#include <Arduino.h>
#include <math.h>
#include <string.h>

const int ADC_PIN = 26;
const int N = 5;
const float E[N] = {-0.80f, -0.70f, -0.60f, -0.50f, -0.40f};
const float TARGET_LOW = -0.65f, TARGET_HIGH = -0.55f;
const float MAX_PPM = 5.0f;
const float SLOPE = 2.0f;        // synthetic uA / ppm
const float INTERCEPT = 1.0f;    // synthetic uA
const float MIN_PROMINENCE = 0.30f;  // arbitrary prototype acceptance threshold

// "Virtual potentiostat" and "controller" communicate via this struct.
struct ScanPacket {
  float potential[N];
  float current[N];
  bool samplePresent[N];
  bool transportOK;
};
struct Result {
  bool valid;
  float potential;
  float peak_uA;
  float ppm;
  const char *reason;
};


// PB-02: Records the abstract electrode configuration only.
void configureVirtualElectrodes() {
  Serial1.println("PB-02 MODEL: WE=working; RE=reference; CE=counter [NOT physical]");
}

// PB-03: Represents SWV settings/command; DOES NOT synthesize the waveform.
void requestVirtualSWV() {
  Serial1.println("PB-03 MODEL: SWV requested; scan E=-0.80..-0.40 V; step=0.10 V");
}

// PB-04: A virtual potentiostat creates synthetic current samples.
// PB-05: ScanPacket is the *simulated* transport interface to controller.
ScanPacket makeVirtualMeasurement(float injectedPpm, int mode) {
  ScanPacket p;
  p.transportOK = (mode != 4);
  for (int i = 0; i < N; ++i) {
    p.potential[i] = E[i];
    p.current[i] = (i == 1 || i == 3) ? 2.0f : 1.0f;
    p.samplePresent[i] = true;
  }
  p.current[2] = INTERCEPT + SLOPE * injectedPpm;
  if (mode == 1) p.current[0] = 20.0f;   // stronger off-target peak
  if (mode == 2) p.samplePresent[2] = false;
  if (mode == 5) p.current[2] += 3.0f;   // overlapping interference at target
  return p;
}

// PB-06/07/08/09: Select target, check local peak, calibrate and validate.
Result evaluate(const ScanPacket &p, float slope, float intercept) {
  Result r = {false, 0.0f, 0.0f, 0.0f, "UNDETERMINED"};
  if (!p.transportOK) { r.reason = "COMMUNICATION_TIMEOUT"; return r; }
  if (!isfinite(slope) || fabsf(slope) < 0.00001f || !isfinite(intercept)) {
    r.reason = "INVALID_CALIBRATION"; return r;
  }
  int best = -1;
  for (int i = 0; i < N; ++i) {
    if (!p.samplePresent[i]) continue;
    if (!isfinite(p.potential[i])) { r.reason = "INVALID_POTENTIAL"; return r; }
    if (p.potential[i] < TARGET_LOW || p.potential[i] > TARGET_HIGH) continue;
    if (!isfinite(p.current[i])) { r.reason = "INVALID_CURRENT"; return r; }
    if (best < 0 || p.current[i] > p.current[best]) best = i;
  }
  if (best < 0) { r.reason = "MISSING_TARGET_SAMPLE"; return r; }
  r.potential = p.potential[best];
  r.peak_uA = p.current[best];
  if (best == 0 || best == N-1 || !p.samplePresent[best-1] || !p.samplePresent[best+1]) {
    r.reason = "CANNOT_CHECK_PEAK_SHAPE"; return r;
  }
  if (!isfinite(p.current[best-1]) || !isfinite(p.current[best+1])) {
    r.reason = "INVALID_CURRENT"; return r;
  }
  float shoulder = fmaxf(p.current[best-1], p.current[best+1]);
  if (r.peak_uA - shoulder < MIN_PROMINENCE) {
    r.reason = "NO_DISTINCT_LOCAL_PEAK"; return r;
  }
  float c = (r.peak_uA - intercept) / slope;
  if (!isfinite(c) || c < 0.0f || c > MAX_PPM) {
    r.reason = "OUTSIDE_CALIBRATION_RANGE"; return r;
  }
  r.ppm = c;
  r.valid = true;
  r.reason = "VALID_SYNTHETIC_RESULT";
  return r;
}


// Application and test harness share the same acquisition/evaluation/report path.
const uint32_t REPORT_MS = 1000;
uint32_t nextReport = 0;

void reportMeasurement(int mode, int adc, float potVoltage, float injectedPpm,
                       const ScanPacket &packet, const Result &result) {
  Serial1.println("------------------------------");
  Serial1.print("Mode: "); Serial1.println(mode);
  Serial1.print("ADC: "); Serial1.print(adc);
  Serial1.print("  Knob: "); Serial1.print(potVoltage, 2); Serial1.println(" V");
  Serial1.print("Injected synthetic input: "); Serial1.print(injectedPpm, 2); Serial1.println(" ppm");
  if (packet.transportOK) {
    for (int i = 0; i < N; ++i) {
      Serial1.print("E="); Serial1.print(packet.potential[i], 2);
      Serial1.print(" V; I=");
      if (packet.samplePresent[i]) Serial1.print(packet.current[i], 2);
      else Serial1.print("MISSING");
      Serial1.println(" uA");
    }
  } else Serial1.println("Virtual measurement packet NOT delivered");
  Serial1.print("RESULT: "); Serial1.println(result.reason);
  if (result.valid) {
    Serial1.print("Selected potential: "); Serial1.print(result.potential, 2); Serial1.println(" V");
    Serial1.print("Selected current: "); Serial1.print(result.peak_uA, 2); Serial1.println(" uA");
    Serial1.print("Estimated concentration: "); Serial1.print(result.ppm, 2);
    Serial1.println(" ppm (SIMULATED, NOT VERIFIED PESTICIDE)");
  } else {
    Serial1.println("Estimated concentration: NOT AVAILABLE");
  }
  if (mode == 5) {
    Serial1.println("WARNING: Overlapping interference is NOT distinguishable by this model");
  }
}

Result processPacket(const ScanPacket &packet, int mode) {
  return evaluate(packet, mode == 3 ? 0.0f : SLOPE, INTERCEPT);
}

Result runCycle(int mode, int adc, float injectedPpm) {
  requestVirtualSWV();
  ScanPacket packet = makeVirtualMeasurement(injectedPpm, mode);
  Result result = processPacket(packet, mode);
  reportMeasurement(mode, adc, 3.3f * adc / 4095.0f, injectedPpm, packet, result);
  return result;
}


const uint32_t TRIALS = 1000;
const float TEST_PPM = 2.5f;
bool correctNormal(const Result &r, float expected) {
  return r.valid && fabsf(r.ppm - expected) <= 0.02f &&
         fabsf(r.potential - (-0.60f)) <= 0.001f;
}

void printVerdict(bool pass) {
  Serial1.print("TEST RESULT: ");
  Serial1.println(pass ? "PASS" : "FAIL");
}

void waitUntil(uint32_t deadline) {
  while ((int32_t)(millis() - deadline) < 0) delay(1);
}

// PT-01: time evaluate() only; exclude packet generation and UART printing.
void testAlgorithm() {
  Serial1.println("PT-01 ALGORITHM: 1000 trials; max <= 10000 us");
  uint32_t minimum = UINT32_MAX, maximum = 0, errors = 0;
  uint64_t sum = 0;
  for (uint32_t i = 0; i < TRIALS; ++i) {
    float ppm = 1.0f + 4.0f * (i % 101) / 100.0f;
    ScanPacket packet = makeVirtualMeasurement(ppm, 0);
    uint32_t start = micros();
    Result r = evaluate(packet, SLOPE, INTERCEPT);
    uint32_t elapsed = micros() - start;
    if (elapsed < minimum) minimum = elapsed;
    if (elapsed > maximum) maximum = elapsed;
    sum += elapsed;
    if (!correctNormal(r, ppm)) ++errors;
  }
  Serial1.print("Trials: "); Serial1.println(TRIALS);
  Serial1.print("Min us: "); Serial1.println(minimum);
  Serial1.print("Mean us: "); Serial1.println((double)sum / TRIALS, 3);
  Serial1.print("Max us: "); Serial1.println(maximum);
  Serial1.print("Incorrect results: "); Serial1.println(errors);
  if (maximum == 0) Serial1.println("Timing below clock resolution; no positive duration resolved");
  printVerdict(maximum <= 10000 && errors == 0);
}

// PT-02: 10 successive report-start intervals using the same reporting path.
void testCadence() {
  Serial1.println("PT-02 CADENCE: 10 intervals; 900..1100 ms");
  uint32_t previous = 0, minimum = UINT32_MAX, maximum = 0;
  uint32_t deadline = millis();
  bool ok = true;
  for (int i = 0; i <= 10; ++i) {
    waitUntil(deadline);
    uint32_t now = micros();
    if (i > 0) {
      uint32_t interval = now - previous;
      if (interval < minimum) minimum = interval;
      if (interval > maximum) maximum = interval;
      Serial1.print("Interval us: "); Serial1.println(interval);
      if (interval < 900000 || interval > 1100000) ok = false;
    }
    previous = now;
    if (!correctNormal(runCycle(0, 2048, TEST_PPM), TEST_PPM)) ok = false;
    deadline += REPORT_MS;
  }
  Serial1.print("Min interval us: "); Serial1.println(minimum);
  Serial1.print("Max interval us: "); Serial1.println(maximum);
  printVerdict(ok);
}

// PT-03: isolated synchronous processing, not physical transport throughput.
void testThroughput() {
  Serial1.println("PT-03 THROUGHPUT: 1000 synthetic packets; no loss");
  uint32_t produced = 0, consumed = 0, correct = 0;
  uint32_t start = micros();
  for (uint32_t i = 0; i < TRIALS; ++i) {
    float ppm = 1.0f + 4.0f * (i % 101) / 100.0f;
    ScanPacket packet = makeVirtualMeasurement(ppm, 0);
    ++produced;
    Result r = processPacket(packet, 0);
    ++consumed;
    if (correctNormal(r, ppm)) ++correct;
  }
  uint32_t elapsed = micros() - start;
  Serial1.print("Produced: "); Serial1.println(produced);
  Serial1.print("Consumed: "); Serial1.println(consumed);
  Serial1.print("Correct: "); Serial1.println(correct);
  Serial1.print("Lost: "); Serial1.println(produced - consumed);
  Serial1.print("Elapsed us: "); Serial1.println(elapsed);
  if (elapsed > 0) {
    Serial1.print("Packets/s: "); Serial1.println(1000000.0 * consumed / elapsed, 2);
  } else Serial1.println("Packets/s: NOT RESOLVED");
  printVerdict(produced == TRIALS && consumed == produced && correct == produced);
}

// PT-04: elapsed from synthetic fault injection to completion of report writes.
void testErrorLatency() {
  Serial1.println("PT-04 ERROR LATENCY: modes 2/3/4; <=1000 ms each");
  const char *expected[] = {"MISSING_TARGET_SAMPLE", "INVALID_CALIBRATION", "COMMUNICATION_TIMEOUT"};
  bool ok = true;
  for (int mode = 2; mode <= 4; ++mode) {
    uint32_t start = micros();
    Result r = runCycle(mode, 2048, TEST_PPM);
    Serial1.flush();
    uint32_t elapsed = micros() - start;
    bool pass = !r.valid && strcmp(r.reason, expected[mode-2]) == 0 && elapsed <= 1000000;
    Serial1.print("Fault mode: "); Serial1.println(mode);
    Serial1.print("Error report latency us: "); Serial1.println(elapsed);
    printVerdict(pass);
    ok = ok && pass;
  }
  Serial1.println("PT-04 OVERALL"); printVerdict(ok);
}

// PT-05: switch from timeout to normal and resume on the next scheduled cycle.
void testRecovery() {
  Serial1.println("PT-05 RECOVERY: mode 4 to 0; <=2000 ms");
  Result fault = runCycle(4, 2048, TEST_PPM);
  Serial1.flush();
  uint32_t start = micros();
  uint32_t deadline = millis() + REPORT_MS;
  waitUntil(deadline);
  Result recovered = runCycle(0, 2048, TEST_PPM);
  Serial1.flush();
  uint32_t elapsed = micros() - start;
  Serial1.print("Recovery latency us: "); Serial1.println(elapsed);
  printVerdict(!fault.valid && strcmp(fault.reason, "COMMUNICATION_TIMEOUT") == 0 &&
               correctNormal(recovered, TEST_PPM) && elapsed <= 2000000);
}

// PT-06: run full reporting path for ten simulated minutes at nominal cadence.
void testStability() {
  Serial1.println("PT-06 STABILITY: 600000 ms simulated time; do not stop simulation");
  uint32_t start = millis(), deadline = start, cycles = 0, errors = 0;
  uint32_t lastHeartbeat = start;
  while (millis() - start < 600000UL) {
    waitUntil(deadline);
    if (millis() - start >= 600000UL) break;
    if (!correctNormal(runCycle(0, 2048, TEST_PPM), TEST_PPM)) ++errors;
    ++cycles;
    deadline += REPORT_MS;
    if (millis() - lastHeartbeat >= 60000UL) {
      Serial1.print("Stability elapsed ms: "); Serial1.println(millis() - start);
      Serial1.print("Cycles: "); Serial1.println(cycles);
      lastHeartbeat = millis();
    }
  }
  Serial1.print("Elapsed ms: "); Serial1.println(millis() - start);
  Serial1.print("Completed cycles: "); Serial1.println(cycles);
  Serial1.print("Incorrect results: "); Serial1.println(errors);
  printVerdict(millis() - start >= 600000UL && cycles >= 600 && errors == 0);
}

void setup() {
  Serial1.begin(115200);
  analogReadResolution(12);
  Serial1.println("SPRINT 4 PERFORMANCE TEST - SYNTHETIC ONLY");
  Serial1.println("p=PT01 c=PT02 t=PT03 e=PT04 r=PT05 s=PT06 h=help");
  configureVirtualElectrodes();
}
void loop() {
  if (Serial1.available()) {
    switch (Serial1.read()) {
      case 'p': testAlgorithm(); break;
      case 'c': testCadence(); break;
      case 't': testThroughput(); break;
      case 'e': testErrorLatency(); break;
      case 'r': testRecovery(); break;
      case 's': testStability(); break;
      case 'h': case 'H': Serial1.println("p=PT01 c=PT02 t=PT03 e=PT04 r=PT05 s=PT06 h=help"); break;
      default: break;
    }
  }
  delay(1);
}
