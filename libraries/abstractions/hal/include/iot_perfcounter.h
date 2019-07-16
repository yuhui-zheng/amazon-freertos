/*
 * Amazon FreeRTOS performance counter
 * Copyright (C) 2019 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * http://aws.amazon.com/freertos
 * http://www.FreeRTOS.org
 */

/**
 * @file iot_perfcounter.h
 * @brief This file contains all the Performance counters HAL API definitions
 *
 * Performance counter uses a hardware peripheral timer to track time elapsed since
 * the start of the counter. The interface implementation is MCU specific, and counter
 * resolution is configurable via FreeRTOSConfig.h.
 *
 * To use the interfaces, application code needs to:
 * - Initialize counter by calling iot_perfcounter_open().
 * - Get counter value for as many times as you want in whatever thread context
 *   by calling iot_perfcounter_get_value().
 * - Time elapsed can be derived by ( counter value / counter frequency ),
 *   where counter frequency can be acquired by calling iot_perfcounter_get_frequency_hz().
 * - Once completely done with performance measurement,
 *   free resources by calling iot_perfcounter_close().
 *
 * @warning It's not recommended to repurpose the hardware timer, nor have timer vector interrupt
 *          priority lower than other peripheral vector interrupt priorities. Please refer to implementation
 *          specific details.
 */

#ifndef _IOT_PERFCOUNTER_H_
#define _IOT_PERFCOUNTER_H_

/**
 * @brief Initialize a hardware timer which is to be used as performance counter.
 */
void iot_perfcounter_open( void );

/**
 * @brief Deinitialize the hardware timer.
 */
void iot_perfcounter_close( void );

/**
 * @brief Get current count from the performance counter.
 *
 * @return Total count since counter started.
 */
uint64_t iot_perfcounter_get_value( void );

/**
 * @brief Get configured frequency of the performance counter.
 *
 * @return Frequency which the counter is configured to run at.
 */
uint32_t iot_perfcounter_get_frequency_hz( void );

#endif /* _IOT_PERFCOUNTER_H_ */
