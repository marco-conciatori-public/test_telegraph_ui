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
#include <termios.h> // Required for non-canonical input

// --- CONFIGURATION ---
#define LED_COUNT 186
#define SPI_DEVICE "/dev/spidev0.0"
#define SPI_FREQ 2400000 
#define BITS_PER_PIXEL 24
#define SPI_BITS_PER_LED_BIT 3

// --- KEY DEFINITIONS ---
#define KEY_UP    1000
#define KEY_DOWN  1001
#define KEY_OTHER 0
#define KEY_Q     'q' // Quit
#define KEY_A     'a' // Reset to LED 1
#define KEY_R     'r' // Red
#define KEY_G     'g' // Green
#define KEY_B     'b' // Blue
#define KEY_W     'w' // White (New Function)

// --- COLOR DEFINITIONS ---
#define COLOR_RED    0xFF0000
#define COLOR_GREEN  0x00FF00
#define COLOR_BLUE   0x0000FF
#define COLOR_WHITE  0xFFFFFF
#define COLOR_OFF    0x000000

// GRB Format for WS2812B
typedef struct {
    uint8_t g;
    uint8_t r;
    uint8_t b;
} pixel_t;

int spi_fd = -1;
uint8_t *tx_buffer = NULL;
size_t tx_buffer_len = 0;

// Terminal state variables
struct termios original_termios;
int term_mode_set = 0;

// --- TERMINAL CONTROL ---

// Function to reset the terminal to its original state
void reset_terminal_mode() {
    if (term_mode_set) {
        tcsetattr(STDIN_FILENO, TCSANOW, &original_termios);
        term_mode_set = 0;
    }
}

// Function to set the terminal to non-canonical (raw/cbreak) mode
int set_keypress_mode() {
    struct termios new_termios;

    if (tcgetattr(STDIN_FILENO, &original_termios) < 0) {
        perror("tcgetattr failed");
        return -1;
    }

    new_termios = original_termios;

    // Set canonical mode off, echo off
    new_termios.c_lflag &= ~(ICANON | ECHO);

    // Set minimum number of characters to read to 1
    new_termios.c_cc[VMIN] = 1; 
    // Set timeout to 0 (non-blocking read)
    new_termios.c_cc[VTIME] = 0; 

    if (tcsetattr(STDIN_FILENO, TCSANOW, &new_termios) < 0) {
        perror("tcsetattr failed");
        return -1;
    }

    term_mode_set = 1;
    return 0;
}

// Custom function to read input and detect special keys (like arrows)
int get_key() {
    // Use `fgetc` instead of `getchar` to avoid potential buffering issues
    int c = fgetc(stdin); 

    // Check for Escape sequence (0x1b)
    if (c == 0x1b) {
        // Look for the next two characters in the sequence: [A or [B
        if (fgetc(stdin) == '[') {
            switch (fgetc(stdin)) {
                case 'A': return KEY_UP;   // Up Arrow
                case 'B': return KEY_DOWN; // Down Arrow
            }
        }
        // If it was just an ESC key or an unknown sequence, treat as other
        return KEY_OTHER; 
    }

    // Ignore EOF which is -1
    return (c == EOF) ? KEY_OTHER : c;
}

// --- SPI & LED CONTROL ---

// Signal handler to clean up
void cleanup(int signum) {
    reset_terminal_mode(); // Always reset terminal mode first
    
    if (tx_buffer) {
        // Clear buffer (all zeros = all LEDs off)
        memset(tx_buffer, 0, tx_buffer_len);
        if (spi_fd >= 0) write(spi_fd, tx_buffer, tx_buffer_len);
        free(tx_buffer);
    }
    if (spi_fd >= 0) close(spi_fd);
    printf("\nExiting and clearing LEDs.\n");
    exit(0);
}

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

    // Calculate buffer size:
    // (LEDs * 24 bits-color * 3 bits-spi) / 8 bits-per-byte
    tx_buffer_len = (LED_COUNT * BITS_PER_PIXEL * SPI_BITS_PER_LED_BIT) / 8;
    
    // Add reset padding (>280us low). At 2.4MHz, 100 bytes is plenty.
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
    // We generate 3 SPI bytes for the 8 bits of 'val'
    // Data 0 = 100 (binary)
    // Data 1 = 110 (binary)
    
    uint32_t p = 0;
    for (int b = 7; b >= 0; b--) {
        // Shift existing pattern to make room
        // OR in the new 3-bit pattern (110 or 100)
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

    // Offset in buffer: index * 9 bytes (because 24 bits * 3 SPI bits / 8 = 9 bytes)
    uint8_t *ptr = tx_buffer + (index * 9); 

    // WS2812B expects GRB order
    encode_byte(g, ptr);      // Green first (Bytes 0-2)
    encode_byte(r, ptr + 3);  // Red next (Bytes 3-5)
    encode_byte(b, ptr + 6);  // Blue last (Bytes 6-8)
}

void show() {
    if (write(spi_fd, tx_buffer, tx_buffer_len) < 0) {
        perror("SPI Write failed");
    }
}

void clear() {
    // Zero out data (leaving the reset padding at the end alone is fine, or zero it all)
    memset(tx_buffer, 0, tx_buffer_len);
    show();
}

// --- MAIN LOOP LOGIC ---

// Function to update the LED state and print status
void draw_state(int current_led, uint32_t current_color) {
    // Clear all LEDs
    clear();
    
    // Set the current LED to the specified color
    if (current_led >= 0 && current_led < LED_COUNT) {
        set_pixel(current_led, current_color);
    }
    show();

    // Determine color name for display
    const char *color_name = "Custom";
    if (current_color == COLOR_RED) color_name = "Red";
    else if (current_color == COLOR_GREEN) color_name = "Green";
    else if (current_color == COLOR_BLUE) color_name = "Blue";
    else if (current_color == COLOR_WHITE) color_name = "White";
    else if (current_color == COLOR_OFF) color_name = "Off";

    // Print status
    printf("\rCurrent LED: %-3d/%d. Color: %s. Controls: [^/v] Nav, [r,g,b,w] Color, [a] Reset, [q] Quit.", 
           current_led + 1, LED_COUNT, color_name);
    fflush(stdout);
}


int main() {
    signal(SIGINT, cleanup);
    
    if (spi_init() < 0) {
        cleanup(0);
        return 1;
    }
    
    if (set_keypress_mode() < 0) {
        cleanup(0);
        return 1;
    }

    printf("SPI WS2812B Test Started (Pi 5 Compatible)\n");
    printf("Controls: [^/v] Nav, [r,g,b] Color, [w] White, [a] Reset to LED 1, [q] Quit.\n\n");

    int current_led = 0;
    uint32_t current_color = COLOR_WHITE; // Start with white

    draw_state(current_led, current_color);

    while (1) {
        int key = get_key();

        switch (key) {
            case KEY_UP:
                // Go to next LED
                current_led = (current_led + 1) % LED_COUNT;
                break;
            case KEY_DOWN:
                // Go to previous LED
                current_led = (current_led - 1 + LED_COUNT) % LED_COUNT;
                break;

            // Color Controls
            case KEY_R:
                current_color = COLOR_RED;
                break;
            case KEY_G:
                current_color = COLOR_GREEN;
                break;
            case KEY_B:
                current_color = COLOR_BLUE;
                break;
            case KEY_W: // Set to White
                current_color = COLOR_WHITE;
                break;

            case KEY_A:
                current_led = 0;
                break;
            
            // Quit
            case KEY_Q:
                cleanup(0);
                return 0;

            case KEY_OTHER:
            default:
                // Ignore other keys
                break;
        }

        if (key != KEY_OTHER) {
            draw_state(current_led, current_color);
        }
        
        // Short delay to prevent busy-waiting from consuming too much CPU
        usleep(1000); // 1 millisecond delay
    }
    
    cleanup(0);
    return 0;
}