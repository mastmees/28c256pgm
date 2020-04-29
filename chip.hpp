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
#ifndef __chip_hpp__
#define __chip_hpp__

#include <avr/io.h>
#include <util/delay.h>
#include <avr/wdt.h>

#define ce_high() PORTD|=0x20
#define ce_low() PORTD&=(~0x20)
#define oe_high() PORTD|=0x40
#define oe_low() PORTD&=(~0x40)
#define we_high() PORTD|=0x10
#define we_low() PORTD&=(~0x10)

extern volatile uint16_t busytimer;

class Chip
{
public:

  Chip()
  {
    we_high();
    oe_high();
    ce_high();
    setadr(0);
  }
  
  inline uint16_t setadr(uint16_t adr)
  {
    PORTA=adr&0xff;
    PORTB=adr>>8;
    return adr;
  }

  inline uint16_t getadr(void)
  {
    return (((uint16_t)PORTB)<<8)|PORTA; 
  }
  
  inline void nextadr(void)
  {
    uint8_t b=PORTA+1;
    PORTA=b;
    if (!b)
    {
      PORTB=PORTB+1;
    }
  }

  uint8_t read(void)
  {
    uint8_t b;
    ce_low();
    _delay_us(1);
    oe_low();
    _delay_us(1);
    b=PINC;
    oe_high();
    _delay_us(1);
    ce_high();
    _delay_us(1);
    return b;
  }
  
  void rawwrite(uint8_t b)
  {
    PORTC=b;
    DDRC=0xff;
    ce_low();
    _delay_us(1);
    we_low();
    _delay_us(1);
    we_high();
    _delay_us(1);
    ce_high();
    _delay_us(1);
    DDRC=0x00;
    PORTC=0xff;
  }

  uint8_t is_writing(void)
  {
    uint8_t b;
    ce_low();
    oe_low();
    _delay_us(1);
    b=PINC;
    oe_high();
    ce_high();
    _delay_us(1);
    ce_low();
    oe_low();
    _delay_us(1);
    b^=PINC;
    oe_high();
    ce_high();
    return b&0x40;
  }
    
  uint8_t write(uint8_t b)
  {
    uint16_t tries;
    if (read()==b)
      return 1;
    rawwrite(b);
    wdt_reset();
    WDTCSR|=0x40;
    _delay_us(200);
    tries=4000; // 3us per try = 12ms
    while (tries--)
    {
      if (!is_writing())
      {
        return read()==b;
      }
    }
    return 0;
  }
  
  uint8_t pagewrite(uint8_t *b,uint8_t c,uint16_t *erradr)
  {
    uint8_t *s=b,i=c,w=0;
    uint16_t tries;
    tries=getadr();
    // first check if any writes are actually needed
    while (i--)
    {
      if (read()!=*s++)
        w++;
      nextadr();
    }    
    if (!w)
      return 1;
    // need, to write something, so rewing and write
    setadr(tries);
    i=c;
    s=b;
    while (i--)
    {
      rawwrite(*s++);
      nextadr();
    }
    wdt_reset();
    WDTCSR|=0x40;
    _delay_us(200);  // wait for write cycle to start
    setadr(tries);
    tries=4000;      // 3us per try = 12ms
    while (tries--)
    {
      if (!is_writing())
      {
        while (c--)
        {
          if (read()!=*b)
          {
            if (erradr)
              *erradr=getadr();
            return 0;
          }
          b++;
          nextadr();
        }        
        return 1;
      }
    }
    return 0;
    
  }
    
};

#endif
