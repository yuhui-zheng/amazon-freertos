/*
 * Amazon FreeRTOS V1.x.x
 * Copyright (C) 2019 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * http://aws.amazon.com/freertos
 * http://www.FreeRTOS.org
 */

/**
 * @file iot_perfcounter.c
 * @brief File of the implementation for performance counter APIs calling STM drivers.
 */

#include <stdint.h>

/* API definition to be implemented in this file. */
#include "FreeRTOS.h"
#include "iot_perfcounter.h"

/* Marvell APIs to be called in this file. */
#include "mw300.h"
#include "mw300_gpt.h"
#include "mw300_clock.h"
#include "mw300_driver.h"


/**
 * @brief Default performance counter frequency.
 *
 * Default is set to 10MHz ==> (uint32_t) 0xffffffff / 10MHz = 429.4967s.
 * So roughly every 7 mins, kernel needs to serve an overflow IRQ.
 */
#define PERF_COUNTER_FREQ_DEFAULT    ( 10000000 )

/**
 * @brief Maximum performance counter frequency.
 *
 * Maximum frequency for APB1 is 80MHz. Kernel needs to serve and overflow
 * IRQ in each ~53.6870s.
 */
#define PERF_COUNTER_FREQ_MAX        ( 80000000 )

/**
 * User suggested performance counter frequency.
 *
 * @warning The value of configHAL_PERF_COUNTER_FREQ needs to be smaller than
 * TIM clock.
 *
 * @warning TIM prescaler only takes integer. configHAL_PERF_COUNTER_FREQ can
 * be any value, but prescaler is (uint32_t)(PCLK1 frequency / configHAL_PERF_COUNTER_FREQ).
 */
#ifdef configHAL_PERF_COUNTER_FREQ
    #define HAL_PERF_COUNTER_FREQ    ( configHAL_PERF_COUNTER_FREQ )
#else
    #define HAL_PERF_COUNTER_FREQ    ( PERF_COUNTER_FREQ_DEFAULT )
#endif

/**
 * @brief Timer width, counter period, and loading value.
 */
#define HW_TIMER_32_WIDTH            ( sizeof( uint32_t ) * 8 )
#define HW_TIMER_32_CONST_PERIOD     ( UINT32_MAX )
#define HW_TIMER_32_LOADING_VALUE    ( 0x0UL )

/* MW320 has 2 GPT. MW322 has 4 GPT. Use GPT0 for simplicity. */
#define GPT_COUNTER_ID               GPT0_ID
#define GPT_CLOCK_ID                 CLK_GPT0

/*-------------------- Static Variables ---------------------*/
static uint32_t ulTimerOverflow = 0;

/*----------------------- IRQ handler -----------------------*/

/**
 * Override weakly defined IRQ handler with our own logic.
 *
 * Assume GPT0 is used. If GPT_COUNTER_ID is not GPT0_ID,
 * please override the specific IRQ handler you are using.
 */
void GPT0_IRQHandler_Overflow( void )
{
    ++ulTimerOverflow;
}

/* Override weak pragma. */
#define GPT0_IRQHandler    GPT0_IRQHandler_Overflow

/*-----------------------------------------------------------*/

void iot_perfcounter_open( void )
{
    /* Configure input clock for GPT. */
    CLK_ModuleClkDivider( GPT_CLOCK_ID, 50 );

    /* Enable component clock. */
    CLK_ModuleClkEnable( GPT_CLOCK_ID );

    /* Initialize GPT. */
    GPT_Config_Type xGptConfig = { 0 };

    xGptConfig.clockSrc = GPT_CLOCK_0;                /* Clock source default from PMU. */
    xGptConfig.cntUpdate = GPT_CNT_VAL_UPDATE_NORMAL; /* Only every 3-4 counter ticks are updated to CNT_VAL. */
    xGptConfig.clockDivider = 0;
    xGptConfig.clockPrescaler = CLK_GetSystemClk() / HAL_PERF_COUNTER_FREQ - 1;
    xGptConfig.uppVal = HW_TIMER_32_CONST_PERIOD; /* Upper bound of the counter.
                                                   * To minimize the need for serving interrupt,
                                                   * this is always set to maximum possible value. */

    GPT_Init( GPT_COUNTER_ID, &xGptConfig );

    /* Configure interrupt type.
     * This routine assumes the GPT is dedicated to a single purpose, and
     * disables all interrupts other than CNT overflow interrupt.
     * All other types of interrupt disabled --
     * channel status, channel error status, and DMA overflow. */
    GPT_IntMask( GPT_COUNTER_ID, GPT_INT_ALL_MSK, MASK );
    GPT_IntMask( GPT_COUNTER_ID, GPT_INT_CNT_UPP, UNMASK );

    /* Start timer. */
    GPT_Start( GPT_COUNTER_ID );
}

/*-----------------------------------------------------------*/

void iot_perfcounter_close( void )
{
    /* Stop counter. */
    GPT_Stop( GPT_COUNTER_ID );

    /* Disable component clock. */
    CLK_ModuleClkDisable( GPT_CLOCK_ID );
}

/*-----------------------------------------------------------*/

uint64_t iot_perfcounter_get_value( void )
{
    UBaseType_t uxCriticalSectionType = portSET_INTERRUPT_MASK_FROM_ISR();

    uint64_t ullCounterValue = ( ( ( uint64_t ) ulTimerOverflow << HW_TIMER_32_WIDTH ) | GPT_GetCounterVal( GPT_COUNTER_ID ) );

    portCLEAR_INTERRUPT_MASK_FROM_ISR( uxCriticalSectionType );

    return ullCounterValue;
}

/*-----------------------------------------------------------*/

uint32_t iot_perfcounter_get_frequency_hz( void )
{
    uint32_t ulSystemFreqHz = CLK_GetSystemClk();

    return ulSystemFreqHz;
}

/*-----------------------------------------------------------*/

void iot_perfcounter_zero_out( void )
{
}

/*-----------------------------------------------------------*/
