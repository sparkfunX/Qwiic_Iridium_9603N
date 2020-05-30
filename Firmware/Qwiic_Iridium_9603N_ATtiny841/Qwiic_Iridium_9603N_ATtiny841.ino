/*
  Qwiic Iridium 9603N
  By: Paul Clark
  Date: 21st September 2019
  Version: 1.1

  Change Log:

  V1.1 21st September 2019
  Added the low power mode (requested by Adam Garbo): bit 6 of the IO Register indicates if the 841 should enter
  low power mode after sleep_after millis of inactivity. Set bit 6 high to enable the low power mode.

  V1.0 11th August 2019:
  First commit
  
  Based extensively on:
  Qwiic MP3 Trigger (April 23rd, 2018) and the SparkFun Ublox library
  By: Nathan Seidle, SparkFun Electronics
  
  License: This code is public domain but you buy me a beer if you use this and we meet someday (Beerware license).

  goToSleep adapted from Jack Christensen's AVR Sleep example for the ATtinyX4:
  https://gist.github.com/JChristensen/5616922

  Uses Spence Konde's ATTinyCore boards:
  https://github.com/SpenceKonde/ATTinyCore
  https://github.com/SpenceKonde/ATTinyCore/blob/master/Installation.md
  Add this URL to the Additional Boards Manager URLs in File\Preferences:
  http://drazzy.com/package_drazzy.com_index.json
  and then use the Boards Manager to add support for the ATtiny

  Set Board to ATtiny441/841 (No bootloader)
  Set Chip to ATtiny841
  Set Clock to 8MHz(internal)
  Set BOD Level to BOD Enabled 1.8V
  Set BOD Mode (Active) and (Sleep) to BOD Disabled (this helps reduce power consumption in low power mode)
  Set Save EEPROM to EEPROM Not Retained
  * Set Pin Mapping to Clockwise *
  * Set Wire Modes to Slave Only *
  Set millis()/micros():"Enabled" to Enabled

  If you are using AVRDUDESS to program the ATtiny841, set the Fuses to:
  L: 0xE2 (CKDIV8 disabled, CKOUT disabled, SUT slowly rising, CKSEL internal 8MHz)
  H: 0xDF (RSTDISBL not disabled, DWEN not enabled, SPIEN enabled, WDTON not always on, EESAVE EEPROM not retained, BOD disabled)
  E: 0xFF (SELFPRGEN disabled)

  Pin Allocation:
  0:  Physical Pin 13 (PA0)           : 9603N ON_OFF - pull high to enable the 9603N, pull low to disable it
  1:  Physical Pin 12 (PA1 / TXD0)    : Serial TXD0 - connected to 9603N TX(IN)
  2:  Physical Pin 11 (PA2 / RXD0)    : Serial RXD0 - connected to 9603N RX(OUT)
  3:  Physical Pin 10 (PA3)           : PWR_EN - pull high to enable the 5.3V supply to the 9603N via the P-FET
  4:  Physical Pin 9  (PA4 / SCL/SCK) : I2C SCL
  5:  Physical Pin 8  (PA5 / MISO)    : MISO - connected to pin 1 of the ISP header
  6:  Physical Pin 7  (PA6 / MOSI/SDA): I2C SDA
  7:  Physical Pin 6  (PA7)           : 9603N Network Available - high when the 9603 can successfully receive the Ring Channel
  8:  Physical Pin 5  (PB2)           : LTC3225 Power Good - goes high when the supercapacitors are charged (will also be high when !SHDN is low!)
  9:  Physical Pin 3  (PB1 / INT0)    : 9603N Ring Indicator - will pulse low twice when a new Mobile Terminated SBD message is available
  10: Physical Pin 2  (PB0)           : LTC3225 !SHDN - pull low to disable the supercapacitor charger
  11: Physical Pin 4  (!RESET / PB3)  : ATtiny841 !Reset

  The ATtiny's I2C address is 0x63

  The I2C 'registers' are:
  IO_REG: 0x10 (the I/O pin register)
  LEN_REG: 0xFD (the serial length register: 2 bytes (MSB, LSB) indicating how many serial characters are available to be read)
  DATA_REG: 0xFF (the serial data register: used to read and write serial data from/to the 9603N)

  To read the I/O pins, the Master should: beginTransmission(0x63); write(0x10); endTransmission(false); requestFrom(0x63, 1);

  To write to the I/O pins, the Master should: beginTransmission(0x63); write(0x10); write(<new_output_pin_configuration>); endTransmission(); 
  
  Use a read-modify-write approach when setting output pin(s).

  To write serial data to the 9603N, the Master should: beginTransmission(0x63); write(0xFF); write each byte; endTransmission();

  If there are more than 32 bytes to be written, multiple cycles should be used using endTransmission(false) so the bus is not released part way through.

  To read serial data from the 9603N, the Master should: beginTransmission(0x63); write(0xFD); endTransmission(false); requestFrom(0x63, 2);
  
  The two bytes returned by the requestFrom will be the number of bytes waiting in the serial buffer in MSB, LSB format

  The Master should then request _all_ the serial data (up to) SER_PACKET_SIZE (default 8) bytes at a time from the DATA_REG 'register' 0xFF.
  E.g. if there are 13 bytes waiting to be read, the Master should:

  beginTransmission(0x63); write(0xFF); endTransmission(false);
  requestFrom(0x63, 8, false);
  requestFrom(0x63, 5);

  (This is because the requestEvent does not know how many bytes have been requested!)

  The Ring Indicator bit in the IO_REGISTER is a flag set by the RI signal via INT0.
  To clear the flag: read the I/O pins; clear the IO_RI bit; write the new pin configuration.

*/

#include <Wire.h>

#include <avr/sleep.h> //Needed for sleep_mode
//#include <avr/power.h> //Needed for powering down perihperals such as the ADC/TWI and Timers

//Define the ATtiny841's I2C address
const uint8_t I2C_ADDRESS = 0x63;

//Define the I2C 'registers'
#define IO_REG 0x10 // Read/write the I/O pins
#define LEN_REG 0xFD // The number of serial bytes waiting to be read (MSB, LSB)
#define DATA_REG 0xFF // The serial data 'register'

volatile uint8_t last_address = 0; // Last receiveEvent 'address', used to define what to send during a requestEvent
volatile uint8_t serAvailLSB = 0; // Stored LSB of serial available
volatile uint8_t serAvailMSB = 0; // Stored MSB of serial available

#define SER_PACKET_SIZE 8 // Return up to this many bytes when reading serial data from the DATA_REG

//These are the bit definitions for the IO 'register'
const uint8_t IO_SHDN    = (1 << 0); // LTC3225 !SHDN : Read / Write
const uint8_t IO_PWR_EN  = (1 << 1); // 9603N power enable via the P-FET : Read / Write
const uint8_t IO_ON_OFF  = (1 << 2); // 9603N ON_OFF pin : Read / Write
const uint8_t IO_RI      = (1 << 3); // 9603N Ring Indicator _flag_ : Read / Write (Set by the INT0 service routine, _cleared_ by writing a _1_ to this bit)
const uint8_t IO_NA      = (1 << 4); // 9603N Network Available : Read only
const uint8_t IO_PGOOD   = (1 << 5); // LTC3225 PGOOD : Read only
const uint8_t IO_LOW_PWR = (1 << 6); // Low Power Mode : Read / Write : Set this bit to enable low power mode

//Low Power Settings
const unsigned long sleep_after = 1000; // Engage low power mode after this many millis (if low power mode is enabled)
volatile unsigned long last_activity; // Used to store the value of millis when the last I2C activity took place
volatile boolean LOW_POWER_MODE = false; // Indicates if low power mode is in use

//Create the IO 'register'
//A '1' in any of the bits indicates that the pin is ON (not necessarily that it is HIGH!)
volatile byte IO_REGISTER;

//Create a flag for the RI (INT0) interrupt and clear it
volatile boolean RI_FLAG = false;

//Digital pins
const byte ON_OFF = 0; // 9603N ON_OFF - pull high to enable the 9603N, pull low to disable it
const byte TXPIN = 1; // Serial Tx pin
const byte RXPIN = 2; // Serial Rx pin
const byte PWR_EN = 3; // PWR_EN - pull high to enable the 5.3V supply to the 9603N via the P-FET
const byte NA = 7; // 9603N Network Available - high when the 9603 can successfully receive the Ring Channel
const byte PGOOD = 8; // LTC3225 Power Good - goes high when the supercapacitors are charged (will also be high when !SHDN is low!)
const byte RI = 9; // 9603N Ring Indicator - will pulse low twice when a new Mobile Terminated SBD message is available
const byte SHDN = 10; // LTC3225 !SHDN - pull high to enable the supercapacitor charger

//Define the ON and OFF states for each pin
#define ON_OFF__ON  HIGH // 9603N ON_OFF - pull high to enable the 9603N, pull low to disable it
#define ON_OFF__OFF LOW
#define PWR_EN__ON  HIGH // PWR_EN - pull high to enable the 5.3V supply to the 9603N via the P-FET
#define PWR_EN__OFF LOW
#define SHDN__ON    HIGH // LTC3225 !SHDN - pull high to enable the supercapacitor charger
#define SHDN__OFF   LOW
#define NA__ON      HIGH // 9603N Network Available - high when the 9603 can successfully receive the Ring Channel
#define NA__OFF     LOW
#define PGOOD__ON   HIGH // LTC3225 Power Good - goes high when the supercapacitors are charged (will also be high when !SHDN is low!)
#define PGOOD__OFF  LOW

//INT0 Interrupt Service Routine
//Called on the FALLING edge of the RI pin
void int0ISR() { RI_FLAG = true; } // Set RI_FLAG to true

void setup()
{
  // Digital outputs
  pinMode(ON_OFF, OUTPUT);
  digitalWrite(ON_OFF, ON_OFF__OFF); // Disable the 9603N until PGOOD has gone high
  pinMode(PWR_EN, OUTPUT);
  digitalWrite(PWR_EN, PWR_EN__OFF); // Disable power to the 9603N until PGOOD has gone high
  pinMode(SHDN, OUTPUT);
  digitalWrite(SHDN, SHDN__OFF); // Disable the LTC3225 supercapacitor charger

  // Initialize the IO_REGISTER
  IO_REGISTER = 0; // Clear the IO register

  // Digital inputs
  pinMode(PGOOD, INPUT); // Has its own pullup
  pinMode(NA, INPUT);
  pinMode(RI, INPUT);

  // Serial pins for software serial
  pinMode(TXPIN, OUTPUT); // Tx pin
  pinMode(RXPIN, INPUT); // Rx pin

  // Enable RI interrupts
  attachInterrupt(0, int0ISR, FALLING); // Attach the INT0 interrupt, falling edge (ATTinyCore requires the pin number to be 0)

  // Open serial communication with the 9603N
  Serial.begin(19200); //9603N communicates at 19200bps

  //Begin listening on I2C
  startI2C();

  //Initialise last_activity
  last_activity = millis();
}

void loop()
{
  if (LOW_POWER_MODE) // Is low power mode enabled?
  {
    // If low power mode is enabled, put the 841 into power-down mode after sleep_after millis of inactivity
    if (millis() > (last_activity + sleep_after)) // Have we reached the inactivity limit?
    {
      goToSleep(); // Put the 841 into power-down mode
      // ZZZzzz...
      last_activity = millis(); // After waking, update last_activity so we stay awake for at least sleep_after millis
    }
  }
  noIntDelay(1); // Delay for 1msec to avoid thrashing millis()
}

//Begin listening on I2C bus as I2C slave using the global I2C_ADDRESS
void startI2C()
{
//  Wire.end(); //Before we can change addresses we need to stop

  Wire.begin(I2C_ADDRESS); //Do the Wire.begin using the defined I2C_ADDRESS

  //The connections to the interrupts are severed when a Wire.begin occurs. So re-declare them.
  Wire.onReceive(receiveEvent);
  Wire.onRequest(requestEvent);
}

//goToSleep adapted from: https://gist.github.com/JChristensen/5616922
//Hardwired for the ATtiny441/841 (may not work on other ATtiny's)
//Brown Out Detection is disabled via the fuse bits:
//Set BOD Mode (Active) and (Sleep) to BOD Disabled in the board settings.
void goToSleep(void)
{
    byte adcsra = ADCSRA; //save ADCSRA (ADC control and status register A)
    ADCSRA &= ~_BV(ADEN); //disable ADC by clearing the ADEN bit
    
    byte acsr0a = ACSR0A; //save ACSR0A (Analog Comparator 0 control and status register)
    ACSR0A &= ~_BV(ACIE0); //disable AC0 interrupt
    ACSR0A |= _BV(ACD0); //disable ACO by setting the ACD0 bit
    
    byte acsr1a = ACSR1A; //save ACSR1A (Analog Comparator 1 control and status register)
    ACSR1A &= ~_BV(ACIE1); //disable AC1 interrupt
    ACSR1A |= _BV(ACD1); //disable AC1 by setting the ACD1 bit
    
    byte prr = PRR; // Save the power reduction register
    // Disable the ADC, USART1, SPI, Timer1 and Timer2
    // (Leave TWI, USART0 and Timer0 enabled)
    PRR |= _BV(PRADC) | _BV(PRUSART1) | _BV(PRSPI) | _BV(PRTIM1) | _BV(PRTIM2); 
    
    byte mcucr = MCUCR; // Save the MCU Control Register
    // Set Sleep Enable (SE=1), Power-down Sleep Mode (SM1=1, SM0=0), INT0 Falling Edge (ISC01=1, ISC00=0)
    MCUCR = _BV(SE) | _BV(SM1) | _BV(ISC01);

    sleep_cpu(); //go to sleep
    
    MCUCR = mcucr; // Restore the MCU control register
    PRR = prr; // Restore the power reduction register
    ACSR1A = acsr1a; // Restore ACSR1A    
    ACSR0A = acsr0a; // Restore ACSR0A    
    ADCSRA = adcsra; //Restore ADCSRA
}

//Software delay. Does not rely on internal timers.
void noIntDelay(byte amount)
{
  for (volatile byte y = 0 ; y < amount ; y++)
  {
    //ATtiny84 at 8MHz
    for (volatile unsigned int x = 0 ; x < 350 ; x++) //1ms at 8MHz
    {
      __asm__("nop\n\t");
    }
  }
}
