#include "AudioStream.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <fstream>
#include <random>
#include <algorithm>
#include <thread>

#ifdef _WIN32
#include <iphlpapi.h>
#elif defined(__linux__)
#include <sys/types.h>
#include <ifaddrs.h>
#endif


int main()
{
    setlocale(LC_ALL, "");

    std::random_device rd;
    std::mt19937 mte(rd());

    std::uniform_int_distribution<int> dist(0, 999999);

    std::string pair_code;
    int main_socket_port = 5540;
    std::string audio_format;

    std::ifstream fin;
    std::ofstream fout;

    fin.open("config.ini", std::ios::in);

    if (!fin.is_open()) {
        fout.open("config.ini", std::ios::out);

        main_socket_port = 5540;

        int number_code = dist(mte);
        std::string str_code = std::to_string(number_code);

        std::string default_audio_format = "pcm 16 48000";
        audio_format = default_audio_format;

        pair_code = std::string(6 - std::fmin(6, str_code.length()), '0') + str_code;

        if (fout.is_open()) {
            fout << pair_code << '\n';
            fout << main_socket_port << '\n';
            fout << default_audio_format << '\n';
            fout.close();
        }
        else {
            printf("(warning-main): unable to write 'config.ini' file. this pair code will be temporary for this session only\n\n");
        }
    }
    else {
        std::string temp_str;

        std::getline(fin, pair_code);
        std::getline(fin, temp_str);
        std::getline(fin, audio_format);

        main_socket_port = std::stoi(temp_str);

        fin.close();
    }

    printf("(main): listing network ip addresses\n\n");

    #if defined(_WIN32)
    ULONG ptr_len = 15000;
    PIP_ADAPTER_ADDRESSES all_addr_ptr = (IP_ADAPTER_ADDRESSES*)HeapAlloc(GetProcessHeap(), 0, ptr_len);

    DWORD ret = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, NULL, all_addr_ptr, &ptr_len);

    if (ret) {
        LPVOID other_err = nullptr;

        switch (ret)
        {
        case ERROR_ADDRESS_NOT_ASSOCIATED:
            printf("(err-main): ERROR_ADDRESS_NOT_ASSOCIATED\n");
            break;

        case ERROR_BUFFER_OVERFLOW:
            printf("(err-main): ERROR_BUFFER_OVERFLOW\n");
            break;

        case ERROR_INVALID_PARAMETER:
            printf("(err-main): ERROR_INVALID_PARAMETER\n");
            break;

        case ERROR_NOT_ENOUGH_MEMORY:
            printf("(err-main): ERROR_NOT_ENOUGH_MEMORY\n");
            break;

        case ERROR_NO_DATA:
            printf("(err-main): ERROR_NO_DATA\n");
            break;

        default:
            if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                NULL, ret, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                (LPTSTR)&other_err, 0, NULL)) {
                printf("(err-main): %s\n", other_err);
                LocalFree(other_err);
            }
            else {
                printf("(err-main): unknown error %ul", ret);
            }
        }

        return ret;
    }

    PIP_ADAPTER_ADDRESSES curr_adapter_ptr = all_addr_ptr;
    PIP_ADAPTER_UNICAST_ADDRESS addr_unicast;

    while (curr_adapter_ptr) {
        printf("(main): ");

        addr_unicast = curr_adapter_ptr->FirstUnicastAddress;
        while (addr_unicast) {
            auto local_sockaddr = reinterpret_cast<sockaddr_in*>(addr_unicast->Address.lpSockaddr);
            in_addr local_address = local_sockaddr->sin_addr;

            char local_sockaddr_name[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &local_address, local_sockaddr_name, INET_ADDRSTRLEN);

            printf("%s", local_sockaddr_name);

            addr_unicast = addr_unicast->Next;
        }

        printf("\n");

        curr_adapter_ptr = curr_adapter_ptr->Next;
    }

    if (all_addr_ptr)
        HeapFree(GetProcessHeap(), 0, all_addr_ptr);

    #elif defined(__linux__)

    struct ifaddrs *ifaddr;
    int family;
    char host[NI_MAXHOST];

    if (getifaddrs(&ifaddr) == -1) {
        printf("(main): getifaddrs");
    }

    for (struct ifaddrs *ifa = ifaddr; ifa != NULL;
            ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;

        family = ifa->ifa_addr->sa_family;

        if (family == AF_INET) {
            int ret = getnameinfo(ifa->ifa_addr,
                    (family == AF_INET) ? sizeof(struct sockaddr_in) :
                                            sizeof(struct sockaddr_in6),
                    host, NI_MAXHOST,
                    NULL, 0, NI_NUMERICHOST);
            if (ret != 0) {
                printf("(main): getnameinfo() failed: %s\n", gai_strerror(ret));
                break;
            }

            if (strcmp(host, "127.0.0.1") == 0)
                continue;

            printf("(main): %s\n", host);
        }
    }

    freeifaddrs(ifaddr);
    #endif

    printf("\n");

    printf("(main): socket port = %d\n", main_socket_port);
    printf("(main): pair code = %s\n", pair_code.c_str());
    printf("(main): audio config = %s\n\n", audio_format.c_str());

    std::unique_ptr<AudioStream> audio_stream = std::make_unique<AudioStream>(pair_code, main_socket_port, audio_format);
    bool initialized = audio_stream->Init();

    while (initialized)
    {
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }

    printf("(main): exiting\n\n");
    audio_stream.reset();

#ifdef _WIN32
    system("pause");
#endif

    return 0;
}