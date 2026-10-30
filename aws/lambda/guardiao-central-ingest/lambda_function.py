import json
import os
import time
import uuid
from decimal import Decimal

import boto3
from botocore.exceptions import ClientError


dynamodb = boto3.resource("dynamodb")
table = dynamodb.Table(os.environ["CENTRAL_TABLE_NAME"])


def decimalize(value):
    if isinstance(value, float):
        return Decimal(str(value))
    if isinstance(value, dict):
        return {key: decimalize(item) for key, item in value.items()}
    if isinstance(value, list):
        return [decimalize(item) for item in value]
    return value


def process_event(event):
    if not isinstance(event, dict):
        return {"ok": False, "error": "EVENTO_CENTRAL_INVALIDO"}

    # Aceita CloudEvent (data) e evento padrão do EventBridge (detail).
    detail = event.get("detail") or event.get("data") or {}

    if not isinstance(detail, dict):
        return {"ok": False, "error": "EVENTO_CENTRAL_INVALIDO"}

    company_id = str(detail.get("company_id", "")).strip()
    event_id = str(detail.get("event_id", "")).strip()

    if not company_id or not event_id:
        return {"ok": False, "error": "COMPANY_OU_EVENT_ID_AUSENTE"}

    received_at = int(detail.get("received_at", time.time()))
    event_key = f"{received_at:010d}#{event_id}"

    item = {
        "company_id": company_id,
        "event_key": event_key,
        "entity": "CENTRAL_INCIDENT",
        "event_id": event_id,
        "received_at": received_at,
        "source_account": detail.get("source_account", "local-study-account"),
        "device_id": detail.get("device_id", detail.get("device", "")),
        "reason": detail.get("reason", ""),
        "mode": detail.get("mode", "PROTECTION"),
        "locked": bool(detail.get("locked", True)),
        "source": "GUARDIAO_CENTRAL_EVENTBRIDGE",
        "central_received_at": int(time.time()),
        "payload": decimalize(detail)
    }

    try:
        table.put_item(
            Item=item,
            ConditionExpression=(
                "attribute_not_exists(company_id) "
                "AND attribute_not_exists(event_key)"
            )
        )

        return {
            "ok": True,
            "company_id": company_id,
            "event_id": event_id
        }

    except ClientError as error:
        if error.response["Error"]["Code"] == "ConditionalCheckFailedException":
            return {
                "ok": True,
                "duplicate": True,
                "event_id": event_id
            }
        raise


def lambda_handler(event, context):
    print("EVENTO_CENTRAL_RECEBIDO:", json.dumps(event, default=str))

    # O assinante do EventBridge pode entregar um lote de eventos.
    events = event if isinstance(event, list) else [event]

    results = [process_event(item) for item in events]

    return {
        "ok": all(result.get("ok") for result in results),
        "processed": len(results),
        "results": results
    }