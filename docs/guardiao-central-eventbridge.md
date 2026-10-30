# Guardião Central — ingestão de alertas

Fluxo validado:
EventBridge guardiao-central-bus
→ assinante guardiao-central-process-protection-alerts
→ Lambda guardiao-central-ingest
→ DynamoDB guardiao-central-incidents

Teste confirmado:
event_id: central-test-013
company_id: empresa-demo
reason: SETPOINT_BLOCKED