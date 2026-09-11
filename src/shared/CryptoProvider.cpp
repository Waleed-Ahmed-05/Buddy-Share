#include "shared/CryptoProvider.h"

#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include <memory>
#include <stdexcept>

namespace buddyshare::shared {

namespace {

constexpr std::size_t kSaltLength = 16;
constexpr std::size_t kAesKeyLength = 32;  // AES-256.
constexpr std::size_t kIvLength = 12;      // recommended GCM nonce length.
constexpr std::size_t kGcmTagLength = 16;
constexpr int kPbkdf2Iterations = 210000;  // OWASP-recommended floor for PBKDF2-HMAC-SHA256.
constexpr int kRsaKeyBits = 2048;

constexpr char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// RAII wrappers around OpenSSL's C handles so an early throw/return never leaks one (R.1).
using EvpCipherCtxPtr = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;
using EvpPkeyPtr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using EvpPkeyCtxPtr = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;
using BioPtr = std::unique_ptr<BIO, decltype(&BIO_free)>;

Bytes random_bytes(std::size_t length) {
    Bytes bytes(length);
    if (RAND_bytes(bytes.data(), static_cast<int>(length)) != 1) {
        throw std::runtime_error("Failed to generate random bytes.");
    }
    return bytes;
}

int base64_char_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

}  // namespace

DerivedKey CryptoProvider::derive_key(const std::string& password) const {
    Bytes salt = random_bytes(kSaltLength);
    Bytes key = derive_key_with_salt(password, salt);
    return DerivedKey{std::move(key), std::move(salt)};
}

Bytes CryptoProvider::derive_key_with_salt(const std::string& password, const Bytes& salt) const {
    Bytes key(kAesKeyLength);
    if (PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()), salt.data(),
                           static_cast<int>(salt.size()), kPbkdf2Iterations, EVP_sha256(),
                           static_cast<int>(key.size()), key.data()) != 1) {
        throw std::runtime_error("PBKDF2 key derivation failed.");
    }
    return key;
}

EncryptedContent CryptoProvider::encrypt(const Bytes& plaintext, const Bytes& key) const {
    Bytes iv = random_bytes(kIvLength);

    EvpCipherCtxPtr ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!ctx || EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv.size()),
                             nullptr) != 1 ||
        EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), iv.data()) != 1) {
        throw std::runtime_error("Failed to initialize AES-256-GCM encryption.");
    }

    Bytes ciphertext;
    if (!plaintext.empty()) {
        ciphertext.resize(plaintext.size());
        int out_len = 0;
        if (EVP_EncryptUpdate(ctx.get(), ciphertext.data(), &out_len, plaintext.data(),
                               static_cast<int>(plaintext.size())) != 1) {
            throw std::runtime_error("AES-256-GCM encryption failed.");
        }
        ciphertext.resize(static_cast<std::size_t>(out_len));
    }

    unsigned char final_block[EVP_MAX_BLOCK_LENGTH];
    int final_len = 0;
    if (EVP_EncryptFinal_ex(ctx.get(), final_block, &final_len) != 1) {
        throw std::runtime_error("AES-256-GCM encryption failed to finalize.");
    }
    ciphertext.insert(ciphertext.end(), final_block, final_block + final_len);

    Bytes tag(kGcmTagLength);
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, static_cast<int>(tag.size()),
                             tag.data()) != 1) {
        throw std::runtime_error("Failed to retrieve the AES-256-GCM authentication tag.");
    }
    // The tag rides along at the end of ciphertext, per this class's header comment.
    ciphertext.insert(ciphertext.end(), tag.begin(), tag.end());

    return EncryptedContent{std::move(iv), std::move(ciphertext)};
}

std::optional<Bytes> CryptoProvider::decrypt(const EncryptedContent& content,
                                              const Bytes& key) const {
    if (content.ciphertext.size() < kGcmTagLength) return std::nullopt;
    const std::size_t cipher_len = content.ciphertext.size() - kGcmTagLength;

    EvpCipherCtxPtr ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!ctx || EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(content.iv.size()),
                             nullptr) != 1 ||
        EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), content.iv.data()) != 1) {
        return std::nullopt;
    }

    Bytes plaintext;
    if (cipher_len > 0) {
        plaintext.resize(cipher_len);
        int out_len = 0;
        if (EVP_DecryptUpdate(ctx.get(), plaintext.data(), &out_len, content.ciphertext.data(),
                               static_cast<int>(cipher_len)) != 1) {
            return std::nullopt;
        }
        plaintext.resize(static_cast<std::size_t>(out_len));
    }

    Bytes tag(content.ciphertext.begin() + static_cast<std::ptrdiff_t>(cipher_len),
              content.ciphertext.end());
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, static_cast<int>(tag.size()),
                             tag.data()) != 1) {
        return std::nullopt;
    }

    unsigned char final_block[EVP_MAX_BLOCK_LENGTH];
    int final_len = 0;
    if (EVP_DecryptFinal_ex(ctx.get(), final_block, &final_len) <= 0) {
        // GCM authentication tag mismatch: wrong key and tampered ciphertext both land here,
        // indistinguishably, by design (never return unauthenticated plaintext).
        return std::nullopt;
    }
    plaintext.insert(plaintext.end(), final_block, final_block + final_len);

    return plaintext;
}

Bytes CryptoProvider::wrap_key(const Bytes& aes_key, const std::string& public_key_base64) const {
    const Bytes der = base64_decode(public_key_base64);
    const unsigned char* der_ptr = der.data();
    EvpPkeyPtr pkey(d2i_PUBKEY(nullptr, &der_ptr, static_cast<long>(der.size())), EVP_PKEY_free);
    if (!pkey) throw std::runtime_error("Invalid RSA public key.");

    EvpPkeyCtxPtr ctx(EVP_PKEY_CTX_new(pkey.get(), nullptr), EVP_PKEY_CTX_free);
    if (!ctx || EVP_PKEY_encrypt_init(ctx.get()) <= 0 ||
        EVP_PKEY_CTX_set_rsa_padding(ctx.get(), RSA_PKCS1_OAEP_PADDING) <= 0) {
        throw std::runtime_error("Failed to initialize RSA-OAEP encryption.");
    }

    std::size_t out_len = 0;
    if (EVP_PKEY_encrypt(ctx.get(), nullptr, &out_len, aes_key.data(), aes_key.size()) <= 0) {
        throw std::runtime_error("Failed to determine the RSA-OAEP output size.");
    }
    Bytes wrapped(out_len);
    if (EVP_PKEY_encrypt(ctx.get(), wrapped.data(), &out_len, aes_key.data(), aes_key.size()) <=
        0) {
        throw std::runtime_error("RSA-OAEP encryption failed.");
    }
    wrapped.resize(out_len);
    return wrapped;
}

std::optional<Bytes> CryptoProvider::unwrap_key(const Bytes& wrapped_key,
                                                 const std::string& private_key_pem) const {
    BioPtr bio(BIO_new_mem_buf(private_key_pem.data(), static_cast<int>(private_key_pem.size())),
               BIO_free);
    if (!bio) return std::nullopt;
    EvpPkeyPtr pkey(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr), EVP_PKEY_free);
    if (!pkey) return std::nullopt;

    EvpPkeyCtxPtr ctx(EVP_PKEY_CTX_new(pkey.get(), nullptr), EVP_PKEY_CTX_free);
    if (!ctx || EVP_PKEY_decrypt_init(ctx.get()) <= 0 ||
        EVP_PKEY_CTX_set_rsa_padding(ctx.get(), RSA_PKCS1_OAEP_PADDING) <= 0) {
        return std::nullopt;
    }

    std::size_t out_len = 0;
    if (EVP_PKEY_decrypt(ctx.get(), nullptr, &out_len, wrapped_key.data(), wrapped_key.size()) <=
        0) {
        return std::nullopt;
    }
    Bytes unwrapped(out_len);
    if (EVP_PKEY_decrypt(ctx.get(), unwrapped.data(), &out_len, wrapped_key.data(),
                          wrapped_key.size()) <= 0) {
        // Wrong keypair (e.g. another reader's private key): never return garbage key material.
        return std::nullopt;
    }
    unwrapped.resize(out_len);
    return unwrapped;
}

RsaKeyPair CryptoProvider::generate_rsa_keypair() {
    EvpPkeyPtr pkey(
        EVP_PKEY_Q_keygen(nullptr, nullptr, "RSA", static_cast<std::size_t>(kRsaKeyBits)),
        EVP_PKEY_free);
    if (!pkey) throw std::runtime_error("RSA key generation failed.");

    unsigned char* der = nullptr;
    const int der_len = i2d_PUBKEY(pkey.get(), &der);
    if (der_len <= 0) throw std::runtime_error("Failed to encode the RSA public key.");
    const Bytes public_key_der(der, der + der_len);
    OPENSSL_free(der);

    BioPtr bio(BIO_new(BIO_s_mem()), BIO_free);
    if (!bio || PEM_write_bio_PrivateKey(bio.get(), pkey.get(), nullptr, nullptr, 0, nullptr,
                                          nullptr) != 1) {
        throw std::runtime_error("Failed to encode the RSA private key.");
    }
    BUF_MEM* buffer = nullptr;
    BIO_get_mem_ptr(bio.get(), &buffer);

    RsaKeyPair keypair;
    keypair.public_key_base64 = base64_encode(public_key_der);
    keypair.private_key_pem.assign(buffer->data, buffer->length);
    return keypair;
}

std::optional<std::string> CryptoProvider::public_key_from_private(
    const std::string& private_key_pem) {
    BioPtr bio(BIO_new_mem_buf(private_key_pem.data(), static_cast<int>(private_key_pem.size())),
               BIO_free);
    if (!bio) return std::nullopt;
    EvpPkeyPtr pkey(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr), EVP_PKEY_free);
    if (!pkey) return std::nullopt;

    unsigned char* der = nullptr;
    const int der_len = i2d_PUBKEY(pkey.get(), &der);
    if (der_len <= 0) return std::nullopt;
    const Bytes public_key_der(der, der + der_len);
    OPENSSL_free(der);

    return base64_encode(public_key_der);
}

std::string CryptoProvider::base64_encode(const Bytes& data) {
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);

    std::size_t i = 0;
    while (i + 3 <= data.size()) {
        const unsigned int chunk = (static_cast<unsigned int>(data[i]) << 16) |
                                    (static_cast<unsigned int>(data[i + 1]) << 8) |
                                    static_cast<unsigned int>(data[i + 2]);
        out.push_back(kBase64Alphabet[(chunk >> 18) & 0x3F]);
        out.push_back(kBase64Alphabet[(chunk >> 12) & 0x3F]);
        out.push_back(kBase64Alphabet[(chunk >> 6) & 0x3F]);
        out.push_back(kBase64Alphabet[chunk & 0x3F]);
        i += 3;
    }

    const std::size_t remaining = data.size() - i;
    if (remaining == 1) {
        const unsigned int chunk = static_cast<unsigned int>(data[i]) << 16;
        out.push_back(kBase64Alphabet[(chunk >> 18) & 0x3F]);
        out.push_back(kBase64Alphabet[(chunk >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (remaining == 2) {
        const unsigned int chunk = (static_cast<unsigned int>(data[i]) << 16) |
                                    (static_cast<unsigned int>(data[i + 1]) << 8);
        out.push_back(kBase64Alphabet[(chunk >> 18) & 0x3F]);
        out.push_back(kBase64Alphabet[(chunk >> 12) & 0x3F]);
        out.push_back(kBase64Alphabet[(chunk >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

Bytes CryptoProvider::base64_decode(const std::string& text) {
    Bytes out;
    out.reserve((text.size() / 4) * 3);

    unsigned int buffer = 0;
    int bits_collected = 0;
    for (char c : text) {
        if (c == '=' || c == '\n' || c == '\r') continue;
        const int value = base64_char_value(c);
        if (value < 0) continue;  // ignore whitespace/unexpected characters rather than throwing.
        buffer = (buffer << 6) | static_cast<unsigned int>(value);
        bits_collected += 6;
        if (bits_collected >= 8) {
            bits_collected -= 8;
            out.push_back(static_cast<unsigned char>((buffer >> bits_collected) & 0xFF));
        }
    }
    return out;
}

}  // namespace buddyshare::shared
