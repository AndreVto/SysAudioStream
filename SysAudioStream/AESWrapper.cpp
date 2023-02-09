#include "AESWrapper.h"

#include <cmath>
#include <stdexcept>

AESWrapper::AESWrapper() 
{
    m_key_length = AES_KEY_SIZE;
    m_iv_length = AES_KEY_SIZE;
}

AESWrapper::~AESWrapper() 
{  
}

void AESWrapper::SetKey(byte* aes_key, size_t key_length) 
{
    if (m_key_length != key_length) {
        throw std::invalid_argument("key length different");
    }

    memcpy(m_key, aes_key, m_key_length);
}

void AESWrapper::SetIv(byte* aes_iv, size_t iv_length) 
{
    if (m_iv_length != iv_length) {
        throw std::invalid_argument("iv length different");        
    }

    memcpy(m_iv, aes_iv, m_iv_length);
}

void AESWrapper::GenerateKey(std::string password)
{
    // TODO: Generate keys properly from a password

    int size_trunc = std::fmin(password.size(), m_key_length);

    for (int i = 0; i < size_trunc; ++i) {
        m_key[i] = password[i];
    }

    byte padding_byte = m_key[(size_trunc - 1)];

    for (int i = size_trunc; i < m_key_length; ++i) {
        m_key[i] = padding_byte;
    }
}

size_t AESWrapper::Encrypt(const byte* buffer, size_t buffer_len, byte* encrypted_buffer) 
{
    int new_buf_size = buffer_len;

    if (buffer_len % AES_BLOCKLEN) {
        // Make the length multiple of 16 bytes
        new_buf_size += AES_BLOCKLEN - (buffer_len % AES_BLOCKLEN); 
    }
    else {
        new_buf_size += AES_BLOCKLEN;
    }

    memcpy(encrypted_buffer, buffer, buffer_len);

    pkcs7_padding_pad_buffer(encrypted_buffer, buffer_len, new_buf_size, AES_BLOCKLEN);

    AES_init_ctx_iv(&m_encrypt_ctx, m_key, m_iv);
    AES_CBC_encrypt_buffer(&m_encrypt_ctx, encrypted_buffer, new_buf_size);

    return new_buf_size;
}

size_t AESWrapper::Decrypt(byte* encrypted_buffer, size_t encrypted_buffer_len, byte* decrypted_buffer) 
{
    AES_init_ctx_iv(&m_decrypt_ctx, m_key, m_iv);
    AES_CBC_decrypt_buffer(&m_decrypt_ctx, encrypted_buffer, encrypted_buffer_len);

    size_t actualDataLength = pkcs7_padding_data_length(encrypted_buffer, encrypted_buffer_len, AES_BLOCKLEN);

    for (int i = 0; i < actualDataLength;i++) {
        decrypted_buffer[i] = encrypted_buffer[i];
    }

    return actualDataLength;
}