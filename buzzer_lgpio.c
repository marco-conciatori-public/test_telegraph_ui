/**
 * buzzer_lgpio.c
 * A C program for Raspberry Pi 5 to control a 7-wire buzzer module.
 * * ADAPTED FOR RASPBERRY PI 5 (RP1 CHIP)
 * * * Hardware Requirements:
 * - 1 x Ground (GND)
 * - 1 x 5V (VCC)
 * - 1 x Clock Signal (PWM Frequency Source)
 * - 4 x Volume Data bits (Parallel Interface)
 * * * Dependencies:
 * - liblgpio-dev (The modern replacement for pigpio on Pi 5)
 * Install with: sudo apt-get install liblgpio-dev
 * * * Compilation:
 * gcc -o buzzer_lgpio.exe buzzer_lgpio.c -llgpio
 * * * Execution:
 * sudo ./buzzer_lgpio.exe
 */

#define _POSIX_C_SOURCE 199309L // For nanosleep if needed, though usleep is used

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> // For usleep
#include <signal.h>
#include <assert.h>
#include <lgpio.h>  // Replaces pigpio

// --- GPIO Pin Definitions ---
// Note: On Raspberry Pi 5, the 40-pin header is typically controlled 
// by GPIO Chip 4. Use `gpiodetect` to verify if unsure.
#define GPIO_CHIP   4 

#define PIN_CLOCK   18  // PWM Pin
#define PIN_VOL_0   23  // LSB
#define PIN_VOL_1   24
#define PIN_VOL_2   25
#define PIN_VOL_3   8   // MSB

// Constants
#define PWM_DUTY_50 50.0 // Duty cycle in percentage for lgpio
#define MAX_VOLUME  8
#define MIN_VOLUME  0

// Global handle for the GPIO chip
int hGpio = -1;

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
 * @param volume The desired volume level (0-15). Values are clamped.
 */
void set_volume(int volume) {
    int temp_volume = volume;

    if (temp_volume > MAX_VOLUME) temp_volume = MAX_VOLUME;
    if (temp_volume < MIN_VOLUME) temp_volume = MIN_VOLUME;

    // Logic remains the same, just swapping gpioWrite for lgGpioWrite
    // Note: lgGpioWrite takes the chip handle as the first argument
    
    if (temp_volume >= 4) {
        temp_volume = temp_volume - 4;
        lgGpioWrite(hGpio, PIN_VOL_3, 1);
    }
    else {
        lgGpioWrite(hGpio, PIN_VOL_3, 0);
    }
    
    if (temp_volume >= 2) {
        temp_volume = temp_volume - 2;
        lgGpioWrite(hGpio, PIN_VOL_3, 1);
    }
    else {
        lgGpioWrite(hGpio, PIN_VOL_3, 0);
    }

    assert(temp_volume >= 0 && temp_volume <= 1);
    lgGpioWrite(hGpio, PIN_VOL_0, temp_volume);

    printf("Volume set to: %d\n", volume);
}

/**
 * Starts the Clock/Tone signal at a specific frequency.
 * Uses lgTxPwm for software/hardware timed PWM.
 * @param frequency_hz Frequency in Hertz
 */
void start_tone(int frequency_hz) {
    if (frequency_hz <= 0) {
        // Stop PWM
        lgTxPwm(hGpio, PIN_CLOCK, 0, 0, 0, 0);
        return;
    }

    // lgTxPwm(handle, gpio, freq, duty_cycle_percent, pulse_width, pulse_cycles)
    // We use frequency and duty cycle mode.
    lgTxPwm(hGpio, PIN_CLOCK, frequency_hz, PWM_DUTY_50, 0, 0);
}

/**
 * Stops the Clock/Tone signal.
 */
void stop_tone() {
    lgTxPwm(hGpio, PIN_CLOCK, 0, 0, 0, 0); // 0 Frequency stops it
    lgGpioWrite(hGpio, PIN_CLOCK, 0);      // Ensure low state
}

/**
 * Initializes GPIO pins and library.
 */
int setup_gpio() {
    // Open the GPIO chip
    hGpio = lgGpiochipOpen(GPIO_CHIP);
    if (hGpio < 0) {
        fprintf(stderr, "Failed to open GPIO Chip %d. \n"
                        "On Pi 5, header pins are usually on Chip 4.\n"
                        "Check 'gpiodetect' output.\n", GPIO_CHIP);
        return -1;
    }

    // Set pin modes to Output
    int err = 0;
    // We OR the errors to check if any failed
    err |= lgGpioClaimOutput(hGpio, 0, PIN_CLOCK, 0);
    err |= lgGpioClaimOutput(hGpio, 0, PIN_VOL_0, 0);
    err |= lgGpioClaimOutput(hGpio, 0, PIN_VOL_1, 0);
    err |= lgGpioClaimOutput(hGpio, 0, PIN_VOL_2, 0);
    err |= lgGpioClaimOutput(hGpio, 0, PIN_VOL_3, 0);

    if (err < 0) {
        fprintf(stderr, "Failed to claim GPIO outputs. Ensure no other process is using them.\n");
        return -1;
    }

    // Initialize with 0 volume and no tone
    set_volume(0);
    stop_tone();

    return 0;
}

int main(void) {
    signal(SIGINT, signal_handler);

    printf("Initializing GPIO (lgpio) for Raspberry Pi 5...\n");
    if (setup_gpio() != 0) {
        return 1;
    }

    printf("System Ready. Press Ctrl+C to exit.\n");

    // --- Demo Sequence ---
    
    // 1. Ramp up volume at 440Hz
    printf("Test 1: Ramping Volume Up at 440Hz\n");
    start_tone(440);
    
    for (int v = MIN_VOLUME; v <= MAX_VOLUME; v++) {
        if (!keep_running) break;
        set_volume(v);
        usleep(200000); // 200ms
    }

    if (keep_running) {
        usleep(1000000); // Wait 1 second
    }

    // 2. Frequency Sweep
    printf("Test 2: Frequency Sweep at Max Volume\n");
    set_volume(MAX_VOLUME); // Use max volume for melody
    
    int notes[] = {261, 293, 329, 349, 392, 440, 493, 523}; // C Major
    for (int i = 0; i < 8; i++) {
        if (!keep_running) break;
        printf("Frequency: %d Hz\n", notes[i]);
        start_tone(notes[i]);
        usleep(500000); // 500ms
    }

    // 3. Siren Effect
    printf("Test 3: Siren Effect\n");
    while (keep_running) {
        set_volume(MAX_VOLUME);
        start_tone(880);
        usleep(300000); 
        if (!keep_running) break;

        set_volume(MAX_VOLUME / 2);
        start_tone(440);
        usleep(300000);
    }

    // --- Cleanup ---
    printf("\nShutting down...\n");
    stop_tone();
    set_volume(0);
    
    // Free the GPIO pins
    lgGpioFree(hGpio, PIN_CLOCK);
    lgGpioFree(hGpio, PIN_VOL_0);
    lgGpioFree(hGpio, PIN_VOL_1);
    lgGpioFree(hGpio, PIN_VOL_2);
    lgGpioFree(hGpio, PIN_VOL_3);

    lgGpiochipClose(hGpio);
    printf("Done.\n");

    return 0;
}