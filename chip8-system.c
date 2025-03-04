#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "SDL2/SDL.h"

// Keybinds for keypad buttons 
/* 
1,2,3,C,
4,5,6,D,
7,8,9,E,
A,0,B,F
*/
int keybinds[16] = {
    SDL_SCANCODE_X, SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, 
    SDL_SCANCODE_Q, SDL_SCANCODE_W, SDL_SCANCODE_E, SDL_SCANCODE_A, 
    SDL_SCANCODE_S, SDL_SCANCODE_D, SDL_SCANCODE_Z, SDL_SCANCODE_C, 
    SDL_SCANCODE_4, SDL_SCANCODE_R, SDL_SCANCODE_F, SDL_SCANCODE_V};

// current opcode
unsigned short opcode;

// all memory (4KB)
#define MEMORYSIZE 4096
unsigned char memory[MEMORYSIZE];

// Registers, V0 -> VF. VF is used as carry flag
#define REGISTERCOUNT 16
unsigned char V[REGISTERCOUNT];

// Index register
unsigned short I;

// Program counter
unsigned short pc;

// MEMORY MAPPING:
//
// 0x000-0x1FF: unused (used for interpreter itself, or font set)
// 0x050-0x0A0: built-in font set (0-F)
// 0x200-0xFFF: Program ROM and work RAM


// Screen State (in pixels):
#define SCREENX 64
#define SCREENY 32
unsigned char gfx[SCREENX * SCREENY]; // indexed y*SCREENX + x

// timer registers (count at 60Hz, when set above 0 count down to 0) (system buzzer sounds when sound timer reaches 0)
unsigned char delay_timer;
unsigned char sound_timer;

// 60 Hz accumulator that increments by deltaTime. When it reaches 1/60th of a second, subtracts 1/60th of a second and decrements timers
double accumulator;

// Stack. Used to remember the location before a jump is performed.
#define STACKSIZE 16
unsigned short stack[STACKSIZE];
unsigned short sp;

// Keypad. Used for input.
#define KEYPADSIZE 16
unsigned char key[KEYPADSIZE];


// Base fontset. Each group of 5 corresponds to 1 character.
#define FONTSETSIZE 80
unsigned char chip8_fontset[FONTSETSIZE] =
{ 
  0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
  0x20, 0x60, 0x20, 0x20, 0x70, // 1
  0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
  0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
  0x90, 0x90, 0xF0, 0x10, 0x10, // 4
  0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
  0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
  0xF0, 0x10, 0x20, 0x40, 0x40, // 7
  0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
  0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
  0xF0, 0x90, 0xF0, 0x90, 0x90, // A
  0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
  0xF0, 0x80, 0x80, 0x80, 0xF0, // C
  0xE0, 0x90, 0x90, 0x90, 0xE0, // D
  0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
  0xF0, 0x80, 0xF0, 0x80, 0x80  // F
};


int CHIP8_INITIALIZE() 
{
    printf("Initializing Chip-8...\n");
    pc     = 0x200;
    opcode = 0;
    I      = 0;
    sp     = 0;

    printf("Clearing display...\n");
    memset(gfx, 0, SCREENX*SCREENY);

    printf("Clearing stack...\n");
    memset(stack, 0, STACKSIZE);

    printf("Clearing registers...\n");
    memset(V, 0, REGISTERCOUNT);

    printf("Clearing memory...\n");
    memset(memory, 0, MEMORYSIZE);

    printf("Loading fontset...\n");
    memcpy(memory, chip8_fontset, FONTSETSIZE*sizeof(unsigned char));

    printf("Resetting timers...\n");
    // Reset timers
    delay_timer = 0;
    sound_timer = 0;

    printf("Loading program...\n");
    // Read program and load it into memory
    FILE *progFile = fopen("Tetris.ch8", "rb");
    if (progFile == NULL) {
        fprintf(stderr, "Error reading program file\n");
        return 1;
    }

    printf("Reading program...\n");
    fseek(progFile, 0L, SEEK_END);
    long bufferSize = ftell(progFile);
    printf("Program file size: %ld\n", bufferSize);
    rewind(progFile);

    fread(memory+0x200,sizeof(unsigned char),3896,progFile);

    if (fclose(progFile) == EOF) {
        fprintf(stderr, "Error closing program file\n");
    }

    printf("Done!\n");

    printf("Initialization Complete!\n");
    return 0;
}

int CHIP8_TIMERDECREMENT() 
{
    if (delay_timer > 0) {
        delay_timer--;
    }
    if (sound_timer > 0) {
        sound_timer--;
    }
    return 0;
}

int CHIP8_EMULATECYCLE(double deltaTime) 
{
    // regardless of opcodes, get current keyboard state
    Uint8 *keyboardState = SDL_GetKeyboardState(NULL);
    SDL_Event ev;

    // timing
    //printf("%f\n",accumulator);
    accumulator += deltaTime;
    while (accumulator > 1000/60.0) {
        accumulator -= 1000/60.0;
        CHIP8_TIMERDECREMENT();
    }

    // used for error codes
    int starting_pc = pc;

    // convenience and readability
    unsigned char  x;
    unsigned char  y;
    unsigned char  n;
    unsigned short nnn;
    unsigned char  kk;

    // opcodes are 2 bytes long, so we read 2 bytes from memory.
    // this also means program counter always increments by 2
    opcode = memory[pc] << 8 | memory[pc + 1];

    printf("%04X | %04X | ", pc, opcode);

    // by default, program increments program counter by 2 every time: however, some functions such as jumps should not.
    bool incPc = true;

    if (opcode == 0) {
        return 1;
    }

    // handle opcode (based on first 4 bits)
    // if opcode has been implemented, description will start with "# "
    switch(opcode & 0xF000) // mask first 4 bits
    {
        case 0x0000:
            switch(opcode & 0x000F)
            {
                case 0x0000:
                    printf("# Clear Screen\n");
                    // clear screen data
                    memset(gfx, 0, SCREENX*SCREENY);
                break;

                case 0x000E:
                    printf("# Return from subroutine\n");
                    // jump back to current stack element (and then automatically step forwards)
                    pc = stack[sp-1u];
                    
                    // pop off stack
                    stack[sp-1u] = 0;
                    if (sp == 0) {
                        fprintf(stderr, "Error on instruction %04X: cannot pop off of empty stack\n",starting_pc);
                        return 2;
                    } else {
                        sp--;
                    }
                break;

                default:
                    printf("Unknown opcode: 0x%X\n", opcode);
            }
        break;

        case 0x1000:
            printf("# Jump to location %03X\n", opcode & 0x0FFF);
            nnn = opcode & 0x0FFF;
            pc = nnn;
            incPc = false;
        break;

        case 0x2000:
            printf("# Call Subroutine at %03X\n", opcode & 0x0FFF);
            nnn = opcode & 0x0FFF;
            if (sp == 15u) {
                fprintf(stderr, "Exceeded max subroutine depth of 16\n");
                return 2;
            } else {
                stack[sp] = pc;
                sp++;
                pc = nnn;
                incPc = false;
            }
        break;

        case 0x3000:
            printf("# Skip next if V%01X == %02X\n", (opcode & 0x0F00) >> 8, opcode & 0x00FF);
            x = (opcode & 0x0F00) >> 8;
            kk = (opcode & 0x00FF);
            if (V[x] == kk) {
                pc += 2;
            }
        break;

        case 0x4000:
            printf("# Skip next if V%01X != %02X\n", (opcode & 0x0F00) >> 8, opcode & 0x00FF);
            x = (opcode & 0x0F00) >> 8;
            kk = (opcode & 0x00FF);
            if (V[x] != kk) {
                pc += 2;
            }
        break;

        case 0x5000:
            printf("# Skip next if V%01X == V%01X\n", (opcode & 0x0F00) >> 8, (opcode & 0x00F0) >> 4);
            x = (opcode & 0x0F00) >> 8;
            y = (opcode & 0x00F0) >> 4;
            if (V[x] == V[y]) {
                pc += 2;
            }
        break;

        case 0x6000:
            printf("# Set register V%01X to %02X\n", (opcode & 0x0F00) >> 8, opcode & 0x00FF);
            x = (opcode & 0x0F00) >> 8;
            kk = opcode & 0x00FF;
            V[x] = kk;
        break;

        case 0x7000:
            printf("# Set V%01X to V%01X + %02X\n", (opcode & 0x0F00) >> 8, (opcode & 0x0F00) >> 8, (opcode & 0x00FF));
            x = (opcode & 0x0F00) >> 8;
            kk = opcode & 0x00FF;
            V[x] = V[x] + kk;
        break;

        case 0x8000:
            // various operations on 2 registers
            switch(opcode & 0x000F) 
            {
                case 0x0000:
                    printf("# Set V%01X to V%01X\n", (opcode & 0x0F00) >> 8, (opcode & 0x00F0) >> 4);
                    x = (opcode & 0x0F00) >> 8;
                    y = (opcode & 0x00F0) >> 4;
                    V[x] = V[y];
                break;

                case 0x0001:
                    printf("# Set V%01X = V%01X OR V%01X\n", (opcode & 0x0F00) >> 8, (opcode & 0x0F00) >> 8, (opcode & 0x00F0) >> 4);
                    x = (opcode & 0x0F00) >> 8;
                    y = (opcode & 0x00F0) >> 4;
                    V[x] = V[x] | V[y];
                break;

                case 0x0002:
                    printf("# Set V%01X = V%01X AND V%01X\n", (opcode & 0x0F00) >> 8, (opcode & 0x0F00) >> 8, (opcode & 0x00F0) >> 4);
                    x = (opcode & 0x0F00) >> 8;
                    y = (opcode & 0x00F0) >> 4;
                    V[x] = V[x] & V[y];
                break;

                case 0x0003:
                    printf("# Set V%01X = V%01X XOR V%01X\n", (opcode & 0x0F00) >> 8, (opcode & 0x0F00) >> 8, (opcode & 0x00F0) >> 4);
                    x = (opcode & 0x0F00) >> 8;
                    y = (opcode & 0x00F0) >> 4;
                    V[x] = V[x] ^ V[y];
                break;

                case 0x0004:
                    printf("# Set V%01X = V%01X + V%01X, set VF = carry\n", (opcode & 0x0F00) >> 8, (opcode & 0x0F00) >> 8, (opcode & 0x00F0) >> 4);
                    x = (opcode & 0x0F00) >> 8;
                    y = (opcode & 0x00F0) >> 4;
                    if ((int)V[x] + (int)V[y] > 0xFF) {
                        V[0xF] = 1;
                    } else {
                        V[0xF] = 0;
                    }
                    V[x] = V[x] + V[y];
                break;

                case 0x0005:
                    printf("# Set V%01X = V%01X - V%01X, set VF = NOT borrow\n", (opcode & 0x0F00) >> 8, (opcode & 0x0F00) >> 8, (opcode & 0x00F0) >> 4);
                    x = (opcode & 0x0F00) >> 8;
                    y = (opcode & 0x00F0) >> 4;
                    if (V[x] > V[y]) {
                        V[0xF] = 1;
                    } else {
                        V[0xF] = 0;
                    }
                    V[x] = V[x] - V[y];
                break;

                case 0x0006:
                    printf("# Set V%01X = V%01X SHR 1. VF set to least-significant bit, then V%01X divided by 2\n", (opcode & 0x0F00) >> 8, (opcode & 0x0F00) >> 8, (opcode & 0x0F00) >> 8);
                    x = (opcode & 0x0F00) >> 8;
                    V[0xF] = V[x] & 0x1; // set to least-significant bit using mask
                    V[x] = V[x] >> 1;
                break;

                case 0x0007:
                    printf("# Set V%01X = V%01X - V%01X, set VF = NOT borrow\n", (opcode & 0x0F00) >> 8, (opcode & 0x00F0) >> 4, (opcode & 0x0F00) >> 8);
                    x = (opcode & 0x0F00) >> 8;
                    y = (opcode & 0x00F0) >> 4;
                    if (V[y] > V[x]) {
                        V[0xF] = 1;
                    } else {
                        V[0xF] = 0;
                    }
                    V[x] = V[y] - V[x];
                break;

                case 0x000E:
                    printf("# Set V%01X = V%01X SHL 1. VF set to most-significant bit, then V%01X multiplied by 2\n", (opcode & 0x0F00) >> 8, (opcode & 0x0F00) >> 8, (opcode & 0x0F00) >> 8);
                    x = (opcode & 0x0F00) >> 8;
                    V[0xF] = V[x] & 0x80; // set to most-significant bit using mask
                    V[x] = V[x] << 1;
                break;

                default:
                    printf("Unknown opcode: 0x%X\n");
            }
        break;

        case 0x9000:
            printf("# Skip next if V%01X != V%01X\n", (opcode & 0x0F00) >> 8, (opcode & 0x00F0) >> 4);
            x = (opcode & 0x0F00) >> 8;
            y = (opcode & 0x00F0) >> 4;
            if (V[x] != V[y]) {
                pc += 2;
            }
        break;

        case 0xA000:
            printf("# Set I = %03X\n", (opcode & 0x0FFF));
            nnn = (opcode & 0x0FFF);
            I = nnn;
        break;

        case 0xB000:
            printf("# Jump to location %03X + V0\n", (opcode & 0x0FFF));
            nnn = (opcode & 0x0FFF);
            pc = (unsigned short)(nnn + (unsigned short)V[0]);
            incPc = false;
        break;

        case 0xC000:
            printf("# Set V%01X = random byte AND %02X\n", (opcode & 0x0F00) >> 8, (opcode & 0x00FF));
            x = (opcode & 0x0F00) >> 8;
            kk = (opcode & 0x00FF);
            
            V[x] = (rand() % 0x100) & kk;
        break;

        case 0xD000:
            printf("# Display %01X-byte sprite starting at memory location I at (V%01X, V%01X), set VF = collision\n", (opcode & 0x000F), (opcode & 0x0F00) >> 8, (opcode & 0x00F0) >> 4);
            x = (opcode & 0x0F00) >> 8;
            y = (opcode & 0x00F0) >> 4;
            n = (opcode & 0x000F);

            V[0xF] = 0;

            for (int i = 0; i < 8*n; i++) {
                int curr_x = V[x] + (i%8);
                int curr_y = V[y] + (i/8);

                int curr_pixel = gfx[curr_y*SCREENX + curr_x];
                int curr_memory = 0;
                // figure out current bit of current memory
                int curr_bit = i%8;
                curr_memory = (memory[I+i/8] & (1 << (7-curr_bit))) >> (7-curr_bit);

                if (curr_pixel == 1 && curr_memory == 1) {
                    V[0xF] = 1;
                }

                gfx[curr_y*SCREENX + curr_x] = curr_pixel ^ curr_memory;
            }

        break;

        case 0xE000:
            switch(opcode & 0x00FF) 
            {
                case 0x009E:
                    printf("# Skip next if key with value V%0X pressed\n", (opcode & 0x0F00) >> 8);
                    x = (opcode & 0x0F00) >> 8;
                    if (keyboardState[keybinds[V[x]]]) {
                        pc += 2;
                    }
                break;

                case 0x00A1:
                    printf("# Skip next if key with value V%0X not pressed\n", (opcode & 0x0F00) >> 8);
                    x = (opcode & 0x0F00) >> 8;
                    if (!keyboardState[keybinds[V[x]]]) {
                        pc += 2;
                    }
                break;

                default:
                    printf("Unknown opcode: 0x%X\n", opcode);
            }
        break;

        case 0xF000:
            switch(opcode & 0x00FF)
            {
                case 0x0007:
                    printf("# Set V%01X = delay timer value\n", (opcode & 0x0F00) >> 8);
                    x = (opcode & 0x0F00) >> 8;
                    V[x] = delay_timer;
                break;
                
                case 0x000A:
                    printf("# Wait for a key press, store the value of the key in V%01X\n", (opcode & 0x0F00) >> 8);
                    x = (opcode & 0x0F00) >> 8;
                    bool found = false;
                    while (SDL_PollEvent(&ev) != 0) {
                        if (ev.type == SDL_KEYDOWN) {
                            for (int i = 0; i < 16; i++) {
                                if (ev.key.keysym.scancode == keybinds[i]) {
                                    found = true;
                                    V[x] = i;
                                }
                                if (found) {
                                    break;
                                }
                            }
                        }
                        if (found) {
                            break;
                        }
                    }
                    if (found) {
                        incPc = true;
                    } else {
                        incPc = false;
                    }
                break;
                
                case 0x0015:
                    printf("# Set delay timer = V%01X\n", (opcode & 0x0F00) >> 8);
                    x = (opcode & 0x0F00) >> 8;
                    delay_timer = V[x];
                break;
                
                case 0x0018:
                    printf("# Set sound timer = V%01X\n", (opcode & 0x0F00) >> 8);
                    x = (opcode & 0x0F00) >> 8;
                    sound_timer = V[x];
                break;
                
                case 0x001E:
                    printf("# Set I = I + V%01X\n", (opcode & 0x0F00) >> 8);
                    x = (opcode & 0x0F00) >> 8;
                    I = I + V[x];
                break;
                
                case 0x0029:
                    printf("# Set I = location of sprite for digit V%01X\n", (opcode & 0x0F00) >> 8);
                    x = (opcode & 0x0F00) >> 8;
                    I = 0x0000 + V[x] * 5; // digit font is kept starting at 0x0000, each digit takes up 0x20 (32) bits
                break;
                
                case 0x0033:
                    printf("# Store BCD representation of V%01X in memory locations I, I+1, I+2\n", (opcode & 0x0F00) >> 8);
                    x = (opcode & 0x0F00) >> 8;
                    memory[I]     = V[x] / 100;
                    memory[I + 1] = (V[x] / 10) % 10;
                    memory[I + 2] = V[x] % 10;
                    printf("%d = %d,%d,%d",V[x],memory[I],memory[I+1],memory[I+2]);
                break;
                
                case 0x0055:
                    printf("# Store registers V0 through V%01X in memory starting at location I\n", (opcode & 0x0F00) >> 8);
                    x = (opcode & 0x0F00) >> 8;
                    for (int reg = 0; reg <= x; reg++) {
                        memory[I + reg] = V[reg];
                    }
                break;
                
                case 0x0065:
                    printf("# Read registers V0 through V%01X from memory starting at location I\n", (opcode & 0x0F00) >> 8);
                    x = (opcode & 0x0F00) >> 8;
                    for (int reg = 0; reg <= x; reg++) {
                        V[reg] = memory[I + reg];
                    }
                break;
                
                default:
                    printf("Unknown opcode: 0x%X\n", opcode);
            }
        break;

        default:
            printf("Unknown opcode: 0x%X\n", opcode);

    }

    if (incPc) {
        pc += 2;
    }

    return 0;
}