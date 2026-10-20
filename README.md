# Guardiao

Protótipo virtual de proteção para equipamentos de refrigeração, criado com ESP32 no Wokwi e AWS IoT Core.

## Problema

Um ataque ou falha no sistema central pode tentar alterar a operação de freezers e elevar a temperatura, causando perda de alimentos.

## Solução

O AutoTemp Guard aplica regras de segurança diretamente no ESP32. Quando detecta uma solicitação insegura, ele bloqueia a ação localmente e envia um alerta para a AWS IoT Core.

A proteção continua funcionando mesmo sem conexão com a nuvem.

## Tecnologias

- ESP32;
- Wokwi;
- C++;
- MQTT com TLS;
- AWS IoT Core;
- Display OLED SSD1306;
- Git e GitHub.

## Fluxo

1. O comando simulado `ATTACK` solicita uma mudança insegura no funcionamento do freezer.
2. O ESP32 identifica a solicitação.
3. A proteção local é ativada com o motivo `SETPOINT_BLOCKED`.
4. O ESP32 publica um alerta em `autotemp/f01/alerts`.
5. O operador confirma o recebimento em `autotemp/f01/ack`.
6. O Monitor Serial mostra `ALERTA_CONFIRMADO`.

## Estrutura

- `firmware/`: código e circuito Wokwi.
- `aws/`: documentação e modelo da política AWS IoT.
- `examples/`: exemplos seguros das mensagens MQTT e da configuração.
- `docs/`: arquitetura, testes e capturas de tela.

## Como testar

1. Abra os arquivos da pasta `firmware/` no Wokwi.
2. Inicie a simulação.
3. Cole a configuração local de Wi-Fi, endpoint e certificados no Monitor Serial.
4. Envie `ATTACK`.
5. No AWS IoT Core, assine `autotemp/f01/alerts`.
6. Copie o `event_id` recebido.
7. Publique a confirmação no tópico `autotemp/f01/ack`:

```json
{
  "event_id": "ID_DO_ALERTA",
  "received": true
}
Atualizado em 20/10/2026
