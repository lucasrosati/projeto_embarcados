// LCD 16x2 (HD44780) via módulo I2C PCF8574, em modo 4 bits.
// Não é thread-safe: só uma task (TaskTelemetry) deve chamar estas funções depois do init.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Procura o módulo em 0x27 e 0x3F. Devolve false se não achar (o IDS segue sem LCD).
bool lcd1602_init(int sda_pin, int scl_pin);

// Escreve uma linha inteira (0 ou 1), completando com espaços até 16 colunas.
void lcd1602_write_line(uint8_t row, const char *text);
