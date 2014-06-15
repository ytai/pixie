#include <stdbool.h>
#include <stdint.h>

#include <xc.h>

#pragma config FOSC = INTOSC, WDTE = ON, CP = OFF, PLLEN = ON, BOREN = OFF, MCLRE = ON, CLKOUTEN = OFF, PWRTE = OFF, LPBOREN = ON,

#define DIN_PIN_MASK (1 << 0)
#define DOUT_PIN_MASK (1 << 1)
#define RED_PIN_MASK   (1 << 2)
#define GREEN_PIN_MASK (1 << 4)
#define BLUE_PIN_MASK  (1 << 5)
#define RGB_PIN_MASK (RED_PIN_MASK | GREEN_PIN_MASK | BLUE_PIN_MASK)

// How long of a silence on the line is considered a latch event.
// Units are 32[us] per count.
#define LATCH_TIME 30

#define TEMPERATURE_20C 82
#define TEMPERATURE_30C 76
#define TEMPERATURE_40C 69
#define TEMPERATURE_50C 62
#define TEMPERATURE_60C 56
#define TEMPERATURE_70C 49

// To convert this value to real temperature (in Celcius):
 // T = 144.43 - temperature * 1.5151515
static uint8_t temperature;

static uint16_t t;
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
  // LED pins are open drain.
  ODCONA = RGB_PIN_MASK;
  // PWM 1/2 go to their alternate pins.
  APFCON = 0x03;
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

static void ReadTemperature() {
  // Configure the ADC to acquire the temperature indicator.
  ADCON0 = 0x75;

  // Wait for acquisition (200us).
  Timer2Delay(200);

  // Sample.
  GO = 1;       // Start.
  while (GO);   // Wait completion.

  t = ADRES;

    // Enable the FVR amplifier (2x)
  FVRCON = 0xB2;

  // Configure the ADC to acquire the FVR.
  ADCON0 = 0x7D;

  // Wait for acquisition (10us).
  Timer2Delay(10);

  // Sample.
  GO = 1;       // Start.
  while (GO);   // Wait completion.

  // Disable the FVR amplifier.
  FVRCON = 0xB0;

  temperature = (0xFFC0 - t) / ADRESH;
}

static void ConvertColor() {
  // Note the the temperature units used are reversed: bigger number == lower
  // temperature. So the condition below actually means temp <= 60C.
  if (temperature >= TEMPERATURE_60C && bit_count == 1) {
    uint8_t i;
    uint8_t const * p = color_array;

    // Read Green bits.
    g = 0;
    for (i = 0; i < 8; ++i) {
      g <<= 1;
      g |= (*p++ & 1);
    }

    // Read Red bits.
    r = 0;
    for (i = 0; i < 8; ++i) {
      r <<= 1;
      r |= (*p++ & 1);
    }

    // Read Blue bits.
    b = 0;
    for (i = 0; i < 8; ++i) {
      b <<= 1;
      b |= (*p++ & 1);
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

    // Force off until told otherwise.
    r = g = b = 0;
}

void main() {
  // High speed.
  OSCCONbits.IRCF = 0xe;

  InitializePwm();
  InitializePins();
  InitializeTimers();
  InitializeTemperature();

  // Enable interrupts on rising edge of DIN.
  IOCAP0 = 1;
  IOCIE = 1;

  // Wait for silence. Do not take interrupts and do not set the output.
  TMR0 = 0;
  while (TMR0 < LATCH_TIME) {
    if (IOCAF0) {
      TMR0 = 0;
      IOCAF0 = 0;
    }
  }

  // Main loop.
  while (true) {
    // FSR0 needs to point to the beginning of the color array.
    FSR0 = (uint16_t) color_array;

    // And we have 24 bits left to read (+1, as 0 means done).
    bit_count = 25;

    // Ready to handle interrupts.
    GIE = 1;

    // Wait for read to begin, followed by a silence.
    // In the meantime, the interrupt handler does the job.
    while (bit_count == 25);

    TMR0 = 0;

    // While we're waiting, read the temperature. This should take ~250us, so
    // we won't be missing our latch time, which is much greater.
    ReadTemperature();

    // And also while we're waiting, if we're done reading our own value,
    // convert it. If we got a silence before having read 24 bits, we will
    // simply skip the conversion and latch the previous values.
    while (TMR0 < LATCH_TIME) {
      if (bit_count == 1) {
        ConvertColor();
        break;
      }
    }

    while (TMR0 < LATCH_TIME);

    // Disable interrupts while processing.
    GIE = 0;

    // Clear the watchdog.
    CLRWDT();
  
    // The color array not contains the 24 bits we've read. Latch.
    LatchColor();
  }
}
