#include "cert_manager.h"
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/x509_csr.h"
#include "mbedtls/pk.h"
#include "mbedtls/rsa.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/bignum.h"
#include "mbedtls/oid.h"

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

// Task for certificate generation with dedicated stack
static void cert_generation_task(void* pvParameters) {
    (void)pvParameters;  // Unused parameter
    ESP_LOGI(TAG, "Certificate generation task started");
    
    esp_err_t err = cert_generate_self_signed("doorstation.local", 3650);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to generate self-signed certificate: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Self-signed certificate generated successfully");
    }
    
    // Task complete, delete itself
    vTaskDelete(NULL);
}

esp_err_t cert_ensure_exists(void) {
    // Check if certificate exists
    if (cert_exists()) {
        return ESP_OK;
    }
    
    // Generate self-signed certificate in a separate task with large stack
    ESP_LOGI(TAG, "No certificate found, generating self-signed certificate (this may take a few seconds)...");
    
    TaskHandle_t cert_task_handle = NULL;
    BaseType_t result = xTaskCreate(
        cert_generation_task,
        "cert_gen",
        16384,  // 16KB stack for certificate generation
        NULL,
        5,      // Priority
        &cert_task_handle
    );
    
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create certificate generation task");
        return ESP_FAIL;
    }
    
    // Wait for the task to complete
    // Poll until certificate exists or timeout
    int timeout_seconds = 30;
    for (int i = 0; i < timeout_seconds * 10; i++) {
        if (cert_exists()) {
            ESP_LOGI(TAG, "Certificate generation completed");
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    ESP_LOGE(TAG, "Certificate generation timed out");
    return ESP_ERR_TIMEOUT;
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
    
    // Check if certificate exists
    if (!cert_exists()) {
        return ESP_ERR_NOT_FOUND;
    }
    
    // Open NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(CERT_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }
    
    // Get self-signed flag
    uint8_t is_self_signed = 0;
    err = nvs_get_u8(nvs_handle, CERT_NVS_SELF_SIGNED_KEY, &is_self_signed);
    if (err == ESP_OK) {
        info->is_self_signed = (is_self_signed != 0);
    }
    
    // Get certificate size
    size_t cert_size = 0;
    err = nvs_get_blob(nvs_handle, CERT_NVS_CERT_KEY, NULL, &cert_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get certificate size: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    // Allocate buffer for certificate
    char* cert_pem = (char*)malloc(cert_size);
    if (!cert_pem) {
        ESP_LOGE(TAG, "Failed to allocate memory for certificate");
        nvs_close(nvs_handle);
        return ESP_ERR_NO_MEM;
    }
    
    // Read certificate from NVS
    err = nvs_get_blob(nvs_handle, CERT_NVS_CERT_KEY, cert_pem, &cert_size);
    nvs_close(nvs_handle);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read certificate: %s", esp_err_to_name(err));
        free(cert_pem);
        return err;
    }
    
    // Parse certificate using mbedtls
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);
    
    int ret = mbedtls_x509_crt_parse(&crt, (const unsigned char*)cert_pem, cert_size);
    free(cert_pem);
    
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to parse certificate: -0x%04x", -ret);
        mbedtls_x509_crt_free(&crt);
        return ESP_FAIL;
    }
    
    // Extract Common Name from subject using mbedtls helper
    ret = mbedtls_x509_dn_gets(info->common_name, CERT_COMMON_NAME_MAX_LEN, &crt.subject);
    if (ret < 0) {
        ESP_LOGW(TAG, "Failed to extract subject DN");
        info->common_name[0] = '\0';
    } else {
        // Try to extract just the CN= part if present
        char *cn_start = (char*)strstr(info->common_name, "CN=");
        if (cn_start) {
            cn_start += 3; // Skip "CN="
            char *cn_end = strchr(cn_start, ',');
            if (cn_end) {
                size_t cn_len = cn_end - cn_start;
                if (cn_len < CERT_COMMON_NAME_MAX_LEN) {
                    memmove(info->common_name, cn_start, cn_len);
                    info->common_name[cn_len] = '\0';
                }
            } else {
                // CN is the last or only field
                memmove(info->common_name, cn_start, strlen(cn_start) + 1);
            }
        }
    }
    
    // Extract issuer information
    ret = mbedtls_x509_dn_gets(info->issuer, CERT_ISSUER_MAX_LEN, &crt.issuer);
    if (ret < 0) {
        ESP_LOGW(TAG, "Failed to extract issuer DN");
        info->issuer[0] = '\0';
    } else {
        // Try to extract just the CN= part if present
        char *cn_start = (char*)strstr(info->issuer, "CN=");
        if (cn_start) {
            cn_start += 3; // Skip "CN="
            char *cn_end = strchr(cn_start, ',');
            if (cn_end) {
                size_t cn_len = cn_end - cn_start;
                if (cn_len < CERT_ISSUER_MAX_LEN) {
                    memmove(info->issuer, cn_start, cn_len);
                    info->issuer[cn_len] = '\0';
                }
            } else {
                // CN is the last or only field
                memmove(info->issuer, cn_start, strlen(cn_start) + 1);
            }
        }
    }
    
    // Format validity dates (not_before)
    struct tm timeinfo_buf;
    struct tm *timeinfo = gmtime_r((time_t*)&crt.valid_from.year, &timeinfo_buf);
    if (timeinfo) {
        timeinfo->tm_year = crt.valid_from.year - 1900;
        timeinfo->tm_mon = crt.valid_from.mon - 1;
        timeinfo->tm_mday = crt.valid_from.day;
        timeinfo->tm_hour = crt.valid_from.hour;
        timeinfo->tm_min = crt.valid_from.min;
        timeinfo->tm_sec = crt.valid_from.sec;
        strftime(info->not_before, CERT_DATE_MAX_LEN, "%Y-%m-%d %H:%M:%S", timeinfo);
    }
    
    // Format validity dates (not_after)
    timeinfo = gmtime_r((time_t*)&crt.valid_to.year, &timeinfo_buf);
    if (timeinfo) {
        timeinfo->tm_year = crt.valid_to.year - 1900;
        timeinfo->tm_mon = crt.valid_to.mon - 1;
        timeinfo->tm_mday = crt.valid_to.day;
        timeinfo->tm_hour = crt.valid_to.hour;
        timeinfo->tm_min = crt.valid_to.min;
        timeinfo->tm_sec = crt.valid_to.sec;
        strftime(info->not_after, CERT_DATE_MAX_LEN, "%Y-%m-%d %H:%M:%S", timeinfo);
    }
    
    // Calculate days until expiration
    time_t now = time(NULL);
    struct tm expiry_tm = {0};
    expiry_tm.tm_year = crt.valid_to.year - 1900;
    expiry_tm.tm_mon = crt.valid_to.mon - 1;
    expiry_tm.tm_mday = crt.valid_to.day;
    expiry_tm.tm_hour = crt.valid_to.hour;
    expiry_tm.tm_min = crt.valid_to.min;
    expiry_tm.tm_sec = crt.valid_to.sec;
    
    time_t expiry_time = mktime(&expiry_tm);
    
    // Calculate difference in days
    if (expiry_time > now) {
        int64_t diff_seconds = (int64_t)expiry_time - (int64_t)now;
        info->days_until_expiry = (uint32_t)(diff_seconds / (24 * 60 * 60));
        info->is_expired = false;
    } else {
        info->days_until_expiry = 0;
        info->is_expired = true;
    }
    
    // Check if expiring soon (< 30 days)
    info->is_expiring_soon = (!info->is_expired && info->days_until_expiry < CERT_EXPIRING_SOON_DAYS);
    
    // Clean up
    mbedtls_x509_crt_free(&crt);
    
    ESP_LOGI(TAG, "Certificate info retrieved: CN=%s, Issuer=%s, Days until expiry=%d, Expired=%d, Expiring soon=%d", 
             info->common_name, info->issuer, info->days_until_expiry, info->is_expired, info->is_expiring_soon);
    
    return ESP_OK;
}

esp_err_t cert_generate_self_signed(const char* common_name, uint32_t validity_days) {
    if (!common_name) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Generating self-signed certificate for CN=%s, validity=%d days", 
             common_name, validity_days);
    
    int ret = 0;
    esp_err_t err = ESP_OK;
    
    // Initialize mbedtls structures
    mbedtls_pk_context key;
    mbedtls_x509write_cert crt;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    
    mbedtls_pk_init(&key);
    mbedtls_x509write_crt_init(&crt);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    
    // Seed the random number generator
    const char *pers = "cert_gen";
    ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                 (const unsigned char *)pers, strlen(pers));
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_ctr_drbg_seed failed: -0x%04x", -ret);
        err = ESP_FAIL;
        goto cleanup;
    }
    
    // Generate 2048-bit RSA key pair
    ESP_LOGI(TAG, "Generating 2048-bit RSA key pair...");
    ret = mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_pk_setup failed: -0x%04x", -ret);
        err = ESP_FAIL;
        goto cleanup;
    }
    
    ret = mbedtls_rsa_gen_key(mbedtls_pk_rsa(key), mbedtls_ctr_drbg_random, &ctr_drbg, 2048, 65537);
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_rsa_gen_key failed: -0x%04x", -ret);
        err = ESP_FAIL;
        goto cleanup;
    }
    
    ESP_LOGI(TAG, "RSA key pair generated successfully");
    
    // Set certificate parameters
    mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&crt, &key);
    mbedtls_x509write_crt_set_issuer_key(&crt, &key);
    
    // Set subject name (Common Name)
    char subject_name[128];
    snprintf(subject_name, sizeof(subject_name), "CN=%s", common_name);
    ret = mbedtls_x509write_crt_set_subject_name(&crt, subject_name);
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_x509write_crt_set_subject_name failed: -0x%04x", -ret);
        err = ESP_FAIL;
        goto cleanup;
    }
    
    // Set issuer name (same as subject for self-signed)
    ret = mbedtls_x509write_crt_set_issuer_name(&crt, subject_name);
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_x509write_crt_set_issuer_name failed: -0x%04x", -ret);
        err = ESP_FAIL;
        goto cleanup;
    }
    
    // Set serial number (use random serial for self-signed)
    unsigned char serial_raw[16];
    ret = mbedtls_ctr_drbg_random(&ctr_drbg, serial_raw, sizeof(serial_raw));
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to generate random serial: -0x%04x", -ret);
        err = ESP_FAIL;
        goto cleanup;
    }
    
    ret = mbedtls_x509write_crt_set_serial_raw(&crt, serial_raw, sizeof(serial_raw));
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_x509write_crt_set_serial_raw failed: -0x%04x", -ret);
        err = ESP_FAIL;
        goto cleanup;
    }
    
    // Set validity period
    char not_before[16];
    char not_after[16];
    time_t now = time(NULL);
    struct tm timeinfo_buf;
    const struct tm *timeinfo = gmtime_r(&now, &timeinfo_buf);
    if (!timeinfo) {
        ESP_LOGE(TAG, "gmtime_r failed for not_before");
        err = ESP_FAIL;
        goto cleanup;
    }
    strftime(not_before, sizeof(not_before), "%Y%m%d%H%M%S", timeinfo);
    
    time_t expiry = now + ((time_t)validity_days * 24 * 60 * 60);
    timeinfo = gmtime_r(&expiry, &timeinfo_buf);
    if (!timeinfo) {
        ESP_LOGE(TAG, "gmtime_r failed for not_after");
        err = ESP_FAIL;
        goto cleanup;
    }
    strftime(not_after, sizeof(not_after), "%Y%m%d%H%M%S", timeinfo);
    
    ret = mbedtls_x509write_crt_set_validity(&crt, not_before, not_after);
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_x509write_crt_set_validity failed: -0x%04x", -ret);
        err = ESP_FAIL;
        goto cleanup;
    }
    
    // Set basic constraints (CA:FALSE)
    ret = mbedtls_x509write_crt_set_basic_constraints(&crt, 0, -1);
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_x509write_crt_set_basic_constraints failed: -0x%04x", -ret);
        err = ESP_FAIL;
        goto cleanup;
    }
    
    // Set key usage
    ret = mbedtls_x509write_crt_set_key_usage(&crt, MBEDTLS_X509_KU_DIGITAL_SIGNATURE | 
                                                     MBEDTLS_X509_KU_KEY_ENCIPHERMENT);
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_x509write_crt_set_key_usage failed: -0x%04x", -ret);
        err = ESP_FAIL;
        goto cleanup;
    }
    
    // Write certificate to PEM buffer
    unsigned char cert_buf[CERT_PEM_MAX_SIZE];
    memset(cert_buf, 0, sizeof(cert_buf));
    
    ret = mbedtls_x509write_crt_pem(&crt, cert_buf, sizeof(cert_buf),
                                     mbedtls_ctr_drbg_random, &ctr_drbg);
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_x509write_crt_pem failed: -0x%04x", -ret);
        err = ESP_FAIL;
        goto cleanup;
    }
    
    // Write private key to PEM buffer
    unsigned char key_buf[CERT_KEY_PEM_MAX_SIZE];
    memset(key_buf, 0, sizeof(key_buf));
    
    ret = mbedtls_pk_write_key_pem(&key, key_buf, sizeof(key_buf));
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_pk_write_key_pem failed: -0x%04x", -ret);
        err = ESP_FAIL;
        goto cleanup;
    }
    
    ESP_LOGI(TAG, "Certificate and key generated successfully");
    
    // Store certificate and key in NVS
    nvs_handle_t nvs_handle;
    err = nvs_open(CERT_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        goto cleanup;
    }
    
    // Store certificate
    size_t cert_len = strlen((char*)cert_buf);
    err = nvs_set_blob(nvs_handle, CERT_NVS_CERT_KEY, cert_buf, cert_len + 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store certificate: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        goto cleanup;
    }
    
    // Store private key
    size_t key_len = strlen((char*)key_buf);
    err = nvs_set_blob(nvs_handle, CERT_NVS_KEY_KEY, key_buf, key_len + 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store private key: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        goto cleanup;
    }
    
    // Store self-signed flag
    err = nvs_set_u8(nvs_handle, CERT_NVS_SELF_SIGNED_KEY, 1);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to store self-signed flag: %s", esp_err_to_name(err));
    }
    
    // Store generation timestamp
    uint32_t current_time = get_current_time();
    err = nvs_set_u32(nvs_handle, CERT_NVS_GENERATED_AT_KEY, current_time);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to store timestamp: %s", esp_err_to_name(err));
    }
    
    // Commit changes
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Self-signed certificate stored successfully in NVS");
    } else {
        ESP_LOGE(TAG, "Failed to commit certificate: %s", esp_err_to_name(err));
    }
    
cleanup:
    // Clean up mbedtls structures
    mbedtls_pk_free(&key);
    mbedtls_x509write_crt_free(&crt);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    
    return err;
}

/**
 * @brief Validate certificate chain integrity
 * 
 * @param cert Certificate to validate
 * @param chain_pem Certificate chain in PEM format (can be NULL)
 * @return esp_err_t ESP_OK if chain is valid, error code otherwise
 */
static esp_err_t validate_certificate_chain(mbedtls_x509_crt* cert, const char* chain_pem) {
    if (!cert) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // If no chain provided, certificate must be self-signed or we can't validate
    if (!chain_pem || strlen(chain_pem) == 0) {
        ESP_LOGI(TAG, "No certificate chain provided - skipping chain validation");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Validating certificate chain");
    
    // Parse certificate chain
    mbedtls_x509_crt chain;
    mbedtls_x509_crt_init(&chain);
    
    int ret = mbedtls_x509_crt_parse(&chain, (const unsigned char*)chain_pem, strlen(chain_pem) + 1);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to parse certificate chain: -0x%04x", -ret);
        mbedtls_x509_crt_free(&chain);
        return ESP_FAIL;
    }
    
    // Verify that the certificate can be validated against the chain
    // This checks that the certificate's issuer matches one of the chain certificates
    uint32_t flags = 0;
    ret = mbedtls_x509_crt_verify(cert, &chain, NULL, NULL, &flags, NULL, NULL);
    
    mbedtls_x509_crt_free(&chain);
    
    if (ret != 0) {
        ESP_LOGE(TAG, "Certificate chain validation failed: -0x%04x, flags: 0x%08x", -ret, flags);
        
        // Log specific validation failures
        if (flags & MBEDTLS_X509_BADCERT_EXPIRED) {
            ESP_LOGE(TAG, "Certificate has expired");
        }
        if (flags & MBEDTLS_X509_BADCERT_REVOKED) {
            ESP_LOGE(TAG, "Certificate has been revoked");
        }
        if (flags & MBEDTLS_X509_BADCERT_CN_MISMATCH) {
            ESP_LOGE(TAG, "Certificate CN mismatch");
        }
        if (flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED) {
            ESP_LOGE(TAG, "Certificate is not trusted");
        }
        
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Certificate chain validation passed");
    return ESP_OK;
}

esp_err_t cert_validate(const char* cert_pem, const char* key_pem) {
    if (!cert_pem || !key_pem) {
        ESP_LOGE(TAG, "Certificate or key is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Validating certificate and private key");
    
    // Check for empty strings
    size_t cert_len = strlen(cert_pem);
    size_t key_len = strlen(key_pem);
    
    if (cert_len == 0 || key_len == 0) {
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
    
    // Parse certificate using mbedtls
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);
    
    int ret = mbedtls_x509_crt_parse(&crt, (const unsigned char*)cert_pem, cert_len + 1);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to parse certificate: -0x%04x", -ret);
        mbedtls_x509_crt_free(&crt);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Certificate parsed successfully");
    
    // Check if certificate is expired
    time_t now = time(NULL);
    struct tm expiry_tm = {0};
    expiry_tm.tm_year = crt.valid_to.year - 1900;
    expiry_tm.tm_mon = crt.valid_to.mon - 1;
    expiry_tm.tm_mday = crt.valid_to.day;
    expiry_tm.tm_hour = crt.valid_to.hour;
    expiry_tm.tm_min = crt.valid_to.min;
    expiry_tm.tm_sec = crt.valid_to.sec;
    
    time_t expiry_time = mktime(&expiry_tm);
    
    if (expiry_time < now) {
        ESP_LOGE(TAG, "Certificate has expired");
        mbedtls_x509_crt_free(&crt);
        return ESP_FAIL;
    }
    
    // Check if certificate is not yet valid
    struct tm valid_from_tm = {0};
    valid_from_tm.tm_year = crt.valid_from.year - 1900;
    valid_from_tm.tm_mon = crt.valid_from.mon - 1;
    valid_from_tm.tm_mday = crt.valid_from.day;
    valid_from_tm.tm_hour = crt.valid_from.hour;
    valid_from_tm.tm_min = crt.valid_from.min;
    valid_from_tm.tm_sec = crt.valid_from.sec;
    
    time_t valid_from_time = mktime(&valid_from_tm);
    
    if (valid_from_time > now) {
        ESP_LOGE(TAG, "Certificate is not yet valid");
        mbedtls_x509_crt_free(&crt);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Certificate validity period is valid");
    
    // Parse private key using mbedtls
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    
    ret = mbedtls_pk_parse_key(&pk, (const unsigned char*)key_pem, key_len + 1, NULL, 0, NULL, NULL);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to parse private key: -0x%04x", -ret);
        mbedtls_pk_free(&pk);
        mbedtls_x509_crt_free(&crt);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Private key parsed successfully");
    
    // Verify that the private key matches the certificate's public key
    // Compare key types first
    mbedtls_pk_type_t cert_key_type = mbedtls_pk_get_type(&crt.pk);
    mbedtls_pk_type_t priv_key_type = mbedtls_pk_get_type(&pk);
    
    if (cert_key_type != priv_key_type) {
        ESP_LOGE(TAG, "Key type mismatch: cert=%d, key=%d", cert_key_type, priv_key_type);
        mbedtls_pk_free(&pk);
        mbedtls_x509_crt_free(&crt);
        return ESP_FAIL;
    }
    
    // Verify key pair by checking if they can be used together
    // We'll do this by comparing the public key from the certificate with the public key derived from the private key
    
    // Get the size needed for the public key export
    unsigned char cert_pub_buf[512];
    unsigned char key_pub_buf[512];
    size_t cert_pub_len = 0;
    size_t key_pub_len = 0;
    
    // Export public key from certificate
    ret = mbedtls_pk_write_pubkey_der(&crt.pk, cert_pub_buf, sizeof(cert_pub_buf));
    if (ret < 0) {
        ESP_LOGE(TAG, "Failed to export certificate public key: -0x%04x", -ret);
        mbedtls_pk_free(&pk);
        mbedtls_x509_crt_free(&crt);
        return ESP_FAIL;
    }
    cert_pub_len = ret;
    
    // Export public key from private key
    ret = mbedtls_pk_write_pubkey_der(&pk, key_pub_buf, sizeof(key_pub_buf));
    if (ret < 0) {
        ESP_LOGE(TAG, "Failed to export private key public component: -0x%04x", -ret);
        mbedtls_pk_free(&pk);
        mbedtls_x509_crt_free(&crt);
        return ESP_FAIL;
    }
    key_pub_len = ret;
    
    // Compare the public keys
    if (cert_pub_len != key_pub_len) {
        ESP_LOGE(TAG, "Public key size mismatch: cert=%d, key=%d", cert_pub_len, key_pub_len);
        mbedtls_pk_free(&pk);
        mbedtls_x509_crt_free(&crt);
        return ESP_FAIL;
    }
    
    // mbedtls_pk_write_pubkey_der writes from the end of the buffer backwards
    // So we need to compare from the end
    if (memcmp(cert_pub_buf + sizeof(cert_pub_buf) - cert_pub_len,
               key_pub_buf + sizeof(key_pub_buf) - key_pub_len,
               cert_pub_len) != 0) {
        ESP_LOGE(TAG, "Public key mismatch - private key does not match certificate");
        mbedtls_pk_free(&pk);
        mbedtls_x509_crt_free(&crt);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Private key matches certificate public key (verified via DER comparison)");
    
    // Clean up
    mbedtls_pk_free(&pk);
    mbedtls_x509_crt_free(&crt);
    
    ESP_LOGI(TAG, "Certificate and key validation passed");
    
    return ESP_OK;
}

esp_err_t cert_upload_custom(const char* cert_pem, const char* key_pem, const char* chain_pem) {
    if (!cert_pem || !key_pem) {
        ESP_LOGE(TAG, "Certificate or key is NULL");
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
    
    // Validate certificate chain if provided
    if (chain_pem && strlen(chain_pem) > 0) {
        size_t chain_len = strlen(chain_pem);
        
        if (chain_len >= CERT_CHAIN_PEM_MAX_SIZE) {
            ESP_LOGE(TAG, "Certificate chain too large: %d bytes (max %d)", 
                     chain_len, CERT_CHAIN_PEM_MAX_SIZE);
            return ESP_ERR_INVALID_SIZE;
        }
        
        // Check for PEM header in chain
        if (strstr(chain_pem, "-----BEGIN CERTIFICATE-----") == NULL) {
            ESP_LOGE(TAG, "Invalid certificate chain format - missing PEM header");
            return ESP_ERR_INVALID_ARG;
        }
        
        // Parse and validate the certificate chain
        ESP_LOGI(TAG, "Validating certificate chain (%d bytes)", chain_len);
        
        mbedtls_x509_crt cert;
        mbedtls_x509_crt_init(&cert);
        
        int ret = mbedtls_x509_crt_parse(&cert, (const unsigned char*)cert_pem, cert_len + 1);
        if (ret != 0) {
            ESP_LOGE(TAG, "Failed to parse certificate for chain validation: -0x%04x", -ret);
            mbedtls_x509_crt_free(&cert);
            return ESP_FAIL;
        }
        
        // Validate the chain integrity
        err = validate_certificate_chain(&cert, chain_pem);
        mbedtls_x509_crt_free(&cert);
        
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Certificate chain validation failed");
            return err;
        }
        
        ESP_LOGI(TAG, "Certificate chain validated successfully");
    } else {
        ESP_LOGI(TAG, "No certificate chain provided");
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
    
    ESP_LOGI(TAG, "Certificate stored in NVS (%d bytes)", cert_len);
    
    // Store private key
    err = nvs_set_blob(nvs_handle, CERT_NVS_KEY_KEY, key_pem, key_len + 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store private key: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    ESP_LOGI(TAG, "Private key stored in NVS (%d bytes)", key_len);
    
    // Store certificate chain if provided
    if (chain_pem && strlen(chain_pem) > 0) {
        size_t chain_len = strlen(chain_pem);
        err = nvs_set_blob(nvs_handle, CERT_NVS_CHAIN_KEY, chain_pem, chain_len + 1);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to store certificate chain: %s", esp_err_to_name(err));
            nvs_close(nvs_handle);
            return err;
        }
        ESP_LOGI(TAG, "Certificate chain stored in NVS (%d bytes)", chain_len);
    } else {
        // Clear any existing chain if none provided
        nvs_erase_key(nvs_handle, CERT_NVS_CHAIN_KEY);
        ESP_LOGI(TAG, "No certificate chain to store");
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
