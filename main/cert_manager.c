#include "cert_manager.h"
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "CERT_MANAGER";

// NVS namespace for certificate data
#define CERT_NVS_NAMESPACE "cert"
#define CERT_NVS_CERT_KEY "cert_pem"
#define CERT_NVS_KEY_KEY "key_pem"
#define CERT_NVS_CHAIN_KEY "chain_pem"
#define CERT_NVS_SELF_SIGNED_KEY "self_signed"
#define CERT_NVS_GENERATED_AT_KEY "generated_at"

// Certificate manager state
static bool cert_initialized = false;

/**
 * @brief Get current timestamp in seconds
 */
static uint32_t get_current_time(void) {
    return (uint32_t)time(NULL);
}

void cert_manager_init(void) {
    ESP_LOGI(TAG, "Initializing certificate manager");
    
    // Open NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(CERT_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return;
    }
    
    // Check if certificate exists
    size_t required_size = 0;
    err = nvs_get_blob(nvs_handle, CERT_NVS_CERT_KEY, NULL, &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "No certificate found - will need to generate or upload");
    } else if (err == ESP_OK) {
        ESP_LOGI(TAG, "Certificate found in NVS (size: %d bytes)", required_size);
    } else {
        ESP_LOGE(TAG, "Error checking for certificate: %s", esp_err_to_name(err));
    }
    
    nvs_close(nvs_handle);
    
    cert_initialized = true;
    ESP_LOGI(TAG, "Certificate manager initialized");
}

bool cert_exists(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(CERT_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return false;
    }
    
    size_t required_size = 0;
    err = nvs_get_blob(nvs_handle, CERT_NVS_CERT_KEY, NULL, &required_size);
    nvs_close(nvs_handle);
    
    return (err == ESP_OK && required_size > 0);
}

esp_err_t cert_get_info(cert_info_t* info) {
    if (!info) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Initialize structure
    memset(info, 0, sizeof(cert_info_t));
    
    // Open NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(CERT_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }
    
    // Check if certificate exists
    if (!cert_exists()) {
        nvs_close(nvs_handle);
        return ESP_ERR_NOT_FOUND;
    }
    
    // Get self-signed flag
    uint8_t is_self_signed = 0;
    err = nvs_get_u8(nvs_handle, CERT_NVS_SELF_SIGNED_KEY, &is_self_signed);
    if (err == ESP_OK) {
        info->is_self_signed = (is_self_signed != 0);
    }
    
    // Get generation timestamp
    uint32_t generated_at = 0;
    nvs_get_u32(nvs_handle, CERT_NVS_GENERATED_AT_KEY, &generated_at);
    
    nvs_close(nvs_handle);
    
    // TODO: Parse certificate to extract actual information
    // For now, set placeholder values
    strncpy(info->common_name, "doorstation.local", CERT_COMMON_NAME_MAX_LEN - 1);
    strncpy(info->issuer, "Self-Signed", CERT_ISSUER_MAX_LEN - 1);
    strncpy(info->not_before, "2024-01-01", CERT_DATE_MAX_LEN - 1);
    strncpy(info->not_after, "2034-01-01", CERT_DATE_MAX_LEN - 1);
    info->days_until_expiry = 3650;
    info->is_expired = false;
    info->is_expiring_soon = false;
    
    ESP_LOGI(TAG, "Certificate info retrieved: CN=%s, Self-Signed=%d", 
             info->common_name, info->is_self_signed);
    
    return ESP_OK;
}

esp_err_t cert_generate_self_signed(const char* common_name, uint32_t validity_days) {
    if (!common_name) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Generating self-signed certificate for CN=%s, validity=%d days", 
             common_name, validity_days);
    
    // TODO: Implement actual certificate generation using mbedtls
    // This is a placeholder implementation
    
    // For now, just store metadata
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(CERT_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }
    
    // Store self-signed flag
    err = nvs_set_u8(nvs_handle, CERT_NVS_SELF_SIGNED_KEY, 1);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }
    
    // Store generation timestamp
    uint32_t current_time = get_current_time();
    err = nvs_set_u32(nvs_handle, CERT_NVS_GENERATED_AT_KEY, current_time);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }
    
    // Commit changes
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Self-signed certificate metadata stored successfully");
    } else {
        ESP_LOGE(TAG, "Failed to store certificate metadata: %s", esp_err_to_name(err));
    }
    
    return err;
}

esp_err_t cert_validate(const char* cert_pem, const char* key_pem) {
    if (!cert_pem || !key_pem) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Validating certificate and private key");
    
    // TODO: Implement actual certificate validation using mbedtls
    // - Verify PEM format
    // - Verify private key matches certificate public key
    // - Verify certificate is not expired
    
    // For now, just do basic checks
    if (strlen(cert_pem) == 0 || strlen(key_pem) == 0) {
        ESP_LOGE(TAG, "Certificate or key is empty");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Check for PEM headers
    if (strstr(cert_pem, "-----BEGIN CERTIFICATE-----") == NULL) {
        ESP_LOGE(TAG, "Invalid certificate format - missing PEM header");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (strstr(key_pem, "-----BEGIN") == NULL) {
        ESP_LOGE(TAG, "Invalid key format - missing PEM header");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Certificate and key validation passed (basic checks)");
    
    return ESP_OK;
}

esp_err_t cert_upload_custom(const char* cert_pem, const char* key_pem, const char* chain_pem) {
    if (!cert_pem || !key_pem) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Uploading custom certificate");
    
    // Validate certificate and key
    esp_err_t err = cert_validate(cert_pem, key_pem);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Certificate validation failed");
        return err;
    }
    
    // Check sizes
    size_t cert_len = strlen(cert_pem);
    size_t key_len = strlen(key_pem);
    
    if (cert_len >= CERT_PEM_MAX_SIZE) {
        ESP_LOGE(TAG, "Certificate too large: %d bytes (max %d)", cert_len, CERT_PEM_MAX_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }
    
    if (key_len >= CERT_KEY_PEM_MAX_SIZE) {
        ESP_LOGE(TAG, "Private key too large: %d bytes (max %d)", key_len, CERT_KEY_PEM_MAX_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }
    
    // Open NVS
    nvs_handle_t nvs_handle;
    err = nvs_open(CERT_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }
    
    // Store certificate
    err = nvs_set_blob(nvs_handle, CERT_NVS_CERT_KEY, cert_pem, cert_len + 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store certificate: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    // Store private key
    err = nvs_set_blob(nvs_handle, CERT_NVS_KEY_KEY, key_pem, key_len + 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store private key: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    // Store certificate chain if provided
    if (chain_pem && strlen(chain_pem) > 0) {
        size_t chain_len = strlen(chain_pem);
        if (chain_len < CERT_CHAIN_PEM_MAX_SIZE) {
            err = nvs_set_blob(nvs_handle, CERT_NVS_CHAIN_KEY, chain_pem, chain_len + 1);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to store certificate chain: %s", esp_err_to_name(err));
                // Continue anyway - chain is optional
            }
        }
    }
    
    // Store self-signed flag (custom certs are not self-signed)
    err = nvs_set_u8(nvs_handle, CERT_NVS_SELF_SIGNED_KEY, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to store self-signed flag: %s", esp_err_to_name(err));
    }
    
    // Store upload timestamp
    uint32_t current_time = get_current_time();
    err = nvs_set_u32(nvs_handle, CERT_NVS_GENERATED_AT_KEY, current_time);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to store timestamp: %s", esp_err_to_name(err));
    }
    
    // Commit changes
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Custom certificate uploaded successfully");
    } else {
        ESP_LOGE(TAG, "Failed to commit certificate: %s", esp_err_to_name(err));
    }
    
    return err;
}

esp_err_t cert_get_pem(char** cert_pem, size_t* cert_size) {
    if (!cert_pem || !cert_size) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Open NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(CERT_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }
    
    // Get certificate size
    size_t required_size = 0;
    err = nvs_get_blob(nvs_handle, CERT_NVS_CERT_KEY, NULL, &required_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get certificate size: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    // Allocate memory
    char* buffer = (char*)malloc(required_size);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate memory for certificate");
        nvs_close(nvs_handle);
        return ESP_ERR_NO_MEM;
    }
    
    // Read certificate
    err = nvs_get_blob(nvs_handle, CERT_NVS_CERT_KEY, buffer, &required_size);
    nvs_close(nvs_handle);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read certificate: %s", esp_err_to_name(err));
        free(buffer);
        return err;
    }
    
    *cert_pem = buffer;
    *cert_size = required_size;
    
    ESP_LOGI(TAG, "Certificate PEM retrieved (%d bytes)", required_size);
    
    return ESP_OK;
}

esp_err_t cert_delete(void) {
    ESP_LOGI(TAG, "Deleting certificate");
    
    // Open NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(CERT_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }
    
    // Erase all certificate data
    nvs_erase_key(nvs_handle, CERT_NVS_CERT_KEY);
    nvs_erase_key(nvs_handle, CERT_NVS_KEY_KEY);
    nvs_erase_key(nvs_handle, CERT_NVS_CHAIN_KEY);
    nvs_erase_key(nvs_handle, CERT_NVS_SELF_SIGNED_KEY);
    nvs_erase_key(nvs_handle, CERT_NVS_GENERATED_AT_KEY);
    
    // Commit changes
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Certificate deleted successfully");
    } else {
        ESP_LOGE(TAG, "Failed to delete certificate: %s", esp_err_to_name(err));
    }
    
    return err;
}
