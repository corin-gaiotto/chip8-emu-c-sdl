#include <stdio.h>
#include <stdbool.h>
#include "SDL2/SDL.h"
#include "chip8-system.c"
#include <time.h>

// must be divisible by (64, 32) and ideally have same aspect ratio
#define WINDOWX 640
#define WINDOWY 320

int main(int argc, char** argv) {
    
    srand(time(NULL));

    // initialize chip-8 emulator
    int errCode = CHIP8_INITIALIZE();
    if (errCode != 0) {
        printf("An error occurred while initializing the emulator. (check stderr)\n");
    }

    // program stepping
    bool stepping = false;


    // initialize graphics
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

    if (SDL_Init(SDL_INIT_EVERYTHING) < 0) {
        fprintf(stderr, "Error initializing SDL\n");
        return 1;
    }

    window = SDL_CreateWindow( "Chip-8 Emulator", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, WINDOWX, WINDOWY, SDL_WINDOW_SHOWN );

	// Make sure creating the window succeeded
	if ( !window ) {
		fprintf(stderr, "Error initializing window\n");
		// End the program
		return 1;
	}

    renderer = SDL_CreateRenderer(window, -1, 0);

    if (!renderer) {
        fprintf(stderr, "Error creating renderer\n");
        fprintf(stderr, SDL_GetError());
        return 1;
    }

    SDL_Event ev;

    bool running = true; // game loop

    int value = 0;

    SDL_Rect fullscreen;
    fullscreen.x = 0;
    fullscreen.y = 0;
    fullscreen.h = WINDOWY;
    fullscreen.w = WINDOWX;

    SDL_Rect curr_pixel;
    curr_pixel.x = 0;
    curr_pixel.y = 0;
    curr_pixel.h = WINDOWY/SCREENY;
    curr_pixel.w = WINDOWX/SCREENX;

    double deltaTime = 0;
    Uint64 start_time = 0;
    Uint64 curr_time = SDL_GetPerformanceCounter();

    while (running) {

        start_time = curr_time;
        curr_time = SDL_GetPerformanceCounter();
        deltaTime = (double)((curr_time - start_time)*1000 / (double)SDL_GetPerformanceFrequency());

        SDL_RenderClear(renderer);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 1);
        SDL_RenderFillRect(renderer, &fullscreen);
        while (SDL_PollEvent(&ev) != 0) {
            if (ev.type == SDL_QUIT) {
                running = false;
            }
        }
        int out = CHIP8_EMULATECYCLE(deltaTime);
        if (out == 1) {
            printf("Program hit end, quitting\n");
            return 0;
        } else if (out == 2) {
            printf("Error during program, quitting\n");
            return 1;
        }

        // Display GFX
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        for (int i = 0; i < SCREENY; i++) {
            for (int j = 0; j < SCREENX; j++) {
                int value = gfx[i*SCREENX+j];
                if (value == 1) {
                    curr_pixel.x = j * WINDOWX / SCREENX;
                    curr_pixel.y = i * WINDOWX / SCREENX;
                    SDL_RenderFillRect(renderer, &curr_pixel);
                }
            }
        }

        if (stepping) {
            char temp;
            scanf("%c",&temp);
        }

        SDL_PumpEvents();
        SDL_RenderPresent(renderer);
        SDL_UpdateWindowSurface(window);
    }


    SDL_DestroyWindow(window);

    SDL_Quit();

    return 0;
}