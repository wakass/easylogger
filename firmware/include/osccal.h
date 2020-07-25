#ifndef OSCCAL_H
#define OSCCAL_H

#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/eeprom.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <string.h>
#include <util/delay.h>

#include "usbdrv.h"
#include "oddebug.h"

#define F_CPU_TARGET F_CPU //16777216
#define OLDSTYLECALIBRATION

/* ------------------------------------------------------------------------- */
/* ------------------------ Oscillator Calibration ------------------------- */
/* ------------------------------------------------------------------------- */

/* Calibrate the RC oscillator to 8.25 MHz. The attiny85 PLL system clock 
 * (with our target frequency F_CPU) of 16.5 MHz is
 * derived from the 64 MHz PLL peripheral clock by scaling. Our timing reference
 * is the Start Of Frame signal (a single SE0 bit) available immediately after
 * a USB RESET. We first do a binary search for the OSCCAL value and then
 * optimize this value with a neighboorhod search.
 */

/* Our target F_CPU is a little different for our gameboy experiments.
 * The DMG  runs at 2^22 = 4194304, which leads to our target attiny-freq of 2^23=8388608
 * This is only a 1.8% deviation to 8.25MHz to start with!
 * Our USB derived target is thus 16.7~etc 
 */


#ifdef OLDSTYLECALIBRATION
static uint16_t calibrateOscillator(void)
{
uchar       step = 128;
uchar       trialValue = 0, optimumValue;
int         x, optimumDev, targetValue = (unsigned)(1499 * (double)F_CPU_TARGET / 10.5e6 + 0.5);

    /* do a binary search: */
    do{
        OSCCAL = trialValue + step;
        x = usbMeasureFrameLength();    /* proportional to current real frequency */
        if(x < targetValue)             /* frequency still too low */
            trialValue += step;
        step >>= 1;
    }while(step > 0);
    /* We have a precision of +/- 1 for optimum OSCCAL here */
    /* now do a neighborhood search for optimum value */
    optimumValue = trialValue;
    optimumDev = x; /* this is certainly far away from optimum */
    for(OSCCAL = trialValue - 1; OSCCAL <= trialValue + 1; OSCCAL++){
        x = usbMeasureFrameLength() - targetValue;
        if(x < 0)
            x = -x;
        if(x < optimumDev){
            optimumDev = x;
            optimumValue = OSCCAL;
        }
    }
    OSCCAL = optimumValue;

    return optimumDev+targetValue; // Return the measured framelength
}
#else


//Taken from https://codeandlife.com/2012/02/22/v-usb-with-attiny45-attiny85-without-a-crystal/
//Calibrates for attiny's overlapping OSCCAL regions, might give better results, might not :P
#define abs(x) ((x) > 0 ? (x) : (-x))

// Called by V-USB after device reset
static void calibrateOscillator() {
    int frameLength, targetLength = (unsigned)(1499 * (double)F_CPU_TARGET / 10.5e6 + 0.5);
    int bestDeviation = 9999;
    uchar trialCal, bestCal, step, region;

    // do a binary search in regions 0-127 and 128-255 to get optimum OSCCAL
    for(region = 0; region <= 1; region++) {
        frameLength = 0;
        trialCal = (region == 0) ? 0 : 128;
        
        for(step = 64; step > 0; step >>= 1) { 
            if(frameLength < targetLength) // true for initial iteration
                trialCal += step; // frequency too low
            else
                trialCal -= step; // frequency too high
                
            OSCCAL = trialCal;
            frameLength = usbMeasureFrameLength();
            
            if(abs(frameLength-targetLength) < bestDeviation) {
                bestCal = trialCal; // new optimum found
                bestDeviation = abs(frameLength -targetLength);
            }
        }
    }

    OSCCAL = bestCal;
}

#endif //OLDSTYLECALIBRATION
/*
Note: This calibration algorithm may try OSCCAL values of up to 192 even if
the optimum value is far below 192. It may therefore exceed the allowed clock
frequency of the CPU in low voltage designs!
You may replace this search algorithm with any other algorithm you like if
you have additional constraints such as a maximum CPU clock.
For version 5.x RC oscillators (those with a split range of 2x128 steps, e.g.
ATTiny25, ATTiny45, ATTiny85), it may be useful to search for the optimum in
both regions.
*/

#endif