# WS2812B Test on Raspberry Pi 5

This guide explains how to compile and run the LED test program.

## Hardware Connections

The WS2812B LEDs operate at 5V, while the Raspberry Pi GPIO is 3.3V.

Wiring:
1. LED 5V -> Raspberry Pi 5V Pin (Pin 2 or 4).
2. LED GND -> Raspberry Pi GND (Pin 6 or 9).
3. LED DIN (Data In) -> GPIO 18 (Physical Pin 12).

**Warnings**:
- Level Shifter: Although it often works directly, the Pi's 3.3V data signal is technically below the 3.5V "High" threshold of 5V LEDs. If the LEDs flicker or don't light up, you need a Logic Level Shifter (3.3V to 5V) on the Data line.
- Power: Powering 50 LEDs individually (one at a time) from the Pi's 5V pin is safe. Do not modify the code to turn them all White at once; that would draw ~3 Amps and could crash your Pi or damage the traces.

## Software Prerequisites

We need the rpi_ws281x library. Since you are on a Pi 5, we must ensure we compile the library locally to match the hardware.

1. Install build tools:
- sudo apt-get update
- sudo apt-get install build-essential git scons cmake
2. Clone and Build the Library:
- git clone [https://github.com/jgarff/rpi_ws281x.git](https://github.com/jgarff/rpi_ws281x.git)
- cd rpi_ws281x
- scons

Note: This creates the libws2811.a static library file.

## Compiling the Test Program

1. Save the C code inside the rpi_ws281x folder you just created (or adjust paths accordingly).
2. Compile command:
Run this command from inside the rpi_ws281x directory:
- gcc -o bin/led_test leds/led_test.c libws2811.a -I. -lm

## Running the Test

The library requires sudo because it accesses hardware memory directly.
- sudo ./bin/led_test

Press ENTER to step through the LEDs one by one.