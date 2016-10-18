#ifndef AudioDevice_H
#define AudioDevice_H

#include <AudioToolbox/AudioToolbox.h>
#include "Function.h"
#include <vector>

// Usage: 
// AudioDevice *device = new AudioDevice(11025, 1);
// device->Initialize();
// device->InitializePlayback(...);
// device->Start();
//
// make sure to initialize either record or playback!
// make sure your callbacks are thread safe, record callbacks both will be called from same thread
//
// Playback callback is called when playback need input, example:
// size_t AudioHandler::fillPlaybackBuffer(void *data, size_t length)
// {
//     int dataSize = length > inputLength ? inputLength : length;
//     memcpy(data, inputData, dataSize);
//     return dataSize;
// }
//
// Record callbacks:
// RecordBufferCallback - used to get a data buffer for recording, return 0 if you want audio device to use internal buffer
// RecordStatusCallback - is notification that last created buffer by buffer callback
// was filled, and in case of error, it specified what size of the buffer from the end wasn't used
// so you can discard that data
// void *AudioHandler::getRecordBuffer(size_t length)
// {
//     void *buffer = malloc(length);
//     return buffer;
// }
//
// void AudioHandler::recordBufferFilled(size_t unusedLength)
// {
//     // todo: discard last unusedLength - bytes, and process remaining the way you want
//     // either saving to file, or to some processing unit for futher playback
// }

// support onty int16, single channel
class AudioDevice
{
public:
    // length == length in bytes!
    // todo: rename callbacks to something more suitable
    typedef Function<size_t(void *data, size_t length)> PlaybackCallback; // fill buffer, return size actualy filled
    typedef Function<void(void *data, size_t length, size_t unusedLength)> RecordStatusCallback; // notify about unused buffer part
    typedef Function<void*(size_t length)> RecordBufferCallback; // get buffer

public:
    AudioDevice(int sampleRate, int channels, int bitsPerChannel);
    ~AudioDevice();

    bool initialize();
    //
    bool initializeRecord(const RecordStatusCallback &statusCallback, const RecordBufferCallback &bufferCallback, bool autoGain = false);
    // callback is called when playback needs input data
    bool initializePlayback(const PlaybackCallback &callback, bool useSpeaker);
    bool start();

    bool isRunning() const { return mStarted; }

protected:
    static OSStatus recordCallback(void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags, const AudioTimeStamp *inTimeStamp,
                                   UInt32 inBusNumber, UInt32 inNumberFrames, AudioBufferList *ioData);
    static OSStatus playbackCallback(void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags, const AudioTimeStamp *inTimeStamp,
                                     UInt32 inBusNumber, UInt32 inNumberFrames, AudioBufferList *ioData);

    AudioUnit mAudioUnit;
    bool mStarted;
    
    AudioStreamBasicDescription mStreamDescription;
    
    RecordBufferCallback mRecordBufferCallback;
    RecordStatusCallback mRecordStatusCallback;
    PlaybackCallback mPlaybackCallback;
    std::vector<char> mRecordBuffer; // internal record buffer
};

#endif