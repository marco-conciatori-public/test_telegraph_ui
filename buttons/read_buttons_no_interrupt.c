#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

// --- CONFIGURATION ---
// The standard I2C device on Raspberry Pi 5 is usually /dev/i2c-1
#define I2C_DEVICE "/dev/i2c-1"

// Common default addresses: 
// PCF8574 is often 0x27 or 0x3F. 
// MCP23017 is often 0x20.
// Check your specific device address using 'i2cdetect -y 1' in terminal.
#define I2C_ADDR 0x27 

int main() {
    int file;
    char *filename = I2C_DEVICE;
    
    // Buffer for data. 
    // If using PCF8574, we read 1 byte directly.
    // If using MCP23017, we might read 2 bytes (16 pins).
    unsigned char buffer[1]; 

    // 1. Open the I2C Bus
    if ((file = open(filename, O_RDWR)) < 0) {
        perror("Failed to open the i2c bus");
        return 1;
    }

    // 2. Specify the address of the slave device
    if (ioctl(file, I2C_SLAVE, I2C_ADDR) < 0) {
        perror("Failed to acquire bus access and/or talk to slave");
        return 1;
    }

    printf("I2C Button Monitor Started on %s at address 0x%x\n", filename, I2C_ADDR);
    printf("Press CTRL+C to exit.\n");

    // Previous state to detect changes (simple edge detection)
    unsigned char lastState = 0xFF; 

    while (1) {
        // --- READ PHASE ---
        
        // NOTE: If using MCP23017, you typically need to write the register 
        // address you want to read from first (e.g., GPIOA = 0x12).
        // write(file, &register_addr, 1); 
        
        if (read(file, buffer, 1) != 1) {
            perror("Failed to read from the i2c bus");
            // Don't exit immediately on read fail, maybe a glitch, but sleep longer
            usleep(100000); 
            continue;
        }

        unsigned char currentState = buffer[0];

        // --- LOGIC PHASE ---
        // Assuming Input Pull-up: 
        // 1 = Not Pressed (High Voltage)
        // 0 = Pressed (Grounded)
        
        if (currentState != lastState) {
            // Iterate through the first 3 bits as an example for your "3 groups"
            // You can expand this loop to check all 8 bits (0-7)
            
            for (int i = 0; i < 8; i++) {
                // Check if specific bit is LOW (Pressed) and was previously HIGH
                int isPressed = !((currentState >> i) & 1);
                int wasPressed = !((lastState >> i) & 1);

                if (isPressed && !wasPressed) {
                    // Action based on which button (or group) was pressed
                    switch(i) {
                        case 0:
                            printf("[Group 1] Signal detected on Pin 0: Initiating Sequence A\n");
                            break;
                        case 1:
                            printf("[Group 2] Signal detected on Pin 1: Logging Data Point\n");
                            break;
                        case 2:
                            printf("[Group 3] Signal detected on Pin 2: Emergency Stop\n");
                            break;
                        default:
                            printf("[Other] Button %d pressed\n", i);
                            break;
                    }
                }
            }
            lastState = currentState;
        }

        // --- TIMING ---
        // If you connect the INT cable to a Pi GPIO, you would replace this 
        // sleep with a wait_for_interrupt() function. 
        // Without INT, we poll every 50ms.
        usleep(50000); 
    }

    close(file);
    return 0;
}