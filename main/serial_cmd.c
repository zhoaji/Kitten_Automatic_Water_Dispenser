#include "serial_cmd.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_system.h"
#include "wifi_mgr.h" // Needed to check status and clear config

static void serial_command_task(void *pvParameters)
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
    };
    
    uart_driver_install(UART_NUM_0, 4096, 4096, 10, NULL, 0);
    uart_param_config(UART_NUM_0, &uart_config);
    uart_set_pin(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    printf("\n");
    printf("===========================================\n");
    printf("   ESP32 LED Control - Serial Commands   \n");
    printf("===========================================\n");
    printf("Available commands:\n");
    printf("  reset     - Clear Wi-Fi config and restart\n");
    printf("  restart   - Restart ESP32\n");
    printf("  ap        - Switch to AP mode\n");
    printf("  status    - Show Wi-Fi status\n");
    printf("===========================================\n");
    printf("\n>");
    fflush(stdout);
    
    uint8_t* data = (uint8_t*) malloc(1024);
    char cmd_buffer[64];
    int cmd_index = 0;
    
    while (1) {
        int len = uart_read_bytes(UART_NUM_0, data, 1024, pdMS_TO_TICKS(100));
        
        for (int i = 0; i < len; i++) {
            char c = (char)data[i];
            
            if (c == '\n' || c == '\r') {
                printf("\n");
                if (cmd_index > 0) {
                    cmd_buffer[cmd_index] = '\0';
                    cmd_index = 0;
                    
                    if (strcmp(cmd_buffer, "reset") == 0 || strcmp(cmd_buffer, "reset_wifi") == 0) {
                        printf("Resetting Wi-Fi configuration...\n");
                        wifi_mgr_clear_config();
                        printf("Wi-Fi config cleared. Restarting...\n");
                        vTaskDelay(pdMS_TO_TICKS(500));
                        esp_restart();
                    } else if (strcmp(cmd_buffer, "restart") == 0) {
                        printf("Restarting ESP32...\n");
                        vTaskDelay(pdMS_TO_TICKS(500));
                        esp_restart();
                    } else if (strcmp(cmd_buffer, "status") == 0) {
                        printf("Wi-Fi Status: %s\n", wifi_mgr_is_connected() ? "Connected" : "Disconnected");
                        printf("SSID: %s\n", wifi_mgr_get_saved_ssid());
                        printf("Retry count: %d/%d\n", wifi_mgr_get_retry_num(), wifi_mgr_get_max_retry());
                    } else if (strcmp(cmd_buffer, "ap") == 0) {
                        printf("Switching to AP mode...\n");
                        wifi_mgr_clear_config();
                        vTaskDelay(pdMS_TO_TICKS(500));
                        esp_restart();
                    } else {
                        printf("Unknown command: %s\n", cmd_buffer);
                        printf("Available commands:\n");
                        printf("  reset     - Clear Wi-Fi config and restart\n");
                        printf("  restart   - Restart ESP32\n");
                        printf("  ap        - Switch to AP mode\n");
                        printf("  status    - Show Wi-Fi status\n");
                    }
                }
                printf(">");
                fflush(stdout);
            }
            else if (c == '\b' || c == 127) {
                if (cmd_index > 0) {
                    cmd_index--;
                    printf("\b \b");
                    fflush(stdout);
                }
            }
            else if (c >= 32 && c < 127) {
                if (cmd_index < sizeof(cmd_buffer) - 1) {
                    cmd_buffer[cmd_index++] = c;
                    printf("%c", c);
                    fflush(stdout);
                }
            }
        }
    }
    
    free(data);
}

void serial_cmd_init(void)
{
    xTaskCreate(serial_command_task, "serial_cmd", 4096, NULL, 5, NULL);
}
