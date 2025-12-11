#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <lgpio.h> // Unified library for GPIO and I2C
#include <time.h>

// --- CONFIGURATION ---
// The I2C bus index (usually 1 for /dev/i2c-1)
#define I2C_DEV_NUM 1
#define I2C_ADDR 0x23

// Raspberry Pi 5 usually uses Chip 4 for the header pins.
#define GPIO_CHIP 4
#define GPIO_INT_PIN 17

// PCA9555 Commands
#define CMD_INPUT_PORT_0 0x00

// Handles
int hI2c; // Handle for I2C
int hGpio; // Handle for GPIO chip

// Track last state to detect edges
unsigned char last_state[2] = {0xFF, 0xFF};

// Helper for timing (Debounce)
long long current_timestamp_ms() {
    struct timespec te; 
    clock_gettime(CLOCK_MONOTONIC, &te);
    return te.tv_sec*1000LL + te.tv_nsec/1000000LL;
}
long long last_interrupt_time = 0;

// --- INTERRUPT CALLBACK ---
void on_interrupt(int num_alerts, lgGpioAlert_p gpio_alerts, void *userdata) {
    long long now = current_timestamp_ms();
    
    // 1. Debounce: Ignore interrupts within 20ms
    if (now - last_interrupt_time < 20) return;
    last_interrupt_time = now;

    // 2. Read PCA9555 using lgpio Block Read
    // This atomic function performs the datasheet sequence:
    // START -> ADDR+W -> CMD(0x00) -> RESTART -> ADDR+R -> DATA0 -> DATA1 -> STOP
    unsigned char data[2];
    int count = lgI2cReadI2CBlockData(hI2c, CMD_INPUT_PORT_0, (char*)data, 2);

    if (count != 2) {
        fprintf(stderr, "Failed to read I2C data. Error: %s\n", lguErrorText(count));
        return;
    }

    // 3. Logic: Compare new data with old data
    for (int i = 0; i < 8; i++) {
        // Check Port 0
        int isPressed = !((data[0] >> i) & 1);
        int wasPressed = !((last_state[0] >> i) & 1);

        if (isPressed && !wasPressed) {
            printf(">> [INTERRUPT] Button %d on Port 0 Pressed!\n", i);
            
            // Example Actions
            if (i == 0) printf("   -> Sequence A Started\n");
            if (i == 1) printf("   -> Data Logged\n");
        }
    }

    // Update state
    last_state[0] = data[0];
    last_state[1] = data[1];
}

int main() {
    int status;

    // --- STEP 1: OPEN I2C WITH LGPIO ---
    // Opens /dev/i2c-1 for the device at I2C_ADDR
    hI2c = lgI2cOpen(I2C_DEV_NUM, I2C_ADDR, 0);
    if (hI2c < 0) {
        fprintf(stderr, "Failed to open I2C: %s\n", lguErrorText(hI2c));
        return 1;
    }

    // --- STEP 2: INITIAL I2C READ ---
    // Read once to clear any stuck interrupts
    unsigned char init_data[2];
    status = lgI2cReadI2CBlockData(hI2c, CMD_INPUT_PORT_0, (char*)init_data, 2);
    if (status == 2) {
        last_state[0] = init_data[0];
        last_state[1] = init_data[1];
        printf("Initial State: Port0=0x%X, Port1=0x%X\n", init_data[0], init_data[1]);
    } else {
        fprintf(stderr, "Initial I2C read failed: %s\n", lguErrorText(status));
    }

    // --- STEP 3: OPEN GPIO CHIP ---
    hGpio = lgGpiochipOpen(GPIO_CHIP);
    if (hGpio < 0) {
        fprintf(stderr, "Error: Could not open GPIO chip %d.\n", GPIO_CHIP);
        lgI2cClose(hI2c); // Cleanup I2C before exit
        return 1;
    }

    // --- STEP 4: CLAIM PIN AND SET INTERRUPT ---
    status = lgGpioClaimAlert(hGpio, 0, LG_FALLING_EDGE, GPIO_INT_PIN, -1);
    if (status < 0) {
        fprintf(stderr, "Error claiming GPIO alert: %s\n", lguErrorText(status));
        lgGpiochipClose(hGpio);
        lgI2cClose(hI2c);
        return 1;
    }

    lgGpioSetAlertsFunc(hGpio, GPIO_INT_PIN, on_interrupt, NULL);

    printf("Program Running. Waiting for interrupts on GPIO %d (Chip %d)...\n", GPIO_INT_PIN, GPIO_CHIP);
    printf("Press Enter to quit.\n");

    // --- STEP 5: KEEP ALIVE ---
    while (getchar() != '\n');

    // Cleanup
    lgGpiochipClose(hGpio);
    lgI2cClose(hI2c);
    printf("Exiting...\n");
    return 0;
}