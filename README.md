# Sprint 4 | RC Boat Voltammetry subsystem

ELN0008 Applied Project #1. Synthetic Wokwi Pico proof of concept, not physical pesticide detection.

## Reproduce tested build
Open `04_Integrated_Test/sketch.ino` and its `diagram.json` in a Raspberry Pi Pico Arduino-Pico Wokwi project. Serial1 at 115200; potentiometer GP26, UART GP0/GP1. Commands 0–5 select functional modes; p/c/t/e/r/s run performance tests; h displays help. Use midpoint for normal/off-target/overlap, zero for blank, maximum for upper boundary. Stability runs 600 simulated seconds.

## Separate builds
Each folder contains its own sketch and circuit. `01_Application` normal-only; `02_Functional_Testing` modes 0–5; `03_Performance_Testing` p/c/t/e/r/s. These are derived from the integrated code and require Wokwi regression. Only integrated v4.0 is the source of recorded Wokwi evidence.

## Results
Nine FT and six PT executed. FT-08 warning criterion met, but selectivity fails (+1.50 ppm); open ISS-04. All six simulated performance tests passed. Conservative capability score 14/15. See reports and 07_Evidence for full raw logs.

## Pending student actions
Publish repository, provide real GitHub/demo links and actual commit history, confirm review/retro and actual dates/hours, and verify separated builds. No physical electrochemical validation claimed.
