/*
 * $Id: pa_mac_core.c,v 1.1 2002-02-11 00:27:25 dmazzoni Exp $
 * pa_mac_core.c
 * Implementation of PortAudio for Mac OS X Core Audio
 *
 * PortAudio Portable Real-Time Audio Library
 * Latest Version at: http://www.portaudio.com
 *
 * Authors: Ross Bencina and Phil Burk
 * Copyright (c) 1999-2000 Ross Bencina and Phil Burk
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * Any person wishing to distribute modifications to the Software is
 * requested to send the modifications to the original developer so that
 * they can be incorporated into the canonical version.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * CHANGE HISTORY:
 
 3.29.2001 - Phil Burk - First pass... converted from Window MME code with help from Darren.
 3.30.2001 - Darren Gibbs - Added more support for dynamically querying device info.
 12.7.2001 - Gord Peters - Tweaks to compile on PA V17 and OS X 10.1
 2.7.2002 - Darren and Phil - fixed isInput so GetProperty works better, 
             fixed device queries for numChannels and sampleRates,
    one CoreAudio device now maps to separate input and output PaDevices,
    audio input works if using same CoreAudio device (some HW devices make separate CoreAudio devices).
    
TODO:
O- conversion for other user formats besides paFloat32
O- FIFO between input and output callbacks if different devices, like in pa_mac.c
O- debug mono output
O- error check for unsupported sample rates
O- BUG: if ask for 22050 when only 44100 supported then get 44100 and no error message
 */
#include <CoreServices/CoreServices.h>
#include <CoreAudio/CoreAudio.h>
#include <sys/time.h>
#include <unistd.h>
#include "portaudio.h"
#include "pa_host.h"
#include "pa_trace.h"

/************************************************* Constants ********/
/* Switches for debugging. */
#define PA_SIMULATE_UNDERFLOW    (0)  /* Set to one to force an underflow of the output buffer. */

/* To trace program, enable TRACE_REALTIME_EVENTS in pa_trace.h */
#define PA_TRACE_RUN             (0)
#define PA_TRACE_START_STOP      (1)

#define PA_MIN_LATENCY_MSEC      (8)
#define MIN_TIMEOUT_MSEC         (1000)

#define PRINT(x) { printf x; fflush(stdout); }
#define ERR_RPT(x) PRINT(x)
#define DBUG(x)  /* PRINT(x) */
#define DBUGX(x) /* PRINT(x) */

// define value of isInput passed to CodeAudio routines
#define IS_INPUT   (true)
#define IS_OUTPUT   (false)

/**************************************************************
 * Structure for internal host specific stream data.
 * This is allocated on a per stream basis.
 */
typedef struct PaHostSoundControl
{
    AudioDeviceID      pahsc_AudioDeviceID;  // Must be the same for input and output for now.
    /* Input -------------- */
    int                pahsc_BytesPerHostInputBuffer;
    int                pahsc_BytesPerUserInputBuffer;    /* native buffer size in bytes */
    /* Output -------------- */
    int                pahsc_BytesPerHostOutputBuffer;
    int                pahsc_BytesPerUserOutputBuffer;    /* native buffer size in bytes */
    /* Run Time -------------- */
    PaTimestamp        pahsc_FramesPlayed;
    long               pahsc_LastPosition;                /* used to track frames played. */
    /* For measuring CPU utilization. */
    // LARGE_INTEGER      pahsc_EntryCount;
    // LARGE_INTEGER      pahsc_LastExitCount;
    /* Init Time -------------- */
    int                pahsc_FramesPerHostBuffer;
    int                pahsc_UserBuffersPerHostBuffer;

    //   CRITICAL_SECTION   pahsc_StreamLock;                  /* Mutext to prevent threads from colliding. */
    int                pahsc_StreamLockInited;

}
PaHostSoundControl;

/**************************************************************
 * Structure for internal extended device info.
 * There will be one or two PortAudio devices for each Core Audio device:
 *   one input and or one output.
 */
typedef struct PaHostDeviceInfo
{
    PaDeviceInfo      paInfo;
    AudioDeviceID     audioDeviceID;
}
PaHostDeviceInfo;

/************************************************* Shared Data ********/
/* FIXME - put Mutex around this shared data. */
static int sNumPaDevices = 0;   /* Total number of PaDeviceInfos */
static int sNumInputDevices = 0; /* Total number of input PaDeviceInfos */
static int sNumOutputDevices = 0;
static PaHostDeviceInfo *sDeviceInfos = NULL;
static int sDefaultInputDeviceID = paNoDevice;
static int sDefaultOutputDeviceID = paNoDevice;
static int sPaHostError = 0;

static int sNumCoreDevices = 0;
static AudioDeviceID *sCoreDeviceIDs;   // Array of Core AudioDeviceIDs

static const char sMapperSuffixInput[] = " - Input";
static const char sMapperSuffixOutput[] = " - Output";

/************************************************* Macros ********/

/* Convert external PA ID to an internal ID that includes WAVE_MAPPER */
#define PaDeviceIdToWinId(id) (((id) < sNumInputDevices) ? (id - 1) : (id - sNumInputDevices - 1))

/************************************************* Prototypes **********/

static PaError Pa_QueryDevices( void );
PaError PaHost_GetTotalBufferFrames( internalPortAudioStream   *past );

static int PaHost_ScanDevices( Boolean isInput );
static int PaHost_QueryDeviceInfo( PaHostDeviceInfo *hostDeviceInfo, int coreDeviceIndex, Boolean isInput );

static PaDeviceID Pa_QueryDefaultInputDevice( void );
static PaDeviceID Pa_QueryDefaultOutputDevice( void );
static void PaHost_CalcHostBufferSize( internalPortAudioStream *past );

/********************************* BEGIN CPU UTILIZATION MEASUREMENT ****/
static void Pa_StartUsageCalculation( internalPortAudioStream   *past )
{
    PaHostSoundControl *pahsc = (PaHostSoundControl *) past->past_DeviceData;
    if( pahsc == NULL ) return;
    /* Query system timer for usage analysis and to prevent overuse of CPU. */
    // QueryPerformanceCounter( &pahsc->pahsc_EntryCount );
}

static void Pa_EndUsageCalculation( internalPortAudioStream   *past )
{
#if 0
    // LARGE_INTEGER CurrentCount = { 0, 0 };
    // LONGLONG      InsideCount;
    // LONGLONG      TotalCount;
    /*
    ** Measure CPU utilization during this callback. Note that this calculation
    ** assumes that we had the processor the whole time.
    */
#define LOWPASS_COEFFICIENT_0   (0.9)
#define LOWPASS_COEFFICIENT_1   (0.99999 - LOWPASS_COEFFICIENT_0)

    PaHostSoundControl *pahsc = (PaHostSoundControl *) past->past_DeviceData;
    if( pahsc == NULL ) return;

    if( QueryPerformanceCounter( &CurrentCount ) )
    {
        if( past->past_IfLastExitValid )
        {
            InsideCount = CurrentCount.QuadPart - pahsc->pahsc_EntryCount.QuadPart;
            TotalCount =  CurrentCount.QuadPart - pahsc->pahsc_LastExitCount.QuadPart;
            /* Low pass filter the result because sometimes we get called several times in a row.
             * That can cause the TotalCount to be very low which can cause the usage to appear
             * unnaturally high. So we must filter numerator and denominator separately!!!
             */
            past->past_AverageInsideCount = (( LOWPASS_COEFFICIENT_0 * past->past_AverageInsideCount) +
                                             (LOWPASS_COEFFICIENT_1 * InsideCount));
            past->past_AverageTotalCount = (( LOWPASS_COEFFICIENT_0 * past->past_AverageTotalCount) +
                                            (LOWPASS_COEFFICIENT_1 * TotalCount));
            past->past_Usage = past->past_AverageInsideCount / past->past_AverageTotalCount;
        }
        pahsc->pahsc_LastExitCount = CurrentCount;
        past->past_IfLastExitValid = 1;
    }
#endif
}

/************************************************************************/
static PaDeviceID Pa_QueryDefaultInputDevice( void )
{
    OSStatus err = noErr;
    UInt32  count;
    int          i;
    AudioDeviceID tempDeviceID = kAudioDeviceUnknown;
    PaDeviceID  defaultDeviceID;

    // get the default output device for the HAL
    // it is required to pass the size of the data to be returned
    count = sizeof(AudioDeviceID);
    err = AudioHardwareGetProperty( kAudioHardwarePropertyDefaultInputDevice,  &count, (void *) &tempDeviceID);
    if (err != noErr) goto Bail;
    // scan input devices to see which one matches this device
    defaultDeviceID = paNoDevice;
    for( i=0; i<sNumInputDevices; i++ )
    {
        DBUG(("Pa_QueryDefaultInputDevice: i = %d, aDevId = %d\n", i, sDeviceInfos[i].audioDeviceID ));
        if( sDeviceInfos[i].audioDeviceID == tempDeviceID )
        {
            defaultDeviceID = i;
            break;
        }
    }
Bail:
    return defaultDeviceID;
}

/************************************************************************/
static PaDeviceID Pa_QueryDefaultOutputDevice( void )
{
    OSStatus err = noErr;
    UInt32  count;
    int          i;
    AudioDeviceID tempDeviceID = kAudioDeviceUnknown;
    PaDeviceID  defaultDeviceID;

    // get the default output device for the HAL
    // it is required to pass the size of the data to be returned
    count = sizeof(AudioDeviceID);
    err = AudioHardwareGetProperty( kAudioHardwarePropertyDefaultOutputDevice,  &count, (void *) &tempDeviceID);
    if (err != noErr) goto Bail;
    // scan input devices to see which one matches this device
    defaultDeviceID = paNoDevice;
    for( i=sNumInputDevices; i<sNumPaDevices; i++ )
    {
        DBUG(("Pa_QueryDefaultOutputDevice: i = %d, aDevId = %d\n", i, sDeviceInfos[i].audioDeviceID ));
        if( sDeviceInfos[i].audioDeviceID == tempDeviceID )
        {
            defaultDeviceID = i;
            break;
        }
    }
Bail:
    return defaultDeviceID;
}

/****************************************** END CPU UTILIZATION *******/

static PaError Pa_QueryDevices( void )
{
    OSStatus err = noErr;
    UInt32   outSize;
    Boolean  outWritable;
    int         numBytes;

    // find out how many Core Audio devices there are, if any
    err = AudioHardwareGetPropertyInfo(kAudioHardwarePropertyDevices, &outSize, &outWritable);
    if (err != noErr)
        ERR_RPT(("Couldn't get info about list of audio devices\n"));

    // calculate the number of device available
    sNumCoreDevices = outSize / sizeof(AudioDeviceID);

    // Bail if there aren't any devices
    if (sNumCoreDevices < 1)
        ERR_RPT(("No Devices Available\n"));

    // make space for the devices we are about to get
    sCoreDeviceIDs = malloc(outSize);

    // get an array of AudioDeviceIDs
    err = AudioHardwareGetProperty(kAudioHardwarePropertyDevices, &outSize, (void *)sCoreDeviceIDs);
    if (err != noErr)
        ERR_RPT(("Couldn't get list of audio device IDs\n"));

    // Allocate structures to hold device info pointers.
    // There will be a maximum of two Pa devices per Core Audio device, input and/or output.
    numBytes = sNumCoreDevices * 2 * sizeof(PaHostDeviceInfo);
    sDeviceInfos = (PaHostDeviceInfo *) PaHost_AllocateFastMemory( numBytes );
    if( sDeviceInfos == NULL ) return paInsufficientMemory;

    // Scan all the Core Audio devices to see which support input and allocate a
    // PaHostDeviceInfo structure for each one.
    PaHost_ScanDevices( IS_INPUT );
    sNumInputDevices = sNumPaDevices;
    // Now scan all the output devices.
    PaHost_ScanDevices( IS_OUTPUT );
    sNumOutputDevices = sNumPaDevices - sNumInputDevices;

    sDefaultInputDeviceID = Pa_QueryDefaultInputDevice();
    sDefaultOutputDeviceID = Pa_QueryDefaultOutputDevice();

    return paNoError;
}

/************************************************************************************/
long Pa_GetHostError()
{
    return sPaHostError;
}

/*************************************************************************/
int Pa_CountDevices()
{
    if( sNumPaDevices <= 0 ) Pa_Initialize();
    return sNumPaDevices;
}

/*************************************************************************/

/* Allocate a string containing the device name. */
char *deviceNameFromID(AudioDeviceID deviceID, Boolean isInput )
{
    OSStatus err = noErr;
    UInt32  outSize;
    Boolean  outWritable;
    char     *deviceName = nil;
    // query size of name
    err =  AudioDeviceGetPropertyInfo(deviceID, 0, isInput, kAudioDevicePropertyDeviceName, &outSize, &outWritable);
    if (err == noErr)
    {
        deviceName = malloc( outSize + 1);
        if( deviceName )
        {
            err = AudioDeviceGetProperty(deviceID, 0, isInput, kAudioDevicePropertyDeviceName, &outSize, deviceName);
            if (err != noErr)
                ERR_RPT(("Couldn't get audio device name.\n"));
        }
    }

    return deviceName;
}

/*************************************************************************/

OSStatus deviceDataFormatFromID(AudioDeviceID deviceID , AudioStreamBasicDescription *desc, Boolean isInput )
{
    UInt32  outSize = sizeof(*desc);
    return AudioDeviceGetProperty(deviceID, 0, isInput, kAudioDevicePropertyStreamFormat, &outSize, desc);
}

/*************************************************************************/
// An AudioStreamBasicDescription is passed in to query whether or not
// the format is supported. A kAudioDeviceUnsupportedFormatError will
// be returned if the format is not supported and kAudioHardwareNoError
// will be returned if it is supported. AudioStreamBasicDescription
// fields set to 0 will be ignored in the query, but otherwise values
// must match exactly.

Boolean deviceDoesSupportFormat(AudioDeviceID deviceID, AudioStreamBasicDescription *desc, Boolean isInput )
{
    OSStatus err = noErr;
    UInt32  outSize;

    outSize = sizeof(*desc);
    err = AudioDeviceGetProperty(deviceID, 0, isInput, kAudioDevicePropertyStreamFormatSupported, &outSize, desc);

    if (err == kAudioHardwareNoError)
        return true;
    else
        return false;
}

/*************************************************************************/
// return an error string
char* coreAudioErrorString (int errCode )
{
    char *str;

    switch (errCode)
    {
    case kAudioHardwareUnspecifiedError:
        str = "kAudioHardwareUnspecifiedError";
        break;
    case kAudioHardwareNotRunningError:
        str = "kAudioHardwareNotRunningError";
        break;
    case kAudioHardwareUnknownPropertyError:
        str = "kAudioHardwareUnknownPropertyError";
        break;
    case kAudioDeviceUnsupportedFormatError:
        str = "kAudioDeviceUnsupportedFormatError";
        break;
    case kAudioHardwareBadPropertySizeError:
        str = "kAudioHardwareBadPropertySizeError";
        break;
    case kAudioHardwareIllegalOperationError:
        str = "kAudioHardwareIllegalOperationError";
        break;
    default:
        str = "Unknown CoreAudio Error!";
        break;
    }

    return str;
}

/*************************************************************************
** PaDeviceInfo structures have already been created
** so just return the pointer.
**
*/
const PaDeviceInfo* Pa_GetDeviceInfo( PaDeviceID id )
{
    if( id < 0 || id >= sNumPaDevices )
        return NULL;

    return &sDeviceInfos[id].paInfo;
}

/*************************************************************************
** Scan all of the Core Audio devices to see which support input or output.
** Changes sNumDevices, and fills in sDeviceInfos.
*/
static int PaHost_ScanDevices( Boolean isInput )
{
    int coreDeviceIndex;
    int result;
    PaHostDeviceInfo  *hostDeviceInfo;
    int numAdded = 0;

    for(  coreDeviceIndex=0; coreDeviceIndex<sNumCoreDevices; coreDeviceIndex++ )
    {
        // try to fill in next PaHostDeviceInfo
        hostDeviceInfo = &sDeviceInfos[sNumPaDevices];
        result = PaHost_QueryDeviceInfo( hostDeviceInfo, coreDeviceIndex, isInput );
        DBUG(("PaHost_ScanDevices: paDevId = %d, coreDevId = %d\n", sNumPaDevices, hostDeviceInfo->audioDeviceID ));
        if( result > 0 )
        {
            sNumPaDevices += 1;  // bump global counter if we got one
            numAdded += 1;
        }
        else if( result < 0 ) return result;
    }
    return numAdded;
}

/*************************************************************************
** Try to fill in the device info for this device.
** Return 1 if a good device that PA can use.
** Return 0 if not appropriate
** or return negative error.
**
*/
static int PaHost_QueryDeviceInfo( PaHostDeviceInfo *hostDeviceInfo, int coreDeviceIndex, Boolean isInput )
{
    OSErr   err;
    int index;
    AudioStreamBasicDescription formatDesc;
    Boolean result;
    AudioDeviceID    devID;
    double *sampleRates = NULL; /* non-const ptr */

    PaDeviceInfo *deviceInfo = &hostDeviceInfo->paInfo;
    double possibleSampleRates[] = {8000.0, 11025.0, 22050.0, 44100.0, 48000.0, 88200.0, 96000.0};
    int maxNumSampleRates = sizeof( possibleSampleRates ) / sizeof( double );

    deviceInfo->structVersion = 1;
    deviceInfo->maxInputChannels = 0;
    deviceInfo->maxOutputChannels = 0;
    deviceInfo->numSampleRates = -1;

    devID = sCoreDeviceIDs[ coreDeviceIndex ];
    hostDeviceInfo->audioDeviceID = devID;

    // Figure out supported sample rates
    // Make room in case device supports all rates.
    sampleRates = (double*)PaHost_AllocateFastMemory( maxNumSampleRates * sizeof(double) );
    if( sampleRates == NULL ) return paInsufficientMemory;

    deviceInfo->sampleRates = sampleRates;
    deviceInfo->numSampleRates = 0;

    // Loop through the possible sampling rates and check each to see if the device supports it.
    for (index = 0; index < maxNumSampleRates; index ++)
    {
        memset( &formatDesc, 0, sizeof(AudioStreamBasicDescription) );
        formatDesc.mSampleRate = possibleSampleRates[index];
        result = deviceDoesSupportFormat( devID, &formatDesc, isInput );

        if (result == true)
        {
            deviceInfo->numSampleRates += 1;
            *sampleRates = possibleSampleRates[index];
            sampleRates++;
        }
    }
    // If no sample rates supported, then not a very good device.
    if( deviceInfo->numSampleRates == 0 ) goto error;

    // Get data format info from the device.
    err = deviceDataFormatFromID(devID, &formatDesc, isInput );
    // If no channels supported, then not a very good device.
    if( (err != noErr) || (formatDesc.mChannelsPerFrame == 0) ) goto error;

    if( isInput )
    {
        deviceInfo->maxInputChannels = formatDesc.mChannelsPerFrame;
    }
    else
    {
        deviceInfo->maxOutputChannels = formatDesc.mChannelsPerFrame;
    }

    // FIXME - where to put current sample rate?:  formatDesc.mSampleRate

    // Right now the Core Audio headers only define one formatID: LinearPCM
    // Apparently LinearPCM must be Float32 for now.
    switch (formatDesc.mFormatID)
    {
    case kAudioFormatLinearPCM:
        deviceInfo->nativeSampleFormats = paFloat32;

        // FIXME - details about the format are in these flags.
        // formatDesc.mFormatFlags

        // here are the possibilities
        // kLinearPCMFormatFlagIsFloat   // set for floating point, clear for integer
        // kLinearPCMFormatFlagIsBigEndian  // set for big endian, clear for little
        // kLinearPCMFormatFlagIsSignedInteger // set for signed integer, clear for unsigned integer,
        //    only valid if kLinearPCMFormatFlagIsFloat is clear
        // kLinearPCMFormatFlagIsPacked   // set if the sample bits are packed as closely together as possible,
        //    clear if they are high or low aligned within the channel
        // kLinearPCMFormatFlagIsAlignedHigh  // set if the sample bits are placed
        break;

    default:
        deviceInfo->nativeSampleFormats = paFloat32;  // FIXME
        break;
    }


    // Get the device name
    deviceInfo->name = deviceNameFromID( devID, isInput );
    return 1;

error:
    if( sampleRates != NULL ) free( sampleRates );
    return 0;
}

/*************************************************************************
** Returns recommended device ID.
** On the PC, the recommended device can be specified by the user by
** setting an environment variable. For example, to use device #1.
**
**    set PA_RECOMMENDED_OUTPUT_DEVICE=1
**
** The user should first determine the available device ID by using
** the supplied application "pa_devs".
*/
#define PA_ENV_BUF_SIZE  (32)
#define PA_REC_IN_DEV_ENV_NAME  ("PA_RECOMMENDED_INPUT_DEVICE")
#define PA_REC_OUT_DEV_ENV_NAME  ("PA_RECOMMENDED_OUTPUT_DEVICE")

static PaDeviceID PaHost_GetEnvDefaultDeviceID( char *envName )
{
#if 0
    UInt32   hresult;
    char    envbuf[PA_ENV_BUF_SIZE];
    PaDeviceID recommendedID = paNoDevice;

    /* Let user determine default device by setting environment variable. */
    hresult = GetEnvironmentVariable( envName, envbuf, PA_ENV_BUF_SIZE );
    if( (hresult > 0) && (hresult < PA_ENV_BUF_SIZE) )
    {
        recommendedID = atoi( envbuf );
    }

    return recommendedID;
#endif
    return paNoDevice;
}

static PaError Pa_MaybeQueryDevices( void )
{
    if( sNumPaDevices == 0 )
    {
        return Pa_QueryDevices();
    }
    return 0;
}

/**********************************************************************
** Check for environment variable, else query devices and use result.
*/
PaDeviceID Pa_GetDefaultInputDeviceID( void )
{
    PaError result;
    result = PaHost_GetEnvDefaultDeviceID( PA_REC_IN_DEV_ENV_NAME );
    if( result < 0 )
    {
        result = Pa_MaybeQueryDevices();
        if( result < 0 ) return result;
        result = sDefaultInputDeviceID;
    }
    return result;
}

PaDeviceID Pa_GetDefaultOutputDeviceID( void )
{
    PaError result;
    result = PaHost_GetEnvDefaultDeviceID( PA_REC_OUT_DEV_ENV_NAME );
    if( result < 0 )
    {
        result = Pa_MaybeQueryDevices();
        if( result < 0 ) return result;
        result = sDefaultOutputDeviceID;
    }
    return result;
}

/**********************************************************************
** Initialize Host dependant part of API.
*/

PaError PaHost_Init( void )
{
#if PA_SIMULATE_UNDERFLOW
    PRINT(("WARNING - Underflow Simulation Enabled - Expect a Big Glitch!!!\n"));
#endif
    return Pa_MaybeQueryDevices();
}

/**********************************************************************
** Fill any available output buffers and use any available
** input buffers by calling user callback.
*/
static PaError Pa_TimeSlice( internalPortAudioStream   *past, const AudioBufferList*  inInputData,
                             AudioBufferList*  outOutputData )
{
    PaError           result = 0;
    char             *inBufPtr;
    char             *outBufPtr;
    int               gotInput = 0;
    int               gotOutput = 0;
    int               i;
    int               buffersProcessed = 0;
    int               done = 0;
    PaHostSoundControl *pahsc = (PaHostSoundControl *) past->past_DeviceData;
    if( pahsc == NULL ) return paInternalError;

    past->past_NumCallbacks += 1;

#if PA_SIMULATE_UNDERFLOW
    if(gUnderCallbackCounter++ == UNDER_SLEEP_AT)
    {
        Sleep(UNDER_SLEEP_FOR);
    }
#endif

#if PA_TRACE_RUN
    AddTraceMessage("Pa_TimeSlice: past_NumCallbacks ", past->past_NumCallbacks );
#endif

    Pa_StartUsageCalculation( past );

    /* If we are using output, then we need an empty output buffer. */
    gotOutput = 0;
    if( past->past_NumOutputChannels > 0 )
    {
        outBufPtr =  outOutputData->mBuffers[0].mData,
                     gotOutput = 1;
    }

    /* If we are using input, then we need a full input buffer. */
    gotInput = 0;
    inBufPtr = NULL;
    if(  past->past_NumInputChannels > 0  )
    {
        inBufPtr = inInputData->mBuffers[0].mData,
                   gotInput = 1;
    }

    buffersProcessed += 1;

    /* Each host buffer contains multiple user buffers so do them all now. */
    for( i=0; i<pahsc->pahsc_UserBuffersPerHostBuffer; i++ )
    {
        if( done )
        {
            if( gotOutput )
            {
                /* Clear remainder of wave buffer if we are waiting for stop. */
                AddTraceMessage("Pa_TimeSlice: zero rest of wave buffer ", i );
                memset( outBufPtr, 0, pahsc->pahsc_BytesPerUserOutputBuffer );
            }
        }
        else
        {
            /* Convert 16 bit native data to user data and call user routine. */
            result = Pa_CallConvertFloat32( past, (float *) inBufPtr, (float *) outBufPtr );
            if( result != 0) done = 1;
        }
        if( gotInput ) inBufPtr += pahsc->pahsc_BytesPerUserInputBuffer;
        if( gotOutput) outBufPtr += pahsc->pahsc_BytesPerUserOutputBuffer;
    }

    Pa_EndUsageCalculation( past );

#if PA_TRACE_RUN
    AddTraceMessage("Pa_TimeSlice: buffersProcessed ", buffersProcessed );
#endif

    return (result != 0) ? result : done;
}

OSStatus appIOProc (AudioDeviceID  inDevice, const AudioTimeStamp*  inNow,
                    const AudioBufferList*  inInputData, const AudioTimeStamp*  inInputTime,
                    AudioBufferList*  outOutputData, const AudioTimeStamp* inOutputTime,
                    void* contextPtr)
{

    PaError      result = 0;
    internalPortAudioStream *past;
    PaHostSoundControl *pahsc;
    past = (internalPortAudioStream *) contextPtr;
    pahsc = (PaHostSoundControl *) past->past_DeviceData;

    // Just Curious: printf("Num input Buffers: %d; Num output Buffers: %d.\n", inInputData->mNumberBuffers, outOutputData->mNumberBuffers);

    /* Has someone asked us to abort by calling Pa_AbortStream()? */
    if( past->past_StopNow )
    {
        past->past_IsActive = 0; /* Will cause thread to return. */
    }
    /* Has someone asked us to stop by calling Pa_StopStream()
     * OR has a user callback returned '1' to indicate finished.
     */
    else if( past->past_StopSoon )
    {
        // FIXME - pretend all done
        past->past_IsActive = 0; /* Will cause thread to return. */
    }
    else
    {
        /* Process full input buffer and fill up empty output buffers. */
        if( (result = Pa_TimeSlice( past, inInputData, outOutputData )) != 0)
        {
            /* User callback has asked us to stop. */
#if PA_TRACE_START_STOP
            AddTraceMessage( "Pa_OutputThreadProc: TimeSlice() returned ", result );
#endif
            past->past_StopSoon = 1; /* Request that audio play out then stop. */
            result = paNoError;
        }
    }

    // FIXME PaHost_UpdateStreamTime( pahsc );

    return result;
}

#if 0
static int PaHost_CalcTimeOut( internalPortAudioStream *past )
{
    /* Calculate timeOut longer than longest time it could take to play all buffers. */
    int timeOut = (UInt32) (1500.0 * PaHost_GetTotalBufferFrames( past ) / past->past_SampleRate);
    if( timeOut < MIN_TIMEOUT_MSEC ) timeOut = MIN_TIMEOUT_MSEC;
    return timeOut;
}
#endif
/*******************************************************************/
PaError PaHost_OpenInputStream( internalPortAudioStream   *past )
{
    PaHostSoundControl *pahsc;
    const PaHostDeviceInfo *hostDeviceInfo;
    PaError          result = paNoError;
    UInt32           bytesPerHostBuffer;
    UInt32           dataSize;
    OSStatus         err = noErr;
    int              bytesPerInputFrame;

    pahsc = (PaHostSoundControl *) past->past_DeviceData;

    DBUG(("PaHost_OpenStream: deviceID = 0x%x\n", past->past_InputDeviceID));
    if( (past->past_InputDeviceID < 0) || (past->past_InputDeviceID >= sNumPaDevices) )
    {
        return paInvalidDeviceId;
    }
    hostDeviceInfo = &sDeviceInfos[past->past_InputDeviceID];

    // calculate buffer sizes in bytes
    bytesPerInputFrame = Pa_GetSampleSize(past->past_InputSampleFormat) * past->past_NumInputChannels;
    pahsc->pahsc_BytesPerUserInputBuffer = past->past_FramesPerUserBuffer * bytesPerInputFrame;
    pahsc->pahsc_BytesPerHostInputBuffer = pahsc->pahsc_FramesPerHostBuffer * bytesPerInputFrame;

    // Change the bufferSize of the device! Is this per device or just for our stream?
    bytesPerHostBuffer = pahsc->pahsc_BytesPerHostInputBuffer;
    dataSize = sizeof(UInt32);
    err = AudioDeviceSetProperty( hostDeviceInfo->audioDeviceID, 0, 0, IS_INPUT,
                                  kAudioDevicePropertyBufferSize, dataSize, &bytesPerHostBuffer);
    if( err != noErr )
    {
        ERR_RPT(("Could not force buffer size!"));
        result = paHostError;
        goto error;
    }

    return result;

error:
    return result;
}

/*******************************************************************/
PaError PaHost_OpenOutputStream( internalPortAudioStream   *past )
{
    PaHostSoundControl *pahsc;
    const PaHostDeviceInfo *hostDeviceInfo;
    PaError          result = paNoError;
    UInt32           bytesPerHostBuffer;
    UInt32           dataSize;
    OSStatus         err = noErr;
    int              bytesPerOutputFrame;

    pahsc = (PaHostSoundControl *) past->past_DeviceData;

    DBUG(("PaHost_OpenStream: deviceID = 0x%x\n", past->past_OutputDeviceID));
    if( (past->past_OutputDeviceID < 0) || (past->past_OutputDeviceID >= sNumPaDevices) )
    {
        return paInvalidDeviceId;
    }
    hostDeviceInfo = &sDeviceInfos[past->past_OutputDeviceID];

    // calculate buffer sizes in bytes
    bytesPerOutputFrame = Pa_GetSampleSize(past->past_OutputSampleFormat) * past->past_NumOutputChannels;
    pahsc->pahsc_BytesPerUserOutputBuffer = past->past_FramesPerUserBuffer * bytesPerOutputFrame;
    pahsc->pahsc_BytesPerHostOutputBuffer = pahsc->pahsc_FramesPerHostBuffer * bytesPerOutputFrame;

    // Change the bufferSize of the device! Is this per device or just for our stream?
    bytesPerHostBuffer = pahsc->pahsc_BytesPerHostOutputBuffer;
    dataSize = sizeof(UInt32);
    err = AudioDeviceSetProperty( hostDeviceInfo->audioDeviceID, 0, 0, IS_OUTPUT,
                                  kAudioDevicePropertyBufferSize, dataSize, &bytesPerHostBuffer);
    if( err != noErr )
    {
        ERR_RPT(("Could not force buffer size!"));
        result = paHostError;
        goto error;
    }

    return result;

error:
    return result;
}

/*******************************************************************/
PaError PaHost_GetTotalBufferFrames( internalPortAudioStream   *past )
{
    PaHostSoundControl *pahsc = (PaHostSoundControl *) past->past_DeviceData;
    return pahsc->pahsc_FramesPerHostBuffer;
}


/*******************************************************************
* Determine how many User Buffers we can put into our CoreAudio stream buffer.
* Uses:
*    past->past_FramesPerUserBuffer, etc.
* Sets:
*    past->past_NumUserBuffers
*    pahsc->pahsc_UserBuffersPerHostBuffer
*    pahsc->pahsc_FramesPerHostBuffer
*/
static void PaHost_CalcHostBufferSize( internalPortAudioStream *past )
{
    PaHostSoundControl *pahsc = past->past_DeviceData;
    unsigned int  minNumUserBuffers;

    // Determine number of user buffers based on minimum latency.
    minNumUserBuffers = Pa_GetMinNumBuffers( past->past_FramesPerUserBuffer, past->past_SampleRate );
    // Compare to user requested number in user wants more than the minimum.
    past->past_NumUserBuffers = ( minNumUserBuffers > past->past_NumUserBuffers ) ?
                                minNumUserBuffers : past->past_NumUserBuffers;
    DBUG(("PaHost_CalcNumHostBuffers: min past_NumUserBuffers = %d\n", past->past_NumUserBuffers ));

    // For CoreAudio, we only have one Host buffer, so...
    pahsc->pahsc_UserBuffersPerHostBuffer = past->past_NumUserBuffers;
    // Calculate size of CoreAudio buffer.
    pahsc->pahsc_FramesPerHostBuffer = past->past_FramesPerUserBuffer * past->past_NumUserBuffers;

    DBUG(("PaHost_CalcNumHostBuffers: pahsc_UserBuffersPerHostBuffer = %d\n", pahsc->pahsc_UserBuffersPerHostBuffer ));
    DBUG(("PaHost_CalcNumHostBuffers: pahsc_FramesPerHostBuffer = %d\n", pahsc->pahsc_FramesPerHostBuffer ));
}

/*******************************************************************/
PaError PaHost_OpenStream( internalPortAudioStream   *past )
{
    PaError             result = paNoError;
    PaHostSoundControl *pahsc;

    /* Allocate and initialize host data. */
    pahsc = (PaHostSoundControl *) malloc(sizeof(PaHostSoundControl));
    if( pahsc == NULL )
    {
        result = paInsufficientMemory;
        goto error;
    }
    memset( pahsc, 0, sizeof(PaHostSoundControl) );
    past->past_DeviceData = (void *) pahsc;

    // If we are using both input and out, then they must be on the same CoreAudio device,
    // until we implement a FIFO between two devices.
    if( (past->past_OutputDeviceID != paNoDevice) && (past->past_InputDeviceID != paNoDevice) )
    {
        AudioDeviceID inputID = sDeviceInfos[past->past_InputDeviceID].audioDeviceID;
        AudioDeviceID outputID = sDeviceInfos[past->past_OutputDeviceID].audioDeviceID;
        if( inputID != outputID )
        {
            ERR_RPT(("PortAudio: input and output must use same CoreAudio device!\n"));
            return paInvalidDeviceId;
        }
    }

    PaHost_CalcHostBufferSize( past );

    {
        int msecLatency = (int) ((PaHost_GetTotalBufferFrames(past) * 1000) / past->past_SampleRate);
        PRINT(("PortAudio on OSX - Latency = %d frames, %d msec\n", PaHost_GetTotalBufferFrames(past), msecLatency ));
    }

    /* ------------------ OUTPUT */
    if( (past->past_OutputDeviceID != paNoDevice) && (past->past_NumOutputChannels > 0) )
    {
        result = PaHost_OpenOutputStream( past );
        if( result < 0 ) goto error;
        pahsc->pahsc_AudioDeviceID = sDeviceInfos[past->past_OutputDeviceID].audioDeviceID;
    }

    /* ------------------ INPUT */
    if( (past->past_InputDeviceID != paNoDevice) && (past->past_NumInputChannels > 0) )
    {
        result = PaHost_OpenInputStream( past );
        if( result < 0 ) goto error;
        pahsc->pahsc_AudioDeviceID = sDeviceInfos[past->past_InputDeviceID].audioDeviceID;
    }

    return result;

error:
    PaHost_CloseStream( past );
    return result;
}

/*************************************************************************/
PaError PaHost_StartOutput( internalPortAudioStream *past )
{
    return 0;
}

/*************************************************************************/
PaError PaHost_StartInput( internalPortAudioStream *past )
{
    return 0;
}

/*************************************************************************/
PaError PaHost_StartEngine( internalPortAudioStream *past )
{
    OSStatus            err = noErr;
    PaError             result = paNoError;
    PaHostSoundControl *pahsc = (PaHostSoundControl *) past->past_DeviceData;

    past->past_StopSoon = 0;
    past->past_StopNow = 0;
    past->past_IsActive = 1;
    pahsc->pahsc_FramesPlayed = 0.0;
    pahsc->pahsc_LastPosition = 0;

    // Associate an IO proc with the device and pass a pointer to the audio data context
    err = AudioDeviceAddIOProc(pahsc->pahsc_AudioDeviceID, (AudioDeviceIOProc)appIOProc, past);
    if (err != noErr) goto error;

    // start playing sound through the device
    err = AudioDeviceStart(pahsc->pahsc_AudioDeviceID, (AudioDeviceIOProc)appIOProc);
    if (err != noErr) goto error;
    return result;

#if PA_TRACE_START_STOP
    AddTraceMessage( "PaHost_StartEngine: TimeSlice() returned ", result );
#endif

error:
    return paHostError; // FIXME - save host error
}

/*************************************************************************/
PaError PaHost_StopEngine( internalPortAudioStream *past, int abort )
{
    OSStatus  err = noErr;
    PaHostSoundControl *pahsc = (PaHostSoundControl *) past->past_DeviceData;
    if( pahsc == NULL ) return paNoError;
    (void) abort;

    /* Tell background thread to stop generating more data and to let current data play out. */
    past->past_StopSoon = 1;
    /* If aborting, tell background thread to stop NOW! */
    if( abort ) past->past_StopNow = 1;
    past->past_IsActive = 0;

#if PA_TRACE_START_STOP
    AddTraceMessage( "PaHost_StopOutput: pahsc_HWaveOut ", (int) pahsc->pahsc_HWaveOut );
#endif

    // FIXME - we should ask proc to stop instead of stopping abruptly
    err = AudioDeviceStop(pahsc->pahsc_AudioDeviceID, (AudioDeviceIOProc)appIOProc);
    if (err != noErr) goto Bail;

    err = AudioDeviceRemoveIOProc(pahsc->pahsc_AudioDeviceID, (AudioDeviceIOProc)appIOProc);
    if (err != noErr) goto Bail;

    return paNoError;

Bail:
    return paHostError; // FIXME - save err
}

/*************************************************************************/
PaError PaHost_StopInput( internalPortAudioStream *past, int abort )
{
    return paNoError;
}

/*************************************************************************/
PaError PaHost_StopOutput( internalPortAudioStream *past, int abort )
{
    return paNoError;
}

/*******************************************************************/
PaError PaHost_CloseStream( internalPortAudioStream   *past )
{
    PaHostSoundControl *pahsc;

    if( past == NULL ) return paBadStreamPtr;
    pahsc = (PaHostSoundControl *) past->past_DeviceData;
    if( pahsc == NULL ) return paNoError;

#if PA_TRACE_START_STOP
    AddTraceMessage( "PaHost_CloseStream: pahsc_HWaveOut ", (int) pahsc->pahsc_HWaveOut );
#endif

    free( pahsc );
    past->past_DeviceData = NULL;

    return paNoError;
}

/*************************************************************************
** Determine minimum number of buffers required for this host based
** on minimum latency. 
*/
int Pa_GetMinNumBuffers( int framesPerUserBuffer, double sampleRate )
{
    int   minUserBuffers;
    int   minFramesPerHostBuffer;

    // Calculate minimum and maximum sizes based on timing and sample rate.
    minFramesPerHostBuffer = (int) (PA_MIN_LATENCY_MSEC * sampleRate / 1000.0);
    // round up to nearest multiple of 8
    minFramesPerHostBuffer = (minFramesPerHostBuffer + 7) & ~7;
    DBUG(("Pa_GetMinNumBuffers: minFramesPerHostBuffer = %d\n", minFramesPerHostBuffer ));

    minUserBuffers = (minFramesPerHostBuffer + framesPerUserBuffer - 1) / framesPerUserBuffer;
    if( minUserBuffers < 1 ) minUserBuffers = 1;

    return minUserBuffers;
}

/*************************************************************************
** Cleanup device info.
*/
PaError PaHost_Term( void )
{
    int i;

    if( sDeviceInfos != NULL )
    {
        for( i=0; i<sNumPaDevices; i++ )
        {
            if( sDeviceInfos[i].paInfo.name != NULL ) free( (char*)sDeviceInfos[i].paInfo.name );
            if( sDeviceInfos[i].paInfo.sampleRates != NULL ) free( (void*)sDeviceInfos[i].paInfo.sampleRates );
        }
        free( sDeviceInfos );
        sDeviceInfos = NULL;
    }

    sNumPaDevices = 0;
    return paNoError;
}

/*************************************************************************/
void Pa_Sleep( long msec )
{
    /*       struct timeval timeout;
           timeout.tv_sec = msec / 1000;
           timeout.tv_usec = (msec % 1000) * 1000;
           select( 0, NULL, NULL, NULL, &timeout );
    */
    usleep( msec * 1000 );
}

/*************************************************************************
 * Allocate memory that can be accessed in real-time.
 * This may need to be held in physical memory so that it is not
 * paged to virtual memory.
 * This call MUST be balanced with a call to PaHost_FreeFastMemory().
 */
void *PaHost_AllocateFastMemory( long numBytes )
{
    void *addr = malloc( numBytes ); /* FIXME - do we need physical memory? */
    if( addr != NULL ) memset( addr, 0, numBytes );
    return addr;
}

/*************************************************************************
 * Free memory that could be accessed in real-time.
 * This call MUST be balanced with a call to PaHost_AllocateFastMemory().
 */
void PaHost_FreeFastMemory( void *addr, long numBytes )
{
    if( addr != NULL ) free( addr );
}


/***********************************************************************/
PaError PaHost_StreamActive( internalPortAudioStream   *past )
{
    PaHostSoundControl *pahsc;
    if( past == NULL ) return paBadStreamPtr;
    pahsc = (PaHostSoundControl *) past->past_DeviceData;
    if( pahsc == NULL ) return paInternalError;
    return (PaError) past->past_IsActive;
}

/*************************************************************************/
PaTimestamp Pa_StreamTime( PortAudioStream *stream )
{
    PaHostSoundControl *pahsc;
    internalPortAudioStream   *past = (internalPortAudioStream *) stream;
    if( past == NULL ) return paBadStreamPtr;
    pahsc = (PaHostSoundControl *) past->past_DeviceData;

    // FIXME PaHost_UpdateStreamTime( pahsc );
    return pahsc->pahsc_FramesPlayed;
}
