# Observações de campo

Registro de comportamentos vistos nos testes com a placa real que **parecem suspeitos, mas não significam
necessariamente um ataque**. Serve para calibrar os limiares, explicar falsos positivos no relatório e lembrar que um
evento do IDS é um **indício**, não uma prova (spec §1 e §6).

Cada entrada traz o que foi visto, os dados, as explicações benignas possíveis e o que se concluiu. Quando a causa não
foi confirmada, isso é dito.

---

## 1. Deauth reais abaixo do limiar (12/11/2026)

**Contexto:** firmware da parte 1 (detector + LCD/LEDs), ESP32 conectado ao hotspot do celular do integrante (rede
própria), canal 6, RSSI entre -44 e -48 dBm. Teste do botão BOOT em andamento.

**O que foi visto:** numa única janela de 10 s, o sniffer contou **16 frames de deauthentication reais** (vindos do
rádio, não do teste sintético). Nas janelas anteriores e seguintes, a contagem foi 0.

```text
I (790631) telemetry: stats {..."seq":79,..."channel":6,"rssi":-45,"window_s":10,
  "counts":{"beacon":646,"probe_req":18,"deauth":16,"disassoc":0},"capture_queue_drops":0,...,"source":"live"}
```

Janelas vizinhas: `seq` 78 e 80 com `deauth: 0`. Os sintéticos não entram em `counts`. Eles aparecem só em
`synthetic_injected`, que ficou em 75 o tempo todo nessa janela. Logo, os 16 são reais.

**Resultado do IDS:** nenhum alerta. O limiar de `DEAUTH_BURST` é 20 por BSSID em 10 s, e 16 ficou abaixo. Esse era o
comportamento esperado.

**Explicações benignas possíveis (não confirmadas):**

- **O hotspot desconectando um aparelho:** o celular manda deauth quando um cliente sai, fica inativo ou o hotspot
  reorganiza conexões. O iPhone costuma repetir o frame várias vezes seguidas.
- **Um aparelho saindo da rede:** um notebook ou celular que desliga o Wi-Fi, troca de rede ou entra em suspensão pode
  mandar deauth ao AP.
- **Redes vizinhas no mesmo canal:** o sniffer escuta **todo o canal 6**, não só o hotspot. Roteadores vizinhos também
  desconectam clientes deles (por *band steering*, troca de canal, inatividade ou reinício).
- **Retransmissões:** se o destinatário não confirma o recebimento, o mesmo deauth é reenviado. Um único evento pode
  aparecer como vários frames.

**Limitação desta observação:** a parte 1 ainda não registra **qual BSSID** mandou cada deauth fora dos alertas. Não dá
para dizer se veio do hotspot ou de um vizinho.

**O que isso ensina:**

1. **Deauth isolado ou em pequena quantidade é normal** numa rede com uso real. Um IDS que alertasse a cada deauth
   geraria falsos positivos o tempo todo. Isso justifica o uso de limiar + janela + agrupamento por BSSID.
2. **O limiar de 20 deixou pouca margem:** 16 já apareceu em uso normal. Antes de baixar o limiar, é preciso medir
   mais. Talvez valha até subir, ou usar um limiar relativo ao histórico do BSSID.
3. **Agrupar por BSSID importa:** se a contagem fosse do canal inteiro, deauth de várias redes vizinhas somados
   poderiam passar de 20 sem nenhum ataque.

**Próximo passo sugerido:** a parte 2 vai salvar as estatísticas no SQLite. Com isso, dá para medir a distribuição de
deauth por janela ao longo de horas, e por BSSID se adicionarmos esse campo ao `stats`, para escolher o limiar com
dados.

---

## Outros comportamentos normais que podem parecer suspeitos

Referência rápida para interpretar os números do `stats` e do LCD.

| Observação | Por que costuma ser normal |
|---|---|
| **Beacons variando muito** (ex.: 285 numa janela, ~600 nas seguintes) | Cada AP manda ~10 beacons/s (intervalo de 102,4 ms). O total depende de quantas redes há no canal e do alcance. A primeira janela é parcial, porque o sniffer liga no meio dela. |
| **Picos de probe request** (ex.: 9 → 38) | Celulares procuram redes o tempo todo, sobretudo com a tela de Wi-Fi aberta ou ao desbloquear. Vários usam MAC aleatório, então um aparelho parece vários. |
| **Disassoc esporádico** | Aparelho entrando em suspensão, saindo da rede ou AP removendo cliente inativo. |
| **RSSI do AP oscilando** (ex.: -26 → -51 dBm) | Mover o celular ou a placa, ou pessoas passando entre eles. Com o hotspot no bolso, o sinal muda bastante. |
| **Falhas antes de conectar** (`reason=2`, `205` ou `201`) | `201` = AP não encontrado (fora de alcance ou hotspot sem anunciar a rede); `2`/`205` = falhas de autenticação na primeira tentativa, comuns com hotspot de iPhone. A reconexão automática resolve. |
| **`capture_queue_drops` > 0 em rajadas** | A fila tem 128 posições. Num canal muito cheio, ou durante transmissões do próprio ESP32, alguns frames podem se perder. Isso é limitação de captura (spec §2), não um evento de rede. |

---

## Como adicionar uma entrada

Copie o formato da entrada 1: data, contexto (rede, canal, RSSI, versão do firmware), trecho do log, o que o IDS fez,
explicações possíveis, o que foi confirmado ou não, e o que isso ensina. **Não declare causa sem confirmação.**
