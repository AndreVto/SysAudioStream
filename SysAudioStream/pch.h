//
// Header for standard system include files.
//

#pragma once

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>
#include <hstring.h>
#endif

#ifdef __linux__
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <string>
#include <arpa/inet.h>
#endif

#include <sstream>
#include <condition_variable>

#ifdef _WIN32
#include <winrt/Windows.Media.Devices.h>

#include <mmreg.h>
#include <mmdeviceapi.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <AudioClient.h>
#include <comdef.h>

#include "Common.h"
#endif

#include <forward_list>