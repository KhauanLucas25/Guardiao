# Estimativa de custos operacionais — Guardião

## Objetivo

Este documento apresenta uma estimativa de custo mensal para operar o Guardião em uma conta AWS paga, sem créditos promocionais. Os valores são referências para planejamento e podem variar conforme região, câmbio, mensagens, acessos ao painel, tamanho dos dados e retenção de logs.

## Arquitetura considerada

O cálculo considera o fluxo atual:

`Microcontrolador/Wokwi -> AWS IoT Core -> Lambda -> DynamoDB -> EventBridge -> Lambda central -> API Gateway -> Dashboard`

Serviços considerados:

- AWS IoT Core
- AWS Lambda
- Amazon DynamoDB em modo sob demanda
- Amazon EventBridge
- Amazon API Gateway HTTP API
- Amazon Cognito
- Amazon CloudWatch Logs

O cenário não inclui EC2, RDS, NAT Gateway, Load Balancer, VPC Endpoint, AWS Support Business ou telemetria contínua a cada poucos segundos.

## Premissas

- Dispositivos conectados 24 horas por dia via MQTT.
- O microcontrolador publica alertas quando um comando perigoso é bloqueado; não envia telemetria contínua em intervalos curtos.
- O dashboard é acessado por uma equipe de monitoramento durante o horário comercial.
- As tabelas DynamoDB usam capacidade sob demanda.
- As funções Lambda usam memória padrão e execuções curtas.
- Logs são mantidos por prazo curto, recomendado entre 7 e 14 dias.

## Estimativa por cenário

| Cenário | Uso considerado | Estimativa mensal em USD | Estimativa mensal em BRL* |
|---|---|---:|---:|
| Demonstração | 1 dispositivo, 2–5 usuários, poucos testes | US$ 0,10 a US$ 0,50 | R$ 0,55 a R$ 2,75 |
| Operação pequena | 10 dispositivos, equipe consultando diariamente, alertas moderados | US$ 1 a US$ 5 | R$ 5,50 a R$ 27,50 |
| Operação média | 100 dispositivos, equipe de monitoramento, alertas moderados | US$ 10 a US$ 30 | R$ 55 a R$ 165 |

\* Conversão ilustrativa usando US$ 1 ≈ R$ 5,50. A cobrança da AWS ocorre em dólar e a conversão da fatura pode variar.

## Como os custos são formados

### AWS IoT Core

Há cobrança pela conexão MQTT e pelas mensagens publicadas ou recebidas. Uma conexão contínua de um único dispositivo tem custo muito baixo; o volume de mensagens é o principal fator de crescimento.

No Guardião, o impacto é reduzido porque o dispositivo publica alertas de proteção, e não temperatura continuamente a cada poucos segundos.

### AWS Lambda

Lambda cobra por quantidade de execuções e duração. O Guardião executa funções quando recebe um alerta, processa o evento central ou atende uma consulta ao dashboard. Como as execuções são curtas, o custo tende a ser baixo em operação pequena.

### DynamoDB

No modo sob demanda, há cobrança por leitura, gravação e armazenamento. Cada alerta gera registros nas tabelas operacional e central. Consultas recorrentes ao painel também consomem leituras.

### API Gateway HTTP API

O custo depende das chamadas recebidas. Atualizações automáticas muito frequentes no painel podem elevar o volume. Para reduzir custo, recomenda-se atualizar a tela a cada 30–60 segundos, em vez de consultar continuamente.

### EventBridge

Cada incidente central publicado é um evento personalizado. Em volumes pequenos ou médios, seu custo é pouco representativo frente a uma arquitetura com muitos dispositivos e telemetria intensa.

### CloudWatch Logs

Logs podem se tornar o principal custo evitável se houver muitos `print`, erros repetidos ou retenção longa. Recomenda-se registrar eventos importantes, configurar retenção de 7 a 14 dias e evitar logs de debug em produção.

### Cognito

O Cognito gerencia login e grupos de acesso. Para uma equipe pequena, o custo tende a ser baixo; a cobrança depende do plano e da quantidade de usuários ativos mensais.

## O que mais aumenta a fatura

1. Telemetria enviada em intervalos muito curtos, por exemplo a cada segundo.
2. Muitos dispositivos conectados e publicando mensagens frequentemente.
3. Dashboard com atualização automática agressiva, como a cada poucos segundos.
4. Tabelas grandes consultadas por `scan` com frequência.
5. CloudWatch Logs com retenção longa ou excesso de logs de depuração.
6. Inclusão futura de serviços com custo fixo, como NAT Gateway, RDS, EC2 ou Load Balancer.

## Recomendações para manter o custo baixo

- Enviar alertas por exceção, como já ocorre no Guardião.
- Manter DynamoDB em modo sob demanda enquanto o volume for variável ou baixo.
- Consultar o dashboard em intervalos moderados.
- Manter retenção curta no CloudWatch Logs.
- Criar AWS Budgets com alerta em valores baixos, por exemplo US$ 1, US$ 5 e US$ 10.
- Revisar mensalmente o Cost Explorer agrupado por serviço.

## Referências oficiais

- AWS Lambda Pricing — https://aws.amazon.com/lambda/pricing/
- Amazon API Gateway Pricing — https://aws.amazon.com/api-gateway/pricing/
- Amazon DynamoDB Pricing — https://aws.amazon.com/dynamodb/pricing/
- Amazon EventBridge Pricing — https://aws.amazon.com/eventbridge/pricing/
- AWS IoT Core Pricing — https://aws.amazon.com/iot-core/pricing/
- Amazon Cognito Pricing — https://aws.amazon.com/cognito/pricing/

