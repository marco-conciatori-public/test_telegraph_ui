#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "ws2811.h"

// --- CONFIGURATION ---
#define TARGET_FREQ     WS2811_TARGET_FREQ
#define GPIO_PIN        18   // PCM/PWM pin. Pin 12 (physical) on the header.
#define DMA             10   // DMA channel to use
#define LED_COUNT       186   // Number of LEDs in your chain
#define STRIP_TYPE      WS2811_STRIP_GRB // PDF Page 4 confirms GRB order

// Global variable for the LED string structure
ws2811_t ledstring = {
    .freq = TARGET_FREQ,
    .dmanum = DMA,
    .channel = {
        [0] = {
            .gpionum = GPIO_PIN,
            .count = LED_COUNT,
            .invert = 0,
            .brightness = 255, // Max brightness (0-255)
            .strip_type = STRIP_TYPE,
        },
        [1] = {
            .gpionum = 0,
            .count = 0,
            .invert = 0,
            .brightness = 0,
        },
    },
};

// Signal handler to clear LEDs on Ctrl+C
void cleanup(int signum) {
    printf("\nInterrupted! Clearing LEDs and exiting...\n");
    
    // Turn off all LEDs
    for (int i = 0; i < LED_COUNT; i++) {
        ledstring.channel[0].leds[i] = 0;
    }
    ws2811_render(&ledstring);
    ws2811_fini(&ledstring);
    exit(signum);
}

int main() {
    ws2811_return_t ret;

    // Register signal handlers for clean exit
    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);

    // Initialize the library
    if ((ret = ws2811_init(&ledstring)) != WS2811_SUCCESS) {
        fprintf(stderr, "ws2811_init failed: %s\n", ws2811_get_return_t_str(ret));
        return ret;
    }

    printf("WS2812B Test Program Started\n");
    printf("Controls: Press ENTER to advance to the next LED.\n");
    printf("Press Ctrl+C to exit.\n\n");

    // Clear strip first
    for (int i = 0; i < LED_COUNT; i++) {
        ledstring.channel[0].leds[i] = 0;
    }
    ws2811_render(&ledstring);

    // Loop through each LED
    for (int i = 0; i < LED_COUNT; i++) {
        // 1. Set current LED to White (0x00RRGGBB format, but library handles GRB order)
        // White = Red(FF) + Green(FF) + Blue(FF) -> 0x00FFFFFF
        ledstring.channel[0].leds[i] = 0x00FFFFFF;

        // 2. Render to strip
        if ((ret = ws2811_render(&ledstring)) != WS2811_SUCCESS) {
            fprintf(stderr, "ws2811_render failed: %s\n", ws2811_get_return_t_str(ret));
            break;
        }

        // 3. Prompt user
        printf("LED %d is ON. Press ENTER for next...", i + 1);
        fflush(stdout);

        // 4. Wait for Enter key
        while(getchar() != '\n');

        // 5. Turn OFF current LED before moving to next
        ledstring.channel[0].leds[i] = 0; 
        
        // Render the OFF state immediately (optional, but safer for power)
        ws2811_render(&ledstring);
    }

    printf("\nTest Complete.\n");
    // Clean up
    ws2811_fini(&ledstring);
    return 0;
}