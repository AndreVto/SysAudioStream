#pragma once

#include "aes.h"
#include "pkcs7_padding.h"

#include <string>
#include <cstring>

using byte = unsigned char;

#define AES_KEY_SIZE 16

class AESWrapper 
{
public:
    static const size_t KeySize() { return AES_KEY_SIZE; }

    AESWrapper();
    ~AESWrapper();

    void SetKey(byte* aes_key, size_t key_length);
    void SetIv(byte* aes_iv, size_t iv_length);

    void GenerateKey(std::string password);

    size_t Encrypt(const byte* buffer, size_t buffer_len, byte* encrypted_buffer);
    size_t Decrypt(byte* encrypted_buffer, size_t encrypted_buffer_len, byte* decrypted_buffer);

private:
    AES_ctx m_encrypt_ctx;
    AES_ctx m_decrypt_ctx;

    byte m_key[AES_KEY_SIZE];
    byte m_iv[AES_KEY_SIZE];

    size_t m_key_length;
    size_t m_iv_length;
};