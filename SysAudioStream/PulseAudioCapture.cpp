#ifdef __linux__

#include "pch.h"
#include <sstream>
#include "PulseAudioCapture.h"
#include <thread>

pa_operation *pa_op = nullptr;

pa_stream *stream = NULL;
pa_mainloop *pa_ml = nullptr;
pa_mainloop_api *pa_mlapi = nullptr;
pa_context *pa_ctx = nullptr;

std::unique_ptr<std::thread> mainloop_thread;

PulseAudioCapture* self_obj = nullptr;

char* default_device_name = nullptr;

void pa_state_cb(pa_context *c, void *userdata);
void pa_get_default_sink_monitor(pa_context *c, const pa_server_info *i, void *userdata);
int pa_set_initial_config();

static pa_sample_spec ss = {
    .format = PA_SAMPLE_FLOAT32LE,
    .rate = 48000,
    .channels = 2
};

void pa_state_cb(pa_context *c, void *userdata) 
{
	pa_context_state_t state;
	int *pa_ready = (int*) userdata;

	state = pa_context_get_state(c);
	switch  (state) {
		case PA_CONTEXT_UNCONNECTED:
		case PA_CONTEXT_CONNECTING:
		case PA_CONTEXT_AUTHORIZING:
		case PA_CONTEXT_SETTING_NAME:
		default:
			break;

		case PA_CONTEXT_FAILED:
            printf("(pulseaudio): Connection failure: %s\n", pa_strerror(pa_context_errno(c)));

		case PA_CONTEXT_TERMINATED:
			*pa_ready = 2;
			break;
		case PA_CONTEXT_READY:
			*pa_ready = 1;
			break;
	}
}

void pa_get_default_sink_monitor(pa_context *c, const pa_server_info *i, void *userdata)
{
	char *monitor = (char*)calloc(strlen(i->default_sink_name) + 9, 1);
    strcat(monitor, i->default_sink_name);
	strcat(monitor, ".monitor");

    default_device_name = monitor;
}

int pa_set_initial_config() 
{
    int state = 0;
    int pa_ready = 0;
    int ret = 0;

    pa_ml = pa_mainloop_new();
    pa_mlapi = pa_mainloop_get_api(pa_ml);
    pa_ctx = pa_context_new(pa_mlapi, "SysAudioStream");

    pa_context_connect(pa_ctx, NULL, PA_CONTEXT_NOFLAGS, NULL);
    pa_context_set_state_callback(pa_ctx, pa_state_cb, &pa_ready);
    
    while (true) {
        if (pa_ready == 0) {
            pa_mainloop_iterate(pa_ml, 1, NULL);
            continue;
        }

        if (pa_ready == 2) {
            pa_context_disconnect(pa_ctx);
            pa_context_unref(pa_ctx);
            pa_mainloop_free(pa_ml);
            return -1;
        }

        switch (state) 
        {
            case 0:
                pa_op = pa_context_get_server_info(pa_ctx, pa_get_default_sink_monitor, NULL);
                state++;
                break;
                
            case 1:
                if (pa_operation_get_state(pa_op) == PA_OPERATION_DONE) {
                    pa_operation_unref(pa_op);
                    return 0;
                }
                break;

            default:
                printf("(pulseaudio): in state %d\n", state);
                return -1;
        }

        pa_mainloop_iterate(pa_ml, 1, NULL);
    }
}

void quit(int ret) 
{
    if (pa_mlapi) {
        pa_mlapi->quit(pa_mlapi, ret);
        pa_mlapi = nullptr;
    }
}

void stream_state_callback(pa_stream *s, void *userdata) 
{
    switch (pa_stream_get_state(s)) {
        case PA_STREAM_CREATING:
        case PA_STREAM_TERMINATED:
            break;

        case PA_STREAM_READY:
            {
                const pa_buffer_attr *stream_attr;
                char cmt[PA_CHANNEL_MAP_SNPRINT_MAX], sst[PA_SAMPLE_SPEC_SNPRINT_MAX];

                printf("(pulseaudio): Stream successfully created.\n");

                if (!(stream_attr = pa_stream_get_buffer_attr(s)))
                    printf("(pulseaudio): pa_stream_get_buffer_attr() failed: %s\n", pa_strerror(pa_context_errno(pa_stream_get_context(s))));
                else {
                    printf("(pulseaudio): Buffer metrics: maxlength=%u, fragsize=%u\n", stream_attr->maxlength, stream_attr->fragsize);                    
                }

                printf("(pulseaudio): Using sample spec '%s', channel map '%s'.\n",
                        pa_sample_spec_snprint(sst, sizeof(sst), pa_stream_get_sample_spec(s)),
                        pa_channel_map_snprint(cmt, sizeof(cmt), pa_stream_get_channel_map(s)));

                printf("(pulseaudio): Connected to device %s (%u, %ssuspended).\n",
                        pa_stream_get_device_name(s),
                        pa_stream_get_device_index(s),
                        pa_stream_is_suspended(s) ? "" : "not ");
            }

            break;

        case PA_STREAM_FAILED:
        default:
            printf("(pulseaudio): Stream error: %s\n", pa_strerror(pa_context_errno(pa_stream_get_context(s))));
            quit(1);
    }
}

void stream_read_callback(pa_stream *s, size_t length, void *userdata) 
{
    const void *data;
    size_t actualbytes = 0;

    if (pa_stream_peek(s, &data, &actualbytes) < 0) {
        printf("(pulseaudio): pa_stream_peek() failed: %s\n", pa_strerror(pa_context_errno(pa_ctx)));
        quit(1);
        return;
    }

    if (!actualbytes) {
		printf("(pulseaudio): no audio data\n");
        return;
    }

	if (!data) {
		printf("(pulseaudio): got audio hole of %u bytes\n",
		     (unsigned int)actualbytes);
		pa_stream_drop(s);
		return;
	}

    self_obj->m_callback(actualbytes, (uint8_t*) data);

    pa_stream_drop(s);
}

void stream_suspended_callback(pa_stream *s, void *userdata) 
{
    if (pa_stream_is_suspended(s))
        printf("(pulseaudio): Stream device suspended.\n");
    else
        printf("(pulseaudio): Stream device resumed.\n");
    
}

void stream_started_callback(pa_stream *s, void *userdata) 
{
    printf("(pulseaudio): Stream started.\n");
}

void stream_moved_callback(pa_stream *s, void *userdata) 
{
    printf("(pulseaudio): Stream moved to device %s (%u, %ssuspended).\n", pa_stream_get_device_name(s), pa_stream_get_device_index(s), pa_stream_is_suspended(s) ? "" : "not ");
}

void stream_buffer_attr_callback(pa_stream *s, void *userdata) 
{
    printf("(pulseaudio): Stream buffer attributes changed. \n");
}

void stream_event_callback(pa_stream *s, const char *name, pa_proplist *pl, void *userdata) 
{
    char *t;

    t = pa_proplist_to_string_sep(pl, ", ");
    printf("(pulseaudio): Got event '%s', properties '%s'\n", name, t);
    pa_xfree(t);
}

void t_mainloop_thread()
{    
    printf("(pulseaudio): mainloop thread started\n");

    int ret = 0;

    if (pa_mainloop_run(pa_ml, &ret) < 0) {
        printf("(pulseaudio): pa_mainloop_run() failed.\n");
        return;
    }

    printf("(pulseaudio): mainloop thread ended\n");
}

PulseAudioCapture::PulseAudioCapture() 
{
    self_obj = this;
}

PulseAudioCapture::~PulseAudioCapture() 
{

}

void PulseAudioCapture::SetAudioReadyCallback(PacketCallback callback)
{
    m_callback = callback;
}

bool PulseAudioCapture::InitializeAudioDevice(std::string audio_fmt)
{
    if (pa_set_initial_config() < 0) {
        printf("(pulseaudio): Failed to set initial PulseAudio configuration\n");
        return false;
    }

    std::vector<std::string> audio_config;

    std::string temp;
    std::stringstream sstr(audio_fmt);

    while (sstr >> temp) {
        audio_config.push_back(temp);
    }

    if (audio_config.size() != 3) {
        printf("(pulseaudio): Audio format has invalid number of configurations\n");
        return false;
    }

    std::string config_fmt = audio_config[0];
    std::string config_bits = audio_config[1];
    std::string config_rate = audio_config[2];

    m_bitsPerSample = std::stoi(config_bits);
    m_sampleRate = std::stoi(config_rate);

    ss.rate = m_sampleRate;

    if (config_fmt == "pcm") {
        switch (m_bitsPerSample)
        {
            case 16:
                ss.format = PA_SAMPLE_S16LE;
                break;
            
            case 24:
                ss.format = PA_SAMPLE_S24LE;
                break;

            case 32:
                ss.format = PA_SAMPLE_S32LE;
                break;
        }

        m_audioFormat = 0;
    }
    else if (config_fmt == "float") {
        ss.format = PA_SAMPLE_FLOAT32LE;
        m_bitsPerSample = 32;
        m_audioFormat = 1;
    }
    else return false;

    m_nChannels = ss.channels;
    m_enginePeriod = 0;

    if (!pa_sample_spec_valid(&ss)) {
        printf("(pulseaudio): Invalid sample specification\n");
        return false;
    }

    return true;
}

void PulseAudioCapture::StopCapture()
{
    quit(1);

    if (mainloop_thread) {
        if (mainloop_thread->joinable())
            mainloop_thread->join();

        mainloop_thread.reset();
    }

    mainloop_thread = nullptr;

    if (stream) {
		pa_stream_set_read_callback(stream, NULL, NULL);
        pa_stream_set_state_callback(stream, NULL, NULL);
        pa_stream_set_suspended_callback(stream, NULL, NULL);
        pa_stream_set_moved_callback(stream, NULL, NULL);
        pa_stream_set_started_callback(stream, NULL, NULL);
        pa_stream_set_event_callback(stream, NULL, NULL);
        pa_stream_set_buffer_attr_callback(stream, NULL, NULL);

        pa_stream_disconnect(stream);
        pa_stream_unref(stream);
    }

    stream = nullptr;

    if (pa_ctx) {
		pa_context_disconnect(pa_ctx);
        pa_context_unref(pa_ctx);
    }

    pa_ctx = nullptr;

    if (pa_ml) {
        pa_mainloop_free(pa_ml);        
    }

    pa_ml = nullptr;
}

void PulseAudioCapture::AsyncStartCapture()
{
    int r;
    pa_buffer_attr buffer_attr;

    if (!(stream = pa_stream_new(pa_ctx, "Desktop Audio", &ss, NULL))) {
        printf("(pulseaudio): pa_stream_new() failed: %s\n", pa_strerror(pa_context_errno(pa_ctx)));
        StopCapture();
        return;
    }

    pa_stream_set_state_callback(stream, stream_state_callback, NULL);
    pa_stream_set_read_callback(stream, stream_read_callback, NULL);
    pa_stream_set_suspended_callback(stream, stream_suspended_callback, NULL);
    pa_stream_set_moved_callback(stream, stream_moved_callback, NULL);
    pa_stream_set_started_callback(stream, stream_started_callback, NULL);
    pa_stream_set_event_callback(stream, stream_event_callback, NULL);
    pa_stream_set_buffer_attr_callback(stream, stream_buffer_attr_callback, NULL);

	pa_stream_flags_t flags = PA_STREAM_START_CORKED;

    if ((r = pa_stream_connect_record(stream, default_device_name, 0, flags)) < 0) {
        printf("(pulseaudio): pa_stream_connect_record() failed: %s\n", pa_strerror(pa_context_errno(pa_ctx)));
        StopCapture();
        return;
    }

    free(default_device_name);

    mainloop_thread = std::make_unique<std::thread>(t_mainloop_thread);
}

void PulseAudioCapture::AsyncStopCapture()
{
    
}

void PulseAudioCapture::SetPlaybackState(bool playing)
{
    if (playing) 
        pa_stream_cork(stream, 0, nullptr, nullptr); // Stream is resumed
    else 
        pa_stream_cork(stream, 1, nullptr, nullptr); // Stream is paused
}

int PulseAudioCapture::GetAudioFormat() const
{
    return m_audioFormat;
}

int PulseAudioCapture::GetBitsPerSample() const
{
    return m_bitsPerSample;
}

int PulseAudioCapture::GetChannels() const
{
    return m_nChannels;
}

int PulseAudioCapture::GetSamplerate() const
{
    return m_sampleRate;
}

int PulseAudioCapture::GetEnginePeriod() const
{
    return m_enginePeriod;
}

#endif