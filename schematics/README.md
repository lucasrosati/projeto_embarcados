# Esquemáticos

- `Esquemático do circuito.png`: esquema no Cirkit Designer. ESP32 de 38 pinos, LCD 16x2 I2C e dois LEDs
- `Foto da montagem física na protoboard.jpeg`: montagem real
- [montagem.md](montagem.md): guia de montagem e pinos

Pinos: LCD SDA=GPIO21, SCL=GPIO22, VCC=5V; LED vermelho (alerta)=GPIO23; LED verde (status)=GPIO19. Buzzer adiado
(falta transistor).

Status: circuito montado. O firmware atual ainda não aciona o LCD nem os LEDs.
