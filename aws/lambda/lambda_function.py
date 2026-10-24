import json
import os
import re
import time
import uuid
from decimal import Decimal

import boto3
from botocore.exceptions import ClientError


dynamodb = boto3.resource("dynamodb")
table = dynamodb.Table(os.environ["TABLE_NAME"])
sns = boto3.client("sns")

SNS_TOPIC_ARN = os.environ.get("SNS_TOPIC_ARN", "")
TTL_SECONDS = 7 * 24 * 60 * 60


def decimalize(value):
    if isinstance(value, float):
        return Decimal(str(value))
    if isinstance(value, dict):
        return {key: decimalize(item) for key, item in value.items()}
    if isinstance(value, list):
        return [decimalize(item) for item in value]
    return value


def response(status_code, body):
    return {
        "statusCode": status_code,
        "body": json.dumps(body, default=str)
    }


def lambda_handler(event, context):
    if isinstance(event, str):
        event = json.loads(event)

    if isinstance(event, dict) and "body" in event:
        body = event["body"]
        event = json.loads(body) if isinstance(body, str) else body

    if not isinstance(event, dict):
        return response(400, {"ok": False, "error": "EVENTO_INVALIDO"})

    device = str(event.get("device", "")).strip()
    reason = str(event.get("reason", event.get("event", ""))).strip()
    locked = event.get("locked", event.get("blocked", False))

    if not re.fullmatch(r"f\d{2,}", device):
        return response(400, {"ok": False, "error": "DEVICE_INVALIDO"})

    if not reason:
        return response(400, {"ok": False, "error": "MOTIVO_AUSENTE"})

    if locked is not True:
        return response(400, {"ok": False, "error": "ALERTA_SEM_BLOQUEIO"})

    now = int(time.time())
    event_id = str(event.get("event_id", "")).strip()

    if not event_id:
        event_id = f"{device}-{now}-{uuid.uuid4().hex[:8]}"

    item = {
        "pk": device,
        "sk": f"ALERT#{event_id}",
        "entity": "ALERT",
        "event_id": event_id,
        "received_at": now,
        "ttl": now + TTL_SECONDS,
        "reason": reason,
        "mode": str(event.get("mode", "PROTECTION")),
        "locked": True,
        "source": str(event.get("source", "AWS_IOT_CORE")),
        "payload": decimalize(event)
    }

    try:
        table.put_item(
            Item=item,
            ConditionExpression="attribute_not_exists(pk) AND attribute_not_exists(sk)"
        )
    except ClientError as error:
        if error.response["Error"]["Code"] == "ConditionalCheckFailedException":
            return response(200, {
                "ok": True,
                "duplicate": True,
                "event_id": event_id
            })
        raise

    if SNS_TOPIC_ARN:
        message = (
            "ALERTA DE PROTECAO - GUARDIAO\n\n"
            f"Dispositivo: {device}\n"
            f"Evento: {event_id}\n"
            f"Motivo: {reason}\n"
            "Status: comando remoto bloqueado; protecao local ativa.\n\n"
            "Consulte o painel AWS para investigar o evento."
        )

        sns.publish(
            TopicArn=SNS_TOPIC_ARN,
            Subject=f"[GUARDIAO] Protecao acionada - {device}",
            Message=message
        )

    return response(201, {
        "ok": True,
        "event_id": event_id,
        "email_sent": bool(SNS_TOPIC_ARN)
    })