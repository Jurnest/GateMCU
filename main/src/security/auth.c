#include <string.h>
#include "gateway.h"
#include "esp_system.h"
#include "nvs.h"
#include "mbedtls/sha256.h"

#define AUTH_NAMESPACE "gateway"
#define AUTH_SALT_KEY "admin_salt"
#define AUTH_HASH_KEY "admin_hash"

static bool load_auth(uint8_t salt[16], uint8_t hash[32])
{
    nvs_handle handle;
    size_t salt_len = 16, hash_len = 32;
    if (nvs_open(AUTH_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;
    esp_err_t err = nvs_get_blob(handle, AUTH_SALT_KEY, salt, &salt_len);
    if (err == ESP_OK) err = nvs_get_blob(handle, AUTH_HASH_KEY, hash, &hash_len);
    nvs_close(handle);
    return err == ESP_OK && salt_len == 16 && hash_len == 32;
}

bool gateway_auth_configured(void)
{
    uint8_t salt[16], hash[32];
    return load_auth(salt, hash);
}

esp_err_t gateway_auth_store(const char *password)
{
    if (!password || strlen(password) < 8 || strlen(password) > 63) return ESP_ERR_INVALID_ARG;
    uint8_t salt[16], hash[32], input[16 + 63];
    esp_fill_random(salt, sizeof(salt));
    memcpy(input, salt, sizeof(salt));
    size_t password_len = strlen(password);
    memcpy(input + sizeof(salt), password, password_len);
    if (mbedtls_sha256_ret(input, sizeof(salt) + password_len, hash, 0) != 0) return ESP_FAIL;

    nvs_handle handle;
    esp_err_t err = nvs_open(AUTH_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(handle, AUTH_SALT_KEY, salt, sizeof(salt));
    if (err == ESP_OK) err = nvs_set_blob(handle, AUTH_HASH_KEY, hash, sizeof(hash));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    memset(input, 0, sizeof(input));
    memset(hash, 0, sizeof(hash));
    return err;
}

bool gateway_auth_verify(const char *password)
{
    if (!password || !gateway_auth_configured()) return false;
    size_t password_len = strlen(password);
    if (password_len > 63) return false;
    uint8_t salt[16], stored[32], actual[32], input[16 + 63];
    if (!load_auth(salt, stored)) return false;
    memcpy(input, salt, sizeof(salt));
    memcpy(input + sizeof(salt), password, password_len);
    bool valid = false;
    if (mbedtls_sha256_ret(input, sizeof(salt) + password_len, actual, 0) == 0) {
        unsigned diff = 0;
        for (size_t i = 0; i < sizeof(actual); i++) diff |= actual[i] ^ stored[i];
        valid = diff == 0;
    }
    memset(input, 0, sizeof(input));
    memset(actual, 0, sizeof(actual));
    memset(stored, 0, sizeof(stored));
    return valid;
}
