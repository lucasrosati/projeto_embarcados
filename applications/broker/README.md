# Broker MQTT (Mosquitto)

Mosquitto local no PC, na mesma rede do ESP32 (spec §3). Exige usuário e senha, e cada usuário só acessa os próprios
tópicos.

| Arquivo | No Git? | Conteúdo |
|---|---|---|
| `mosquitto.conf` | sim | porta 1883, sem acesso anônimo, ACL, log no terminal |
| `acl` | sim | `esp32-ids-01` só publica em `ids/esp32-ids-01/#`; `ids-backend` só lê `ids/#`; `ids-tester` só publica em `ids/synthetic-01/#` |
| `setup_credentials.py` | sim | gera as senhas e grava `passwd`, `applications/backend/.env` e o `sdkconfig` do firmware |
| `passwd` | **não** | hash das senhas (gerado) |

## Uso

```sh
brew install mosquitto                     # uma vez
python3 setup_credentials.py               # uma vez (ou para trocar as senhas); detecta o IP do PC
./start_broker.sh                          # sobe o broker; Ctrl+C para parar
```

- **Se o IP do PC mudar** (por exemplo, o hotspot reconectou), rode `python3 setup_credentials.py --broker-ip <novo IP>`
  e regrave o ESP32. Esse comando também gera senhas novas, então reinicie o back-end.
- **Segurança:** não abra a porta 1883 para a internet (sem *port forwarding*). Os tópicos e o contrato de dados estão em
  [docs/architecture.md](../../docs/architecture.md#tópicos-mqtt).
