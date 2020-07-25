/* Name: main.c
 * Project: EasyLogger
 * Author: Christian Starkjohann
 * Creation Date: 2006-04-23
 * Tabsize: 4
 * Copyright: (c) 2006 by OBJECTIVE DEVELOPMENT Software GmbH
 * License: Proprietary, free under certain conditions. See Documentation.
 * This Revision: $Id$
 */

#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/eeprom.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <string.h>
#include <util/delay.h>

#include "usbdrv.h"
#include "oddebug.h"
#include "osccal.h"

/*
Pin assignment:
PB0 = Test-clock output

PB3, PB4 = USB data lines, see usbconfig.h
*/

#define BIT_OUT PB0

#define UTIL_BIN4(x)        (uchar)((0##x & 01000)/64 + (0##x & 0100)/16 + (0##x & 010)/4 + (0##x & 1))
#define UTIL_BIN8(hi, lo)   (uchar)(UTIL_BIN4(hi) * 16 + UTIL_BIN4(lo))

#ifndef NULL
#define NULL    ((void *)0)
#endif

/* ------------------------------------------------------------------------- */

static uchar    reportBuffer[2];    /* buffer for HID reports */
static uchar    idleRate;           /* in 4 ms units */

#define HIDSERIAL_INBUFFER_SIZE 32
//Serial
static uchar received = 0;
static uchar outBuffer[8];
static uchar inBuffer[HIDSERIAL_INBUFFER_SIZE];
static uchar reportId = 0;
static uchar bytesRemaining;
static uchar* pos;
static uint16_t    fcpu_usb = 0;
static uint16_t    fcpu_target  = 0;



/* ------------------------------------------------------------------------- */


PROGMEM const char usbHidReportDescriptor[USB_CFG_HID_REPORT_DESCRIPTOR_LENGTH] = {    /* USB report descriptor */
    0x06, 0x00, 0xff,              // USAGE_PAGE (Generic Desktop)
    0x09, 0x01,                    // USAGE (Vendor Usage 1)
    0xa1, 0x01,                    // COLLECTION (Application)
    0x15, 0x00,                    //   LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x00,              //   LOGICAL_MAXIMUM (255)
    0x75, 0x08,                    //   REPORT_SIZE (8)
    0x95, 0x08,                    //   REPORT_COUNT (8)

    0x09, 0x00,                    //   USAGE (Undefined)  
    0x82, 0x02, 0x01,              //   INPUT (Data,Var,Abs,Buf)
    0x95, HIDSERIAL_INBUFFER_SIZE, //   REPORT_COUNT (32)

    0x09, 0x00,                    //   USAGE (Undefined)        
    0xb2, 0x02, 0x01,              //   FEATURE (Data,Var,Abs,Buf)
    0xc0                           // END_COLLECTION
};


/* ------------------------------------------------------------------------- */

static void timerInit(void)
{
    TCCR0A  = 1 << COM0A0; //Toggle OC0A on compare-match
    
    //Normal mode now, 
    // CTC mode (like the two lines below) 
    // somehow threw of vusb into not synchronizing
    // Even though no interrupts would be generated
    // TCCR0A |= 1 << WGM01; //CTC mode so that OCR0A controls match/
    // TIMSK = 1 << OCIE0A; //Enable timer counter compare match

    TCCR0B = 2 << CS00; //Scaling of clk_IO/8
    // This with a TOP of 0xFF in normal mode means 
    // the clock output on OC0A is clk/256/8

    TIMSK &= ~(1<<TOIE0); //Clear the overflow interrupt enable
    DDRB |= 1 << DDB0;
    
}

ISR(TIMER0_OVF_vect, ISR_NAKED)
{
    sei();
    reti();
}



/* ------------------------------------------------------------------------- */
/* ------------------------ interface to USB driver ------------------------ */
/* ------------------------------------------------------------------------- */

uchar	usbFunctionSetup(uchar data[8])
{
usbRequest_t    *rq = (void *)data;

    usbMsgPtr = reportBuffer;
    if((rq->bmRequestType & USBRQ_TYPE_MASK) == USBRQ_TYPE_CLASS){    /* class request type */
        if(rq->bRequest == USBRQ_HID_GET_REPORT){  
            /* wValue: ReportType (highbyte), ReportID (lowbyte) */
            /* we only have one report type, so don't look at wValue */  
            return USB_NO_MSG; /* Use usbFunctionRead() to send data to host */
        }else if(rq->bRequest == USBRQ_HID_GET_IDLE){
            usbMsgPtr = &idleRate;
            return 1;
        }else if(rq->bRequest == USBRQ_HID_SET_IDLE){
            idleRate = rq->wValue.bytes[1];
        }else if(rq->bRequest == USBRQ_HID_SET_REPORT){
            /* since we have only one report type, we can ignore the report-ID */
            pos = inBuffer;
            bytesRemaining = rq->wLength.word;
            if(bytesRemaining > sizeof(inBuffer))
                bytesRemaining = sizeof(inBuffer);
            return USB_NO_MSG;  /* use usbFunctionWrite() to receive data from host */
        }
    }else{
        /* no vendor specific requests implemented */
    }
	return 0;
}


void    usbEventResetReady(void)
{
    /* Disable interrupts during oscillator calibration since
     * usbMeasureFrameLength() counts CPU cycles.
     */
    cli();
    uint8_t osccal_target;
    uint8_t osccal_usb;
    fcpu_target = calibrateOscillator(F_CPU_TARGET, &osccal_target); //Do this first since OSCCAL is set by calibrate
    fcpu_usb = calibrateOscillator(F_CPU_USB, &osccal_usb);
    sei();
    
    /* store only the calibrated real target value in EEPROM */
    eeprom_write_byte(0, osccal_target);   
    
}

/* usbFunctionRead() is called when the host requests a chunk of data from
 * the device. For more information see the documentation in usbdrv/usbdrv.h.
 */
uchar   usbFunctionRead(uchar *data, uchar len)
{
    data[0] = fcpu_usb & 0xFF;
    data[1] = (fcpu_usb & 0xFF00) >> 8;
    data[2] = fcpu_target & 0xFF;
    data[3] = (fcpu_target & 0xFF00) >> 8;
    data[4] = OSCCAL;
    return 5;
}

/* usbFunctionWrite() is called when the host sends a chunk of data to the
 * device. For more information see the documentation in usbdrv/usbdrv.h.
 */
uchar   usbFunctionWrite(uchar *data, uchar len)
{
    if (reportId == 0) {
        int i;
        if(len > bytesRemaining)
            len = bytesRemaining;
        bytesRemaining -= len;
        //int start = (pos==inBuffer)?1:0;
        for(i=0;i<len;i++) {
            if (data[i]!=0) {
                *pos++ = data[i];
             }
        }
        if (bytesRemaining == 0) {
            received = 1;
            *pos++ = 0;
            return 1;
        } else {
            return 0;
        }
    } else {
        return 1;
    }
}

size_t serial_read(uchar *buffer)
{
    if(received == 0) return 0;
    int i;
    for(i=0; inBuffer[i] != 0 && i < HIDSERIAL_INBUFFER_SIZE; i++)
    {
        buffer[i] = inBuffer[i];
    }
    inBuffer[0] = 0;
    buffer[i] = 0;
    received = 0;
    return i;
}

// write one character
size_t serial_write(uint8_t data)
{
  memset((void*)outBuffer, 0, 8);
  outBuffer[0] = data;
  usbSetInterrupt(outBuffer, 8);
  return 1;
}

/* ------------------------------------------------------------------------- */
/* --------------------------------- main ---------------------------------- */
/* ------------------------------------------------------------------------- */

int main(void)
{
uchar   i;
uchar   calibrationValue;
    
    calibrationValue = eeprom_read_byte(0); /* calibration value from last time */
    if(calibrationValue != 0xff){
        OSCCAL = calibrationValue;
    }
    odDebugInit();
    usbDeviceDisconnect();
    for(i=0;i<20;i++){  /* 300 ms disconnect */
        _delay_ms(15);
    }
    usbDeviceConnect();
    
    
    wdt_enable(WDTO_1S);
    
    usbInit();
    timerInit();
    sei();

    for(;;){    /* main event loop */
        wdt_reset();
        usbPoll();
        if(usbInterruptIsReady()){ /* we can send another key */
            static uchar buf[8];
            if (serial_read((uchar*)&buf) !=0 ){
                serial_write(buf[0]);
                }
        }
    }
    return 0;
}

