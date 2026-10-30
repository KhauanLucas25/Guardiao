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
eventbridgev2 = boto3.client("eventbridgev2")

SNS_TOPIC_ARN = os.environ.get("SNS_TOPIC_ARN", "")
CENTRAL_EVENT_BUS_ARN = os.environ.get("CENTRAL_EVENT_BUS_ARN", "")
COMPANY_ID = os.environ.get("COMPANY_ID", "empresa-demo")

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


def publish_to_guardiao_central(item, context):
    if not CENTRAL_EVENT_BUS_ARN:
        print("EVENTBRIDGE_CENTRAL_NAO_CONFIGURADO")
        return False

    account_id = "local-study-account"

    if getattr(context, "invoked_function_arn", None):
        account_id = context.invoked_function_arn.split(":")[4]

    detail = {
        "company_id": COMPANY_ID,
        "event_id": item["event_id"],
        "device_id": item["pk"],
        "received_at": item["received_at"],
        "reason": item["reason"],
        "mode": item["mode"],
        "locked": item["locked"],
        "source_account": account_id
    }

    try:
        result = eventbridgev2.put_events(
            EventBusArn=CENTRAL_EVENT_BUS_ARN,
            Entries=[
                {
                    "Source": "guardiao.company",
                    "DetailType": "ProtectionAlert",
                    "Detail": json.dumps(detail)
                }
            ]
        )

        entry = result.get("Entries", [{}])[0]

        if entry.get("ErrorCode"):
            print("ERRO_EVENTBRIDGE_CENTRAL:", entry)
            return False

        print("EVENTBRIDGE_CENTRAL_PUBLICADO:", entry)
        return True

    except Exception as error:
        # A proteção local e o alerta principal não podem falhar
        # caso o encaminhamento central esteja indisponível.
        print(f"ERRO_EVENTBRIDGE_CENTRAL: {str(error)}")
        return False


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

    central_event_published = publish_to_guardiao_central(item, context)

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
        "email_sent": bool(SNS_TOPIC_ARN),
        "central_event_published": central_event_published
    })