## Comando de teste `ATTACK`

O comando `ATTACK` representa uma tentativa simulada de alterar remotamente o funcionamento do equipamento de refrigeração.

Ao receber `ATTACK`, o simulador tenta solicitar uma condição insegura para o freezer, como elevar o setpoint de temperatura e acionar uma operação incompatível com a conservação dos alimentos.

O AutoTemp Guard não executa essa solicitação como controle normal. A lógica local identifica que o pedido é inseguro e ativa a proteção.

O comando não explora sistemas, não acessa equipamentos reais e não realiza atividades ofensivas. Ele existe apenas para testar o mecanismo de defesa do protótipo no Wokwi.

## Fluxo de proteção

1. O operador envia `ATTACK` pelo Monitor Serial ou pelo tópico MQTT de comandos.
2. O simulador solicita uma alteração insegura no funcionamento do freezer.
3. O ESP32 avalia a solicitação por regras locais.
4. O sistema bloqueia a alteração e muda para `PROTECTION`.
5. A razão do bloqueio é registrada como `SETPOINT_BLOCKED`.
6. Um único alerta `PROTECTION_ACTIVATED` é publicado em `autotemp/f01/alerts`.
7. O operador confirma o recebimento em `autotemp/f01/ack`.
8. O ESP32 registra `ALERTA_CONFIRMADO` no Monitor Serial.