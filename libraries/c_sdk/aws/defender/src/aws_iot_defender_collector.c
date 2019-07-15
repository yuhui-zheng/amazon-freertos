/*
 * Amazon FreeRTOS Defender V2.0.0
 * Copyright (C) 2018 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
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

/* Standard includes */
#include <stdio.h>

/* Defender internal include. */
#include "private/aws_iot_defender_internal.h"

#include "platform/iot_metrics.h"

#include "platform/iot_clock.h"

/* FreeRTOS specific. -- todo: this file should not take dependency on FreeRTOS.  */
#include "task.h"

/* Used in debugging. It will decode the report with cbor format and print to stdout. */
#define DEBUG_CBOR_PRINT                0

/* Long tags are not recommended to be used. */
#define  HEADER_TAG                     AwsIotDefenderInternal_SelectTag( "header", "hed" )
#define  REPORTID_TAG                   AwsIotDefenderInternal_SelectTag( "report_id", "rid" )
#define  VERSION_TAG                    AwsIotDefenderInternal_SelectTag( "version", "v" )
#define  VERSION_1_0                    "1.0" /* Used by defender service to indicate the schema change of report, e.g. adding new field. */
#define  VERSION_1_1                    "1.1"
#define  METRICS_TAG                    AwsIotDefenderInternal_SelectTag( "metrics", "met" )

/* Network metrics -- TCP connections. */
#define TCP_CONN_TAG                    AwsIotDefenderInternal_SelectTag( "tcp_connections", "tc" )
#define EST_CONN_TAG                    AwsIotDefenderInternal_SelectTag( "established_connections", "ec" )
#define TOTAL_TAG                       AwsIotDefenderInternal_SelectTag( "total", "t" )
#define CONN_TAG                        AwsIotDefenderInternal_SelectTag( "connections", "cs" )
#define REMOTE_ADDR_TAG                 AwsIotDefenderInternal_SelectTag( "remote_addr", "rad" )

/* Kernel metrics.
 * Prefixed with k (kernel), m (mcu), t (task), hp (hp), st (stack). */
#define KERNEL_METIRCS                  AwsIotDefenderInternal_SelectTag( "kernel_metrics", "km" )
#define KERNEL_MCU_UPTIME               AwsIotDefenderInternal_SelectTag( "mcu_uptime", "mupt" )
#define KERNEL_MCU_UTILIZATION          AwsIotDefenderInternal_SelectTag( "mcu_utilization", "mpct" )

#define KERNEL_HEAP_FREE_SIZE           AwsIotDefenderInternal_SelectTag( "heap_free_size", "hp" )
#define KERNEL_HEAP_LARGEST_FREE        AwsIotDefenderInternal_SelectTag( "heap_largest_free_block", "hplg" )
#define KERNEL_HEAP_SMALLEST_FREE       AwsIotDefenderInternal_SelectTag( "heap_smallest_free_block", "hpsm" )
#define KERNEL_HEAP_NUM_OF_FREE         AwsIotDefenderInternal_SelectTag( "heap_free_blocks", "hpno" )
#define KERNEL_HEAP_LOW_WATERMARK       AwsIotDefenderInternal_SelectTag( "heap_low_watermark", "hplo" )
#define KERNEL_HEAP_SUCCESSFUL_ALLOC    AwsIotDefenderInternal_SelectTag( "heap_succ_alloc", "hpal" )
#define KERNEL_HEAP_SUCCESSFUL_FREE     AwsIotDefenderInternal_SelectTag( "heap_succ_free", "hpfr" )

#define KERNEL_NUM_OF_TASKS             AwsIotDefenderInternal_SelectTag( "num_of_tasks", "tno" )
#define KERNEL_TASK_DETAILS             AwsIotDefenderInternal_SelectTag( "task_details", "tdet" )

#define KERNEL_TASK_ID                  AwsIotDefenderInternal_SelectTag( "task_id", "tid" )
#define KERNEL_TASK_NAME                AwsIotDefenderInternal_SelectTag( "task_name", "tn" )
#define KERNEL_TASK_STATUS              AwsIotDefenderInternal_SelectTag( "task_status", "ts" )
#define KERNEL_TASK_PRIORITY            AwsIotDefenderInternal_SelectTag( "task_priority", "tp" )
#define KERNEL_TASK_ABS_CYCLES          AwsIotDefenderInternal_SelectTag( "task_abs_cycles", "tcyc" )
#define KERNEL_TASK_PERCENTAGE          AwsIotDefenderInternal_SelectTag( "task_percentage", "tpct" )
#define KERNEL_STACK_HIGH_WATERMARK     AwsIotDefenderInternal_SelectTag( "stack_high_watermark", "sthi" )

/* Device type.
 * Prefixed with d (device). */
#define DEVICE_TYPE                     AwsIotDefenderInternal_SelectTag( "device_type", "dtp" )

/* Return value for _taskNameFilterIdle(). */
#define TASK_NAME_EQUAL                 ( 1 )
#define TASK_NAME_NOT_EQUAL             ( 0 )

/**
 * Structure to hold a metrics report.
 */
typedef struct _metricsReport
{
    IotSerializerEncoderObject_t object; /* Encoder object handle. */
    uint8_t * pDataBuffer;               /* Raw data buffer to be published with MQTT. */
    size_t size;                         /* Raw data size. */
} _metricsReport_t;


/*---------------------- Static Variables -------------------------*/

/* Initialize metrics report. */
static _metricsReport_t _report =
{
    .object      = IOT_SERIALIZER_ENCODER_CONTAINER_INITIALIZER_STREAM,
    .pDataBuffer = NULL,
    .size        = 0
};

/* Define a "snapshot" global array of metrics flag. */
static uint32_t _metricsFlagSnapshot[ DEFENDER_METRICS_GROUP_COUNT ];

/* Report id integer. */
static uint64_t _AwsIotDefenderReportId = 0;

/*---------------------- Helper Functions -------------------------*/

static void _assertSuccess( IotSerializerError_t error );

static void _assertSuccessOrBufferToSmall( IotSerializerError_t error );

static void _copyMetricsFlag( void );

static uint8_t _taskNameFilterIdle( const char * strA );

static void _serialize( void );

static void _serializeTcpConnections( void * param1,
                                      const IotListDouble_t * pTcpConnectionsMetricsList );

static void _serializeKernelRuntimeStats( void * param1 );

#if DEBUG_CBOR_PRINT == 1
    static void _printReport();
#endif

/*-----------------------------------------------------------*/

void _assertSuccess( IotSerializerError_t error )
{
    ( void ) error;
    AwsIotDefender_Assert( error == IOT_SERIALIZER_SUCCESS );
}

/*-----------------------------------------------------------*/

void _assertSuccessOrBufferToSmall( IotSerializerError_t error )
{
    ( void ) error;
    AwsIotDefender_Assert( error == IOT_SERIALIZER_SUCCESS || error == IOT_SERIALIZER_BUFFER_TOO_SMALL );
}

/*-----------------------------------------------------------*/

static uint8_t _taskNameFilterIdle( const char * strA )
{
    uint8_t index = 0;
    uint8_t retval = TASK_NAME_NOT_EQUAL;
    char idleName[] = "IDLE";

    /* The maximum length of task name string (NULL included) is defined in FreeRTOSConfig.h. */
    for( index = 0; index < configMAX_TASK_NAME_LEN; index++ )
    {
        if( ( strA[ index ] == '\0' ) && ( idleName[ index ] == '\0' ) )
        {
            retval = TASK_NAME_EQUAL;
            break;
        }
        else if( ( strA[ index ] == '\0' ) || ( idleName[ index ] == '\0' ) || ( strA[ index ] != idleName[ index ] ) )
        {
            break;
        }
    }

    return retval;
}

/*-----------------------------------------------------------*/

uint8_t * AwsIotDefenderInternal_GetReportBuffer( void )
{
    return _report.pDataBuffer;
}

/*-----------------------------------------------------------*/

size_t AwsIotDefenderInternal_GetReportBufferSize( void )
{
    /* Encoder might over-calculate the needed size. Therefore encoded size might be smaller than buffer size: _report.size. */
    return _report.pDataBuffer == NULL ? 0
           : _defenderEncoder.getEncodedSize( &_report.object, _report.pDataBuffer );
}

/*-----------------------------------------------------------*/

bool AwsIotDefenderInternal_CreateReport( void )
{
    /* Assert report buffer is not allocated. */
    AwsIotDefender_Assert( _report.pDataBuffer == NULL && _report.size == 0 );

    bool result = true;

    IotSerializerEncoderObject_t * pEncoderObject = &( _report.object );

    size_t dataSize = 0;
    uint8_t * pReportBuffer = NULL;

    /* Copy the metrics flag user specified. */
    _copyMetricsFlag();

    /* Report id, a monotonically increased integer. */
    _AwsIotDefenderReportId++;

    /* Dry-run serialization to calculate the required size. */
    _serialize();

    /* Get the calculated required size. */
    dataSize = _defenderEncoder.getExtraBufferSizeNeeded( pEncoderObject );

    /* Clean the encoder object handle. */
    _defenderEncoder.destroy( pEncoderObject );

    /* Allocate memory once. */
    pReportBuffer = AwsIotDefender_MallocReport( dataSize * sizeof( uint8_t ) );

    if( pReportBuffer != NULL )
    {
        _report.pDataBuffer = pReportBuffer;
        _report.size = dataSize;

        /* Actual serialization. */
        _serialize();

        /* Output the report to stdout if debugging mode is enabled. */
        #if DEBUG_CBOR_PRINT == 1
            _printReport();
        #endif
    }
    else
    {
        result = false;
    }

    return result;
}

/*-----------------------------------------------------------*/

void AwsIotDefenderInternal_DeleteReport( void )
{
    /* Destroy the encoder object. */
    _defenderEncoder.destroy( &( _report.object ) );

    /* Free the memory of data buffer. */
    AwsIotDefender_FreeReport( _report.pDataBuffer );

    /* Reset report members. */
    _report.pDataBuffer = NULL;
    _report.size = 0;
    _report.object = ( IotSerializerEncoderObject_t ) IOT_SERIALIZER_ENCODER_CONTAINER_INITIALIZER_STREAM;
}

/*
 * report:
 * {
 *  "header": {
 *      "report_id": 1530304554,
 *      "version": "1.0"
 *  },
 *  "metrics": {
 *      ...
 *  }
 * }
 */
static void _serialize( void )
{
    IotSerializerError_t serializerError = IOT_SERIALIZER_SUCCESS;

    IotSerializerEncoderObject_t * pEncoderObject = &( _report.object );

    IotSerializerEncoderObject_t reportMap = IOT_SERIALIZER_ENCODER_CONTAINER_INITIALIZER_MAP;
    IotSerializerEncoderObject_t headerMap = IOT_SERIALIZER_ENCODER_CONTAINER_INITIALIZER_MAP;
    IotSerializerEncoderObject_t metricsMap = IOT_SERIALIZER_ENCODER_CONTAINER_INITIALIZER_MAP;

    /* Define an assert function for serialization returned error. */
    void (* assertNoError)( IotSerializerError_t ) = _report.pDataBuffer == NULL ? _assertSuccessOrBufferToSmall
                                                     : _assertSuccess;

    uint8_t metricsGroupCount = 0;
    uint32_t i = 0;

    serializerError = _defenderEncoder.init( pEncoderObject, _report.pDataBuffer, _report.size );
    assertNoError( serializerError );

    /* Create the outermost map with 2 keys: "header", "metrics". */
    serializerError = _defenderEncoder.openContainer( pEncoderObject, &reportMap, 2 );
    assertNoError( serializerError );

    /* Create the "header" map with 2 keys: "report_id", "version". */
    serializerError = _defenderEncoder.openContainerWithKey( &reportMap,
                                                             HEADER_TAG,
                                                             &headerMap,
                                                             2 );
    assertNoError( serializerError );

    /* Append key-value pair of "report_Id" which uses clock time. */
    serializerError = _defenderEncoder.appendKeyValue( &headerMap,
                                                       REPORTID_TAG,
                                                       IotSerializer_ScalarSignedInt( ( int64_t ) _AwsIotDefenderReportId ) );
    assertNoError( serializerError );

    /* Append key-value pair of "version". */
    serializerError = _defenderEncoder.appendKeyValue( &headerMap,
                                                       VERSION_TAG,
                                                       IotSerializer_ScalarTextString( VERSION_1_1 ) );
    assertNoError( serializerError );

    /* Close the "header" map. */
    serializerError = _defenderEncoder.closeContainer( &reportMap, &headerMap );
    assertNoError( serializerError );

    /* Count how many metrics groups user specified. */
    for( i = 0; i < DEFENDER_METRICS_GROUP_COUNT; i++ )
    {
        metricsGroupCount += _metricsFlagSnapshot[ i ] > 0;
    }

    /* Create the "metrics" map with number of keys as the number of metrics groups. */
    serializerError = _defenderEncoder.openContainerWithKey( &reportMap,
                                                             METRICS_TAG,
                                                             &metricsMap,
                                                             metricsGroupCount );
    assertNoError( serializerError );

    for( i = 0; i < DEFENDER_METRICS_GROUP_COUNT; i++ )
    {
        /* Skip if this metrics group has 0 metrics flag. */
        if( _metricsFlagSnapshot[ i ] )
        {
            switch( i )
            {
                case AWS_IOT_DEFENDER_METRICS_TCP_CONNECTIONS:
                    IotMetrics_GetTcpConnections( ( void * ) &metricsMap, _serializeTcpConnections );
                    break;

                case AWS_IOT_DEFENDER_METRICS_TASK_RUNTIME_STAT:
                    /* todo: For proto only. Needs to separate FreeRTOS specific implementation from abstraction. */
                    _serializeKernelRuntimeStats( ( void * ) &metricsMap );
                    break;

                default:
                    /* The index of metricsFlagSnapshot must be one of the metrics group. */
                    AwsIotDefender_Assert( 0 );
            }
        }
    }

    /* Close the "metrics" map. */
    serializerError = _defenderEncoder.closeContainer( &reportMap, &metricsMap );
    assertNoError( serializerError );

    /* Close the "report" map. */
    serializerError = _defenderEncoder.closeContainer( pEncoderObject, &reportMap );
    assertNoError( serializerError );
}

/*-----------------------------------------------------------*/

static void _copyMetricsFlag( void )
{
    /* Copy the metrics flags to snapshot so that it is unlocked quicker. */
    IotMutex_Lock( &_AwsIotDefenderMetrics.mutex );

    /* Memory copy from the metricsFlag array to metricsFlagSnapshot array. */
    memcpy( _metricsFlagSnapshot, _AwsIotDefenderMetrics.metricsFlag, sizeof( _metricsFlagSnapshot ) );

    IotMutex_Unlock( &_AwsIotDefenderMetrics.mutex );
}

/*-----------------------------------------------------------*/

static void _serializeTcpConnections( void * param1,
                                      const IotListDouble_t * pTcpConnectionsMetricsList )
{
    IotSerializerEncoderObject_t * pMetricsObject = ( IotSerializerEncoderObject_t * ) param1;

    AwsIotDefender_Assert( pMetricsObject != NULL );

    IotSerializerError_t serializerError = IOT_SERIALIZER_SUCCESS;

    IotSerializerEncoderObject_t tcpConnectionMap = IOT_SERIALIZER_ENCODER_CONTAINER_INITIALIZER_MAP;
    IotSerializerEncoderObject_t establishedMap = IOT_SERIALIZER_ENCODER_CONTAINER_INITIALIZER_MAP;
    IotSerializerEncoderObject_t connectionsArray = IOT_SERIALIZER_ENCODER_CONTAINER_INITIALIZER_ARRAY;

    IotLink_t * pListIterator = NULL;
    IotMetricsTcpConnection_t * pMetricsTcpConnection = NULL;

    size_t total = IotListDouble_Count( pTcpConnectionsMetricsList );

    uint32_t tcpConnFlag = _metricsFlagSnapshot[ AWS_IOT_DEFENDER_METRICS_TCP_CONNECTIONS ];

    uint8_t hasEstablishedConnections = ( tcpConnFlag & AWS_IOT_DEFENDER_METRICS_TCP_CONNECTIONS_ESTABLISHED ) > 0;
    /* Whether "connections" should show up is not only determined by user input, but also if there is at least 1 connection. */
    uint8_t hasConnections = ( tcpConnFlag & AWS_IOT_DEFENDER_METRICS_TCP_CONNECTIONS_ESTABLISHED_CONNECTIONS ) > 0 &&
                             ( total > 0 );
    uint8_t hasTotal = ( tcpConnFlag & AWS_IOT_DEFENDER_METRICS_TCP_CONNECTIONS_ESTABLISHED_TOTAL ) > 0;
    uint8_t hasRemoteAddr = ( tcpConnFlag & AWS_IOT_DEFENDER_METRICS_TCP_CONNECTIONS_ESTABLISHED_REMOTE_ADDR ) > 0;

    void (* assertNoError)( IotSerializerError_t ) = _report.pDataBuffer == NULL ? _assertSuccessOrBufferToSmall
                                                     : _assertSuccess;

    /* Create the "tcp_connections" map with 1 key "established_connections" */
    serializerError = _defenderEncoder.openContainerWithKey( pMetricsObject,
                                                             TCP_CONN_TAG,
                                                             &tcpConnectionMap,
                                                             1 );
    assertNoError( serializerError );

    /* if user specify any metrics under "established_connections" */
    if( hasEstablishedConnections )
    {
        /* Create the "established_connections" map with "total" and/or "connections". */
        serializerError = _defenderEncoder.openContainerWithKey( &tcpConnectionMap,
                                                                 EST_CONN_TAG,
                                                                 &establishedMap,
                                                                 hasConnections + hasTotal );
        assertNoError( serializerError );

        /* if user specify any metrics under "connections" and there are at least one connection */
        if( hasConnections )
        {
            /* create array "connections" under "established_connections" */
            serializerError = _defenderEncoder.openContainerWithKey( &establishedMap,
                                                                     CONN_TAG,
                                                                     &connectionsArray,
                                                                     total );
            assertNoError( serializerError );

            IotContainers_ForEach( pTcpConnectionsMetricsList, pListIterator )
            {
                IotSerializerEncoderObject_t connectionMap = IOT_SERIALIZER_ENCODER_CONTAINER_INITIALIZER_MAP;

                /* open a map under "connections" */
                serializerError = _defenderEncoder.openContainer( &connectionsArray,
                                                                  &connectionMap,
                                                                  hasRemoteAddr );
                assertNoError( serializerError );

                /* add remote address */
                if( hasRemoteAddr )
                {
                    pMetricsTcpConnection = IotLink_Container( IotMetricsTcpConnection_t, pListIterator, link );

                    serializerError = _defenderEncoder.appendKeyValue( &connectionMap, REMOTE_ADDR_TAG,
                                                                       IotSerializer_ScalarTextString( pMetricsTcpConnection->pRemoteAddress ) );
                    assertNoError( serializerError );
                }

                serializerError = _defenderEncoder.closeContainer( &connectionsArray, &connectionMap );
                assertNoError( serializerError );
            }

            serializerError = _defenderEncoder.closeContainer( &establishedMap, &connectionsArray );
            assertNoError( serializerError );
        }

        if( hasTotal )
        {
            serializerError = _defenderEncoder.appendKeyValue( &establishedMap,
                                                               TOTAL_TAG,
                                                               IotSerializer_ScalarSignedInt( total ) );
            assertNoError( serializerError );
        }

        serializerError = _defenderEncoder.closeContainer( &tcpConnectionMap, &establishedMap );
        assertNoError( serializerError );
    }

    serializerError = _defenderEncoder.closeContainer( pMetricsObject, &tcpConnectionMap );
    assertNoError( serializerError );
}

/*-----------------------------------------------------------*/

static void _serializeKernelRuntimeStats( void * param1 )
{
    /* Read FreeRTOS task information. */
    /* todo: below needs to be moved to FreeRTOS abstraction. */
    UBaseType_t uxArraySize = uxTaskGetNumberOfTasks();

    /* todo: below is uint32_t, but driver supports uint64_t. casting.*/
    uint32_t ulTotalTime = 0;
    uint32_t ulIdleTime = 0;

    TaskStatus_t * pxTaskStatusArray = pvPortMalloc( uxArraySize * sizeof( TaskStatus_t ) );
    HeapStats_t * pxHeapStatus = pvPortMalloc( sizeof( HeapStats_t ) );

    uxArraySize = uxTaskGetSystemState( pxTaskStatusArray, uxArraySize, &ulTotalTime );
    vPortGetHeapStats( pxHeapStatus );

    /* todo: unnecessary opaque type within the same file. pass in the right type of pointer. */
    IotSerializerEncoderObject_t * pMetricsObject = ( IotSerializerEncoderObject_t * ) param1;
    IotSerializerEncoderObject_t mcuUptimeMap = IOT_SERIALIZER_ENCODER_CONTAINER_INITIALIZER_MAP;
    IotSerializerEncoderObject_t taskDetailsArray = IOT_SERIALIZER_ENCODER_CONTAINER_INITIALIZER_ARRAY;

    IotSerializerError_t serializerError = IOT_SERIALIZER_SUCCESS;
    int i = 0;

    void (* assertNoError)( IotSerializerError_t ) = _report.pDataBuffer == NULL ? _assertSuccessOrBufferToSmall
                                                     : _assertSuccess;

    /* Create the "kernel_metrics" map. */
    serializerError = _defenderEncoder.openContainerWithKey( pMetricsObject,
                                                             KERNEL_METIRCS,
                                                             &mcuUptimeMap,
                                                             12 ); /* define macro */
    assertNoError( serializerError );

    /* heap_free_size
     * heap_largest_free_block
     * heap_smallest_free_block
     * heap_free_blocks
     * heap_low_watermark
     * heap_succ_alloc
     * heap_succ_free */
    serializerError = _defenderEncoder.appendKeyValue( &mcuUptimeMap,
                                                       KERNEL_HEAP_FREE_SIZE,
                                                       IotSerializer_ScalarSignedInt( pxHeapStatus->xAvailableHeapSpaceInBytes ) );
    assertNoError( serializerError );

    serializerError = _defenderEncoder.appendKeyValue( &mcuUptimeMap,
                                                       KERNEL_HEAP_LARGEST_FREE,
                                                       IotSerializer_ScalarSignedInt( pxHeapStatus->xSizeOfLargestFreeBlockInBytes ) );
    assertNoError( serializerError );

    serializerError = _defenderEncoder.appendKeyValue( &mcuUptimeMap,
                                                       KERNEL_HEAP_SMALLEST_FREE,
                                                       IotSerializer_ScalarSignedInt( pxHeapStatus->xSizeOfSmallestFreeBlockInBytes ) );
    assertNoError( serializerError );

    serializerError = _defenderEncoder.appendKeyValue( &mcuUptimeMap,
                                                       KERNEL_HEAP_NUM_OF_FREE,
                                                       IotSerializer_ScalarSignedInt( pxHeapStatus->xNumberOfFreeBlocks ) );
    assertNoError( serializerError );

    serializerError = _defenderEncoder.appendKeyValue( &mcuUptimeMap,
                                                       KERNEL_HEAP_LOW_WATERMARK,
                                                       IotSerializer_ScalarSignedInt( pxHeapStatus->xMinimumEverFreeBytesRemaining ) );
    assertNoError( serializerError );

    serializerError = _defenderEncoder.appendKeyValue( &mcuUptimeMap,
                                                       KERNEL_HEAP_SUCCESSFUL_ALLOC,
                                                       IotSerializer_ScalarSignedInt( pxHeapStatus->xNumberOfSuccessfulAllocations ) );
    assertNoError( serializerError );

    serializerError = _defenderEncoder.appendKeyValue( &mcuUptimeMap,
                                                       KERNEL_HEAP_SUCCESSFUL_FREE,
                                                       IotSerializer_ScalarSignedInt( pxHeapStatus->xNumberOfSuccessfulFrees ) );
    assertNoError( serializerError );

    /* num_of_tasks. */
    serializerError = _defenderEncoder.appendKeyValue( &mcuUptimeMap,
                                                       KERNEL_NUM_OF_TASKS,
                                                       IotSerializer_ScalarSignedInt( uxArraySize ) );
    assertNoError( serializerError );

    /* device_type. */
    serializerError = _defenderEncoder.appendKeyValue( &mcuUptimeMap,
                                                       DEVICE_TYPE,
                                                       IotSerializer_ScalarTextString( configPLATFORM_NAME ) );
    assertNoError( serializerError );


    /* Task details.
     * The ideal scenario is, all tasks fits into a report, which in reality will not always be the case.
     * While in prototyping phase to not hit MTU, use short tags and see which is better:
     * - have a sorting algorithm, and surface the most heavy loaded tasks.
     * - surface the defined number of tasks.
     * - surface a category of tasks. (e.g. only running and ready. )
     */
    serializerError = _defenderEncoder.openContainerWithKey( &mcuUptimeMap,
                                                             KERNEL_TASK_DETAILS,
                                                             &taskDetailsArray,
                                                             uxArraySize );
    assertNoError( serializerError );

    for( i = 0; i < uxArraySize; i++ )
    {
        IotSerializerEncoderObject_t taskEntryMap = IOT_SERIALIZER_ENCODER_CONTAINER_INITIALIZER_MAP;

        serializerError = _defenderEncoder.openContainer( &taskDetailsArray,
                                                          &taskEntryMap,
                                                          7 ); /* define macro. */
        assertNoError( serializerError );

        /* task_id, task_name, task_status, task_priority, task_abs_cycles, task_percentage, stack_high_watermark. */
        serializerError = _defenderEncoder.appendKeyValue( &taskEntryMap,
                                                           KERNEL_TASK_ID,
                                                           IotSerializer_ScalarSignedInt( pxTaskStatusArray[ i ].xTaskNumber ) );
        assertNoError( serializerError );

        serializerError = _defenderEncoder.appendKeyValue( &taskEntryMap,
                                                           KERNEL_TASK_NAME,
                                                           IotSerializer_ScalarTextString( pxTaskStatusArray[ i ].pcTaskName ) );
        assertNoError( serializerError );

        serializerError = _defenderEncoder.appendKeyValue( &taskEntryMap,
                                                           KERNEL_TASK_STATUS,
                                                           IotSerializer_ScalarSignedInt( pxTaskStatusArray[ i ].eCurrentState ) );
        assertNoError( serializerError );

        serializerError = _defenderEncoder.appendKeyValue( &taskEntryMap,
                                                           KERNEL_TASK_PRIORITY,
                                                           IotSerializer_ScalarSignedInt( pxTaskStatusArray[ i ].uxCurrentPriority ) );
        assertNoError( serializerError );

        serializerError = _defenderEncoder.appendKeyValue( &taskEntryMap,
                                                           KERNEL_TASK_ABS_CYCLES,
                                                           IotSerializer_ScalarSignedInt( pxTaskStatusArray[ i ].ulRunTimeCounter ) );
        assertNoError( serializerError );

        /* if idle, save the idle time counter. */
        if( TASK_NAME_EQUAL == _taskNameFilterIdle( pxTaskStatusArray[ i ].pcTaskName ) )
        {
            ulIdleTime = pxTaskStatusArray[ i ].ulRunTimeCounter;
        }

        serializerError = _defenderEncoder.appendKeyValue( &taskEntryMap,
                                                           KERNEL_TASK_PERCENTAGE,
                                                           IotSerializer_ScalarSignedInt( pxTaskStatusArray[ i ].ulRunTimeCounter / ( ulTotalTime / 100 ) ) );


        assertNoError( serializerError );

        serializerError = _defenderEncoder.appendKeyValue( &taskEntryMap,
                                                           KERNEL_STACK_HIGH_WATERMARK,
                                                           IotSerializer_ScalarSignedInt( pxTaskStatusArray[ i ].usStackHighWaterMark ) );
        assertNoError( serializerError );

        serializerError = _defenderEncoder.closeContainer( &taskDetailsArray, &taskEntryMap );
        assertNoError( serializerError );
    }

    /* Close array container */
    serializerError = _defenderEncoder.closeContainer( &mcuUptimeMap, &taskDetailsArray );
    assertNoError( serializerError );

    /* mcu_uptime, mcu_utilization. */
    serializerError = _defenderEncoder.appendKeyValue( &mcuUptimeMap,
                                                       KERNEL_MCU_UPTIME,
                                                       IotSerializer_ScalarSignedInt( ulTotalTime ) );
    assertNoError( serializerError );

    /* Calculate for MCU utilization, given now we know how many cycles are spent in IDLE. */
    serializerError = _defenderEncoder.appendKeyValue( &mcuUptimeMap,
                                                       KERNEL_MCU_UTILIZATION,
                                                       IotSerializer_ScalarSignedInt( ( ulTotalTime - ulIdleTime ) / ( ulTotalTime / 100 ) ) );
    assertNoError( serializerError );

    /* Close entire report. */
    serializerError = _defenderEncoder.closeContainer( pMetricsObject, &mcuUptimeMap );
    assertNoError( serializerError );

    /* Free memory. */
    vPortFree( pxTaskStatusArray );
    vPortFree( pxHeapStatus );
}

/*-----------------------------------------------------------*/

#if DEBUG_CBOR_PRINT == 1
    #include "cbor.h"
    /*-----------------------------------------------------------*/

    static void _printReport()
    {
        CborParser cborParser;
        CborValue cborValue;

        cbor_parser_init(
            _report.pDataBuffer,
            _report.size,
            0,
            &cborParser,
            &cborValue );
        cbor_value_to_pretty( stdout, &cborValue );
    }
#endif /* if DEBUG_CBOR_PRINT == 1 */
