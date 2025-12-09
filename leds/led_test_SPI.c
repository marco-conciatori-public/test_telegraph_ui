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

// --- CONFIGURATION ---
#define LED_COUNT 186
#define SPI_DEVICE "/dev/spidev0.0"
#define SPI_FREQ 2400000 
#define BITS_PER_PIXEL 24
#define SPI_BITS_PER_LED_BIT 3

// GRB Format for WS2812B
typedef struct {
    uint8_t g;
    uint8_t r;
    uint8_t b;
} pixel_t;

int spi_fd = -1;
uint8_t *tx_buffer = NULL;
size_t tx_buffer_len = 0;

// Signal handler to clean up
void cleanup(int signum) {
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
    // 1 WS bit = 3 SPI bits. 
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
    if (index >= LED_COUNT) return;

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

int main() {
    signal(SIGINT, cleanup);
    
    if (spi_init() < 0) return 1;

    printf("SPI WS2812B Test Started (Pi 5 Compatible)\n");
    printf("Controls: Press ENTER for next LED. Ctrl+C to exit.\n\n");

    clear();

    for (int i = 0; i < LED_COUNT; i++) {
        // Set Pixel 'i' to White (0xFFFFFF)
        set_pixel(i, 0xFFFFFF);
        show();

        printf("LED %d is ON. Press ENTER...", i + 1);
        fflush(stdout);
        while(getchar() != '\n');

        // Turn it off
        set_pixel(i, 0x000000);
        show();
    }

    printf("\nDone!\n");
    cleanup(0);
    return 0;
}