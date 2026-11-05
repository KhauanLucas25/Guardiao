# Cenários de Ataque Simulados — Guardião

## Objetivo

O Guardião é um protótipo acadêmico de segurança operacional para sistemas de refrigeração conectados. Este documento descreve os cenários **simulados e controlados** utilizados para validar a proteção local do dispositivo e o fluxo de alertas na AWS.

Os testes não executam ataques contra sistemas reais, não coletam credenciais e não devem ser interpretados como uma avaliação completa de segurança.

## Ameaça simulada

O cenário principal representa uma tentativa de enviar remotamente uma configuração insegura ao controlador de refrigeração, como uma elevação indevida do setpoint ou uma alteração incompatível com o estado seguro do equipamento.

No protótipo, o comando `ATTACK` é apenas um gatilho de demonstração. Ele encaminha uma solicitação remota insegura pelo mesmo fluxo de comandos utilizado na simulação; não é malware e não explora vulnerabilidades reais.

## Possíveis origens da solicitação maliciosa

### 1. Uso indevido de credenciais MQTT

Uma credencial de comunicação exposta ou usada de forma indevida poderia permitir a publicação de comandos em tópicos MQTT destinados ao dispositivo. O teste considera o efeito operacional dessa publicação, sem usar credenciais comprometidas de verdade.

### 2. Operador remoto mal-intencionado ou com erro operacional

Uma pessoa com acesso legítimo ao sistema pode enviar uma configuração fora dos limites seguros, intencionalmente ou por engano. Esse cenário reforça que a validação não deve depender apenas de quem enviou o comando.

### 3. Sistema externo ou integração comprometida

Uma aplicação, automação ou serviço integrado ao ecossistema MQTT pode falhar ou ser comprometido e emitir comandos não confiáveis. O Guardião trata a solicitação recebida como não confiável até que os parâmetros sejam validados localmente.

### 4. Repetição ou alteração de comandos remotos

Um comando anteriormente válido pode ser reenviado fora de contexto, ou seus parâmetros podem ser alterados antes de chegar ao equipamento. A simulação demonstra a necessidade de validar cada solicitação no momento do recebimento.

## Resposta do Guardião

Quando a solicitação viola as regras de segurança, o controlador:

1. identifica parâmetros inseguros;
2. bloqueia a alteração localmente;
3. mantém o modo de proteção ativo;
4. gera um evento com o motivo `SETPOINT_BLOCKED`;
5. publica o alerta por MQTT no AWS IoT Core;
6. encaminha o incidente para processamento, armazenamento, dashboard centralizado e notificação por e-mail na AWS.

## Limitações do protótipo

- A validação foi realizada em simuladores e testes controlados;
- não houve teste em equipamento físico ou operação de longa escala;
- o projeto não substitui análise de risco, testes de invasão autorizados, monitoramento profissional ou requisitos de segurança industrial;
- certificados, chaves privadas e tokens não devem ser incluídos no repositório, vídeos ou capturas de tela.

## Tecnologias relacionadas

ESP32 virtualizado no Wokwi, C++ com Arduino Framework, MQTT sobre TLS, AWS IoT Core, AWS Lambda, Amazon DynamoDB, Amazon EventBridge, Amazon API Gateway, Amazon Cognito, Amazon SNS, CloudWatch Logs, HTML, CSS, JavaScript, GitHub e Vercel.
