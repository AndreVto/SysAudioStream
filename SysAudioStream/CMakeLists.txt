cmake_minimum_required(VERSION 3.0.0)
project(SASLinux VERSION 0.1.0)

add_executable(SASLinux Main.cpp aes.cpp pkcs7_padding.cpp AESWrapper.cpp AudioStream.cpp WASAPICapture.cpp PulseAudioCapture.cpp)

target_link_libraries(SASLinux pulse)
target_compile_options(SASLinux PRIVATE -Ofast)
