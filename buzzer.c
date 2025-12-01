/**
 * buzzer_control.c
 * * A C program for Raspberry Pi to control a 7-wire buzzer module.
 * * Hardware Requirements:
 * - 1 x Ground (GND)
 * - 1 x 5V (VCC)
 * - 1 x Clock Signal (PWM Frequency Source)
 * - 4 x Volume Data bits (Parallel Interface)
 * * Dependencies:
 * - pigpio library (http://abyz.me.uk/rpi/pigpio/)
 * Install with: sudo apt-get install pigpio
 * * Compilation:
 * gcc -o buzzer buzzer_control.c -lpigpio -lrt -lpthread
 * * Execution:
 * sudo ./buzzer
 * * Author: Gemini (Assisted for Marco Conciatori)
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <assert.h>
#include <pigpio.h>

// --- GPIO Pin Definitions (BCM Numbering) ---
// Adjust these to match your specific wiring scheme
#define PIN_CLOCK   18  // The "Clock" pin driving the frequency (PWM)

// 4-Bit Volume Interface
#define PIN_VOL_0   23  // LSB
#define PIN_VOL_1   24
#define PIN_VOL_2   25
#define PIN_VOL_3   8   // MSB

// Constants
#define PWM_DUTY_50 128 // pigpio duty cycle range is 0-255. 128 is approx 50%
#define MAX_VOLUME  8
#define MIN_VOLUME  0

// Global flag for clean exit on Ctrl+C
volatile int keep_running = 1;

/**
 * Signal handler to catch Ctrl+C and exit the loop safely
 */
void signal_handler(int sig) {
    if (sig == SIGINT) {
        keep_running = 0;
    }
}

/**
 * Sets the 4-bit volume pins based on an integer value (0-15).
 * * @param volume The desired volume level (0-15). Values are clamped.
 */
void set_volume(int volume) {
    int temp_volume = volume;

    if (temp_volume > MAX_VOLUME) temp_volume = MAX_VOLUME;
    if (temp_volume < MIN_VOLUME) temp_volume = MIN_VOLUME;

    if (temp_volume >= 4) {
        temp_volume = temp_volume - 4;
        gpioWrite(PIN_VOL_3, 1);
    }
    else {
        gpioWrite(PIN_VOL_3, 0);
    }
    
    if (temp_volume >= 2) {
        temp_volume = temp_volume - 2;
        gpioWrite(PIN_VOL_3, 1);
    }
    else {
        gpioWrite(PIN_VOL_3, 0);
    }

    // check that volume is either 0 or 1
    assert(temp_volume >= 0 && temp_volume <= 1);
    gpioWrite(PIN_VOL_0, temp_volume);

    // Optional: Print status for debugging
    printf("Volume set to: %d (Binary: %d%d%d%d)\n", volume);
}

/**
 * Starts the Clock/Tone signal at a specific frequency.
 * Uses DMA-timed PWM for stability.
 * * @param frequency_hz Frequency in Hertz
 */
void start_tone(int frequency_hz) {
    if (frequency_hz <= 0) {
        // If 0, stop the tone
        gpioPWM(PIN_CLOCK, 0);
        return;
    }

    // Set the frequency for the pin
    gpioSetPWMfrequency(PIN_CLOCK, frequency_hz);
    
    // Set duty cycle to 50% to generate a square wave
    gpioPWM(PIN_CLOCK, PWM_DUTY_50);
}

/**
 * Stops the Clock/Tone signal.
 */
void stop_tone() {
    gpioPWM(PIN_CLOCK, 0); // 0 Duty cycle = Off
}

/**
 * Initializes GPIO pins and library.
 */
int setup_gpio() {
    if (gpioInitialise() < 0) {
        fprintf(stderr, "pigpio initialization failed\n");
        return -1;
    }

    // Set pin modes to Output
    gpioSetMode(PIN_CLOCK, PI_OUTPUT);
    gpioSetMode(PIN_VOL_0, PI_OUTPUT);
    gpioSetMode(PIN_VOL_1, PI_OUTPUT);
    gpioSetMode(PIN_VOL_2, PI_OUTPUT);
    gpioSetMode(PIN_VOL_3, PI_OUTPUT);

    // Initialize with 0 volume and no tone
    set_volume(0);
    stop_tone();

    return 0;
}

int main(void) {
    // Register signal handler for clean shutdown
    signal(SIGINT, signal_handler);

    printf("Initializing GPIO for Buzzer Control...\n");
    if (setup_gpio() != 0) {
        return 1;
    }

    printf("System Ready. Press Ctrl+C to exit.\n");

    // --- Demo Sequence ---
    
    // 1. Ramp up volume at a constant frequency (e.g., 440Hz - A4)
    printf("Test 1: Ramping Volume Up at 440Hz\n");
    start_tone(440);
    
    for (int v = 0; v <= 15; v++) {
        if (!keep_running) break;
        set_volume(v);
        printf("Volume: %d\n", v);
        gpioSleep(PI_TIME_RELATIVE, 0, 200000); // Sleep 200ms
    }

    if (keep_running) {
        gpioSleep(PI_TIME_RELATIVE, 1, 0); // Wait 1 second
    }

    // 2. Change Frequencies (Melody test) with max volume
    printf("Test 2: Frequency Sweep at Max Volume\n");
    set_volume(15);
    
    int notes[] = {261, 293, 329, 349, 392, 440, 493, 523}; // C Major scale
    for (int i = 0; i < 8; i++) {
        if (!keep_running) break;
        printf("Frequency: %d Hz\n", notes[i]);
        start_tone(notes[i]);
        gpioSleep(PI_TIME_RELATIVE, 0, 500000); // 500ms per note
    }

    // 3. Siren Effect (Modulating both volume and frequency)
    printf("Test 3: Siren Effect\n");
    while (keep_running) {
        // High pitch, high volume
        set_volume(15);
        start_tone(880);
        gpioSleep(PI_TIME_RELATIVE, 0, 300000);
        if (!keep_running) break;

        // Low pitch, lower volume
        set_volume(8);
        start_tone(440);
        gpioSleep(PI_TIME_RELATIVE, 0, 300000);
    }

    // --- Cleanup ---
    printf("\nShutting down...\n");
    stop_tone();
    set_volume(0);
    
    // Reset pins to input (safe state) or clear PWM
    gpioSetMode(PIN_CLOCK, PI_INPUT);
    gpioSetMode(PIN_VOL_0, PI_INPUT);
    gpioSetMode(PIN_VOL_1, PI_INPUT);
    gpioSetMode(PIN_VOL_2, PI_INPUT);
    gpioSetMode(PIN_VOL_3, PI_INPUT);

    gpioTerminate();
    printf("Done.\n");

    return 0;
}