#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <lgpio.h>
#include <time.h>

// --- CONFIGURATION ---
#define I2C_DEVICE "/dev/i2c-1"
#define I2C_ADDR 0x23

// Raspberry Pi 5 usually uses Chip 4 for the header pins. 
// If this doesn't work, try 0.
#define GPIO_CHIP 4
#define GPIO_INT_PIN 17

// PCA9555 Commands
#define CMD_INPUT_PORT_0 0x00

// Global file descriptor for I2C so the callback can use it
int i2c_fd;
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
// This function runs automatically when the INT pin goes LOW
void on_interrupt(int num_alerts, lgGpioAlert_p gpio_alerts, void *userdata) {
    long long now = current_timestamp_ms();
    
    // 1. Debounce: Ignore interrupts that happen within 20ms of the last one
    if (now - last_interrupt_time < 20) return;
    last_interrupt_time = now;

    // 2. Read PCA9555 to clear the interrupt
    // Write Command Byte (Set pointer to Port 0)
    unsigned char reg_ptr = CMD_INPUT_PORT_0;
    if (write(i2c_fd, &reg_ptr, 1) != 1) {
        perror("Failed to write register pointer");
        return;
    }

    // Read 2 Bytes (Port 0 and Port 1)
    unsigned char data[2];
    if (read(i2c_fd, data, 2) != 2) {
        perror("Failed to read I2C data");
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
    int hGpio;
    int status;

    // --- STEP 1: OPEN I2C ---
    if ((i2c_fd = open(I2C_DEVICE, O_RDWR)) < 0) {
        perror("Failed to open I2C bus");
        return 1;
    }
    if (ioctl(i2c_fd, I2C_SLAVE, I2C_ADDR) < 0) {
        perror("Failed to acquire bus access");
        return 1;
    }

    // --- STEP 2: INITIAL I2C READ ---
    // Read once to clear any stuck interrupts before we start monitoring
    unsigned char ptr = CMD_INPUT_PORT_0;
    write(i2c_fd, &ptr, 1);
    unsigned char init_data[2];
    if (read(i2c_fd, init_data, 2) == 2) {
        last_state[0] = init_data[0];
        last_state[1] = init_data[1];
        printf("Initial State: Port0=0x%X, Port1=0x%X\n", init_data[0], init_data[1]);
    }

    // --- STEP 3: OPEN GPIO CHIP ---
    hGpio = lgGpiochipOpen(GPIO_CHIP);
    if (hGpio < 0) {
        fprintf(stderr, "Error: Could not open GPIO chip %d. Try changing GPIO_CHIP to 0.\n", GPIO_CHIP);
        return 1;
    }

    // --- STEP 4: CLAIM PIN AND SET INTERRUPT ---
    // Claim Pin 17 as Input, Active Low (Pull-Up not strictly needed if PCA9555 drives it, but safe)
    // LG_SET_PULL_UP ensures the line doesn't float if PCA isn't connected.
    status = lgGpioClaimAlert(hGpio, 0, LG_FALLING_EDGE, GPIO_INT_PIN, -1);
    if (status < 0) {
        fprintf(stderr, "Error claiming GPIO alert: %s\n", lguErrorText(status));
        return 1;
    }

    // Register the callback function
    lgGpioSetAlertsFunc(hGpio, GPIO_INT_PIN, on_interrupt, NULL);

    printf("Program Running. Waiting for interrupts on GPIO %d (Chip %d)...\n", GPIO_INT_PIN, GPIO_CHIP);
    printf("Press Enter to quit.\n");

    // --- STEP 5: KEEP ALIVE ---
    // The library handles the listening in a separate thread.
    // We just keep the main thread alive.
    while (getchar() != '\n');

    // Cleanup
    lgGpiochipClose(hGpio);
    close(i2c_fd);
    printf("Exiting...\n");
    return 0;
}