#include <stdbool.h>
#include <stdint.h>

#include <xc.h>

#pragma config FOSC = INTOSC, WDTE = OFF, CP = OFF, PLLEN = ON

#define DIN_PIN_MASK (1 << 0)
#define DOUT_PIN_MASK (1 << 1)
#define RED_PIN_MASK   (1 << 2)
#define GREEN_PIN_MASK (1 << 4)
#define BLUE_PIN_MASK  (1 << 5)
#define RGB_PIN_MASK (RED_PIN_MASK | GREEN_PIN_MASK | BLUE_PIN_MASK)

static void InitializePwm() {
  // Fosc, no prescaler = 32MHz. Full cycle will be: 32MHz / 2^16 = 488Hz
  PWM1CLKCON = 0;
  PWM2CLKCON = 0;
  PWM3CLKCON = 0;

  // Set the period to 2^16
  PWM1PRH = PWM1PRL = 0xFF;
  PWM2PRH = PWM2PRL = 0xFF;
  PWM3PRH = PWM3PRL = 0xFF;

  // Set the phase to 0.
  PWM1PHH = PWM1PHL = 0;
  PWM2PHH = PWM2PHL = 0;
  PWM3PHH = PWM3PHL = 0;

  // Set the offset to 0.
  PWM1OFH = PWM1OFL = 0;
  PWM2OFH = PWM2OFL = 0;
  PWM3OFH = PWM3OFL = 0;

  // Set the duty cycle to 0.
  PWM1DCH = PWM1DCL = 0;
  PWM2DCH = PWM2DCL = 0;
  PWM3DCH = PWM3DCL = 0;

  // Latch the values on next trigger.
  PWMLD = 0x07;

  // Enable the output pins.
  PWM1CON = 0x40;
  PWM2CON = 0x40;
  PWM3CON = 0x40;

  // Enable all PWM modules.
  PWMEN = 0x07;
}

static void InitializePins() {
  // Disable analog inputs.
  ANSELA = 0;
  // All outputs default to 0.
  LATA = 0;
  // LED pins are open drain.
  ODCONA = RGB_PIN_MASK;
  // PWM 1/2 go to their alternate pins.
  APFCON = 0x03;
  // Set the output pins as output.
  TRISA = ~(RGB_PIN_MASK | DOUT_PIN_MASK);
}

static void InitializeEcho() {
  IOCAP0 = 1;
  IOCAN0 = 1;
}

static void InitializeTimer() {
  OPTION_REG = 0x07;
}

bank0 uint8_t r, g, b;

void LatchColor() {
    PWM3DCH = r;
    PWM3DCL = r;
    PWM2DCH = g;
    PWM2DCL = g;
    PWM1DCH = b;
    PWM1DCL = b;
    PWMLD = 0x07;
}

// Implemented in assembly.
void Read();

// TODO:
// - Optimize.
// - Derive timing requirements.
// - Watchdog.
// - Over-temperature protection.

void main() {
  uint16_t i = 0;
  uint16_t delay;
  // High speed.
  OSCCONbits.IRCF = 0xe;

  InitializePwm();
  InitializePins();
  InitializeEcho();
  InitializeTimer();

  // Enable interrupts
  GIE = 1;

  while (true) {
    asm("BANKSEL 0");
    Read();

    // Echo on.
    IOCIE = 1;

    // Wait for silence.
    TMR0 = 0;
    while (TMR0 < 30);

    // Echo off.
    IOCIE = 0;

    // Latch.
    LatchColor();
  }
}
