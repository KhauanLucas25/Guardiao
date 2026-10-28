import json
import os
import boto3
from decimal import Decimal
from boto3.dynamodb.conditions import Key

dynamodb = boto3.resource("dynamodb")

ALLOWED_ORIGIN = "https://guardiao-dashboard.vercel.app"

# Grupos Cognito autorizados a usar o dashboard.
ALLOWED_GROUPS = {"ADMIN_EMPRESA", "SOC_GUARDIAO"}


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


def get_user_groups(event):
    """
    Obtém grupos do JWT que já foi validado pelo API Gateway.
    """
    claims = (
        event.get("requestContext", {})
        .get("authorizer", {})
        .get("jwt", {})
        .get("claims", {})
    )

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


def lambda_handler(event, context):
    method = event.get("requestContext", {}).get("http", {}).get("method", "GET")
    path = event.get("rawPath", "/incidents")

    if method == "OPTIONS":
        return make_response(204, {})

    if method != "GET":
        return make_response(405, {
            "message": "Método não permitido."
        })

    user_groups = get_user_groups(event)

    if not user_groups.intersection(ALLOWED_GROUPS):
        print("ACESSO_NEGADO_GRUPO", sorted(user_groups))

        return make_response(403, {
            "message": "Você não possui permissão para acessar o dashboard."
        })

    try:
        # A empresa é definida pela variável da Lambda.
        # O navegador não pode escolher a empresa consultada.
        company_id = os.environ["COMPANY_ID"]

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

            return make_response(200, {
                "company_id": company_id,
                "count": len(devices),
                "items": devices
            })

        # Rota: GET /incidents
        if path == "/incidents":
            alerts_table = dynamodb.Table(os.environ["TABLE_NAME"])

            result = alerts_table.scan(Limit=100)
            items = result.get("Items", [])

            # Exibe somente alertas da empresa configurada.
            # Eventos antigos, sem company_id, não são exibidos por segurança.
            alerts = [
                item for item in items
                if item.get("source") == "AWS_IOT_CORE"
                and item.get("company_id") == company_id
            ]

            alerts.sort(
                key=lambda item: item.get(
                    "created_at",
                    item.get("received_at", item.get("timestamp", ""))
                ),
                reverse=True
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