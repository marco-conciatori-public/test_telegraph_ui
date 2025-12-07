/**
 * buzzer_interactive_single_pins.c
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
 * gcc -o buzzer/buzzer_interactive_single_pins.exe buzzer/buzzer_interactive_single_pins.c -llgpio
 * * * Execution:
 * sudo ./buzzer/buzzer_interactive_single_pins.exe
 */

#define _DEFAULT_SOURCE // Required for usleep in modern glibc
#define _POSIX_C_SOURCE 200809L 

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h> // For usleep, read
#include <signal.h>
#include <assert.h>
#include <termios.h> // For raw mode input
#include <lgpio.h>  // Replaces pigpio

// --- GPIO Pin Definitions ---
// Note: On Raspberry Pi 5, the 40-pin header is typically controlled 
// by GPIO Chip 4. Use `gpiodetect` to verify if unsure.
#define GPIO_CHIP   4

#define PIN_CLOCK   18  // PWM Pin
#define PIN_VOL_0   23
#define PIN_VOL_1   24
#define PIN_VOL_2   25
#define PIN_VOL_3   22

// Constants
#define PWM_DUTY_50 50.0 // Duty cycle in percentage for lgpio
#define FREQ_STEP   50   // Hz step for arrows
#define MIN_FREQ    100
#define MAX_FREQ    2000

// Global handle for the GPIO chip
int hGpio = -1;

// Terminal settings storage
struct termios orig_termios;

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
 * Restore terminal to original settings (canonical mode)
 */
void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    printf("\nTerminal mode restored.\n");
}

/**
 * Set terminal to raw mode (read char by char, no echo)
 */
void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode); // Ensure restoration on exit
    
    struct termios raw = orig_termios;
    // Disable Echo and Canonical mode (line buffering)
    raw.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

/**
 * Sets the 4-bit volume pins.
 */
void set_volume(bool vol_pin_3, bool vol_pin_2, bool vol_pin_1, bool vol_pin_0) {  
    lgGpioWrite(hGpio, PIN_VOL_3, vol_pin_3);
    lgGpioWrite(hGpio, PIN_VOL_2, vol_pin_2);
    lgGpioWrite(hGpio, PIN_VOL_1, vol_pin_1);
    lgGpioWrite(hGpio, PIN_VOL_0, vol_pin_0);
}

/**
 * Stops the Clock/Tone signal.
 */
void stop_tone() {
    lgTxPwm(hGpio, PIN_CLOCK, 0, 0, 0, 0); // 0 Frequency stops it
    lgGpioWrite(hGpio, PIN_CLOCK, 0);      // Ensure low state
}

/**
 * Starts the Clock/Tone signal at a specific frequency.
 * @param frequency_hz Frequency in Hertz
 */
void start_tone(int frequency_hz) {
    if (frequency_hz <= 0) {
        stop_tone();
        return;
    }
    lgTxPwm(hGpio, PIN_CLOCK, frequency_hz, PWM_DUTY_50, 0, 0);
}

/**
 * Initializes GPIO pins and library.
 */
int setup_gpio() {
    // Open the GPIO chip
    // Try opening chip 4 (Standard Pi 5) if 10 fails, or pass via arg in real scenario
    hGpio = lgGpiochipOpen(GPIO_CHIP);
    
    // Fallback logic could be added here, but sticking to define
    if (hGpio < 0) {
        fprintf(stderr, "Failed to open GPIO Chip %d. \n"
                        "On Pi 5, header pins are usually on Chip 4.\n"
                        "Check 'gpiodetect' output.\n", GPIO_CHIP);
        return -1;
    }

    // Set pin modes to Output
    int err = 0;
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
    set_volume(0, 0, 0, 0);
    stop_tone();

    return 0;
}

int main(void) {
    signal(SIGINT, signal_handler);

    printf("Initializing GPIO (lgpio) for Raspberry Pi 5...\n");
    if (setup_gpio() != 0) {
        return 1;
    }

    printf("System Ready.\n");
    printf("Controls:\n");
    printf("  [a]  Pin 3 (ON/OFF)\n");
    printf("  [s]  Pin 2 (ON/OFF)\n");
    printf("  [d]  Pin 1 (ON/OFF)\n");
    printf("  [f]  Pin 0 (ON/OFF)\n");
    printf("  [ARROWS] Frequency +/- %dHz\n", FREQ_STEP);
    printf("  [q]     Quit\n");
    printf("\n");

    // Enable raw mode for immediate keypress detection
    enable_raw_mode();

    // State variables for individual pins
    bool v3 = 0;
    bool v2 = 0;
    bool v1 = 0;
    bool v0 = 0;

    int current_freq = 440;
    
    // Set initial state
    set_volume(v3, v2, v1, v0);
    start_tone(current_freq);
    
    printf("\rPins [3210]: %d%d%d%d | Freq: %d Hz   ", v3, v2, v1, v0, current_freq);
    fflush(stdout);

    char c;
    while (keep_running && read(STDIN_FILENO, &c, 1) == 1) {
        if (c == 'q') break;

        // Detect ANSI Escape sequences (Arrow keys)
        if (c == '\033') {
            char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) == 0) continue;
            if (read(STDIN_FILENO, &seq[1], 1) == 0) continue;

            if (seq[0] == '[') {
                switch (seq[1]) {
                    case 'C': // RIGHT Arrow
                        if (current_freq < MAX_FREQ) current_freq += FREQ_STEP;
                        break;
                    case 'D': // LEFT Arrow
                        if (current_freq > MIN_FREQ) current_freq -= FREQ_STEP;
                        break;
                }
            }
        } 
        else {
            // Manual Pin Control Logic
            switch(c) {
                case 'a': v3 = !v3; break;
                case 's': v2 = !v2; break;
                case 'd': 
                    v1 = !v1;
                    if (v1) v0 = 0; // Ensure not both 1
                    break;                
                case 'f': 
                    v0 = !v0;
                    if (v0) v1 = 0; // Ensure not both 1
                    break;
            }
        }

        // Apply changes
        set_volume(v3, v2, v1, v0);
        start_tone(current_freq);
        
        // Print status (binary representation of pins)
        printf("\rPins [3210]: %d%d%d%d | Freq: %d Hz   ", v3, v2, v1, v0, current_freq);
        fflush(stdout);
    }

    // --- Cleanup ---
    stop_tone();
    set_volume(0, 0, 0, 0);
    
    lgGpioFree(hGpio, PIN_CLOCK);
    lgGpioFree(hGpio, PIN_VOL_0);
    lgGpioFree(hGpio, PIN_VOL_1);
    lgGpioFree(hGpio, PIN_VOL_2);
    lgGpioFree(hGpio, PIN_VOL_3);

    lgGpiochipClose(hGpio);
    
    return 0;
}