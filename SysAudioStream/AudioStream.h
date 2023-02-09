#pragma once

#include <thread>
#include <cstdint>
#include <memory>
#include "pch.h"

#include "PulseAudioCapture.h"
#include "WASAPICapture.h"

#include "AESWrapper.h"
#include "RandomGenerator.h"

#ifdef __linux__
typedef int SOCKET;
#endif

struct EncryptedData 
{
	uint8_t iv[16];
	uint8_t* data;
};

struct StreamSettings 
{
	int android_port;

	int audio_format;
	int bits_per_sample;
	int n_channels;
	int sample_rate;
	int engine_period;
	int cmd_port;
};

struct CmdStreamPacket 
{
	// cmd = 0 (measure latency)
	// cmd = 1 (play stream)
	// cmd = 2 (pause stream)
	// cmd = 3 (stop stream)
	int cmd;
};

class AudioStream
{
public:
	AudioStream(std::string password, int conn_socket_port, std::string audio_fmt);
	~AudioStream();

	bool Init();

private:
	void t_cmd_receiver();
	void t_connection_receiver();

	AESWrapper m_aes_wrapper;

	std::string m_password;
	std::string m_audio_fmt;

	SOCKET m_cmd_socket;
	u_short m_cmd_socket_port;

	SOCKET m_send_audio_socket;

	SOCKET m_connection_receiver_socket;
	u_short m_connection_receiver_socket_port;

	std::unique_ptr<std::thread> m_connections_thread;
	std::unique_ptr<std::thread> m_cmd_thread;

	RandomGenerator m_random_gen;

	#ifdef _WIN32
	winrt::com_ptr<winrt::SDKTemplate::WASAPICapture> m_capture;
	#elif defined(__linux__)
	std::unique_ptr<PulseAudioCapture> m_capture;
	#endif

	byte m_audio_streaming_buffer[65536] = { 0 };
};