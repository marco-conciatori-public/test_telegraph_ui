#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>
#include <signal.h>
#include <termios.h> 

// --- CONFIGURATION ---
#define LED_COUNT 186
#define SPI_DEVICE "/dev/spidev0.0"
#define SPI_FREQ 2400000 
#define BITS_PER_PIXEL 24
#define SPI_BITS_PER_LED_BIT 3
#define COLOR_STEP 16 
#define INTENSITY_STEP 20 

// Global state
int spi_fd = -1;
uint8_t *tx_buffer = NULL;
size_t tx_buffer_len = 0;
struct termios saved_terminal_settings;
int current_led_index = 0;
uint32_t current_color = 0x8F8F8F; 

// Forward declarations
void show();
void set_pixel(int index, uint32_t color);
void fill_black();
void restore_terminal_settings();

// --- Terminal Control ---
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
    tattr.c_cc[VMIN] = 1; 
    tattr.c_cc[VTIME] = 0; 
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &tattr);

    // Ensure terminal is restored on exit
    atexit(restore_terminal_settings);
}

// --- SPI and LED Control ---

// Initialize SPI port
int spi_init() {
    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;
    uint32_t speed = SPI_FREQ;

    if ((spi_fd = open(SPI_DEVICE, O_RDWR)) < 0) {
        perror("Failed to open SPI device");
        return -1;
    }

    if (ioctl(spi_fd, SPI_IOC_WR_MODE, &mode) < 0) return -1;
    if (ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) return -1;
    if (ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) return -1;

    // Calculate data_len: (LEDs * 24 bits-color * 3 bits-spi) / 8 bits-per-byte
    size_t data_len = (LED_COUNT * BITS_PER_PIXEL * SPI_BITS_PER_LED_BIT) / 8;
    tx_buffer_len = data_len + 100; // + Padding for Reset

    tx_buffer = malloc(tx_buffer_len);
    if (!tx_buffer) return -1;
    
    // Initialize strip to Black
    fill_black();
    show();
    
    return 0;
}

// Fills the buffer with the SPI pattern for "Off" (RGB 0,0,0)
// memset(0) sends RESET signals. This sends "Black" data.
void fill_black() {
    // The SPI pattern for a byte '0' (00000000) at 2.4MHz is:
    // 100 100 100 100 100 100 100 100 (binary)
    // Grouped into 3 SPI bytes: 10010010 (0x92), 01001001 (0x49), 00100100 (0x24)
    const uint8_t black_pattern[3] = {0x92, 0x49, 0x24};
    
    // Fill the data area of the buffer with this pattern
    for (int i = 0; i < LED_COUNT * 3; i++) { // 3 bytes per pixel color * 3 colors = 9 bytes/LED
        // We write 9 bytes per LED. The pattern repeats every 3 bytes.
        tx_buffer[i * 3 + 0] = black_pattern[0];
        tx_buffer[i * 3 + 1] = black_pattern[1];
        tx_buffer[i * 3 + 2] = black_pattern[2];
    }
    
    // Zero out the padding area at the end to ensure a RESET happens AFTER data
    size_t data_end = (LED_COUNT * 9);
    memset(tx_buffer + data_end, 0, tx_buffer_len - data_end);
}

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
    if (index < 0 || index >= LED_COUNT) return;

    // Extract RGB
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;

    // Offset: 9 bytes per LED
    uint8_t *ptr = tx_buffer + (index * 9); 
    printf("Setting pixel %d to color R=%d G=%d B=%d\n", index, r, g, b);

    // WS2812B expects GRB order
    encode_byte(g, ptr);      // Green first (Bytes 0-2)
    encode_byte(r, ptr + 3);  // Red next (Bytes 3-5)
    encode_byte(b, ptr + 6);  // Blue last (Bytes 6-8)
}

void show() {
    if (spi_fd >= 0) if (write(spi_fd, tx_buffer, tx_buffer_len) < 0) perror("SPI Write failed");
}

void cleanup(int signum) {
    if (tx_buffer) {
        // Send "Black" to all LEDs to turn them off physically
        fill_black();
        show();
        free(tx_buffer);
    }
    if (spi_fd >= 0) close(spi_fd);
    restore_terminal_settings();
    printf("\nClean exit.\n");
    exit(0);
}

void update_display() {
    // 1. Fill buffer with "Black" patterns (Logic 0)
    fill_black();
    
    // 2. Overwrite the specific LED with color
    set_pixel(current_led_index, current_color);
    
    // 3. Send
    show();
}

// Safety clamp
uint8_t clamp(int val) {
    if (val > 255) return 255;
    if (val < 0) return 0;
    return (uint8_t)val;
}

void print_status() {
    uint8_t r = (current_color >> 16) & 0xFF;
    uint8_t g = (current_color >> 8) & 0xFF;
    uint8_t b = current_color & 0xFF;
    printf("\r\033[KLED: %03d/%03d | Color: R=%03d G=%03d B=%03d", 
           current_led_index + 1, LED_COUNT, r, g, b);
    fflush(stdout);
}

void print_help() {
    printf("\n--- Interactive LED Controller ---\n");
    printf("Controls:\n");
    printf("  [a/s]: next/previous LED (circular)\n");
    printf("  [d/f]: +/- intensity\n");
    printf("  [e/r/t]: make color more Red/Green/Blue\n");
    printf("  [w]: Set color to White\n");
    printf("  [q]: Close program\n");
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

    printf("SPI WS2812B Controller (Pi 5)\n");
    print_help();

    update_display();
    print_status();

    char command;
    while (true) {
        // Read a single character immediately
        if (read(STDIN_FILENO, &command, 1) < 1) continue;

        uint8_t r = (current_color >> 16) & 0xFF;
        uint8_t g = (current_color >> 8) & 0xFF;
        uint8_t b = current_color & 0xFF;
        
        // Process the command
        switch (command) {
            case 'q': // Quit
                cleanup(0);
                break;

            case 'a': // Next LED (circular)
                current_led_index = (current_led_index + 1) % LED_COUNT;
                break;

            case 's': // Previous LED (circular)
                current_led_index = (current_led_index - 1 + LED_COUNT) % LED_COUNT;
                break;

            case 'e': // Shift color towards Red
                r = clamp(r + COLOR_STEP);
                g = clamp(g - COLOR_STEP / 2);
                b = clamp(b - COLOR_STEP / 2);
                current_color = (r << 16) | (g << 8) | b;
                break;

            case 'r': // Shift color towards Green
                r = clamp(r - COLOR_STEP / 2);
                g = clamp(g + COLOR_STEP);
                b = clamp(b - COLOR_STEP / 2);
                current_color = (r << 16) | (g << 8) | b;
                break;

            case 't': // Shift color towards Blue
                r = clamp(r - COLOR_STEP / 2);
                g = clamp(g - COLOR_STEP / 2);
                b = clamp(b + COLOR_STEP);
                current_color = (r << 16) | (g << 8) | b;
                break;
                
            case 'w': // Set color to White
                current_color = 0x8F8F8F;
                break;

            case 'd': // Increase Intensity (scale all RGB components)
            {
                uint8_t max_val = r > g ? (r > b ? r : b) : (g > b ? g : b);

                // Handle the "black" edge case (R=G=B=0)
                if (max_val == 0) {
                    r = INTENSITY_STEP;
                    g = INTENSITY_STEP;
                    b = INTENSITY_STEP;
                } else {
                    // Calculate the new target maximum value (clamped at 255)
                    uint8_t new_max_val = (uint8_t)clamp(max_val + INTENSITY_STEP);

                    // If the color is already saturated (max_val == 255) or the increase was 0,
                    // and new_max_val == max_val, no further change is needed.
                    if (new_max_val == max_val) {
                        break;
                    }

                    // Calculate the scaling factor (always > 1.0)
                    // floating-point for precise ratio preservation.
                    double scale_factor = (double)new_max_val / max_val;

                    // Apply the scaling and update the components, rounding the result
                    r = (uint8_t)round((double)r * scale_factor);
                    g = (uint8_t)round((double)g * scale_factor);
                    b = (uint8_t)round((double)b * scale_factor);
                }
                
                current_color = (r << 16) | (g << 8) | b;
                break;
            }
                
            case 'f': // Decrease Intensity (scale all RGB components)
            {
                uint8_t max_val = r > g ? (r > b ? r : b) : (g > b ? g : b);
                if (max_val == 0) {
                    // Already black, no change
                    break;
                }
            
                // Clamp at 0. If the decrease amount is larger than max_val, it becomes black.
                uint8_t new_max_val = (uint8_t)clamp(max_val - INTENSITY_STEP);

                // Calculate the scaling factor (always < 1.0)
                // floating-point for precise ratio preservation.
                double scale_factor = (double)new_max_val / max_val;

                // Apply the scaling and update the components, rounding the result
                r = (uint8_t)round((double)r * scale_factor);
                g = (uint8_t)round((double)g * scale_factor);
                b = (uint8_t)round((double)b * scale_factor);
                
                current_color = (r << 16) | (g << 8) | b;
                break;
            }

            default:
                // Ignore other keys
                break;
        }
        update_display();
        // Print status after every command
        print_status();
    }
    return 0;
}