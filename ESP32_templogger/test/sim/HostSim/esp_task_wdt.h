// Ersatz für den ESP-IDF-Task-Watchdog. sim::wdt() prüft, ob der überwachte
// Task jemals länger als timeout_ms ohne esp_task_wdt_reset() blieb.
#pragma once
#include "Arduino.h"

typedef int   esp_err_t;
typedef void *TaskHandle_t;
#define ESP_OK                 0
#define ESP_ERR_INVALID_STATE  0x103

typedef struct {
  uint32_t timeout_ms;
  uint32_t idle_core_mask;
  bool     trigger_panic;
} esp_task_wdt_config_t;

esp_err_t esp_task_wdt_init(const esp_task_wdt_config_t *cfg);
esp_err_t esp_task_wdt_reconfigure(const esp_task_wdt_config_t *cfg);
esp_err_t esp_task_wdt_add(TaskHandle_t task);
esp_err_t esp_task_wdt_reset(void);
