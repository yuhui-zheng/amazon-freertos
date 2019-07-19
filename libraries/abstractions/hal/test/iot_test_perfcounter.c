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

/*******************************************************************************
 * @file aws_test_hal_perf_cntr.c
 * @brief Functional Unit Test - Performance Counter
 *******************************************************************************
 */

/* Test includes */
#include "unity.h"
#include "unity_fixture.h"

/* FreeRTOS include. */
#include "FreeRTOS.h"
#include "task.h"

/* Driver includes */
#include "iot_perfcounter.h"

/*-----------------------------------------------------------*/

#define aws_halperfcountertestINT_MAX_VALUE            ( 0xFFFFFFFFUL )

#define aws_halperfcountertestDEFAULT_DELAY_TIME_MS    ( 1 )
#define aws_halperfcountertestSEC_TO_MSEC              ( 1000 )

/*-----------------------------------------------------------*/

/* Define Test Group. */
TEST_GROUP( AWS_HAL_PERFCOUNTER_TEST );

/*-----------------------------------------------------------*/

/**
 * @brief Setup function called before each test in this group is executed.
 */
TEST_SETUP( AWS_HAL_PERFCOUNTER_TEST )
{
}

/*-----------------------------------------------------------*/

/**
 * @brief Tear down function called after each test in this group is executed.
 */
TEST_TEAR_DOWN( AWS_HAL_PERFCOUNTER_TEST )
{
}

/*-----------------------------------------------------------*/

/**
 * @brief Function to define which tests to execute as part of this group.
 */
TEST_GROUP_RUNNER( AWS_HAL_PERFCOUNTER_TEST )
{
    RUN_TEST_CASE( AWS_HAL_PERFCOUNTER_TEST, AFQP_AwsHalPerfCounterGetValue );
    RUN_TEST_CASE( AWS_HAL_PERFCOUNTER_TEST, AFQP_AwsHalPerfCounterGetValueWithDelay );

    /* Manually poke this case. */
    /*RUN_TEST_CASE( AWS_HAL_PERFCOUNTER_TEST, manual_test_check_register ); */
}

/*-----------------------------------------------------------*/

/**
 * @brief Test Function to test iot_perfcounter values
 *
 */
TEST( AWS_HAL_PERFCOUNTER_TEST, AFQP_AwsHalPerfCounterGetValue )
{
    uint64_t ullCounter1 = 0, ullCounter2 = 0;

    /* Open the interface. */
    iot_perfcounter_open();

    /* Get the value from perf counter. */
    ullCounter1 = iot_perfcounter_get_value();

    /* This is always true. Attempting to space out two reads. */
    TEST_ASSERT( ullCounter1 >= 0 );

    /* Get the value from perf counter again. */
    ullCounter2 = iot_perfcounter_get_value();

    /* For reference, to overflow 64-bit with clock running at say GHz, it takes years.
     * By when it's probably better to fail the test if it takes that long.
     * Two reads can be equal, depending on what frequency the counter is running at.
     */
    TEST_ASSERT_MESSAGE( ullCounter2 >= ullCounter1, "The value from the second read is expected to be no smaller than the first." );

    /* Close the interface. */
    iot_perfcounter_close();
}

/*-----------------------------------------------------------*/

/**
 * @brief Test Function to test iot_perfcounter values with delay inserted.
 * The delay is used to calculate the next read of perfcounter shall be no smaller than
 * the delay inserted.
 *
 */
TEST( AWS_HAL_PERFCOUNTER_TEST, AFQP_AwsHalPerfCounterGetValueWithDelay )
{
    uint64_t ullCounter1 = 0, ullCounter2 = 0;
    uint32_t ulFreq = 0;

    /* Open the interface. */
    iot_perfcounter_open();

    /* Get counter frequency. */
    ulFreq = iot_perfcounter_get_frequency_hz();

    /* Get the value from perf counter. */
    ullCounter1 = iot_perfcounter_get_value();

    /* Delay for AT MOST 1 msec. (Assume no interrupt.) */
    vTaskDelay( aws_halperfcountertestDEFAULT_DELAY_TIME_MS / portTICK_PERIOD_MS );

    /* Get the value from perf counter again. */
    ullCounter2 = iot_perfcounter_get_value();

    /* Test has been running for a while now. Reading should not be zero.
     * If fails --
     * 1. Timer might not have been started correctly.
     * 2. Timer frequency might not be realistic. */
    TEST_ASSERT_MESSAGE( ( ullCounter1 > 0 && ullCounter2 > 0 ), "Perf counter value did not increase." );

    /* Frequency value should never be zero in any counter configuration. */
    TEST_ASSERT_MESSAGE( ( ulFreq > 0 ), "Counter frequency is expected to be not zero." );

    /* Convert time elapsed to counter cycles. The result can be zero, if counter is running at unrealistic frequency. */
    /*ullCounterThreshold = ( uint64_t ) aws_halperfcountertestDEFAULT_DELAY_TIME_MS * ulFreq / aws_halperfcountertestSEC_TO_MSEC; */

    /* We can never have a golden assertion here, since --
     * 1, if no interrupt during vTaskDelay(), this is always true: ullCounter2 <= ullCounter1 + ullCounterThreshold
     * 2, if interrupt during vTaskDelay(), this is true: ullCounter2 >= ullCounter1 + ullCounterThreshold
     * The only thing we know for sure is ullCounter2 > ullCounter1. And for most of the time
     * ullCounter2 ~= ullCounter1 + ullCounterThreshold.
     *
     * Note that vTaskDelay() delays AT MOST time specified. This is to meet scheduling deadline.
     *
     * Overflow 64-bit is not taken into consideration for similar reason.
     * See comment in previous test.
     */
    TEST_ASSERT_MESSAGE( ( ullCounter2 > ullCounter1 ), "Expected the value from the second read to be larger than the first. " );

    /* Close the interface. */
    iot_perfcounter_close();
}

/*-----------------------------------------------------------*/

/**
 * @brief manual test to help poking register values.
 *
 */
TEST( AWS_HAL_PERFCOUNTER_TEST, manual_test_check_register )
{
    uint64_t ullCounter1 = 0, ullCounter2 = 0;
    uint64_t ullCounterDiff = 0;
    uint32_t ulFreq = 0;
    const TickType_t xDelay = 1000 / portTICK_PERIOD_MS; /* 1 second */

    /* Open the interface. */
    iot_perfcounter_open();

    /* Get perf counter frequency. */
    ulFreq = iot_perfcounter_get_frequency_hz();
    configPRINTF( ( "frequency: %lu\r\n", ulFreq ) );

    /* put it in a loop, that we can manually try things a bit. */
    for( uint8_t i = 0; i < 20; i++ )
    {
        /* Get the value from perf counter. */
        ullCounter1 = iot_perfcounter_get_value();

        /* Sleep. Note that FreeRTOS sleeps for AT MOST the time you specify. */
        vTaskDelay( xDelay );

        /* Get the value from perf counter again. */
        ullCounter2 = iot_perfcounter_get_value();

        /* Check sleep function is "at most" or "at least".
         * assume no overflow. */
        ullCounterDiff = ullCounter2 - ullCounter1;
        configPRINTF( ( "start: %llu, end: %llu, diff: %llu\r\n", ullCounter1, ullCounter2, ullCounterDiff ) );
    }

    /* Close the interface. */
    iot_perfcounter_close();
}
