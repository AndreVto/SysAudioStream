#ifdef _WIN32

#include "pch.h"
#include "WASAPICapture.h"

using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Media::Devices;

using namespace std::literals;

namespace winrt::SDKTemplate
{
    static constexpr uint32_t BITS_PER_BYTE = 8;

    void WASAPICapture::SetState(enum DeviceState state, HRESULT error)
    {
        if (m_deviceState != state)
        {
            m_deviceState = state;

            if (FAILED(error)) {
                _com_error err(error);
                printf("(err-windows): %ws\n", err.ErrorMessage());
            }
        }
    }

    void WASAPICapture::SetAudioReadyCallback(PacketCallback callback)
    {
        m_callback = callback;
    }

    WASAPICapture::WASAPICapture()
    {
        m_audioFormat = 0;
        m_bitsPerSample = 0;
        m_nChannels = 0;
        m_sampleRate = 0;
        m_enginePeriod = 0;
        m_playbackActive = true;
        m_done = false;

        // Set the capture event work queue to use the MMCSS queue
        m_SampleReadyCallback.SetQueueID(m_queueId.get());
    }

    //
    //  InitializeAudioDeviceAsync()
    //
    //  Activates the default audio capture on a asynchronous callback thread.  This needs
    //  to be called from the main UI thread.
    //
    void WASAPICapture::AsyncInitializeAudioDevice(std::string audio_fmt) noexcept try
    {
        m_audio_fmt = audio_fmt;

        com_ptr<IActivateAudioInterfaceAsyncOperation> asyncOp;

        hstring deviceIdString = MediaDevice::GetDefaultAudioRenderId(AudioDeviceRole::Default);        

        // This call must be made on the main UI thread.  Async operation will call back to 
        // IActivateAudioInterfaceCompletionHandler::ActivateCompleted, which must be an agile interface implementation
        check_hresult(ActivateAudioInterfaceAsync(deviceIdString.c_str(), __uuidof(IAudioClient3), nullptr, this, asyncOp.put()));
    }
    catch (...)
    {
        SetState(DeviceState::Error, to_hresult());
    }

    bool WASAPICapture::InitializeAudioDevice(std::string audio_fmt)
    {
        // TODO: Do a proper sync call
        // Check 'ActivateCompleted' for the condition_variable notification

        std::unique_lock lk(m_mutex);

        AsyncInitializeAudioDevice(audio_fmt);

        m_cv.wait(lk, [&] { return m_done; });
        m_done = false;

        return m_deviceState == DeviceState::Initialized;
    }

    //
    //  ActivateCompleted()
    //
    //  Callback implementation of ActivateAudioInterfaceAsync function.  This will be called on MTA thread
    //  when results of the activation are available.
    //
    HRESULT WASAPICapture::ActivateCompleted(IActivateAudioInterfaceAsyncOperation* operation) try
    {
        HRESULT status = S_OK;
        com_ptr<::IUnknown> punkAudioInterface;

        // Check for a successful activation result
        check_hresult(operation->GetActivateResult(&status, punkAudioInterface.put()));
        check_hresult(status);

        // Get the pointer for the Audio Client
        m_audioClient = punkAudioInterface.as<IAudioClient3>();

        check_hresult(m_audioClient->GetMixFormat(m_mixFormat.put()));

        bool isPCM = true;

        std::vector<std::string> audio_config;

        std::string temp;
        std::stringstream ss(m_audio_fmt);

        while (ss >> temp) {
            audio_config.push_back(temp);
        }

        if (audio_config.size() != 3)
            throw hresult_invalid_argument();

        std::string config_fmt = audio_config[0];
        std::string config_bits = audio_config[1];
        std::string config_rate = audio_config[2];

        if (config_fmt == "pcm") {
            m_audioFormat = 0;
        }
        else if (config_fmt == "float") {
            m_audioFormat = 1;
        }
        else throw hresult_invalid_argument();

        m_bitsPerSample = std::stoi(config_bits);
        m_sampleRate = std::stoi(config_rate);
        
        switch (m_mixFormat->wFormatTag)
        {
        case WAVE_FORMAT_PCM:
        case WAVE_FORMAT_IEEE_FLOAT:

            if (m_audioFormat == 0)
                m_mixFormat->wFormatTag = WAVE_FORMAT_PCM;
            else
                m_mixFormat->wFormatTag = WAVE_FORMAT_IEEE_FLOAT;

            m_mixFormat->wBitsPerSample = m_bitsPerSample;
            m_mixFormat->nSamplesPerSec = m_sampleRate;

            m_nChannels = m_mixFormat->nChannels;

            m_mixFormat->nBlockAlign = m_mixFormat->nChannels * m_mixFormat->wBitsPerSample / BITS_PER_BYTE;
            m_mixFormat->nAvgBytesPerSec = m_mixFormat->nSamplesPerSec * m_mixFormat->nBlockAlign;
            
            break;

        case WAVE_FORMAT_EXTENSIBLE:
        {
            WAVEFORMATEXTENSIBLE* pWaveFormatExtensible = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(m_mixFormat.get());

            if (m_audioFormat == 0)
                pWaveFormatExtensible->SubFormat = KSDATAFORMAT_SUBTYPE_PCM; 
            else
                pWaveFormatExtensible->SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

            pWaveFormatExtensible->Format.wBitsPerSample = m_bitsPerSample;
            pWaveFormatExtensible->Format.nSamplesPerSec = m_sampleRate;

            m_nChannels = pWaveFormatExtensible->Format.nChannels;

            pWaveFormatExtensible->Format.nBlockAlign =
                pWaveFormatExtensible->Format.nChannels *
                pWaveFormatExtensible->Format.wBitsPerSample /
                BITS_PER_BYTE;
            pWaveFormatExtensible->Format.nAvgBytesPerSec =
                pWaveFormatExtensible->Format.nSamplesPerSec *
                pWaveFormatExtensible->Format.nBlockAlign;
            pWaveFormatExtensible->Samples.wValidBitsPerSample =
                pWaveFormatExtensible->Format.wBitsPerSample;
            break;
        }

        default:
            // we can only handle float or PCM
            throw hresult_error(HRESULT_FROM_WIN32(ERROR_NOT_FOUND));
            break;
        }

        uint32_t defaultPeriodInFrames = 0;
        uint32_t fundamentalPeriodInFrames = 0;
        uint32_t maxPeriodInFrames = 0;
        uint32_t minPeriodInFrames = 0;

        // The wfx parameter below is optional (Its needed only for MATCH_FORMAT clients). Otherwise, wfx will be assumed 
        // to be the current engine format based on the processing mode for this stream
        check_hresult(m_audioClient->GetSharedModeEnginePeriod(m_mixFormat.get(), &defaultPeriodInFrames, &fundamentalPeriodInFrames, &minPeriodInFrames, &maxPeriodInFrames));

        m_enginePeriod = defaultPeriodInFrames;

        // Initialize the AudioClient in Shared Mode with the user specified buffer
        check_hresult(m_audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
            0,
            defaultPeriodInFrames,
            m_mixFormat.get(),
            nullptr));

        // Get the maximum size of the AudioClient Buffer
        check_hresult(m_audioClient->GetBufferSize(&m_bufferFrames));

        // Get the capture client
        m_audioCaptureClient.capture(m_audioClient, &IAudioClient::GetService);

        // Create Async callback for sample events
        check_hresult(MFCreateAsyncResult(nullptr, &m_SampleReadyCallback, nullptr, m_sampleReadyAsyncResult.put()));

        // Provides the event handle for the system to signal when an audio buffer is ready to be processed by the client
        check_hresult(m_audioClient->SetEventHandle(m_SampleReadyEvent.get()));

        SetState(DeviceState::Initialized);

        {
            std::lock_guard lk(m_mutex);
            m_done = true;
        }

        m_cv.notify_one();

        // Need to return S_OK
        return S_OK;
    }
    catch (...)
    {
        SetState(DeviceState::Error, to_hresult());
        m_audioClient = nullptr;
        m_audioCaptureClient = nullptr;
        m_sampleReadyAsyncResult = nullptr;

        {
            std::lock_guard lk(m_mutex);
            m_done = true;
        }

        m_cv.notify_one();

        // Must return S_OK even on failure.
        return S_OK;
    }

    //
    //  AsyncStartCapture()
    //
    //  Starts asynchronous capture on a separate thread via MF Work Item
    //
    void WASAPICapture::AsyncStartCapture()
    {
        // We should be in the initialzied state if this is the first time through getting ready to capture.
        if (m_deviceState == DeviceState::Initialized ||
            m_deviceState == DeviceState::Stopped)
        {
            SetState(DeviceState::Starting);
            MFPutWorkItem2(MFASYNC_CALLBACK_QUEUE_MULTITHREADED, 0, &m_StartCaptureCallback, nullptr);
        }
    }

    //
    //  OnStartCapture()
    //
    //  Callback method to start capture
    //
    HRESULT WASAPICapture::OnStartCapture(IMFAsyncResult*) try
    {
        // Start the capture
        check_hresult(m_audioClient->Start());
        check_hresult(MFPutWaitingWorkItem(m_SampleReadyEvent.get(), 0, m_sampleReadyAsyncResult.get(), &m_sampleReadyKey));
        SetState(DeviceState::Capturing);

        return S_OK;
    }
    catch (...)
    {
        SetState(DeviceState::Error, to_hresult());
        // Must return S_OK.
        return S_OK;
    }

    //
    //  AsyncStopCapture()
    //
    //  Stop capture asynchronously via MF Work Item
    //
    void WASAPICapture::AsyncStopCapture()
    {
        if ((m_deviceState == DeviceState::Capturing) ||
            (m_deviceState == DeviceState::Error))
        {
            SetState(DeviceState::Stopping);
            MFPutWorkItem2(MFASYNC_CALLBACK_QUEUE_MULTITHREADED, 0, &m_StopCaptureCallback, nullptr);
        }
    }

    void WASAPICapture::SetPlaybackState(bool playing)
    {
        m_playbackActive = playing;
    }

    void WASAPICapture::StopCapture()
    {
        // TODO: Do a proper sync call
        // Check 'OnFinishCapture' for the condition_variable notification

        std::unique_lock lk(m_mutex);
        bool acquired_wait = false;

        if ((m_deviceState == DeviceState::Capturing) ||
            (m_deviceState == DeviceState::Error)) {
            acquired_wait = true;

            SetState(DeviceState::Stopping);
            MFPutWorkItem2(MFASYNC_CALLBACK_QUEUE_MULTITHREADED, 0, &m_StopCaptureCallback, nullptr);
        }

        if (acquired_wait) {
            m_cv.wait(lk, [&] { return m_done; });
            m_done = false;
        }
    }

    //
    //  OnStopCapture()
    //
    //  Callback method to stop capture
    //
    HRESULT WASAPICapture::OnStopCapture(IMFAsyncResult*) try
    {
        // Stop capture by cancelling Work Item
        // Cancel the queued work item (if any)
        if (0 != m_sampleReadyKey)
        {
            MFCancelWorkItem(std::exchange(m_sampleReadyKey, 0));
        }

        m_sampleReadyAsyncResult = nullptr;
        if (m_audioClient)
        {
            auto guard = slim_lock_guard(m_lock);
            m_audioClient->Stop();
        }

        SetState(DeviceState::Flushing);
        check_hresult(MFPutWorkItem2(MFASYNC_CALLBACK_QUEUE_MULTITHREADED, 0, &m_FinishCaptureCallback, nullptr));

        return S_OK;
    }
    catch (...) { return to_hresult(); }

    //
    //  OnFinishCapture()
    //
    //  Because of the asynchronous nature of the MF Work Queues and the DataWriter, there could still be
    //  a sample processing.  So this will get called to finalize the WAV header.
    //
    HRESULT WASAPICapture::OnFinishCapture(IMFAsyncResult*)
    {
        SetState(DeviceState::Stopped);

        m_mixFormat.reset();

        m_audioClient = nullptr;
        m_audioRenderClient = nullptr;

        if (m_audioCaptureClient) {
            m_audioCaptureClient->Release();
            m_audioCaptureClient = nullptr;
        }

        m_sampleReadyAsyncResult = nullptr;

        {
            std::lock_guard lk(m_mutex);
            m_done = true;
        }

        m_cv.notify_one();

        return S_OK;
    }

    //
    //  OnSampleReady()
    //
    //  Callback method when ready to fill sample buffer
    //
    HRESULT WASAPICapture::OnSampleReady(IMFAsyncResult*) try
    {
        OnAudioSampleRequested();

        return S_OK;
    }
    catch (...)
    {
        hresult error = to_hresult();
        SetState(DeviceState::Error, error);
        return error;
    }

    //
    //  OnAudioSampleRequested()
    //
    //  Called when audio device fires m_SampleReadyEvent
    //
    void WASAPICapture::OnAudioSampleRequested()
    {
        auto guard = slim_lock_guard(m_lock);

        // If this flag is set, we have already queued up the async call to finialize the WAV header
        // So we don't want to grab or write any more data that would possibly give us an invalid size
        if ((m_deviceState == DeviceState::Stopping) ||
            (m_deviceState == DeviceState::Flushing))
        {
            return;
        }

        // A word on why we have a loop here:
        // Suppose it has been 10 milliseconds or so since the last time
        // this routine was invoked, and that we're capturing 48000 samples per second.
        //
        // The audio engine can be reasonably expected to have accumulated about that much
        // audio data - that is, about 480 samples.
        //
        // However, the audio engine is free to accumulate this in various ways:
        // a. as a single packet of 480 samples, OR
        // b. as a packet of 80 samples plus a packet of 400 samples, OR
        // c. as 48 packets of 10 samples each.
        //
        // In particular, there is no guarantee that this routine will be
        // run once for each packet.
        //
        // So every time this routine runs, we need to read ALL the packets
        // that are now available;
        //
        // We do this by calling IAudioCaptureClient::GetNextPacketSize
        // over and over again until it indicates there are no more packets remaining.
        uint32_t framesAvailable = 0;
        while (m_playbackActive &&
               m_deviceState == DeviceState::Capturing && 
               SUCCEEDED(m_audioCaptureClient->GetNextPacketSize(&framesAvailable)) && framesAvailable > 0)
        {
            DWORD bytesToCapture = framesAvailable * m_mixFormat->nBlockAlign;

            uint8_t* data = nullptr;
            DWORD dwCaptureFlags = 0;
            uint64_t devicePosition = 0;
            uint64_t qpcPosition = 0;

            auto mixFormat = m_mixFormat.get();

            // Get sample buffer
            check_hresult(m_audioCaptureClient->GetBuffer(&data, &framesAvailable, &dwCaptureFlags, &devicePosition, &qpcPosition));

            // Zero out sample if silence
            if (dwCaptureFlags & AUDCLNT_BUFFERFLAGS_SILENT)
            {
                memset(data, 0, m_mixFormat->nBlockAlign * framesAvailable);
            }

            m_callback(mixFormat->nBlockAlign * framesAvailable, data);

            m_audioCaptureClient->ReleaseBuffer(framesAvailable);
        }

        if (m_deviceState == DeviceState::Capturing)
        {
            check_hresult(MFPutWaitingWorkItem(m_SampleReadyEvent.get(), 0, m_sampleReadyAsyncResult.get(), &m_sampleReadyKey));
        }
    }

    int WASAPICapture::GetAudioFormat() const
    {
        return m_audioFormat;
    }

    int WASAPICapture::GetBitsPerSample() const
    {
        return m_bitsPerSample;
    }

    int WASAPICapture::GetChannels() const
    {
        return m_nChannels;
    }

    int WASAPICapture::GetSamplerate() const
    {
        return m_sampleRate;
    }

    int WASAPICapture::GetEnginePeriod() const
    {
        return m_enginePeriod;
    }
}

#endif