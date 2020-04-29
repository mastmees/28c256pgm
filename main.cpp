/*
MIT License

Copyright (c) 2020 Madis Kaal

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <string.h>
#include <util/delay.h>
#include <ctype.h>
#include "uart.hpp"
#include "chip.hpp"

#define cts_on() PORTD&=(~0x80)
#define cts_off()  PORTD|=0x80
#define cts_is_on() !(PORTD&0x80)
#define cts_is_off()  PORTD&0x80

UART uart;
Chip chip;
volatile uint16_t busytimer;

uint8_t buf[128];
uint8_t idx;

uint8_t blankcheck(void)
{
  chip.setadr(0);
  while (chip.getadr()<0x8000)
  {
    if (chip.read()!=0xff)
      return 0;
    chip.nextadr();
    wdt_reset();
    WDTCSR|=0x40;
  }
  return 1;
}

uint8_t chiperase(void)
{
uint8_t x;
  chip.setadr(0x5555);
  chip.rawwrite(0xaa);
  chip.setadr(0x2aaa);
  chip.rawwrite(0x55);
  chip.setadr(0x5555);
  chip.rawwrite(0x80);
  chip.rawwrite(0xaa);
  chip.setadr(0x2aaa);
  chip.rawwrite(0x55);
  chip.setadr(0x5555);
  chip.rawwrite(0x10);
  // now at least 20ms delay while the chip erases
  for (x=0;x<220;x++)
    _delay_us(100);
  if (blankcheck())
    return 1;
  // if not blank then do it the slow way
  chip.setadr(0);
  while (chip.getadr()<0x8000)
  {
    if (chip.read()!=0xff)
      chip.write(0xff);
    chip.nextadr();
    wdt_reset();
    WDTCSR|=0x40;
  }
  return blankcheck();
}

void chiplock(void)
{
  chip.setadr(0x5555);
  chip.rawwrite(0xaa);
  chip.setadr(0x2aaa);
  chip.rawwrite(0x55);
  chip.setadr(0x5555);
  chip.rawwrite(0xa0);
  // now at least 200us delay to start write cycle
  _delay_us(200);
  while (chip.is_writing())
    ;
}

void chipunlock(void)
{
  chip.setadr(0x5555);
  chip.rawwrite(0xaa);
  chip.setadr(0x2aaa);
  chip.rawwrite(0x55);
  chip.setadr(0x5555);
  chip.rawwrite(0x80);
  chip.setadr(0x5555);
  chip.rawwrite(0xaa);
  chip.setadr(0x2aaa);
  chip.rawwrite(0x55);
  chip.setadr(0x5555);
  chip.rawwrite(0x20);
  // now at least 200us delay to start write cycle
  _delay_us(200);
  while (chip.is_writing())
    ;
}

void hexread(uint16_t adr,uint16_t len)
{
uint16_t a,i;
uint8_t cs,b;
  chip.setadr(adr);
  while (len) {
    a=chip.getadr();
    cs=0x10;
    uart.prints(":10");
    uart.printx(a>>8);
    cs+=a>>8;
    uart.printx(a&0xff);
    cs+=a&255;
    uart.printx(0);
    for (i=0;i<16 && len;i++) {
      b=chip.read();
      cs+=b;
      uart.printx(b);
      chip.nextadr();
      len--;
    }
    uart.printx((~cs)+1);
    uart.prints("\r\n");
    wdt_reset();
    WDTCSR|=0x40;
  }
}

void chipread(void)
{
  uart.prints(":020000040000");
  uart.printx((~6)+1);
  uart.prints("\r\n");
  hexread(0,0x8000);
  uart.prints(":00000001FF\r\n");
}

uint8_t tobin(uint8_t c)
{
  if (c>='0' && c<='9')
    return c-'0';
  if ((c>='a' && c<='f') || (c>='A' && c<='F'))
  {
    return (c&(~0x20))-'0'-7;
  }
  return 0;
}

uint16_t get_u16(uint8_t *&s)
{
uint16_t r=0;
  while (*s)
  {
    if ((*s>='0' && *s<='9') || (*s>='a' && *s<='f') || (*s>='A' && *s<='F'))
      r=(r<<4)|tobin(*s++);
    else
      break;
  }
  return r;
}

uint8_t dehex(uint8_t *&s)
{
uint8_t b;
  if (!*s)
    return 0;
  b=tobin(*s)<<4;
  s++;
  if (!*s)
    return b;
  b=b|tobin(*s);
  s++;
  return b;
}

uint8_t validhex(void)
{
uint8_t *s=&buf[1];
uint8_t cs=0;
  while (*s) {
    cs+=dehex(s);
  }
  return cs==0;
}

void writehex(void)
{
uint8_t *s=&buf[1],*t=buf;
uint8_t nbytes,type;
uint16_t adr;
  if (validhex())
  {
    nbytes=dehex(s);
    adr=dehex(s);
    adr=(adr<<8);
    adr|=dehex(s);
    type=dehex(s);
    switch (type)
    {
      case 0: // data
        chip.setadr(adr);
        type=nbytes;
        while (type--)
          *t++=dehex(s);
        if (!chip.pagewrite(buf,nbytes,&adr))
        {
          uart.prints("write error at ");
          uart.printx(adr>>8);
          uart.printx(adr&0xff);
          uart.prints("\r\n");
        }
        break;
      case 4: // linear segment, assume its 0
        break;
      case 1: // end
        break;
      default:
        uart.prints("unsupported record type\r\n");
    }
  }
  else {
    uart.prints("bad checksum\r\n");
  }
}

uint8_t *token(void)
{
uint8_t *s=&buf[idx];
  while (buf[idx] && isspace(buf[idx]))
    idx++;
  s=&buf[idx];
  while (buf[idx] && (!isspace(buf[idx]))) {
    buf[idx]=tolower(buf[idx]);
    idx++;
  }
  if (buf[idx])
    buf[idx++]='\0';
  return s;
}

void execute(void)
{
uint8_t *s;
uint16_t adr,len;
  if (*buf==':')
  {
    writehex();
  }
  else {
    idx=0;
    s=token();
    if (*s) {
      if (!strcmp((const char*)s,"read"))
      {
        s=token();
        if (*s)
        {
          adr=get_u16(s);
          s=token();
          if (*s)
          {
            len=get_u16(s);
          }
          else
            len=0x8000-adr;
          if (adr<0x8000 && len>0 && len<=(0x8000-adr))
            hexread(adr,len);
          else
            uart.prints("Invalid parameter(s)\r\n");
        }
        else
          chipread();
      }
      else if (!strcmp((const char*)s,"help") || !strcmp((const char*)s,"?"))
      {
        uart.prints("Commands:\r\nblankcheck\r\nread [adr len]\r\nhelp\r\n"
              "erase\r\nlock\r\nunlock\r\nsend intel hex to write\r\n");
      }
      else if (!strcmp((const char*)s,"erase")) {
        if (!chiperase())
          uart.prints("Erase failed\r\n");
      }
      else if (!strcmp((const char*)s,"lock")) {
        chiplock();
      }
      else if (!strcmp((const char*)s,"unlock")) {
        chipunlock();
      }
      else if (!strcmp((const char*)s,"blankcheck")) {
        if (!blankcheck())
          uart.prints("Not blank\r\n");
      }
      else {
        uart.prints("?");
      }
    }
  }
  uart.prints("\r\n>");
}

void process(uint8_t c)
{
  if (c=='\n' || c=='\r')
  {
    uart.prints("\r\n");
    buf[idx]='\0';
    execute();
    idx=0;
    *buf='\0';
    return;
  }
  if (c==8)
  {
    if (idx) {
      idx--;
      uart.prints("\x8 \x8");
    }
    return;
  }
  if (idx<(sizeof(buf)-1)) {
    buf[idx++]=c;
    uart.send(c);
  }
}

ISR(USART0_RX_vect)
{
  uart.received(UDR0);
  if (uart.rxcount()>128 && cts_is_on())
    cts_off();
}

ISR(USART0_UDRE_vect)
{
  uart.transmit();
}

ISR(TIMER0_OVF_vect)
{
  TCNT0=0x0;
  if (uart.rxcount()<64 && cts_is_off())
    cts_on();
  if (busytimer)
  {
    busytimer--;
  }
}

ISR(WDT_vect)
{
}

/*
I/O configuration
-----------------
I/O pin                               direction    DDR  PORT

PA0 A0                                output       1    0
PA1 A1                                output       1    0
PA2 A2                                output       1    0
PA3 A3                                output       1    0
PA4 A4                                output       1    0
PA5 A5                                output       1    0
PA6 A6                                output       1    0
PA7 A7                                output       1    0

PB0 A8                                output       1    0
PB1 A9                                output       1    0
PB2 A10                               output       1    0
PB3 A11                               output       1    0
PB4 A12                               output       1    0
PB5 A13                               output       1    0
PB6 A14                               output       1    0
PB7 unused                            output       1    0

PC0 D0                                input        0    1 
PC1 D1                                input        0    1
PC2 D2                                input        0    1
PC3 D3                                input        0    1
PC4 D4                                input        0    1
PC5 D5                                input        0    1
PC6 D6                                input        0    1
PC7 D7                                input        0    1

PD0 RxD                               input        0    1
PD1 TxD                               output       1    1
PD2 unused                            output       1    0
PD3 unused                            output       1    0
PD4 /WE                               output       1    1
PD5 /CE                               output       1    1
PD6 /OE                               output       1    1
PD7 /CTS                              output       1    1
*/

int main(void)
{
  MCUSR=0;
  MCUCR=0;
  // initial state
  PORTA=0x00;
  PORTB=0x00;
  PORTC=0xff;
  PORTD=0xf3;
  // I/O directions
  DDRA=0xff;
  DDRB=0xff;
  DDRC=0x00;
  DDRD=0xfe;
  //
  set_sleep_mode(SLEEP_MODE_IDLE);
  sleep_enable();
  // configure watchdog to interrupt&reset, 4 sec timeout
  WDTCSR|=0x18;
  WDTCSR=0xe8;
  // configure timer0 for periodic interrupts
  TCCR0B=4; // timer0 clock prescaler to 256
  TCNT0=0;
  TIMSK0=1; // enable overflow interrupts
  //
  uart.initialize(38400);
  cts_on();
  sei();
  // find zero position on both axis
  while (1) {
    sleep_cpu();   // watchdog or I/O interrupt wakes us up
    wdt_reset();
    WDTCSR|=0x40;
    if (uart.ready())
      process(uart.read());
  }
}

