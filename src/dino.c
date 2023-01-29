#include "hardware/gpio.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/sync.h"
#include "pico/rand.h"
#include "pico/stdio.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "picoLCD/LCDops.h"
#include "picoLCD/generalOps.h"
#include "picoLCD/presetChars.h"
#include "picoLCD/presetMessages.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// function copied from pico-examples - button.c
//
// Picoboard has a button attached to the flash CS pin, which the bootrom
// checks, and jumps straight to the USB bootcode if the button is pressed
// (pulling flash CS low). We can check this pin in by jumping to some code in
// SRAM (so that the XIP interface is not required), floating the flash CS
// pin, and observing whether it is pulled low.
//
// This doesn't work if others are trying to access flash at the same time,
// e.g. XIP streamer, or the other core.

bool __no_inline_not_in_flash_func(get_bootsel_button)() {
  const uint CS_PIN_INDEX = 1;

  // Must disable interrupts, as interrupt handlers may be in flash, and we
  // are about to temporarily disable flash access!
  uint32_t flags = save_and_disable_interrupts();

  // Set chip select to Hi-Z
  hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                  GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                  IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

  // Note we can't call into any sleep functions in flash right now
  for (volatile int i = 0; i < 1000; ++i)
    ;

  // The HI GPIO registers in SIO can observe and control the 6 QSPI pins.
  // Note the button pulls the pin *low* when pressed.
  bool button_state = !(sio_hw->gpio_hi_in & (1u << CS_PIN_INDEX));

  // Need to restore the state of chip select, else we are going to have a
  // bad time when we return to code in flash!
  hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                  GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                  IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

  restore_interrupts(flags);

  return button_state;
}

// 16 columns displayed + 1 for preloading generated obstacle
// gameArena is used as the array in which to store things
// to be displayed on the display
int gameArena[2][17];

// GPIO pins used for LCD display
// D0, D1, D2, D3, D4, D5, D6, D7, E, RS, RW, LCD columns, LCD rows
int LCDpins[14] = {12, 11, 10, 9, 8, 7, 6, 5, 13, 15, 14, 16, 2};

void clearLCDCharacter(char position[]) {
  LCDgoto(position);
  LCDwriteMessage(" ");
}

void writeGameArenaToLCD() {
  for (int i = 0; i < 17; i++) {
    char obstaclePosition[10];

    //  first row
    if (gameArena[0][i] > 1) { // any obstacle
      if (i >= 1) { // if the obstacle can still be avoided (index 0 = dino)
        // obstacle at index `i`
        int obstacle = gameArena[0][i];

        // shift obstacle to the left by one
        gameArena[0][i] = 0;
        gameArena[0][i - 1] = obstacle;

        // clear character at index `i`
        sprintf(obstaclePosition, "%X", i);
        clearLCDCharacter(obstaclePosition);

        // write character to LCD to position `i - 0x1`
        sprintf(obstaclePosition, "%X", i - 0x1);
        LCDgoto(obstaclePosition);
        LCDwriteCustomCharacter(obstacle);
      }
    }

    // second row
    if (gameArena[1][i] > 1) {
      if (i >= 1) {
        int obstacle = gameArena[1][i];
        gameArena[1][i] = 0;
        gameArena[1][i - 1] = obstacle;

        // clear character at index `i + 0x40`
        sprintf(obstaclePosition, "%X", i + 0x40);
        clearLCDCharacter(obstaclePosition);

        // write character to LCD to position `i - 0x1 + 0x40`
        sprintf(obstaclePosition, "%X", i - 0x1 + 0x40);
        LCDgoto(obstaclePosition);
        LCDwriteCustomCharacter(obstacle);
      }
    }
  }
}

void waitForInput() {
  while (true) {
    if (get_bootsel_button() ^ 0) {
      break;
    }
  }
}

int main() {
  stdio_init_all();

  // initialize all GPIO pins for the LCD
  for (int gpio = 0; gpio < 11; gpio++) {
    gpio_init(LCDpins[gpio]);
    gpio_set_dir(LCDpins[gpio], true);
    gpio_put(LCDpins[gpio], false);
  }

  LCDinit();
  // args: display, cursor, cursor blink
  LCDdisplayControl(1, 0, 0);

  // custom characters
  // dino 1
  LCDcreateCharacter(1, "00000110", "00000111", "00001111", "00011110",
                     "00011110", "00010100", "00010110", "00011000");
  // dino 2
  LCDcreateCharacter(2, "00000110", "00000111", "00001111", "00011110",
                     "00011110", "00010100", "00011100", "00000110");
  // cloud
  LCDcreateCharacter(3, "00000000", "00000000", "00001100", "00011111",
                     "00000000", "00000110", "00001111", "00000000");
  // bush
  LCDcreateCharacter(4, "00000000", "00000100", "00010100", "00001100",
                     "00000101", "00000110", "00000100", "00000100");

  // start screen
  LCDclear();
  LCDgoto("00");
  LCDwriteMessage("  PRESS BUTTON  ");
  LCDgoto("40");
  LCDwriteMessage("    TO START    ");

  waitForInput();

  while (1) {
    // initialize the gameArena array with 0
    for (int i = 0; i < 17; i++) {
      gameArena[0][i] = 0;
      gameArena[1][i] = 0;
    }

    sleep_ms(20);

    LCDclear();
    LCDgoto("40");
    LCDwriteCustomCharacter(1);

    int dinoJumped; // prevent (constant) flying by checking this variable;

    for (bool collision = false; collision == false;) {
      // shift the arena 3 times before generating an obstacle
      for (int i = 0; i < 3; i++) {
        // if there is an obstacle in front of the dino and we are about
        // to shift the arena (`writeGameArenaToLCD()`), the dino unalives
        if (gameArena[0][1] != 0 && gameArena[0][0] == 1 ||
            gameArena[1][1] != 0 && gameArena[1][0] == 1) {
          collision = 1;
        }

        // shift gameArena and write it to the LCD
        writeGameArenaToLCD();

        if (get_bootsel_button() ^ 0 && dinoJumped < 2) {
          clearLCDCharacter("40");
          LCDgoto("00");
          LCDwriteCustomCharacter(1);
          sleep_ms(100);
          LCDgoto("00");
          LCDwriteCustomCharacter(2);

          gameArena[0][0] = 1;
          gameArena[1][0] = 0;

          dinoJumped++;
        } else {
          clearLCDCharacter("00");
          LCDgoto("40");
          LCDwriteCustomCharacter(1);
          sleep_ms(100);
          LCDgoto("40");
          LCDwriteCustomCharacter(2);

          gameArena[0][0] = 0;
          gameArena[1][0] = 1;
          dinoJumped = 0;
        }
      }

      if (get_rand_32() % 3 == 1) {
        gameArena[0][16] = 3;
      } else if (get_rand_32() % 3 == 2) {
        gameArena[1][16] = 4;
      }
    }

    // game over screen
    LCDclear();
    LCDgoto("00");
    LCDwriteMessage("   GAME OVER!   ");
    LCDgoto("40");
    LCDwriteMessage(" DINO UNALIVED! ");

    waitForInput();
  }
}
