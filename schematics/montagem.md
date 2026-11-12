# Guia de montagem

Este guia segue a montagem real do grupo:

- Esquema no Cirkit Designer: [Esquemático do circuito.png](Esquemático%20do%20circuito.png)
- Foto da protoboard: [Foto da montagem física na protoboard.jpeg](Foto%20da%20montagem%20f%C3%ADsica%20na%20protoboard.jpeg)

As coordenadas (linha e coluna) seguem o esquema.

## Peças

- ESP32 de 38 pinos (ESP-WROOM-32, USB-C)
- 2 protoboards de 400 furos, lado a lado
- LCD 16x2 com módulo I2C (PCF8574)
- 1 LED vermelho e 1 LED verde (5 mm)
- 2 resistores de 300 Ω (faixas laranja, preto, marrom)
- Jumpers macho-macho e 4 macho-fêmea (para o LCD)

## Pinos usados

| ESP32 | Liga em | Função |
|---|---|---|
| 5V | trilho + | alimentação do LCD |
| GND | trilho − | terra comum |
| GPIO21 | LCD SDA | dados I2C |
| GPIO22 | LCD SCL | clock I2C |
| GPIO23 | resistor 300 Ω → LED vermelho | alerta |
| GPIO19 | resistor 300 Ω → LED verde | status |

No esquema do Cirkit os dois LEDs aparecem vermelhos. Na montagem física, o do GPIO19 é verde.

## Passo a passo (USB desconectado)

**1. ESP32.** Encaixe a placa por cima do trilho do meio, ocupando as linhas 1 a 19, com a antena para o lado da
linha 30:

- Pinos da esquerda (3V3 … 5V) na **coluna B** da protoboard da esquerda.
- Pinos da direita (GND … CLK) na **coluna I** da protoboard da direita.

Os jumpers entram na **coluna H**, ao lado do pino.

**2. Alimentação.**

- **GND:** H19 (pino GND, direita) → trilho **−** da protoboard da direita.
- **5V:** C1 da protoboard da esquerda (pino 5V, perto do USB) → trilho **+** da protoboard da direita.

**3. LCD** (jumpers macho-fêmea, lado fêmea no LCD):

| LCD | Vai em |
|---|---|
| GND | trilho − |
| VCC | trilho + |
| SDA | H14 (GPIO21) |
| SCL | H17 (GPIO22) |

**4. LED vermelho (GPIO23).**

- **Resistor:** de H18 até E20, atravessando o canal central.
- **LED:** perna **longa** em A20, perna curta em A21.
- **Jumper:** E21 → trilho −.

**5. LED verde (GPIO19).**

- **Resistor:** de H12 até E10.
- **LED:** perna **longa** em A10, perna curta em A9.
- **Jumper:** E9 → trilho −.

**6. Antes de ligar o USB, confira:**

- Nenhum fio liga o trilho + direto no trilho −.
- Nada está nos pinos SD0–SD3, CMD e CLK (GPIO6–11, ligados à flash).
- A perna longa de cada LED está do lado do resistor.

## Testes

1. **LEDs sem código:** com o USB desligado, mova a ponta do resistor que está na linha do GPIO (H18 ou H12) para a
   linha do pino **3V3** (B19 da protoboard da esquerda, use um furo livre da linha 19). Ligue o USB: o LED acende. Se
   não acender, ele está invertido. Desligue e volte a ponta do resistor para o lugar.
2. **LCD sem código:** ligue o USB e gire o potenciômetro azul atrás do LCD até aparecer uma fileira de 16
   quadradinhos. Se a luz de fundo não acender, confira o jumper (tampinha) nos pinos "LED" do módulo.
3. **Firmware (PoC de rádio):**

   ```sh
   cd esp32-esp8266
   idf.py set-target esp32
   idf.py menuconfig        # IDS Wi-Fi → SSID e senha (rede 2,4 GHz)
   idf.py build
   idf.py -p /dev/cu.usbserial-XXXX flash monitor   # porta: ls /dev/cu.*
   ```

   Funcionou se a serial mostrar `conectado, ip=...` e, a cada 10 s, `beacon=` maior que zero.

O firmware atual **ainda não usa o LCD nem os LEDs**. Por isso eles só podem ser testados como hardware (testes 1 e 2).

## Observação: LCD em 5 V

O LCD está alimentado em 5 V, que é o recomendado para ter contraste legível. O módulo I2C puxa as linhas SDA/SCL para
o VCC dele, e os pinos do ESP32 trabalham em 3,3 V e não são oficialmente tolerantes a 5 V. Essa ligação é muito usada
e costuma funcionar, mas fica registrada como limitação. O correto seria um conversor de nível lógico. Se o LCD ficar
legível em 3,3 V, dá para mover o jumper do trilho + do pino 5V para o 3V3.
