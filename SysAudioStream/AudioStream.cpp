#include "AudioStream.h"

AudioStream::AudioStream(std::string password, int conn_socket_port, std::string audio_fmt)
{
	m_cmd_socket = 0;
	m_cmd_socket_port = 0;

	m_send_audio_socket = 0;

	m_connection_receiver_socket = 0;
	m_connection_receiver_socket_port = 0;

	m_audio_fmt = audio_fmt;
	m_connection_receiver_socket_port = conn_socket_port;

	int key_size = AESWrapper::KeySize();

	if (password.size() > key_size)
		throw std::invalid_argument("password length too big");

	m_password = password;
	m_aes_wrapper.GenerateKey(password);

	m_cmd_thread = nullptr;
	m_connections_thread = nullptr;
}

AudioStream::~AudioStream()
{
	if (m_capture) {
		m_capture->StopCapture();
#if defined(_WIN32)
		m_capture->Release();
#endif
		m_capture = nullptr;
	}

	#if defined(_WIN32)
	MFShutdown();

	shutdown(m_cmd_socket, SD_BOTH);
	shutdown(m_connection_receiver_socket, SD_BOTH);
	shutdown(m_send_audio_socket, SD_BOTH);

	closesocket(m_cmd_socket);
	closesocket(m_connection_receiver_socket);
	closesocket(m_send_audio_socket);
	#elif defined(__linux__)
	shutdown(m_cmd_socket, SHUT_RDWR);
	shutdown(m_connection_receiver_socket, SHUT_RDWR);
	shutdown(m_send_audio_socket, SHUT_RDWR);
	
	close(m_cmd_socket);
	close(m_connection_receiver_socket);
	close(m_send_audio_socket);
	#endif

	if (m_cmd_thread && m_cmd_thread->joinable())
		m_cmd_thread->join();

	m_cmd_thread.reset();

	if (m_connections_thread && m_connections_thread->joinable())
		m_connections_thread->join();

	m_connections_thread.reset();
}

bool AudioStream::Init()
{
	int ret;

	#if defined(_WIN32)
	winrt::init_apartment(winrt::apartment_type::multi_threaded);
	HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);

	if (FAILED(hr)) {
		_com_error err(hr);
		printf("(err-init): MFStartup Error\n");
		printf("(err-init): %ws\n", err.ErrorMessage());
		return false;
	}

	WSADATA wsaData;
	m_send_audio_socket = INVALID_SOCKET;
	m_connection_receiver_socket = INVALID_SOCKET;

	ret = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (ret) {
		_com_error err(hr);
		printf("(err-init): WSAStartup Error\n");
		printf("(err-init): %ws\n", err.ErrorMessage());
		return false;
	}
	#endif

	m_send_audio_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (m_send_audio_socket == -1) {
#if defined(_WIN32)
		printf("(err-init): send_audio_socket failed with error: %ld\n", WSAGetLastError());
		WSACleanup();
#elif defined(__linux__)
		printf("(err-init): send_audio_socket failed with error: %d\n", errno);
#endif
		return false;
	}

	m_connection_receiver_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (m_connection_receiver_socket == -1) {
#if defined(_WIN32)
		printf("(err-init): connection_receiver_socket failed with error: %ld\n", WSAGetLastError());
		WSACleanup();
#elif defined(__linux__)		
		printf("(err-init): connection_receiver_socket failed with error: %d\n", errno);
#endif
		return false;
	}

	sockaddr_in connReceiverAddr{};
	connReceiverAddr.sin_family = AF_INET;
	connReceiverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	connReceiverAddr.sin_port = htons(m_connection_receiver_socket_port);

	ret = bind(m_connection_receiver_socket, reinterpret_cast<sockaddr*>(&connReceiverAddr), sizeof(connReceiverAddr));
	if (ret < 0) {
		#if defined(_WIN32)
		printf("(err-init): connection_receiver_socket bind() failed (errno: %ld)\n", WSAGetLastError());
		#elif defined(__linux__)
		printf("(err-init): connection_receiver_socket bind() failed (errno: %d)\n", errno);
		#endif	
		return false;
	}

	#if defined(_WIN32)
	m_capture = winrt::make_self<winrt::SDKTemplate::WASAPICapture>();
	#elif defined(__linux__)
	m_capture = std::make_unique<PulseAudioCapture>();
	#endif

	m_capture->SetAudioReadyCallback([this](uint32_t audio_size, uint8_t* audio_samples)
	{
		EncryptedData* enc_audio_data = reinterpret_cast<EncryptedData*>(m_audio_streaming_buffer);

		m_random_gen.Generate(enc_audio_data->iv, 16);
		m_aes_wrapper.SetIv(enc_audio_data->iv, 16);

		byte* data_ptr = (byte*)(&enc_audio_data->data);

		int data_size = m_aes_wrapper.Encrypt(audio_samples, audio_size, data_ptr);
		int data_total_size = sizeof(enc_audio_data->iv) + data_size;

		int ret = send(m_send_audio_socket, (const char*)m_audio_streaming_buffer, data_total_size, 0);

		return 0;
	});

	m_cmd_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (m_cmd_socket == -1) {
		#if defined(_WIN32)
		printf("(err-init): cmd_socket failed with error: %ld\n", WSAGetLastError());
		#elif defined(__linux__)
		printf("(err-init): cmd_socket failed with error: %s (errno: %d)\n", strerror(errno), errno);
		#endif
		return false;
	}

	sockaddr_in cmd_sockaddr{};
	cmd_sockaddr.sin_family = AF_INET;
	cmd_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	cmd_sockaddr.sin_port = htons(0);

	ret = bind(m_cmd_socket, reinterpret_cast<sockaddr*>(&cmd_sockaddr), sizeof(cmd_sockaddr));
	if (ret < 0) {
		#if defined(_WIN32)
		printf("(err-init): cmd_socket bind() failed (errno: %ld)\n", WSAGetLastError());
		#elif defined(__linux__)
		printf("(err-init): cmd_socket bind() failed:  %s (errno: %d)\n", strerror(errno), errno);
		#endif
		return false;
	}

	socklen_t addrlen = sizeof(cmd_sockaddr);
	getsockname(m_cmd_socket, reinterpret_cast<sockaddr*>(&cmd_sockaddr), &addrlen); 
	m_cmd_socket_port = ntohs(cmd_sockaddr.sin_port);

	m_connections_thread = std::make_unique<std::thread>(&AudioStream::t_connection_receiver, this);
	m_cmd_thread = std::make_unique<std::thread>(&AudioStream::t_cmd_receiver, this);

	return true;
}

void AudioStream::t_cmd_receiver()
{
	AESWrapper local_aes_wrapper;
	local_aes_wrapper.GenerateKey(m_password);

	byte local_buffer[8192] = { 0 };

	while (true) {
		sockaddr_in remote_sockaddr{};
		socklen_t remote_addrlen = sizeof(remote_sockaddr);

		int recv_bytes = recvfrom(m_cmd_socket, (char*)local_buffer, 8192, 0, reinterpret_cast<sockaddr*>(&remote_sockaddr), &remote_addrlen);

		if (recv_bytes <= 0) {
			#if defined(_WIN32)
			printf("(cmd-thread): recvfrom failed with error: %d\n", WSAGetLastError());
			#elif defined(__linux__)
			printf("(cmd-thread): recvfrom failed with error: %s(errno: %d)\n", strerror(errno), errno);
			#endif
			break;
		}

		auto enc_data = reinterpret_cast<EncryptedData*>(local_buffer);

		local_aes_wrapper.SetIv(enc_data->iv, 16);
		int ret = local_aes_wrapper.Decrypt(&local_buffer[16], recv_bytes - 16, &local_buffer[16]);

		if (ret <= 0) {
			printf("(cmd-thread): invalid command packet\n");
			continue;
		}

		auto cmd_pkt = reinterpret_cast<CmdStreamPacket*>(&local_buffer[16]);

		switch (cmd_pkt->cmd) 
		{
			case 0: 
				// Nothind to do
				// Ping command
				break;
			case 1:
				printf("(cmd-thread): capturing audio\n");
				m_capture->SetPlaybackState(true);
				break;
			case 2:
				printf("(cmd-thread): pausing audio capture\n");
				m_capture->SetPlaybackState(false);
				break;
			case 3:
				printf("(cmd-thread): stopping audio capture\n");
				m_capture->AsyncStopCapture();
				break;
		}

		m_random_gen.Generate(enc_data->iv, 16);
		local_aes_wrapper.SetIv(enc_data->iv, 16);

		uint8_t* data_ptr = (uint8_t*)(&enc_data->data);

		int data_size = local_aes_wrapper.Encrypt(reinterpret_cast<const byte*>(&local_buffer[16]), sizeof(CmdStreamPacket), data_ptr);
		int data_total_size = 16 + data_size;

		ret = sendto(m_cmd_socket, (const char*)local_buffer, data_total_size, 0, (sockaddr*)&remote_sockaddr, remote_addrlen);
		if (ret < 0) {
			#if defined(_WIN32)
			printf("(cmd-thread): sendto failed with error: %d\n", WSAGetLastError());
			#elif defined(__linux__)
			printf("(cmd-thread): sendto failed with error: %s(errno: %d)\n", strerror(errno), errno);
			#endif
			m_capture->AsyncStopCapture();
		}
	}

	#if defined(_WIN32)
	closesocket(m_cmd_socket);
	#elif defined(__linux__)
	close(m_cmd_socket);
	#endif
}

void AudioStream::t_connection_receiver()
{
	byte local_buffer[8192] = { 0 };

	printf("(cr-thread): waiting for Android app to connect...\n");

	while (true) {
		sockaddr_in remote_sockaddr{};
		socklen_t remote_sockaddr_size = sizeof(remote_sockaddr);

		int recv_bytes = recvfrom(m_connection_receiver_socket, (char*)local_buffer, 8192, 0, reinterpret_cast<sockaddr*>(&remote_sockaddr), &remote_sockaddr_size);

		if (recv_bytes <= 0) {
			#if defined(_WIN32)
			printf("(cr-thread): recvfrom failed with error: %d\n", WSAGetLastError());
			#elif defined(__linux__)
			printf("(cr-thread): recvfrom failed with error: %s(errno: %d)\n", strerror(errno), errno);
			#endif
			break;
		}

		in_addr remote_address = remote_sockaddr.sin_addr;

		char remote_sockaddr_name[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &remote_address, remote_sockaddr_name, INET_ADDRSTRLEN);

		m_capture->StopCapture();
		bool initialized = m_capture->InitializeAudioDevice(m_audio_fmt);

		if (!initialized) {
			printf("(err-cr-thread): failed to initialize audio device\n");
			continue;
		}

		auto enc_data = reinterpret_cast<EncryptedData*>(local_buffer);

		m_aes_wrapper.SetIv(enc_data->iv, 16);
		recv_bytes = m_aes_wrapper.Decrypt(&local_buffer[16], recv_bytes - 16, &local_buffer[16]);

		if (recv_bytes <= 0) {
			printf("(err-cr-thread): aes decrypt failed\n");
			continue;
		}

		auto recv_data = reinterpret_cast<StreamSettings*>(&local_buffer[16]);
		u_short remote_port = recv_data->android_port;

		printf("(cr-thread): sending audio samples to %s:%u\n", remote_sockaddr_name, remote_port);

		// Sending audio data settings to Android side
		EncryptedData* enc_metadata = reinterpret_cast<EncryptedData*>(local_buffer);

		StreamSettings st_settings{};
		st_settings.audio_format = m_capture->GetAudioFormat();
		st_settings.bits_per_sample = m_capture->GetBitsPerSample();
		st_settings.engine_period = m_capture->GetEnginePeriod();
		st_settings.n_channels = m_capture->GetChannels();
		st_settings.sample_rate = m_capture->GetSamplerate();
		st_settings.cmd_port = m_cmd_socket_port;

		m_random_gen.Generate(enc_metadata->iv, 16);
		m_aes_wrapper.SetIv(enc_metadata->iv, 16);

		byte* data_ptr = (byte*)(&enc_metadata->data);

		int dataSize = m_aes_wrapper.Encrypt(reinterpret_cast<const byte*>(&st_settings), sizeof(st_settings), data_ptr);
		int dataTotalSize = 16 + dataSize;

		int ret = sendto(m_connection_receiver_socket, (const char*)local_buffer, dataTotalSize, 0, (sockaddr*)&remote_sockaddr, remote_sockaddr_size);
		if (ret < 0) {
			#if defined(_WIN32)
			printf("(cr-thread): sendto failed with error: %d\n", WSAGetLastError());
			#elif defined(__linux__)
			printf("(cr-thread): sendto failed with error: %s(errno: %d)\n", strerror(errno), errno);
			#endif
			return;
		}

		remote_sockaddr.sin_port = htons(remote_port);
		connect(m_send_audio_socket, (sockaddr*)&remote_sockaddr, remote_sockaddr_size);

		m_capture->AsyncStartCapture();
	}

	#if defined(_WIN32)
	closesocket(m_connection_receiver_socket);
	#elif defined(__linux__)
	close(m_connection_receiver_socket);
	#endif
}