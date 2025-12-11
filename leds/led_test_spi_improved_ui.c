#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>
#include <signal.h>
#include <termios.h> // For non-canonical input

// --- CONFIGURATION ---
#define LED_COUNT 186
#define SPI_DEVICE "/dev/spidev0.0"
#define SPI_FREQ 2400000 
#define BITS_PER_PIXEL 24
#define SPI_BITS_PER_LED_BIT 3
#define COLOR_STEP 16 // Step size for R, G, B shifts
#define INTENSITY_STEP 10 // Step size for intensity changes

// Global state and file descriptors
int spi_fd = -1;
uint8_t *tx_buffer = NULL;
size_t tx_buffer_len = 0;
struct termios saved_terminal_settings;
int current_led_index = 0;
uint32_t current_color = 0xFFFFFF; // Start color: White

// Forward declarations
void show();
void set_pixel(int index, uint32_t color);
void update_display();
void restore_terminal_settings();

// --- Terminal Control ---

// Restores the terminal to its original, canonical state
void restore_terminal_settings() {
    tcsetattr(STDIN_FILENO, TCSANOW, &saved_terminal_settings);
}

// Sets the terminal to non-canonical (raw) mode for single-key input
void set_terminal_raw_mode() {
    struct termios tattr;

    // Get current terminal settings
    tcgetattr(STDIN_FILENO, &saved_terminal_settings);
    // Copy settings to modify
    tattr = saved_terminal_settings;

    // Set non-canonical mode: disable ICANON (canonical mode), ECHO (echoing keys), and VMIN/VTIME to read one character immediately
    tattr.c_lflag &= ~(ICANON | ECHO);
    tattr.c_cc[VMIN] = 1; // Read 1 character
    tattr.c_cc[VTIME] = 0; // No timer (wait indefinitely)

    // Apply new settings
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &tattr);

    // Ensure terminal is restored on exit
    atexit(restore_terminal_settings);
}

// Signal handler to clean up (for Ctrl+C or other signals)
void cleanup(int signum) {
    if (tx_buffer) {
        // Clear buffer (all zeros = all LEDs off)
        memset(tx_buffer, 0, tx_buffer_len);
        if (spi_fd >= 0) write(spi_fd, tx_buffer, tx_buffer_len);
        free(tx_buffer);
    }
    if (spi_fd >= 0) close(spi_fd);
    
    // Restore terminal settings is called automatically by atexit, but we call it here explicitly 
    // to ensure the console is usable immediately after the SIGINT message.
    restore_terminal_settings();
    
    printf("\nExiting and clearing LEDs. Bye!\n");
    exit(0);
}

// --- SPI and LED Control Functions (from original code, slightly simplified) ---

// Initialize SPI port
int spi_init() {
    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;
    uint32_t speed = SPI_FREQ;

    if ((spi_fd = open(SPI_DEVICE, O_RDWR)) < 0) {
        perror("Failed to open SPI device. Did you enable it in raspi-config?");
        return -1;
    }

    if (ioctl(spi_fd, SPI_IOC_WR_MODE, &mode) < 0) return -1;
    if (ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) return -1;
    if (ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) return -1;

    // Calculate buffer size: (LEDs * 24 bits-color * 3 bits-spi) / 8 bits-per-byte
    tx_buffer_len = (LED_COUNT * BITS_PER_PIXEL * SPI_BITS_PER_LED_BIT) / 8;
    
    // Add reset padding (>280us low).
    tx_buffer_len += 100; 

    tx_buffer = malloc(tx_buffer_len);
    if (!tx_buffer) {
        fprintf(stderr, "Failed to allocate memory\n");
        return -1;
    }
    memset(tx_buffer, 0, tx_buffer_len);
    
    return 0;
}

// Helper: Encodes a single color byte (8 bits) into 3 SPI bytes (24 bits)
void encode_byte(uint8_t val, uint8_t *ptr) {
    uint32_t p = 0;
    for (int b = 7; b >= 0; b--) {
        // Data 1 = 110 (binary), Data 0 = 100 (binary)
        p = (p << 3) | (((val >> b) & 1) ? 0b110 : 0b100);
    }
    
    // Write 3 bytes to the buffer (MSB first)
    ptr[0] = (p >> 16) & 0xFF;
    ptr[1] = (p >> 8) & 0xFF;
    ptr[2] = p & 0xFF;
}

void set_pixel(int index, uint32_t color) {
    if (index >= LED_COUNT || index < 0) return;

    // Extract RGB
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;

    // Offset in buffer: index * 9 bytes
    uint8_t *ptr = tx_buffer + (index * 9); 

    // WS2812B expects GRB order
    encode_byte(g, ptr);      // Green first (Bytes 0-2)
    encode_byte(r, ptr + 3);  // Red next (Bytes 3-5)
    encode_byte(b, ptr + 6);  // Blue last (Bytes 6-8)
}

void show() {
    if (spi_fd < 0) return; // Guard against uninitialized FD
    if (write(spi_fd, tx_buffer, tx_buffer_len) < 0) {
        perror("SPI Write failed");
    }
}

void clear() {
    memset(tx_buffer, 0, tx_buffer_len);
    show();
}

// --- Interactive UI Helpers ---

// Safety clamp function
uint8_t clamp(int val) {
    if (val > 255) return 255;
    if (val < 0) return 0;
    return (uint8_t)val;
}

// Updates the color of the current LED and refreshes the strip
void update_display() {
    // 1. Clear all LEDs (optional, but good practice for single-LED control)
    memset(tx_buffer, 0, tx_buffer_len);
    
    // 2. Set the current LED to the current color
    set_pixel(current_led_index, current_color);
    
    // 3. Push data to the strip
    show();
}

void print_status() {
    uint8_t r = (current_color >> 16) & 0xFF;
    uint8_t g = (current_color >> 8) & 0xFF;
    uint8_t b = current_color & 0xFF;

    // Use ANSI codes to clear the line and print the status
    printf("\r\033[KLED: %03d/%03d | Color: R=%03d G=%03d B=%03d (0x%06X) | Cmd: ", 
           current_led_index + 1, LED_COUNT, r, g, b, current_color);
    fflush(stdout);
}

void print_help() {
    printf("\n--- Interactive LED Controller ---\n");
    printf("Controls:\n");
    printf("  'a': Next LED (circular)\n");
    printf("  's': Previous LED (circular)\n");
    printf("  'r', 'g', 'b': Shift color component by +/- %d\n", COLOR_STEP);
    printf("  'd': Increase intensity\n");
    printf("  'f': Decrease intensity\n");
    printf("  'w': Set color to White (0xFFFFFF)\n");
    printf("  'q': Quit program (Ctrl+C also works)\n");
    printf("----------------------------------\n");
}


// --- Main Logic ---

int main() {
    // Set up signal handler for cleanup (Ctrl+C)
    signal(SIGINT, cleanup);
    
    // Initialize SPI
    if (spi_init() < 0) {
        fprintf(stderr, "SPI Initialization failed.\n");
        return 1;
    }
    
    // Set terminal to raw mode for single-key input
    set_terminal_raw_mode();

    printf("SPI WS2812B Interactive Controller Started (LED Count: %d)\n", LED_COUNT);
    print_help();

    // Initial display update
    update_display();
    print_status();

    char command;
    while (1) {
        // Read a single character immediately
        if (read(STDIN_FILENO, &command, 1) < 1) continue;

        uint8_t r = (current_color >> 16) & 0xFF;
        uint8_t g = (current_color >> 8) & 0xFF;
        uint8_t b = current_color & 0xFF;
        uint32_t new_color = current_color;
        
        // Process the command
        switch (command) {
            case 'q': // Quit
                cleanup(0);
                break;

            case 'h': // Help
                print_help();
                break;
                
            case 'a': // Next LED (circular)
                current_led_index = (current_led_index + 1) % LED_COUNT;
                update_display();
                break;

            case 's': // Previous LED (circular)
                current_led_index = (current_led_index - 1 + LED_COUNT) % LED_COUNT;
                update_display();
                break;

            case 'r': // Shift color towards Red
                r = clamp(r + COLOR_STEP);
                new_color = (r << 16) | (g << 8) | b;
                current_color = new_color;
                update_display();
                break;

            case 'g': // Shift color towards Green
                g = clamp(g + COLOR_STEP);
                new_color = (r << 16) | (g << 8) | b;
                current_color = new_color;
                update_display();
                break;

            case 'b': // Shift color towards Blue
                b = clamp(b + COLOR_STEP);
                new_color = (r << 16) | (g << 8) | b;
                current_color = new_color;
                update_display();
                break;
                
            case 'w': // Set color to White
                current_color = 0xFFFFFF;
                update_display();
                break;

            case 'd': // Increase Intensity (scale all RGB components)
            {
                // Find the highest component to calculate the scaling factor
                float scale = (255.0 / (float)(r > g ? (r > b ? r : b) : (g > b ? g : b)));
                
                // If current max is 0, treat it as black and set to minimal brightness white
                if (r == 0 && g == 0 && b == 0) {
                     r = g = b = INTENSITY_STEP;
                } else {
                    // Calculate a new target max brightness level
                    int target_max = clamp((int)(r * scale) + INTENSITY_STEP);
                    
                    // Re-scale components
                    r = clamp((int)(r * (target_max / (float)r)));
                    g = clamp((int)(g * (target_max / (float)r)));
                    b = clamp((int)(b * (target_max / (float)r)));
                }

                // Simpler intensity boost without preserving ratios perfectly
                // r = clamp(r + INTENSITY_STEP);
                // g = clamp(g + INTENSITY_STEP);
                // b = clamp(b + INTENSITY_STEP);
                
                new_color = (r << 16) | (g << 8) | b;
                current_color = new_color;
                update_display();
                break;
            }
                
            case 'f': // Decrease Intensity (scale all RGB components)
            {
                // Simpler intensity decrease
                r = clamp(r - INTENSITY_STEP);
                g = clamp(g - INTENSITY_STEP);
                b = clamp(b - INTENSITY_STEP);
                
                new_color = (r << 16) | (g << 8) | b;
                current_color = new_color;
                update_display();
                break;
            }

            default:
                // Ignore other keys
                break;
        }

        // Print status after every command
        print_status();
    }

    return 0; // Should be unreachable
}