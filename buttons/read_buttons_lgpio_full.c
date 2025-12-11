#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <lgpio.h>
#include <time.h>

// --- CONFIGURATION ---
#define I2C_DEV_NUM 1
#define I2C_ADDR 0x27        // Check this with 'i2cdetect -y 1'
#define GPIO_CHIP 4          // For RPi 5
#define GPIO_INT_PIN 17      // The GPIO pin connected to PCA9555 INT

// PCA9555 Register Addresses
#define REG_INPUT_0 0x00
#define REG_CONFIG_0 0x06

// Handles
int hI2c;
int hGpio;
unsigned char last_state[2] = {0xFF, 0xFF};

// --- HELPERS ---
long long current_timestamp_ms() {
    struct timespec te; 
    clock_gettime(CLOCK_MONOTONIC, &te);
    return te.tv_sec*1000LL + te.tv_nsec/1000000LL;
}
long long last_interrupt_time = 0;

// Function to dump all registers for debugging
void debug_dump_registers() {
    unsigned char config[2];
    unsigned char inputs[2];
    
    // Read Configuration (Should be 0xFF 0xFF for inputs)
    lgI2cReadI2CBlockData(hI2c, REG_CONFIG_0, (char*)config, 2);
    // Read Inputs
    lgI2cReadI2CBlockData(hI2c, REG_INPUT_0, (char*)inputs, 2);

    printf("\n--- [DEBUG] PCA9555 Register Dump ---\n");
    printf("Config Regs (0x06, 0x07): 0x%02X 0x%02X (Expected: 0xFF 0xFF)\n", config[0], config[1]);
    printf("Input  Regs (0x00, 0x01): 0x%02X 0x%02X\n", inputs[0], inputs[1]);
    
    // Check INT Pin State physically
    int pin_level = lgGpioRead(hGpio, GPIO_INT_PIN);
    printf("RPi GPIO %d Level:        %s\n", GPIO_INT_PIN, pin_level ? "HIGH (1)" : "LOW (0)");
    
    if (config[0] != 0xFF || config[1] != 0xFF) {
        printf("!! WARNING: Ports are not configured as Inputs! Interrupts won't fire.\n");
    }
    if (pin_level == 0) {
        printf("!! WARNING: INT line is LOW. Interrupt is stuck active or missing pull-up.\n");
    }
    printf("-------------------------------------\n\n");
}

// --- INTERRUPT CALLBACK ---
void on_interrupt(int num_alerts, lgGpioAlert_p gpio_alerts, void *userdata) {
    long long now = current_timestamp_ms();
    if (now - last_interrupt_time < 50) return; // 50ms Debounce
    last_interrupt_time = now;

    printf("\n>>> INTERRUPT DETECTED! Reading data...\n");

    unsigned char data[2];
    int count = lgI2cReadI2CBlockData(hI2c, REG_INPUT_0, (char*)data, 2);

    if (count != 2) {
        fprintf(stderr, "Read failed inside interrupt!\n");
        return;
    }

    printf("    Raw Data: 0x%02X 0x%02X\n", data[0], data[1]);

    for (int i = 0; i < 8; i++) {
        int isPressed = !((data[0] >> i) & 1);
        int wasPressed = !((last_state[0] >> i) & 1);
        if (isPressed && !wasPressed) {
            printf("    *** Button %d Pressed ***\n", i);
        }
    }
    last_state[0] = data[0];
    last_state[1] = data[1];
}

int main() {
    int status;

    printf("Opening I2C Bus %d...\n", I2C_DEV_NUM);
    hI2c = lgI2cOpen(I2C_DEV_NUM, I2C_ADDR, 0);
    if (hI2c < 0) {
        fprintf(stderr, "FATAL: Failed to open I2C. Check connections or Address.\n");
        return 1;
    }

    printf("Opening GPIO Chip %d...\n", GPIO_CHIP);
    hGpio = lgGpiochipOpen(GPIO_CHIP);
    if (hGpio < 0) {
        fprintf(stderr, "FATAL: Failed to open GPIO Chip.\n");
        return 1;
    }

    // --- CRITICAL FIX: ENABLE PULL-UP ---
    // The INT pin is Open Drain. We MUST enable the Pi's internal pull-up 
    // if there isn't one on the board.
    printf("Configuring GPIO %d as Input with Internal PULL-UP...\n", GPIO_INT_PIN);
    status = lgGpioClaimInput(hGpio, LG_SET_PULL_UP, GPIO_INT_PIN);
    if (status < 0) {
        fprintf(stderr, "Error setting pull-up: %s\n", lguErrorText(status));
        return 1;
    }

    // Dump initial state
    debug_dump_registers();

    // Claim Alert
    printf("Attaching Interrupt Handler (Falling Edge)...\n");
    status = lgGpioClaimAlert(hGpio, 0, LG_FALLING_EDGE, GPIO_INT_PIN, -1);
    if (status < 0) {
        fprintf(stderr, "Error claiming alert: %s\n", lguErrorText(status));
        return 1;
    }
    lgGpioSetAlertsFunc(hGpio, GPIO_INT_PIN, on_interrupt, NULL);

    printf("Running... Press Buttons now. (Press 'q' and Enter to exit)\n");

    // --- DEBUG LOOP ---
    // Instead of sleeping forever, let's print the status every 2 seconds
    // to see if the line is stuck.
    while (1) {
        struct timespec ts = {2, 0}; // 2 seconds
        nanosleep(&ts, NULL);
        
        // Un-comment this line if you suspect the chip is crashing silently:
        // debug_dump_registers(); 
        
        // Simple keep-alive dot
        printf("."); 
        fflush(stdout);
    }

    lgGpiochipClose(hGpio);
    lgI2cClose(hI2c);
    return 0;
}