#include <stdbool.h>
#include <stdint.h>

#include <xc.h>

#pragma config FOSC = INTOSC, WDTE = ON, CP = OFF, PLLEN = ON, BOREN = OFF, MCLRE = ON, CLKOUTEN = OFF, PWRTE = OFF, LPBOREN = ON,

#define DIN_PIN_MASK (1 << 5)
#define DOUT_PIN_MASK (1 << 4)
#define RED_PIN_MASK   (1 << 0)
#define GREEN_PIN_MASK (1 << 1)
#define BLUE_PIN_MASK  (1 << 2)
#define RGB_PIN_MASK (RED_PIN_MASK | GREEN_PIN_MASK | BLUE_PIN_MASK)

// How long of a silence on the line is considered a latch event.
// Units are 32[us] per count.
#define LATCH_TIME 30

#define TEMPERATURE_20C 132
#define TEMPERATURE_30C 125
#define TEMPERATURE_40C 119
#define TEMPERATURE_50C 112
#define TEMPERATURE_60C 106
#define TEMPERATURE_70C 99

// To convert this value to real temperature (in Celcius):
// T = -temperature * 1.5151515 + 323
static uint8_t temperature;

static uint8_t r, g, b;
uint8_t color_array[24];
volatile uint8_t bit_count;

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
  PWM1DC = 0;
  PWM2DC = 0;
  PWM3DC = 0;

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
  // Set the output pins as output.
  TRISA = ~(RGB_PIN_MASK | DOUT_PIN_MASK);
}

static void InitializeTimers() {
  // Timer 0 : Instruction clock / 256 = 31.25kHz (32us).
  OPTION_REG = 0x07;

  // Timer 2: Instruction clock / 16 = 500kHz (2us).
  T2CON = 0x06;
}

static void InitializeTemperature() {
  // Enable temperature indicator at high range and FVR.
  FVRCON = 0xB0;

  // Configure ADC.
  ADCON2 = 0x00;
  // Left-aligned, Tad = 1us.
  ADCON1 = 0x20;

  // Wait for FVR to settle.
  while (!FVRRDY);
}

static inline void Timer2Delay(uint8_t count) {
  TMR2 = 0;
  while (TMR2 < count);
}

static inline uint16_t AcquireTemperature() {
  // Configure the ADC to acquire the temperature indicator.
  ADCON0 = 0x75;

  // Wait for acquisition (200us).
  Timer2Delay(100);

  // Sample.
  GO = 1;       // Start.
  while (GO);   // Wait completion.

  return ADRES;
}

static inline uint16_t AcquireVref() {
    // Enable the FVR amplifier (2x)
  FVRCON = 0xB2;

  // Configure the ADC to acquire the FVR.
  ADCON0 = 0x7D;

  // Wait for acquisition (20us).
  Timer2Delay(10);

  // Sample.
  GO = 1;       // Start.
  while (GO);   // Wait completion.

  // Disable the FVR amplifier.
  FVRCON = 0xB0;

  return ADRES;
}

static void ReadTemperature() {
  uint16_t v1 = AcquireVref();
  uint16_t t = AcquireTemperature();
  uint16_t v2 = AcquireVref();

  // Turn ADC off.
  ADCON0 = 0;

  uint8_t vavg = ((v1 >> 1) + (v2 >> 1) + 128) >> 8;
  temperature = (0xFFC0 - t) / vavg - 200;
}

static void ConvertColor() {
  r = g = b = 0;

  // Note the the temperature units used are reversed: bigger number == lower
  // temperature. So the condition below actually means temp <= 60C.
  if (temperature >= TEMPERATURE_60C) {
    uint8_t i;
    uint8_t const * p = color_array;

    // Read Green bits.
    for (i = 0; i < 8; ++i) {
      g <<= 1;
      g |= ((*p++ >> 5) & 1);
    }

    // Read Red bits.
    for (i = 0; i < 8; ++i) {
      r <<= 1;
      r |= ((*p++ >> 5) & 1);
    }

    // Read Blue bits.
    for (i = 0; i < 8; ++i) {
      b <<= 1;
      b |= ((*p++ >> 5) & 1);
    }
  }
}

static void LatchColor() {
    // Prepare the new values in the PWM periods.
    PWM3DC = (uint16_t) r * r;
    PWM2DC = (uint16_t) g * g;
    PWM1DC = (uint16_t) b * b;

    // Latch all at once.
    PWMLD = 0x07;
}

void main() {
  // High speed.
  OSCCONbits.IRCF = 0xe;

  InitializePwm();
  InitializePins();
  InitializeTimers();
  InitializeTemperature();

  // Enable interrupts on rising edge of DIN.
  IOCAP5 = 1;
  IOCIE = 1;

  // Wait for silence. Do not take interrupts and do not set the output.
  TMR0 = 0;
  while (TMR0 < LATCH_TIME) {
    if (IOCAF5) {
      TMR0 = 0;
      IOCAF5 = 0;
    }
  }

  // Main loop.
  while (true) {
    // FSR0 needs to point to the beginning of the color array.
    FSR0 = (uint16_t) color_array;

    // And we have 24 bits left to read (+1, as 1 means done).
    bit_count = 25;

    // Ready to handle interrupts.
    GIE = 1;

    // Wait for read to complete. The interrupt handler will decrement
    // bit_count.
    while (bit_count != 1);

    // Then, wait for silence of LATCH_TIME. The interrupt handler will clear
    // timer 0 on every edge, so we'll get out of this loop only after
    // LATCH_TIME with no edges.
    TMR0 = 0;
    while (TMR0 < LATCH_TIME);

    // Disable interrupts while processing.
    GIE = 0;

    // Read the temperature. This should take ~250us.
    ReadTemperature();
  
    // Convert the color read by the interrupt handler.
    ConvertColor();

    // The color array not contains the 24 bits we've read. Latch.
    LatchColor();

    // Clear the watchdog.
    CLRWDT();
  }
}
