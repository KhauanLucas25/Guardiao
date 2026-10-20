# Arquitetura — AutoTemp Guard

## Objetivo

O AutoTemp Guard é um protótipo virtual de monitoramento e proteção para equipamentos de refrigeração.
Ele demonstra uma camada local que continua protegendo o freezer mesmo se houver falha ou ataque ao sistema central.

## Componentes

- ESP32 virtual no Wokwi;
- Simulação de temperatura do ar e do alimento;
- LEDs para indicar estados do sistema;
- Display OLED para exibir temperatura, estado e conexão AWS;
- AWS IoT Core para receber alertas e enviar confirmações MQTT.

## Fluxo de proteção

1. Um comando inseguro, como `ATTACK`, tenta alterar o funcionamento do freezer.
2. O ESP32 avalia a solicitação localmente.
3. A proteção bloqueia a ação insegura.
4. Um alerta é enviado ao tópico `autotemp/f01/alerts`.
5. O operador confirma o recebimento no tópico `autotemp/f01/ack`.
6. O ESP32 registra `ALERTA_CONFIRMADO` no Monitor Serial.

## Princípio de segurança

A decisão de proteção acontece no ESP32. A AWS recebe o alerta, mas não é necessária para impedir o superaquecimento.