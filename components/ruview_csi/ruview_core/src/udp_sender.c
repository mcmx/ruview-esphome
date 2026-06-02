/**
 * @file udp_sender.c
 * @brief UDP sender implementation with ENOMEM backoff.
 *
 * Ported from firmware/esp32-csi-node/main/stream_sender.c.
 * The ENOMEM backoff logic is preserved exactly.
 */

#include "ruview_core/src/udp_sender.h"
#include "ruview_core/src/compat_logging.h"

#include <string.h>
#include <errno.h>
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

static const char *TAG = "udp_sender";

static int                s_sock = -1;
static struct sockaddr_in s_dest_addr;

/* Counters */
static uint32_t s_raw_sent    = 0;
static uint32_t s_vitals_sent = 0;
static uint32_t s_errors      = 0;

/* ENOMEM backoff (preserved from original) */
static int64_t  s_backoff_until_us  = 0;
#define ENOMEM_COOLDOWN_MS   100
#define ENOMEM_LOG_INTERVAL  50
static uint32_t s_enomem_suppressed = 0;

/* ---- internal ---- */

static esp_err_t create_socket(const char *ip, uint16_t port)
{
    if (s_sock >= 0) {
        close(s_sock);
        s_sock = -1;
    }

    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_sock < 0) {
        ESP_LOGE(TAG, "socket() failed: errno %d", errno);
        return ESP_FAIL;
    }

    memset(&s_dest_addr, 0, sizeof(s_dest_addr));
    s_dest_addr.sin_family = AF_INET;
    s_dest_addr.sin_port   = htons(port);

    if (inet_pton(AF_INET, ip, &s_dest_addr.sin_addr) <= 0) {
        ESP_LOGE(TAG, "Invalid target IP: %s", ip);
        close(s_sock);
        s_sock = -1;
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "UDP sender ready: %s:%u", ip, (unsigned)port);
    return ESP_OK;
}

/* ---- Public API ---- */

esp_err_t udp_sender_init(const char *target_ip, uint16_t target_port)
{
    s_raw_sent    = 0;
    s_vitals_sent = 0;
    s_errors      = 0;
    s_backoff_until_us  = 0;
    s_enomem_suppressed = 0;
    return create_socket(target_ip, target_port);
}

esp_err_t udp_sender_update_target(const char *target_ip, uint16_t target_port)
{
    if (target_ip == NULL) return ESP_ERR_INVALID_ARG;

    memset(&s_dest_addr, 0, sizeof(s_dest_addr));
    s_dest_addr.sin_family = AF_INET;
    s_dest_addr.sin_port   = htons(target_port);

    if (inet_pton(AF_INET, target_ip, &s_dest_addr.sin_addr) <= 0) {
        ESP_LOGE(TAG, "Invalid target IP: %s", target_ip);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Target updated: %s:%u", target_ip, (unsigned)target_port);
    return ESP_OK;
}

int udp_sender_send_raw(const uint8_t *buf, size_t len)
{
    if (s_sock < 0) return -1;

    /* ENOMEM backoff */
    if (s_backoff_until_us > 0) {
        int64_t now = esp_timer_get_time();
        if (now < s_backoff_until_us) {
            s_enomem_suppressed++;
            if ((s_enomem_suppressed % ENOMEM_LOG_INTERVAL) == 1) {
                ESP_LOGW(TAG, "sendto suppressed (ENOMEM backoff, %lu dropped)",
                         (unsigned long)s_enomem_suppressed);
            }
            return -1;
        }
        ESP_LOGI(TAG, "ENOMEM backoff expired, resuming (%lu suppressed)",
                 (unsigned long)s_enomem_suppressed);
        s_backoff_until_us  = 0;
        s_enomem_suppressed = 0;
    }

    int sent = sendto(s_sock, buf, len, 0,
                      (struct sockaddr *)&s_dest_addr, sizeof(s_dest_addr));
    if (sent < 0) {
        s_errors++;
        if (errno == ENOMEM) {
            s_backoff_until_us = esp_timer_get_time()
                               + (int64_t)ENOMEM_COOLDOWN_MS * 1000;
            ESP_LOGW(TAG, "ENOMEM — backoff %d ms", ENOMEM_COOLDOWN_MS);
        } else {
            if (s_errors <= 5 || (s_errors % 100) == 0) {
                ESP_LOGW(TAG, "sendto failed: errno %d (total errors: %lu)",
                         errno, (unsigned long)s_errors);
            }
        }
        return -1;
    }

    s_raw_sent++;
    return sent;
}

esp_err_t udp_sender_send_vitals(const ruview_vitals_pkt_t *vitals)
{
    if (vitals == NULL) return ESP_ERR_INVALID_ARG;

    int sent = udp_sender_send_raw((const uint8_t *)vitals,
                                   sizeof(ruview_vitals_pkt_t));
    if (sent > 0) {
        s_vitals_sent++;
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t udp_sender_deinit(void)
{
    if (s_sock >= 0) {
        close(s_sock);
        s_sock = -1;
        ESP_LOGI(TAG, "UDP sender closed");
    }
    return ESP_OK;
}

void udp_sender_get_counters(uint32_t *raw_sent, uint32_t *vitals_sent,
                             uint32_t *errors)
{
    if (raw_sent)    *raw_sent    = s_raw_sent;
    if (vitals_sent) *vitals_sent = s_vitals_sent;
    if (errors)      *errors      = s_errors;
}
