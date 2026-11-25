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
ScanPacket makeVirtualMeasurement(float injectedPpm) {
  ScanPacket p;
  p.transportOK = true;
  for (int i = 0; i < N; ++i) {
    p.potential[i] = E[i];
    p.current[i] = (i == 1 || i == 3) ? 2.0f : 1.0f;
    p.samplePresent[i] = true;
  }
  p.current[2] = INTERCEPT + SLOPE * injectedPpm;
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

void reportMeasurement(int adc, float potVoltage, float injectedPpm,
                       const ScanPacket &packet, const Result &result) {
  Serial1.println("------------------------------");
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
}

Result processPacket(const ScanPacket &packet) {
  return evaluate(packet, SLOPE, INTERCEPT);
}

Result runCycle(int adc, float injectedPpm) {
  requestVirtualSWV();
  ScanPacket packet = makeVirtualMeasurement(injectedPpm);
  Result result = processPacket(packet);
  reportMeasurement(adc, 3.3f * adc / 4095.0f, injectedPpm, packet, result);
  return result;
}

void setup() {
  Serial1.begin(115200);
  analogReadResolution(12);
  Serial1.println("SPRINT 4 STANDALONE - SYNTHETIC ONLY");
  configureVirtualElectrodes();
  nextReport = millis();
}
void loop() {
  if ((int32_t)(millis() - nextReport) >= 0) {
    nextReport += REPORT_MS;
    int adc = analogRead(ADC_PIN);
    float ppm = MAX_PPM * adc / 4095.0f;
    runCycle(adc, ppm);
    if ((int32_t)(millis() - nextReport) >= 0) nextReport = millis() + REPORT_MS;
  }
  delay(1);
}
