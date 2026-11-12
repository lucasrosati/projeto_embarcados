#include "lcd1602.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "lcd";

// Ligação padrão do PCF8574 nesses módulos: P0=RS, P1=RW, P2=EN, P3=luz de fundo, P4..P7=D4..D7.
#define PIN_RS 0x01
#define PIN_EN 0x04
#define PIN_BL 0x08

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static bool s_ok;

static void write_nibble(uint8_t high_nibble, uint8_t rs)
{
    uint8_t d = (high_nibble & 0xF0) | rs | PIN_BL;
    uint8_t buf[2] = {d | PIN_EN, d};  // pulso em EN: o LCD lê na borda de descida
    i2c_master_transmit(s_dev, buf, sizeof(buf), 50);
}

static void send_byte(uint8_t value, uint8_t rs)
{
    write_nibble(value & 0xF0, rs);
    write_nibble((uint8_t)(value << 4), rs);
    esp_rom_delay_us(50);  // a maioria dos comandos leva ~37 us
}

bool lcd1602_init(int sda_pin, int scl_pin)
{
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = -1,  // escolhe uma porta livre
        .sda_io_num = sda_pin,
        .scl_io_num = scl_pin,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&bus_cfg, &s_bus) != ESP_OK) {
        ESP_LOGE(TAG, "falha ao iniciar o I2C");
        return false;
    }

    const uint16_t candidates[] = {0x27, 0x3F};
    uint16_t addr = 0;
    for (int i = 0; i < 2; i++) {
        if (i2c_master_probe(s_bus, candidates[i], 50) == ESP_OK) {
            addr = candidates[i];
            break;
        }
    }
    if (addr == 0) {
        ESP_LOGW(TAG, "LCD nao encontrado em 0x27 nem 0x3F: confira SDA=%d, SCL=%d, VCC e GND", sda_pin, scl_pin);
        return false;
    }

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 100000,
    };
    if (i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev) != ESP_OK) {
        return false;
    }

    // Sequência de inicialização do HD44780 para modo 4 bits (datasheet, fig. 24).
    vTaskDelay(pdMS_TO_TICKS(50));
    write_nibble(0x30, 0);
    esp_rom_delay_us(4500);
    write_nibble(0x30, 0);
    esp_rom_delay_us(4500);
    write_nibble(0x30, 0);
    esp_rom_delay_us(150);
    write_nibble(0x20, 0);  // agora em 4 bits
    send_byte(0x28, 0);     // 2 linhas, fonte 5x8
    send_byte(0x0C, 0);     // display ligado, sem cursor
    send_byte(0x06, 0);     // cursor avança para a direita
    send_byte(0x01, 0);     // limpa
    esp_rom_delay_us(2000);

    s_ok = true;
    ESP_LOGI(TAG, "LCD 16x2 encontrado em 0x%02X", addr);
    return true;
}

void lcd1602_write_line(uint8_t row, const char *text)
{
    if (!s_ok) {
        return;
    }
    send_byte(0x80 | (row ? 0x40 : 0x00), 0);  // posiciona no início da linha
    int i = 0;
    for (; i < 16 && text[i] != '\0'; i++) {
        send_byte((uint8_t)text[i], PIN_RS);
    }
    for (; i < 16; i++) {
        send_byte(' ', PIN_RS);
    }
}
