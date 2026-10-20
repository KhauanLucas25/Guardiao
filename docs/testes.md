# Testes realizados

## Teste 1 — Inicialização local

**Objetivo:** verificar se o ESP32 inicia a simulação.

**Resultado esperado:** o Monitor Serial mostra `AUTOTEMP_LOCAL_OK`.

**Resultado obtido:** aprovado.

## Teste 2 — Proteção contra comando inseguro

**Passos:**

1. Iniciar a simulação no Wokwi.
2. Enviar `ATTACK` pelo Monitor Serial.
3. Observar o estado da proteção.

**Resultado esperado:** o sistema entra em `PROTECTION`, informa `SETPOINT_BLOCKED` e mantém o bloqueio local ativo.

**Resultado obtido:** aprovado.

## Teste 3 — Alerta MQTT

**Passos:**

1. Configurar Wi-Fi, endpoint e certificados no Monitor Serial.
2. Enviar `ATTACK`.
3. Assinar o tópico `autotemp/f01/alerts` no AWS IoT Core.

**Resultado esperado:** o console recebe um evento `PROTECTION_ACTIVATED`.

**Resultado obtido:** aprovado.

## Teste 4 — Confirmação do alerta

**Passos:**

1. Copiar o `event_id` recebido no alerta.
2. Publicar no tópico `autotemp/f01/ack`:

```json
{
  "event_id": "ID_DO_ALERTA",
  "received": true
}