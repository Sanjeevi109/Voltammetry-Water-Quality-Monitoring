/*
 ELN0008 SPRINT 3 — VOLTAMMETRY END-TO-END SOFTWARE PROOF OF CONCEPT
 Sanjeevi + Mackenzie | version 3.0 (simulated interfaces)

 Hardware in Wokwi: Raspberry Pi Pico, potentiometer
 3V3 -> potentiometer VCC; GND -> GND; GP26 -> SIG
 UART0 GP0 (TX) -> $serialMonitor:RX; GP1 (RX) <- $serialMonitor:TX

 MODEL BOUNDARY:
 - Potentiostat, SWV scan, three electrodes, sample response and data
   transfer are SOFTWARE MODELS, NOT a real electrochemical experiment.
 - Calibration slope/intercept and -0.60 V target are ILLUSTRATIVE ONLY.
 - No genuine EmStat Pico USB/UART communication is implemented.

 Serial Monitor commands (type one character and Enter):
 0 normal | 1 strong off-target interferent | 2 missing target sample
 3 invalid calibration | 4 communication timeout | 5 overlapping
 interferent (ambiguous!) | h help
 - A valid result MUST have a local peak and valid calibration.
*/
#include <math.h>

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

int faultMode = 0;

void printHelp() {
  Serial1.println("Commands: 0=normal, 1=off-target peak, 2=missing sample,");
  Serial1.println("3=invalid calibration, 4=timeout, 5=overlapping interferent, h=help");
}

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

void setup() {
  Serial1.begin(115200);
  analogReadResolution(12);
  Serial1.println("SPRINT 3 VOLTAMMETRY POC v3.0 — SYNTHETIC ONLY");
  printHelp();
  configureVirtualElectrodes();
}

void loop() {
  while (Serial1.available()) {
    char command = Serial1.read();
    if (command >= '0' && command <= '5') faultMode = command - '0';
    if (command == 'h' || command == 'H') printHelp();
  }
  int adc = analogRead(ADC_PIN);
  float potVoltage = 3.3f * adc / 4095.0f;
  float injectedPpm = MAX_PPM * adc / 4095.0f;

  requestVirtualSWV();
  ScanPacket packet = makeVirtualMeasurement(injectedPpm, faultMode);
  float slopeForTest = (faultMode == 3) ? 0.0f : SLOPE;
  Result result = evaluate(packet, slopeForTest, INTERCEPT);

  Serial1.println("------------------------------");
  Serial1.print("Mode: "); Serial1.println(faultMode);
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
  if (faultMode == 5) {
    Serial1.println("WARNING: Overlapping interference is NOT distinguishable by this model");
  }
  delay(1000);
}
