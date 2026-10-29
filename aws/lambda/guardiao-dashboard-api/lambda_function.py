import json
import os
import time
import uuid
import boto3
from decimal import Decimal
from boto3.dynamodb.conditions import Key

dynamodb = boto3.resource("dynamodb")

ALLOWED_ORIGIN = "https://guardiao-dashboard.vercel.app"
ALLOWED_GROUPS = {"ADMIN_EMPRESA", "SOC_GUARDIAO"}
INCIDENTS_INDEX = "company_id-received_at-index"

AUDIT_TABLE_NAME = os.environ.get("AUDIT_TABLE_NAME", "").strip()


def json_decimal(value):
    if isinstance(value, Decimal):
        return int(value) if value % 1 == 0 else float(value)
    raise TypeError(f"Tipo não serializável: {type(value)}")


def make_response(status_code, body):
    return {
        "statusCode": status_code,
        "headers": {
            "Content-Type": "application/json",
            "Access-Control-Allow-Origin": ALLOWED_ORIGIN,
            "Access-Control-Allow-Methods": "GET,OPTIONS",
            "Access-Control-Allow-Headers": "Content-Type,Authorization"
        },
        "body": json.dumps(body, default=json_decimal)
    }


def get_jwt_claims(event):
    return (
        event.get("requestContext", {})
        .get("authorizer", {})
        .get("jwt", {})
        .get("claims", {})
    )


def get_user_groups(claims):
    groups_raw = claims.get("cognito:groups", [])

    if isinstance(groups_raw, list):
        return set(groups_raw)

    if isinstance(groups_raw, str):
        return {
            group.strip()
            for group in groups_raw.replace("[", "").replace("]", "").split(",")
            if group.strip()
        }

    return set()


def write_audit(company_id, claims, groups, action, result, path, context, details=None):
    """
    Registra ações do dashboard sem armazenar senha, token ou certificado.
    Falha de auditoria não deve interromper a consulta operacional.
    """
    if not AUDIT_TABLE_NAME:
        print("AUDITORIA_NAO_CONFIGURADA")
        return

    now = int(time.time())
    audit_id = f"{now:010d}#{uuid.uuid4().hex[:12]}"

    item = {
        "company_id": company_id,
        "audit_id": audit_id,
        "created_at": now,
        "action": action,
        "result": result,
        "path": path,
        "actor_sub": claims.get("sub", "desconhecido"),
        "groups": sorted(groups),
        "request_id": getattr(context, "aws_request_id", "desconhecido")
    }

    if details:
        item["details"] = details

    try:
        audit_table = dynamodb.Table(AUDIT_TABLE_NAME)
        audit_table.put_item(Item=item)
    except Exception as error:
        print(f"ERRO_AUDITORIA: {str(error)}")


def lambda_handler(event, context):
    method = event.get("requestContext", {}).get("http", {}).get("method", "GET")
    path = event.get("rawPath", "/incidents")

    if method == "OPTIONS":
        return make_response(204, {})

    if method != "GET":
        return make_response(405, {
            "message": "Método não permitido."
        })

    try:
        company_id = os.environ["COMPANY_ID"]
    except KeyError:
        print("ERRO_CONFIGURACAO: COMPANY_ID ausente")
        return make_response(500, {
            "message": "Configuração de empresa ausente."
        })

    claims = get_jwt_claims(event)
    user_groups = get_user_groups(claims)

    if not user_groups.intersection(ALLOWED_GROUPS):
        print("ACESSO_NEGADO_GRUPO", sorted(user_groups))

        write_audit(
            company_id=company_id,
            claims=claims,
            groups=user_groups,
            action="DASHBOARD_ACCESS",
            result="DENY",
            path=path,
            context=context,
            details={"reason": "GRUPO_NAO_AUTORIZADO"}
        )

        return make_response(403, {
            "message": "Você não possui permissão para acessar o dashboard."
        })

    try:
        # Rota: GET /devices
        if path == "/devices":
            devices_table = dynamodb.Table(
                os.environ["DEVICES_TABLE_NAME"]
            )

            result = devices_table.query(
                KeyConditionExpression=Key("company_id").eq(company_id)
            )

            devices = result.get("Items", [])
            devices.sort(
                key=lambda item: item.get("device_id", "")
            )

            write_audit(
                company_id=company_id,
                claims=claims,
                groups=user_groups,
                action="DASHBOARD_READ_DEVICES",
                result="ALLOW",
                path=path,
                context=context,
                details={"items_returned": len(devices)}
            )

            return make_response(200, {
                "company_id": company_id,
                "count": len(devices),
                "items": devices
            })

        # Rota: GET /incidents
        if path == "/incidents":
            alerts_table = dynamodb.Table(os.environ["TABLE_NAME"])

            result = alerts_table.query(
                IndexName=INCIDENTS_INDEX,
                KeyConditionExpression=Key("company_id").eq(company_id),
                ScanIndexForward=False,
                Limit=100
            )

            items = result.get("Items", [])

            alerts = [
                item for item in items
                if item.get("source") == "AWS_IOT_CORE"
            ]

            write_audit(
                company_id=company_id,
                claims=claims,
                groups=user_groups,
                action="DASHBOARD_READ_INCIDENTS",
                result="ALLOW",
                path=path,
                context=context,
                details={"items_returned": len(alerts)}
            )

            return make_response(200, {
                "company_id": company_id,
                "count": len(alerts),
                "items": alerts
            })

        return make_response(404, {
            "message": "Rota não encontrada."
        })

    except Exception as error:
        print(f"ERRO_DASHBOARD: {str(error)}")

        return make_response(500, {
            "message": "Não foi possível consultar os dados."
        })