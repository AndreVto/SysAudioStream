#pragma once

#ifdef _WIN32
#include <functional>

namespace winrt::SDKTemplate
{
    enum DeviceState
    {
        Uninitialized,
        Error,
        Discontinuity,
        Flushing,
        Activated,

        Initialized,
        Starting,
        Playing,
        Capturing,
        Pausing,
        Paused,
        Stopping,
        Stopped,
    };

    struct WASAPICapture : winrt::implements<WASAPICapture, IActivateAudioInterfaceCompletionHandler>
    {
    public:
        WASAPICapture();

        typedef std::function<int(uint32_t audio_size, uint8_t* data)> PacketCallback;

        void SetAudioReadyCallback(PacketCallback callback);

        void AsyncInitializeAudioDevice(std::string audio_fmt) noexcept;
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

        // IActivateAudioInterfaceCompletionHandler
        STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation* operation);

    private:
        HRESULT OnStartCapture(IMFAsyncResult* pResult);
        HRESULT OnStopCapture(IMFAsyncResult* pResult);
        HRESULT OnFinishCapture(IMFAsyncResult* pResult);
        HRESULT OnSampleReady(IMFAsyncResult* pResult);

        EmbeddedMFAsyncCallback<&WASAPICapture::OnStartCapture> m_StartCaptureCallback{ this };
        EmbeddedMFAsyncCallback<&WASAPICapture::OnStopCapture> m_StopCaptureCallback{ this };
        EmbeddedMFAsyncCallback<&WASAPICapture::OnSampleReady> m_SampleReadyCallback{ this };
        EmbeddedMFAsyncCallback<&WASAPICapture::OnFinishCapture> m_FinishCaptureCallback{ this };

        void OnAudioSampleRequested();

    private:
        uint32_t m_bufferFrames = 0;

        // Event for sample ready or user stop
        handle m_SampleReadyEvent{ check_pointer(CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS)) };

        MFWORKITEM_KEY m_sampleReadyKey = 0;
        slim_mutex m_lock;
        unique_shared_work_queue m_queueId{ L"Capture" };

        PacketCallback m_callback;
        std::string m_audio_fmt;
        
        // true     = playing
        // false    = paused
        std::atomic<bool> m_playbackActive;

        int m_audioFormat;
        int m_bitsPerSample;
        int m_nChannels;
        int m_sampleRate;
        int m_enginePeriod;

        std::mutex m_mutex;
        std::condition_variable m_cv;
        bool m_done;

        unique_cotaskmem_ptr<WAVEFORMATEX> m_mixFormat;
        com_ptr<IAudioClient3> m_audioClient;
        com_ptr<IAudioRenderClient> m_audioRenderClient;
        com_ptr<IAudioCaptureClient> m_audioCaptureClient;
        com_ptr<IMFAsyncResult> m_sampleReadyAsyncResult;

        std::atomic<DeviceState> m_deviceState = DeviceState::Uninitialized;
        void SetState(enum DeviceState state, HRESULT error = S_OK);
    };
}

#endif