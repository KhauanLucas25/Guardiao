# Configuração AWS IoT Core

Região utilizada: us-east-2

## Tópicos MQTT

- Publicação de alertas: `autotemp/f01/alerts`
- Confirmação de recebimento: `autotemp/f01/ack`
- Comandos de simulação: `autotemp/f01/command`

## Fluxo

1. O ESP32 detecta uma condição de proteção.
2. O ESP32 publica um alerta no tópico `alerts`.
3. O operador confirma o recebimento pelo tópico `ack`.
4. O ESP32 registra `ALERTA_CONFIRMADO` no Monitor Serial.