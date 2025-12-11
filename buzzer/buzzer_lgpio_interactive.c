/**
 * buzzer_lgpio_interactive.c
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
 * gcc -o bin/buzzer_lgpio_interactive buzzer/buzzer_lgpio_interactive.c -llgpio
 * * * Execution:
 * sudo ./bin/buzzer_lgpio_interactive
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
#define GPIO_CHIP   10 

#define PIN_CLOCK   18  // PWM Pin
#define PIN_VOL_0   23
#define PIN_VOL_1   24
#define PIN_VOL_2   25
#define PIN_VOL_3   22

// Constants
#define PWM_DUTY_50 50.0 // Duty cycle in percentage for lgpio
#define MAX_VOLUME  8
#define MIN_VOLUME  0
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
 * Sets the 4-bit volume pins based on an integer value (0-8).
 * @param volume The desired volume level (0-8). Values are clamped.
 */
void set_volume(int volume) {
    int temp_volume = volume;

    if (temp_volume > MAX_VOLUME) temp_volume = MAX_VOLUME;
    if (temp_volume < MIN_VOLUME) temp_volume = MIN_VOLUME;

    bool vol_pin_3 = 0;
    bool vol_pin_2 = 0;
    bool vol_pin_1 = 0;
    bool vol_pin_0 = 0;

    // Logic remains the same, just swapping gpioWrite for lgGpioWrite
    // Note: lgGpioWrite takes the chip handle as the first argument
    
    if (temp_volume >= 4) {
        temp_volume = temp_volume - 4;
        vol_pin_3 = 1;
    }
    if (temp_volume >= 2) {
        temp_volume = temp_volume - 2;
        vol_pin_2 = 1;
    }
    assert(temp_volume >= 0 && temp_volume <= 2);
    if (temp_volume == 2) {
        vol_pin_1 = 1;
    }
    else {
        if (temp_volume == 1) {
            vol_pin_0 = 1;
        }
    }
    lgGpioWrite(hGpio, PIN_VOL_3, vol_pin_3);
    lgGpioWrite(hGpio, PIN_VOL_2, vol_pin_2);
    lgGpioWrite(hGpio, PIN_VOL_1, vol_pin_1);
    lgGpioWrite(hGpio, PIN_VOL_0, vol_pin_0);
    // printf("Volume set to: %d\n", volume);
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
 * Uses lgTxPwm for software/hardware timed PWM.
 * @param frequency_hz Frequency in Hertz
 */
void start_tone(int frequency_hz) {
    if (frequency_hz <= 0) {
        // Stop PWM
        stop_tone();
        return;
    }

    // lgTxPwm(handle, gpio, freq, duty_cycle_percent, pulse_width, pulse_cycles)
    // We use frequency and duty cycle mode.
    lgTxPwm(hGpio, PIN_CLOCK, frequency_hz, PWM_DUTY_50, 0, 0);
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

    printf("System Ready.\n");
    printf("Controls:\n");
    printf("  [UP]    Increase Volume\n");
    printf("  [DOWN]  Decrease Volume\n");
    printf("  [RIGHT] Increase Frequency (+%dHz)\n", FREQ_STEP);
    printf("  [LEFT]  Decrease Frequency (-%dHz)\n", FREQ_STEP);
    printf("  [q]     Quit\n");
    printf("\n");

    // Enable raw mode for immediate keypress detection
    enable_raw_mode();

    int current_vol = 1;
    int current_freq = 440;
    
    // Set initial state
    set_volume(current_vol);
    start_tone(current_freq);
    printf("\rVolume: %d | Freq: %d Hz   ", current_vol, current_freq);
    fflush(stdout);

    char c;
    while (keep_running && read(STDIN_FILENO, &c, 1) == 1) {
        if (c == 'q') break;

        // Detect ANSI Escape sequences (Arrow keys are \033 [ A/B/C/D)
        if (c == '\033') {
            char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) == 0) continue;
            if (read(STDIN_FILENO, &seq[1], 1) == 0) continue;

            if (seq[0] == '[') {
                switch (seq[1]) {
                    case 'A': // UP Arrow
                        if (current_vol < MAX_VOLUME) current_vol++;
                        break;
                    case 'B': // DOWN Arrow
                        if (current_vol > MIN_VOLUME) current_vol--;
                        break;
                    case 'C': // RIGHT Arrow
                        if (current_freq < MAX_FREQ) current_freq += FREQ_STEP;
                        break;
                    case 'D': // LEFT Arrow
                        if (current_freq > MIN_FREQ) current_freq -= FREQ_STEP;
                        break;
                }
            }
        }

        // Apply changes
        set_volume(current_vol);
        start_tone(current_freq);
        
        // Print status (carriage return \r overwrites the line)
        printf("\rVolume: %d | Freq: %d Hz   ", current_vol, current_freq);
        fflush(stdout);
    }

    // --- Cleanup ---
    // Cleanup is handled by signal handler breaking the loop
    // and atexit(disable_raw_mode) restoring terminal.
    
    stop_tone();
    set_volume(0);
    
    // Free the GPIO pins
    lgGpioFree(hGpio, PIN_CLOCK);
    lgGpioFree(hGpio, PIN_VOL_0);
    lgGpioFree(hGpio, PIN_VOL_1);
    lgGpioFree(hGpio, PIN_VOL_2);
    lgGpioFree(hGpio, PIN_VOL_3);

    lgGpiochipClose(hGpio);
    
    // The message "Terminal mode restored" will print automatically via atexit
    return 0;
}