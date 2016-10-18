#include "AudioDevice.h"

const AudioUnitElement kInputBus = 1;
const AudioUnitElement kOutputBus = 0;

//                          -------------------------
//                          | i                   o |
// -- BUS 1 -- from mic -->	| n    REMOTE I/O     u | -- BUS 1 -- to app -->
//                          | p      AUDIO        t |
// -- BUS 0 -- from app -->	| u       UNIT        p | -- BUS 0 -- to speaker -->
//                          | t                   u |
//                          |                     t |
//                          -------------------------

AudioDevice::AudioDevice(int sampleRate, int channels, int bitsPerChannel):
    mAudioUnit(0),
    mStarted(false)
{
    mStreamDescription.mSampleRate = sampleRate;
    mStreamDescription.mFormatID = kAudioFormatLinearPCM;
    mStreamDescription.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    mStreamDescription.mFramesPerPacket = 1;
    mStreamDescription.mChannelsPerFrame = 1;
    mStreamDescription.mBitsPerChannel = 16;
    mStreamDescription.mBytesPerPacket = 2;
    mStreamDescription.mBytesPerFrame = 2;

    mRecordBuffer.resize(mStreamDescription.mBytesPerFrame * 4096); // lets have reserved size for 4k frames
}

AudioDevice::~AudioDevice()
{
    if(mAudioUnit)
    {
        if(mStarted)
        {
            AudioOutputUnitStop(mAudioUnit);
            AudioUnitUninitialize(mAudioUnit);
        }
        
        AudioComponentInstanceDispose(mAudioUnit);
    }
}

bool AudioDevice::initialize()
{
    bool result = true;
    OSStatus error = noErr;
    AudioSessionInitialize(NULL, NULL, NULL, NULL);
    
    UInt32 audioCategory;
    UInt32 audioCategorySize = sizeof(audioCategory);
    AudioSessionGetProperty(kAudioSessionProperty_AudioCategory, &audioCategorySize, &audioCategory);
    if(audioCategory != kAudioSessionCategory_PlayAndRecord)
    {
        error = AudioSessionSetActive(true);
        if(error != noErr) 
        {
            printf("error - can't activate audio session\n");
            result = false;
        }
        
        audioCategory = kAudioSessionCategory_PlayAndRecord;
        error = AudioSessionSetProperty(kAudioSessionProperty_AudioCategory, sizeof(audioCategory), &audioCategory);
        if(error != noErr)
        {
            printf("error - can't set audio category\n");
            result = false;
        }
    }
    
    Float32 bufferSizeInSeconds = 0.02f;
    error = AudioSessionSetProperty(kAudioSessionProperty_PreferredHardwareIOBufferDuration, sizeof(Float32), &bufferSizeInSeconds);
    if(error != noErr)
    {
        printf("error - can't set hardware IO buffer duration\n");
        result = false;
    }
    
    error = AudioSessionSetProperty(kAudioSessionProperty_PreferredHardwareSampleRate, sizeof(mStreamDescription.mSampleRate), &mStreamDescription.mSampleRate);
    if(error != noErr)
    {
        printf("error - can't set hardware sample rate\n");
        result = false;
    }

    // Create audio unit
    AudioComponentDescription componentDescription;
    componentDescription.componentType = kAudioUnitType_Output;
    componentDescription.componentSubType = kAudioUnitSubType_VoiceProcessingIO; // Needed for better voice processing
    componentDescription.componentManufacturer = kAudioUnitManufacturer_Apple;
    componentDescription.componentFlags = 0;
    componentDescription.componentFlagsMask = 0;
    AudioComponent component = AudioComponentFindNext(NULL, &componentDescription);
    error = AudioComponentInstanceNew(component, &mAudioUnit);
    if(error != noErr)
    {
        printf("error - can't create audio IO unit\n");
        result = false;
    }
    
    return result;
}

bool AudioDevice::initializeRecord(const RecordStatusCallback &statusCallback, const RecordBufferCallback &bufferCallback, bool autoGain)
{
    bool result = true;
    OSStatus error = noErr;
    mRecordStatusCallback = statusCallback;
    mRecordBufferCallback = bufferCallback;
    
    UInt32 property = 1;
    error = AudioUnitSetProperty(mAudioUnit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, kInputBus, &property, sizeof(property));
    if(error != noErr)
    {
        printf("error - can't enable input IO\n");
        result = false;
    }
    
    error = AudioUnitSetProperty(mAudioUnit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, kInputBus, &mStreamDescription, sizeof(mStreamDescription));
    if(error != noErr)
    {
        printf("error - can't set stream format for input\n");
        result = false;
    }
    
    if(!autoGain)
    {
        // Turn off auto gain control
        property = 0;
        error = AudioUnitSetProperty(mAudioUnit, kAUVoiceIOProperty_VoiceProcessingEnableAGC, kAudioUnitScope_Global, kInputBus, &property, sizeof(property));
        if(error != noErr)
        {
            
            printf("error - can't turn off automatic gain control %d\n", (int)error);
            result = false;
        }
    }
    
    // Set best voice processing quality
    property = 127;
    error = AudioUnitSetProperty(mAudioUnit, kAUVoiceIOProperty_VoiceProcessingQuality, kAudioUnitScope_Global, kInputBus, &property, sizeof(property));
    if(error != noErr)
    {
        printf("error - can't set voice processing quality %d\n", (int)error);
        result = false;
    }
    
    // Set render callback
    AURenderCallbackStruct callbackStruct;
    callbackStruct.inputProc = AudioDevice::recordCallback;
    callbackStruct.inputProcRefCon = (void*)this;
    error = AudioUnitSetProperty(mAudioUnit, kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Input, kInputBus, &callbackStruct, sizeof(callbackStruct));
    if(error != noErr)
    {
        printf("error - can't set render callback for input\n");
        result = false;
    }
    
    return result;
}

bool AudioDevice::initializePlayback(const PlaybackCallback &callback, bool useSpeaker)
{
    bool result = true;
    OSStatus error = noErr;
    mPlaybackCallback = callback;
    
    UInt32 property = useSpeaker ? 1 : 0;
    error = AudioSessionSetProperty(kAudioSessionProperty_OverrideCategoryDefaultToSpeaker, sizeof(property), &property);
    if(error != noErr)
    {
        printf("error - can't change audio route\n");
        result = false;
    }
    
    //
    // Enable IO for playback
    property = 1;
    error = AudioUnitSetProperty(mAudioUnit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, kOutputBus, &property, sizeof(property));
    if(error != noErr)
    {
        printf("error - can't enable output IO\n");
        result = false;
    }
    
    error = AudioUnitSetProperty(mAudioUnit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, kOutputBus, &mStreamDescription, sizeof(mStreamDescription));
    if(error != noErr)
    {
        printf("error - can't set stream format for output\n");
        result = false;
    }
    
    // Disable unit buffer allocation
    property = 0;
    error = AudioUnitSetProperty(mAudioUnit, kAudioUnitProperty_ShouldAllocateBuffer, kAudioUnitScope_Output, kOutputBus, &property, sizeof(property));
    if(error != noErr)
    {
        printf("error - can't disable buffer allocation\n");
        result = false;
    }
    
    // Set render callback
    AURenderCallbackStruct callbackStruct;
    callbackStruct.inputProc = AudioDevice::playbackCallback;
    callbackStruct.inputProcRefCon = (void*)this;
    error = AudioUnitSetProperty(mAudioUnit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, kOutputBus, &callbackStruct, sizeof(callbackStruct));
    if(error != noErr)
    {
        printf("error - can't set render callback for output\n");
        result = false;
    }
    
    return result;
}

bool AudioDevice::start()
{
    bool result = true;
    OSStatus error = noErr;
    
    error = AudioUnitInitialize(mAudioUnit);
    if(error != noErr)
    {
        printf("error - can't initialize audio IO unit\n");
        result = false;
    }
    
    error = AudioOutputUnitStart(mAudioUnit);
    if(error != noErr)
    {
        printf("error - can't start audio IO unit\n");
        result = false;
    }
    
    mStarted = true;
    return result;
}

OSStatus AudioDevice::recordCallback(void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags, 
        const AudioTimeStamp *inTimeStamp, UInt32 inBusNumber, UInt32 inNumberFrames, AudioBufferList *ioData)
{
    AudioDevice *audioDevice = (AudioDevice*)inRefCon;
    
    // ask user for an input buffer
    AudioBufferList bufferList;
    bufferList.mNumberBuffers = 1;
    bufferList.mBuffers[0].mDataByteSize = inNumberFrames * audioDevice->mStreamDescription.mBytesPerFrame;
    bufferList.mBuffers[0].mNumberChannels = audioDevice->mStreamDescription.mChannelsPerFrame;

    void *buffer = audioDevice->mRecordBufferCallback(bufferList.mBuffers[0].mDataByteSize);
    if(!buffer)
    {
        audioDevice->mRecordBuffer.resize(bufferList.mBuffers[0].mDataByteSize);
        buffer = (void*)&audioDevice->mRecordBuffer[0];
        //printf("num samples %d, buffer size %d\n", inNumberFrames, bufferList.mBuffers[0].mDataByteSize);
    }
    bufferList.mBuffers[0].mData = buffer;

    // obtain record samples
    OSStatus error;
    error = AudioUnitRender(audioDevice->mAudioUnit, ioActionFlags, inTimeStamp, inBusNumber, inNumberFrames, &bufferList);
    if(error != noErr) printf("error - can't get audio data from input\n");
    
    size_t length = (error == noErr) ? bufferList.mBuffers[0].mDataByteSize : 0;
    size_t unusedLength = (error == noErr) ? 0 : bufferList.mBuffers[0].mDataByteSize;
    audioDevice->mRecordStatusCallback(buffer, length, unusedLength); // notify the user
    return noErr;
}

OSStatus AudioDevice::playbackCallback(void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags,
        const AudioTimeStamp *inTimeStamp, UInt32 inBusNumber, UInt32 inNumberFrames, AudioBufferList *ioData)
{
    AudioDevice *audioDevice = (AudioDevice*)inRefCon;
    
    //
    size_t dataSize = inNumberFrames * audioDevice->mStreamDescription.mBytesPerFrame;
    ioData->mNumberBuffers = 1;
    ioData->mBuffers[0].mDataByteSize = (unsigned int)dataSize;
    
    
    size_t size = audioDevice->mPlaybackCallback(ioData->mBuffers[0].mData, dataSize); // fill the buffer
    if(size < dataSize)
    {
        //printf("error - nothing to write to output, pushing silences\n");
        memset((uint8_t*)ioData->mBuffers[0].mData + size, 0, dataSize - size);
    }
    
    return noErr;
}

