#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

// --- CONFIGURATION ---
#define I2C_DEVICE "/dev/i2c-1"
#define I2C_ADDR 0x27

// PCA9555 Command Bytes (from Datasheet Table 4)
#define CMD_INPUT_PORT_0  0x00
#define CMD_INPUT_PORT_1  0x01
#define CMD_CONFIG_PORT_0 0x06
#define CMD_CONFIG_PORT_1 0x07

// GPIO Configuration for Interrupt
// Using GPIO 17 (Physical Pin 11) for the INT line
#define GPIO_INT_PIN "17" 
#define GPIO_PATH "/sys/class/gpio/gpio" GPIO_INT_PIN "/"

// Helper to write to sysfs files
int write_sysfs(const char *path, const char *filename, const char *value) {
    char full_path[128];
    snprintf(full_path, sizeof(full_path), "%s%s", path, filename);
    
    int fd = open(full_path, O_WRONLY);
    if (fd < 0) return -1;
    
    write(fd, value, strlen(value));
    close(fd);
    return 0;
}

// Setup GPIO for Interrupts (Falling Edge)
int setup_gpio_interrupt() {
    // 1. Export the GPIO pin
    int fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd != -1) {
        write(fd, GPIO_INT_PIN, strlen(GPIO_INT_PIN));
        close(fd);
        
        // Wait 500ms for the OS to create the filesystem nodes
        // Without this, the next step fails because the directory doesn't exist yet.
        usleep(500000); 
    }

    // 2. Set Direction to Input
    if (write_sysfs(GPIO_PATH, "direction", "in") < 0) {
        perror("Failed to set GPIO direction");
        return -1;
    }

    // 3. Set Edge Detection to Falling (PCA9555 INT goes LOW on active)
    if (write_sysfs(GPIO_PATH, "edge", "falling") < 0) {
        perror("Failed to set GPIO edge");
        return -1;
    }

    // 4. Open Value file for polling
    char val_path[128];
    snprintf(val_path, sizeof(val_path), "%svalue", GPIO_PATH);
    int val_fd = open(val_path, O_RDONLY);
    if (val_fd < 0) {
        perror("Failed to open GPIO value file");
        return -1;
    }
    return val_fd;
}

int main() {
    int i2c_fd, gpio_fd;
    
    // --- STEP 1: I2C SETUP ---
    if ((i2c_fd = open(I2C_DEVICE, O_RDWR)) < 0) {
        perror("Failed to open I2C bus");
        return 1;
    }
    if (ioctl(i2c_fd, I2C_SLAVE, I2C_ADDR) < 0) {
        perror("Failed to acquire bus access");
        return 1;
    }

    // --- STEP 2: GPIO INTERRUPT SETUP ---
    gpio_fd = setup_gpio_interrupt();
    if (gpio_fd < 0) return 1;

    printf("PCA9555 Interrupt Monitor Started.\n");
    printf("Monitoring GPIO %s for falling edge from PCA9555...\n", GPIO_INT_PIN);

    // Buffer for 2 bytes (Port 0 and Port 1)
    unsigned char data[2];
    unsigned char last_state[2] = {0xFF, 0xFF};

    // --- STEP 3: INITIAL READ (Crucial) ---
    // We must read registers once to clear any existing interrupts on the PCA9555.
    // We write the command byte 0x00 (Input Port 0) to set the internal pointer.
    data[0] = CMD_INPUT_PORT_0;
    if (write(i2c_fd, data, 1) != 1) perror("Init write failed");
    
    // Read 2 bytes: The PCA9555 auto-increments from Reg 0 to Reg 1.
    // This reads both ports and clears interrupts for BOTH.
    if (read(i2c_fd, data, 2) == 2) {
        last_state[0] = data[0];
        last_state[1] = data[1];
    }

    struct pollfd pfd;
    pfd.fd = gpio_fd;
    pfd.events = POLLPRI; // Priority data (interrupt)

    while (1) {
        // --- STEP 4: WAIT FOR INTERRUPT ---
        // This blocks the CPU until the INT line goes LOW. No more 50ms sleep!
        // Timeout is -1 (infinite).
        int ret = poll(&pfd, 1, -1);

        if (ret > 0) {
            if (pfd.revents & POLLPRI) {
                // Determine what happened: seek to start of GPIO value file and read it
                lseek(gpio_fd, 0, SEEK_SET);
                char gpio_val;
                read(gpio_fd, &gpio_val, 1);

                // --- STEP 5: READ PCA9555 ---
                // We send Command 0x00 again to ensure we are reading from Input Port 0
                unsigned char reg_ptr = CMD_INPUT_PORT_0;
                write(i2c_fd, &reg_ptr, 1);

                // Read 2 bytes (Port 0 and Port 1)
                // DATASHEET NOTE: "The interrupt caused by Port 0 will not be cleared by a read of Port 1"
                // Reading both ensures we clear the interrupt regardless of which pin triggered it.
                if (read(i2c_fd, data, 2) != 2) {
                    perror("Failed to read I2C");
                    continue;
                }

                // --- STEP 6: LOGIC ---
                // Check Port 0 (data[0])
                for (int i = 0; i < 8; i++) {
                    int isPressed = !((data[0] >> i) & 1);
                    int wasPressed = !((last_state[0] >> i) & 1);

                    if (isPressed && !wasPressed) {
                        switch(i) {
                            case 0: printf("[Group 1] Sequence A triggered\n"); break;
                            case 1: printf("[Group 2] Data Logged\n"); break;
                            case 2: printf("[Group 3] Emergency Stop\n"); break;
                            default: printf("Button %d on Port 0 Pressed\n", i); break;
                        }
                    }
                }
                
                // Update last state
                last_state[0] = data[0];
                last_state[1] = data[1];
                
                // Simple debounce: interrupts can fire rapidly. 
                // Since the INT line stays LOW until we read, we might not need a huge sleep,
                // but a tiny one helps ignore mechanical switch bounce.
                usleep(20000); 
            }
        }
    }

    close(i2c_fd);
    close(gpio_fd);
    return 0;
}