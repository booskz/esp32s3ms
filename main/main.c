#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "class/hid/hid_device.h"
#include "driver/gpio.h"
#include "led_strip.h"
#include "freertos/task.h"

static const char *TAG = "esp_dev";
#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_INOUT_DESC_LEN + 2 * TUD_HID_DESC_LEN)
const uint8_t hid_report_abs_mouse[] = {
    TUD_HID_REPORT_DESC_ABSMOUSE(HID_REPORT_ID(HID_ITF_PROTOCOL_MOUSE)),
};
const uint8_t hid_report_mouse[] = {
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(HID_ITF_PROTOCOL_MOUSE)),
};
const uint8_t customize[] = {
    TUD_HID_REPORT_DESC_SIMPLE_GAMEPAD_RX_ONLY(HID_REPORT_ID(0x03)),
};
const char *hid_string_descriptor[5] = {
    (char[]){0x09, 0x04},
    "super_ms0",
    "super0",
    "itf0",
    "aaaaaaaaaaaaaaaaaaaxfs",
};

static const uint8_t hid_configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 3, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(0, 4, false, sizeof(hid_report_mouse), 0x81, 8, 1),
    TUD_HID_DESCRIPTOR(1, 4, false, sizeof(hid_report_abs_mouse), 0x82, 8, 1),
    TUD_HID_INOUT_DESCRIPTOR(2, 4, false, sizeof(customize), 0x03, 0x83, 8, 1),
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{

    switch (instance)
    {
    case 0:
        return hid_report_mouse;
    case 1:
        return hid_report_abs_mouse;
    }
    return customize;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
}

uint8_t force[8];
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize)
{
    if (instance == 2)
    {
        memcpy(force, buffer + 1, 7);
    }
}

#define LED_GPIO GPIO_NUM_48

void app_main(void)
{

    // 初始化LED Strip
    led_strip_handle_t led_strip_init(void)
    {
        led_strip_config_t strip_config = {
            .strip_gpio_num = LED_GPIO,
            .max_leds = 1,
        };
        led_strip_rmt_config_t rmt_config = {
            .resolution_hz = 10 * 1000 * 1000, // 10MHz
        };
        led_strip_handle_t led_strip;
        ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
        return led_strip;
    }
    void led_strip_set_color(led_strip_handle_t led_strip, uint8_t r, uint8_t g, uint8_t b)
    {
        for (int i = 0; i < 1; i++)
        {
            ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, i, r, g, b));
        }
        ESP_ERROR_CHECK(led_strip_refresh(led_strip)); // 刷新数据
    }

    ESP_LOGI(TAG, "USB initialization");
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,
        .string_descriptor = hid_string_descriptor,
        .string_descriptor_count = sizeof(hid_string_descriptor) / sizeof(hid_string_descriptor[0]),
        .external_phy = false,
#if (TUD_OPT_HIGH_SPEED)
        .fs_configuration_descriptor = hid_configuration_descriptor,
        .hs_configuration_descriptor = hid_configuration_descriptor,
        .qualifier_descriptor = NULL,
#else
        .configuration_descriptor = hid_configuration_descriptor,
#endif
    };

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    ESP_LOGI(TAG, "USB initialization DONE");

    while (1)
    {
        if (tud_mounted())
        {
            break;
        }
    }

    hid_mouse_report_t report = {.buttons = 0, .x = 0, .y = 0, .wheel = 0, .pan = 0};
    hid_abs_mouse_report_t abs_report = {.buttons = 0, .x = 0, .y = 0, .wheel = 0, .pan = 0};

    led_strip_handle_t led_strip = led_strip_init();

    uint16_t wW = 0;
    uint16_t wH = 0;
    while (1)
    {
        report.x = 0;
        report.y = 0;

        abs_report.x = 0;
        abs_report.y = 0;

        switch (force[0])
        {
        case 11: // move
            report.x = force[1];
            report.y = force[2];
            tud_hid_n_report(0, HID_ITF_PROTOCOL_MOUSE, &report, sizeof(report));
            break;
        case 12:                                 // left press
            report.buttons |= MOUSE_BUTTON_LEFT; // 使用 |= 来添加左键状态
            tud_hid_n_report(0, HID_ITF_PROTOCOL_MOUSE, &report, sizeof(report));
            break;
        case 13:                                  // left release
            report.buttons &= ~MOUSE_BUTTON_LEFT; // 只释放左键，保持其他按键状态
            tud_hid_n_report(0, HID_ITF_PROTOCOL_MOUSE, &report, sizeof(report));
            break;
        case 14:                             // absmove
            uint8_t high_byte_ax = force[1]; // 高字节
            uint8_t low_byte_ax = force[2];  // 低字节
            uint16_t ax = ((uint16_t)high_byte_ax << 8) | low_byte_ax;
            uint8_t high_byte_ay = force[3]; // 高字节
            uint8_t low_byte_ay = force[4];  // 低字节
            uint16_t ay = ((uint16_t)high_byte_ay << 8) | low_byte_ay;

            abs_report.x = (uint16_t)(((float)ax / (float)wW) * 32767);
            abs_report.y = (uint16_t)(((float)ay / (float)wH) * 32767);
            tud_hid_n_report(1, HID_ITF_PROTOCOL_MOUSE, &abs_report, sizeof(abs_report));
            break;
        case 15:                                  // right press
            report.buttons |= MOUSE_BUTTON_RIGHT; // 使用 |= 来添加右键状态
            tud_hid_n_report(0, HID_ITF_PROTOCOL_MOUSE, &report, sizeof(report));
            break;
        case 16:                                   // mid press
            report.buttons |= MOUSE_BUTTON_MIDDLE; // 使用 |= 来添加中键状态
            tud_hid_n_report(0, HID_ITF_PROTOCOL_MOUSE, &report, sizeof(report));
            break;

        case 17:                                     // back press
            report.buttons |= MOUSE_BUTTON_BACKWARD; // 使用 |= 来添加后退键状态
            tud_hid_n_report(0, HID_ITF_PROTOCOL_MOUSE, &report, sizeof(report));
            break;
        case 18:                                    // forw press
            report.buttons |= MOUSE_BUTTON_FORWARD; // 使用 |= 来添加前进键状态
            tud_hid_n_report(0, HID_ITF_PROTOCOL_MOUSE, &report, sizeof(report));
            break;

        case 21: // open rgb
            led_strip_set_color(led_strip, force[1], force[2], force[3]);
            break;
        case 22: // close rgb
            led_strip_set_color(led_strip, 0, 0, 0);
            break;
        case 50:
            uint8_t high_byte_w = force[1]; // 高字节
            uint8_t low_byte_w = force[2];  // 低字节
            wW = ((uint16_t)high_byte_w << 8) | low_byte_w;
            uint8_t high_byte_h = force[3]; // 高字节
            uint8_t low_byte_h = force[4];  // 低字节
            wH = ((uint16_t)high_byte_h << 8) | low_byte_h;
            break;
        case 19:                                   // right release
            report.buttons &= ~MOUSE_BUTTON_RIGHT; // 只释放右键，保持其他按键状态
            tud_hid_n_report(0, HID_ITF_PROTOCOL_MOUSE, &report, sizeof(report));
            break;
        case 20:                                    // middle release
            report.buttons &= ~MOUSE_BUTTON_MIDDLE; // 只释放中键，保持其他按键状态
            tud_hid_n_report(0, HID_ITF_PROTOCOL_MOUSE, &report, sizeof(report));
            break;
        case 23:                                      // back release
            report.buttons &= ~MOUSE_BUTTON_BACKWARD; // 只释放后退键，保持其他按键状态
            tud_hid_n_report(0, HID_ITF_PROTOCOL_MOUSE, &report, sizeof(report));
            break;
        case 24:                                     // forward release
            report.buttons &= ~MOUSE_BUTTON_FORWARD; // 只释放前进键，保持其他按键状态
            tud_hid_n_report(0, HID_ITF_PROTOCOL_MOUSE, &report, sizeof(report));
            break;
        case 25:                // release all
            report.buttons = 0; // 释放所有按键
            tud_hid_n_report(0, HID_ITF_PROTOCOL_MOUSE, &report, sizeof(report));
            break;
        }
        force[0] = 0;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
