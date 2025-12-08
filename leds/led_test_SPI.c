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
// WS2812B V5 Specs:
// Bit '0': T0H ~220-380ns.
// Bit '1': T1H ~580ns-1us.
// Protocol speed: 800kHz (1.25us period).
//
// SPI STRATEGY: 2.4MHz Clock.
// Each WS2812 bit is represented by 3 SPI bits.
// 1 SPI bit duration = 1/2.4MHz = ~416ns.
// Code '0' (1 0 0) -> High 416ns (Close to 380ns limit, usually acceptable)
// Code '1' (1 1 0) -> High 833ns (Perfectly within 580ns-1us range)

#define LED_COUNT 50
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
        write(spi_fd, tx_buffer, tx_buffer_len);
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
    
    // Add some padding bytes for Reset signal (>280us low)
    // At 2.4MHz, 1 byte is ~3.3us. 100 bytes is plenty.
    tx_buffer_len += 100; 

    tx_buffer = malloc(tx_buffer_len);
    memset(tx_buffer, 0, tx_buffer_len); // Initialize to 0 (all dark)
    
    return 0;
}

// Convert a standard 24-bit color to the SPI bit pattern
// color: 0x00RRGGBB (standard storage), but we must send GRB
void set_pixel(int index, uint32_t color) {
    if (index >= LED_COUNT) return;

    // Extract RGB
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;

    // WS2812B wants GRB order
    uint32_t grb = ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;

    // Offset in buffer (3 SPI bits per data bit -> 72 SPI bits (9 bytes) per LED)
    // We skip the reset padding at the end, so we fill from start.
    uint8_t *ptr = tx_buffer + (index * 9); 
    
    // Process 24 bits of color data (MSB first)
    // We fill 9 bytes of SPI buffer for every 1 pixel.
    // 9 bytes = 72 bits. 72 / 3 = 24 WS bits. Correct.
    
    int bit_pos = 0; // 0 to 71
    
    // Reset the 9 bytes for this pixel to 0 first
    memset(ptr, 0, 9);

    for (int i = 23; i >= 0; i--) {
        // Is the i-th bit of GRB set?
        uint8_t bit_val = (grb >> i) & 1;
        
        // Pattern for '1': 110
        // Pattern for '0': 100
        // We need to write these 3 bits into our byte array
        
        // Calculate which byte and which bit within that byte we are targeting
        // This is a bit tricky, doing it byte-aligned is easier.
        // But 24 * 3 = 72 bits, which divides perfectly into 9 bytes.
        // Byte 0: Bits 23, 22, partial 21... 
        
        // Easier approach: build the 3 bits and shift them into a temporary buffer
    }

    // SIMPLIFIED APPROACH:
    // To avoid complex bit shifting, let's look at the pattern logic.
    // We need to output 3 bits: 1X0. 
    // If Data=1, X=1. If Data=0, X=0.
    
    // We can process 8 WS-bits (one color byte) at a time.
    // 8 WS-bits * 3 = 24 SPI-bits = 3 SPI-bytes.
    // So 1 color byte (e.g. Green) turns into 3 SPI bytes.
    
    auto encode_byte = [&](uint8_t val, int offset) {
        // We will generate 3 bytes for the 8 bits of 'val'
        // Example: val = 10000000 (0x80)
        // WS bits: 1 0 0 0 0 0 0 0
        // SPI:     110 100 100 100 100 100 100 100
        
        uint32_t spi_pattern = 0;
        for (int b = 7; b >= 0; b--) {
            uint32_t pattern = ((val >> b) & 1) ? 0b110 : 0b100;
            spi_pattern = (spi_pattern << 3) | pattern;
        }
        // spi_pattern now holds 24 bits (3 bytes).
        // Write them to buffer (MSB first)
        tx_buffer[offset] = (spi_pattern >> 16) & 0xFF;
        tx_buffer[offset+1] = (spi_pattern >> 8) & 0xFF;
        tx_buffer[offset+2] = spi_pattern & 0xFF;
    };

    // GREEN (Byte 0 of pixel data in buffer)
    uint32_t p = 0;
    for(int b=7; b>=0; b--) p = (p << 3) | (((g >> b) & 1) ? 0b110 : 0b100);
    ptr[0] = (p >> 16); ptr[1] = (p >> 8); ptr[2] = p;

    // RED (Byte 3 of pixel data)
    p = 0;
    for(int b=7; b>=0; b--) p = (p << 3) | (((r >> b) & 1) ? 0b110 : 0b100);
    ptr[3] = (p >> 16); ptr[4] = (p >> 8); ptr[5] = p;

    // BLUE (Byte 6 of pixel data)
    p = 0;
    for(int b=7; b>=0; b--) p = (p << 3) | (((b >> b) & 1) ? 0b110 : 0b100);
    ptr[6] = (p >> 16); ptr[7] = (p >> 8); ptr[8] = p;
}

void show() {
    // Send the entire buffer to SPI
    // The trailing 0s in the buffer act as the Reset signal
    if (write(spi_fd, tx_buffer, tx_buffer_len) < 0) {
        perror("SPI Write failed");
    }
}

void clear() {
    // Zero out the data part of the buffer (keep the length)
    // Length is roughly LED_COUNT * 9
    memset(tx_buffer, 0, LED_COUNT * 9);
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
        // Do not show() yet if you want to clear it instantly, 
        // but usually we want to see it turn off before the next one turns on?
        // Let's just update the buffer. The next loop will light the next one.
        // Actually, safer to clear it visually now:
        show();
    }

    printf("\nDone!\n");
    cleanup(0);
    return 0;
}