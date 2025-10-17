#ifndef CERT_MANAGER_H
#define CERT_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Certificate constants
#define CERT_COMMON_NAME_MAX_LEN 64
#define CERT_ISSUER_MAX_LEN 128
#define CERT_DATE_MAX_LEN 32
#define CERT_PEM_MAX_SIZE 4096
#define CERT_KEY_PEM_MAX_SIZE 2048
#define CERT_CHAIN_PEM_MAX_SIZE 4096

// Certificate expiration warning thresholds
#define CERT_EXPIRING_SOON_DAYS 30
#define CERT_EXPIRING_CRITICAL_DAYS 7

// Certificate information structure
typedef struct {
    bool is_self_signed;
    char common_name[CERT_COMMON_NAME_MAX_LEN];
    char issuer[CERT_ISSUER_MAX_LEN];
    char not_before[CERT_DATE_MAX_LEN];
    char not_after[CERT_DATE_MAX_LEN];
    uint32_t days_until_expiry;
    bool is_expired;
    bool is_expiring_soon;  // < 30 days
} cert_info_t;

// Certificate storage structure
typedef struct {
    char cert_pem[CERT_PEM_MAX_SIZE];
    char key_pem[CERT_KEY_PEM_MAX_SIZE];
    char chain_pem[CERT_CHAIN_PEM_MAX_SIZE];  // Optional intermediate certs
    bool is_self_signed;
    uint32_t generated_at;
} certificate_storage_t;

/**
 * @brief Initialize certificate manager
 * 
 * Initializes the certificate management system and loads stored certificates.
 */
void cert_manager_init(void);

/**
 * @brief Get current certificate information
 * 
 * @param info Pointer to cert_info_t structure to fill
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t cert_get_info(cert_info_t* info);

/**
 * @brief Generate self-signed certificate
 * 
 * @param common_name Common Name (CN) for the certificate
 * @param validity_days Number of days the certificate is valid
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t cert_generate_self_signed(const char* common_name, uint32_t validity_days);

/**
 * @brief Upload custom certificate
 * 
 * @param cert_pem Certificate in PEM format
 * @param key_pem Private key in PEM format
 * @param chain_pem Certificate chain in PEM format (optional, can be NULL)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t cert_upload_custom(const char* cert_pem, const char* key_pem, const char* chain_pem);

/**
 * @brief Validate certificate and private key
 * 
 * @param cert_pem Certificate in PEM format
 * @param key_pem Private key in PEM format
 * @return esp_err_t ESP_OK if valid, error code otherwise
 */
esp_err_t cert_validate(const char* cert_pem, const char* key_pem);

/**
 * @brief Get certificate PEM data (for download)
 * 
 * @param cert_pem Pointer to receive certificate PEM string (caller must free)
 * @param cert_size Pointer to receive certificate size
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t cert_get_pem(char** cert_pem, size_t* cert_size);

/**
 * @brief Delete current certificate
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t cert_delete(void);

/**
 * @brief Check if certificate exists
 * 
 * @return true if certificate is configured
 * @return false if no certificate exists
 */
bool cert_exists(void);

#ifdef __cplusplus
}
#endif

#endif // CERT_MANAGER_H
