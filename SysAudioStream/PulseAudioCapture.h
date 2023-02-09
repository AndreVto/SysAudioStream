#pragma once

#ifdef __linux__

#include <functional>
#include <thread>

#include <pulse/error.h>
#include <pulse/pulseaudio.h>
#include <cstdlib>
#include <cstring>

class PulseAudioCapture 
{
    public:
        PulseAudioCapture();
        ~PulseAudioCapture();

        typedef std::function<int(uint32_t audio_size, uint8_t* data)> PacketCallback;

        void SetAudioReadyCallback(PacketCallback callback);

        void AsyncStartCapture();
        void AsyncStopCapture();

        void SetPlaybackState(bool playing);

        bool InitializeAudioDevice(std::string audio_fmt);
        void StopCapture();

        int GetAudioFormat() const;
        int GetBitsPerSample() const;
        int GetChannels() const;
        int GetSamplerate() const;
        int GetEnginePeriod() const;

        PacketCallback m_callback;

    private:

        int m_audioFormat;
        int m_nChannels;
        int m_sampleRate;
        int m_enginePeriod;
        int m_bitsPerSample;

};

#endif