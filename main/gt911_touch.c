// touch_dumper_interrupt_fixed.c - Fixed queue buffer issue
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "esp_lcd_touch_gt911.h"

static const char *TAG = "TOUCH_IRQ";

// Hardware Configuration
#define PIN_TOUCH_SCL                GPIO_NUM_8
#define PIN_TOUCH_SDA                GPIO_NUM_7
#define PIN_TOUCH_RST                GPIO_NUM_22
#define PIN_TOUCH_INT                GPIO_NUM_21
#define TOUCH_I2C_PORT               I2C_NUM_0
#define TOUCH_I2C_FREQ_HZ            (400000)

// Display resolution (portrait 480x800)
#define DISPLAY_H_RES                480
#define DISPLAY_V_RES                800

// Global handles
static esp_lcd_touch_handle_t touch_handle = NULL;
static QueueHandle_t touch_event_queue = NULL;
static uint32_t touch_count = 0;

// Interrupt callback (called from ISR context)
static void touch_interrupt_callback(esp_lcd_touch_handle_t tp)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    uint32_t notification = 1;  // Send a value, not NULL
    
    // Send notification to processing task
    xQueueSendFromISR(touch_event_queue, &notification, &xHigherPriorityTaskWoken);
    
    if (xHigherPriorityTaskWoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

// Touch processing task (runs when interrupt occurs)
static void touch_processing_task(void *pvParameters)
{
    esp_lcd_touch_point_data_t points[5];
    uint8_t point_cnt = 0;
    uint32_t notification;  // Buffer to receive queue data
    
    ESP_LOGI(TAG, "Touch processing task started - waiting for interrupts...");
    
    while (1) {
        // Wait for interrupt notification - MUST provide buffer for queue data
        if (xQueueReceive(touch_event_queue, &notification, portMAX_DELAY) == pdTRUE) {
            // Read touch data from controller
            esp_lcd_touch_read_data(touch_handle);
            
            // Get touch points
            esp_lcd_touch_get_data(touch_handle, points, &point_cnt, 5);
            
            if (point_cnt > 0) {
                touch_count++;
                ESP_LOGI(TAG, "[Touch #%lu] %d point(s):", touch_count, point_cnt);
                
                for (int i = 0; i < point_cnt; i++) {
                    ESP_LOGI(TAG, "  Point %d: X=%4d, Y=%4d, Strength=%3d", 
                             i, points[i].x, points[i].y, points[i].strength);
                }
                ESP_LOGI(TAG, "");  // Blank line for readability
            }
        }
    }
}

// Initialize GT911 touch controller with interrupt
static esp_err_t touch_init(void)
{
    ESP_LOGI(TAG, "Initializing GT911 touch controller (interrupt mode)");
    
    // Step 1: Create I2C master bus
    i2c_master_bus_config_t i2c_bus_cfg = {
        .i2c_port          = TOUCH_I2C_PORT,
        .sda_io_num        = PIN_TOUCH_SDA,
        .scl_io_num        = PIN_TOUCH_SCL,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    
    i2c_master_bus_handle_t i2c_bus;
    esp_err_t ret = i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C master bus creation failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "✓ I2C master bus created");
    
    // Step 2: Create panel IO for touch
    esp_lcd_panel_io_handle_t tp_io;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    tp_io_cfg.scl_speed_hz = TOUCH_I2C_FREQ_HZ;
    
    ret = esp_lcd_new_panel_io_i2c(i2c_bus, &tp_io_cfg, &tp_io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Touch panel IO creation failed: %s", esp_err_to_name(ret));
        i2c_del_master_bus(i2c_bus);
        return ret;
    }
    ESP_LOGI(TAG, "✓ Touch panel IO created");
    
    // Step 3: Configure GT911 with interrupt enabled
    esp_lcd_touch_io_gt911_config_t gt911_cfg = {
        .dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS,  // 0x5D
    };
    
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = DISPLAY_H_RES,
        .y_max = DISPLAY_V_RES,
        .rst_gpio_num = PIN_TOUCH_RST,
        .int_gpio_num = PIN_TOUCH_INT,
        .levels = { 
            .reset = 0,      // Active LOW
            .interrupt = 0   // Active LOW (from your working example)
        },
        .flags = { 
            .swap_xy = 0, 
            .mirror_x = 0, 
            .mirror_y = 0 
        },
        .interrupt_callback = touch_interrupt_callback,  // Enable interrupt mode
        .driver_data = &gt911_cfg,
    };
    
    ret = esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &touch_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GT911 driver creation failed: %s", esp_err_to_name(ret));
        esp_lcd_panel_io_del(tp_io);
        i2c_del_master_bus(i2c_bus);
        return ret;
    }
    
    ESP_LOGI(TAG, "✓ GT911 touch initialized (SDA=GPIO%d, SCL=GPIO%d, INT=GPIO%d)", 
             PIN_TOUCH_SDA, PIN_TOUCH_SCL, PIN_TOUCH_INT);
    return ESP_OK;
}

// Quick test to verify I2C communication (no init, just probe)
static void test_i2c_probe(void)
{
    ESP_LOGI(TAG, "\n=== Testing I2C Communication ===");
    
    // Create temporary I2C bus just for probe test
    i2c_master_bus_config_t i2c_bus_cfg = {
        .i2c_port          = TOUCH_I2C_PORT,
        .sda_io_num        = PIN_TOUCH_SDA,
        .scl_io_num        = PIN_TOUCH_SCL,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    
    i2c_master_bus_handle_t i2c_bus;
    esp_err_t ret = i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C bus for probe: %s", esp_err_to_name(ret));
        return;
    }
    
    // Probe for device at GT911 address
    ret = i2c_master_probe(i2c_bus, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS, 100);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✓ GT911 detected at address 0x%02X", 
                 ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS);
        
        // Create device to read product ID
        i2c_master_dev_handle_t dev;
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS,
            .scl_speed_hz = TOUCH_I2C_FREQ_HZ,
        };
        
        if (i2c_master_bus_add_device(i2c_bus, &dev_cfg, &dev) == ESP_OK) {
            uint8_t write_buf[2] = {0x81, 0x40};
            uint8_t read_buf[3] = {0};
            
            ret = i2c_master_transmit_receive(dev, write_buf, 2, read_buf, 3, 100);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Product ID: 0x%02X 0x%02X 0x%02X", 
                         read_buf[0], read_buf[1], read_buf[2]);
                if (read_buf[0] == 0x39 && read_buf[1] == 0x31 && read_buf[2] == 0x31) {
                    ESP_LOGI(TAG, "✓ GT911 confirmed!");
                }
            }
            i2c_master_bus_rm_device(dev);
        }
    } else {
        ESP_LOGE(TAG, "GT911 not detected at address 0x%02X", 
                 ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS);
    }
    
    i2c_del_master_bus(i2c_bus);
    ESP_LOGI(TAG, "");
}

// Status reporting task (optional)
static void status_report_task(void *pvParameters)
{
    uint32_t last_touch_count = 0;
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));  // Report every 10 seconds
        
        ESP_LOGI(TAG, "=== Status ===");
        ESP_LOGI(TAG, "Total touches: %lu", touch_count);
        ESP_LOGI(TAG, "Touches in last 10s: %lu", touch_count - last_touch_count);
        ESP_LOGI(TAG, "");
        
        last_touch_count = touch_count;
    }
}

// Main application
void app_main(void)
{
    ESP_LOGI(TAG, "======================================");
    ESP_LOGI(TAG, "GT911 Touch Dumper - INTERRUPT MODE");
    ESP_LOGI(TAG, "======================================\n");
    
    // Step 1: Quick probe test to verify hardware
    test_i2c_probe();
    
    // Step 2: Create queue for interrupt notifications
    // IMPORTANT: Queue item size must match what we send (uint32_t)
    touch_event_queue = xQueueCreate(20, sizeof(uint32_t));
    if (touch_event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create queue");
        return;
    }
    
    // Step 3: Initialize touch controller with interrupts
    esp_err_t ret = touch_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Touch initialization failed: %s", esp_err_to_name(ret));
        ESP_LOGI(TAG, "Check hardware connections and try again");
        return;
    }
    
    // Step 4: Create touch processing task (interrupt-driven)
    xTaskCreate(touch_processing_task, "touch_task", 4096, NULL, 10, NULL);
    
    // Step 5: Optional status reporting task
    xTaskCreate(status_report_task, "status_task", 2048, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "✅ Interrupt mode ready! Touch the screen to see coordinates.");
    ESP_LOGI(TAG, "   (Only prints when touch is detected - no polling)\n");
    
    // Main loop - just idle
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}