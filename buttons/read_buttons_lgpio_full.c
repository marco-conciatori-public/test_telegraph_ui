#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <lgpio.h>
#include <time.h>
#include <string.h> // For strerror

// --- CONFIGURATION ---
#define I2C_DEV_NUM 1
#define I2C_ADDR 0x27        // Ensure this matches 'i2cdetect -y 1'
#define GPIO_CHIP 4          // RPi 5 uses Chip 4
#define GPIO_INT_PIN 17      // GPIO pin for interrupt

// PCA9555 Registers
#define REG_INPUT_0 0x00

// Handles
int hI2c;
int hGpio;

// Debounce state
unsigned char last_state[2] = {0xFF, 0xFF};
long long last_interrupt_time = 0;

// Helper: Current time in ms
long long current_timestamp_ms() {
    struct timespec te; 
    clock_gettime(CLOCK_MONOTONIC, &te);
    return te.tv_sec*1000LL + te.tv_nsec/1000000LL;
}

// --- SAFE I2C READ FUNCTION ---
// Replaces BlockRead with Split Write+Read to avoid ENOMSG (-42)
int read_pca9555_inputs(unsigned char *buffer) {
    int status;

    // Step 1: Set the Register Pointer to Input Port 0
    // Transaction: [START] [ADDR+W] [0x00] [STOP]
    status = lgI2cWriteByte(hI2c, REG_INPUT_0);
    if (status < 0) {
        printf("[DEBUG] I2C Write Pointer Failed. Code: %d (%s)\n", status, lguErrorText(status));
        return status;
    }

    // Step 2: Read 2 Bytes from the current pointer
    // Transaction: [START] [ADDR+R] [DATA0] [DATA1] [STOP]
    status = lgI2cReadDevice(hI2c, (char*)buffer, 2);
    if (status < 0) {
        printf("[DEBUG] I2C Read Device Failed. Code: %d (%s)\n", status, lguErrorText(status));
        return status;
    }

    // Check if we actually got 2 bytes
    if (status != 2) {
        printf("[DEBUG] Partial Read. Expected 2 bytes, got %d\n", status);
        return -1;
    }

    return 0; // Success
}

// --- INTERRUPT CALLBACK ---
void on_interrupt(int num_alerts, lgGpioAlert_p gpio_alerts, void *userdata) {
    long long now = current_timestamp_ms();
    
    // Debug print to confirm HW pin triggered
    printf("\n[IRQ] Interrupt Triggered on GPIO %d at %lld ms\n", GPIO_INT_PIN, now);

    // Debounce (50ms)
    if (now - last_interrupt_time < 50) {
        printf("[IRQ] Debounced (Ignored)\n");
        return;
    }
    last_interrupt_time = now;

    unsigned char data[2];
    
    // Attempt to read using the split method
    if (read_pca9555_inputs(data) == 0) {
        printf("[IRQ] Read Success: Port0=0x%02X, Port1=0x%02X\n", data[0], data[1]);

        // Detect Changes
        for (int i = 0; i < 8; i++) {
            int isPressed = !((data[0] >> i) & 1);
            int wasPressed = !((last_state[0] >> i) & 1);

            if (isPressed && !wasPressed) {
                printf(">>> ACTION: Button %d Pressed! <<<\n", i);
            }
        }
        
        // Update state
        last_state[0] = data[0];
        last_state[1] = data[1];
    } else {
        printf("[IRQ] ERROR: Failed to read PCA9555 state.\n");
    }
}

int main() {
    int status;

    printf("--- System Init ---\n");

    // 1. Open I2C
    hI2c = lgI2cOpen(I2C_DEV_NUM, I2C_ADDR, 0);
    if (hI2c < 0) {
        fprintf(stderr, "FATAL: Failed to open I2C bus %d. Error: %s\n", I2C_DEV_NUM, lguErrorText(hI2c));
        return 1;
    }
    printf("I2C Bus Opened. Handle: %d\n", hI2c);

    // 2. Open GPIO
    hGpio = lgGpiochipOpen(GPIO_CHIP);
    if (hGpio < 0) {
        fprintf(stderr, "FATAL: Failed to open GPIO Chip %d. Error: %s\n", GPIO_CHIP, lguErrorText(hGpio));
        lgI2cClose(hI2c);
        return 1;
    }
    printf("GPIO Chip Opened. Handle: %d\n", hGpio);

    // 3. Configure Internal Pull-Up for Interrupt Pin
    printf("Configuring GPIO %d as Input with Pull-Up...\n", GPIO_INT_PIN);
    lgGpioClaimInput(hGpio, LG_SET_PULL_UP, GPIO_INT_PIN);

    // 4. Initial State Check
    printf("Performing initial state read...\n");
    unsigned char init_data[2];
    if (read_pca9555_inputs(init_data) == 0) {
        last_state[0] = init_data[0];
        last_state[1] = init_data[1];
        printf("Initial State: 0x%02X 0x%02X\n", init_data[0], init_data[1]);
    } else {
        fprintf(stderr, "WARNING: Initial read failed. Check I2C wiring/address.\n");
    }

    // 5. Attach Interrupt
    printf("Attaching Interrupt (Falling Edge)...\n");
    status = lgGpioClaimAlert(hGpio, 0, LG_FALLING_EDGE, GPIO_INT_PIN, -1);
    if (status < 0) {
        fprintf(stderr, "FATAL: Claim Alert failed. Error: %s\n", lguErrorText(status));
        return 1;
    }
    
    lgGpioSetAlertsFunc(hGpio, GPIO_INT_PIN, on_interrupt, NULL);

    printf("--- System Ready. Press Buttons. ---\n");

    // Keep alive loop
    while (1) {
        // Just sleep to save CPU. The callback runs in a separate thread.
        sleep(10); 
    }

    lgGpiochipClose(hGpio);
    lgI2cClose(hI2c);
    return 0;
}