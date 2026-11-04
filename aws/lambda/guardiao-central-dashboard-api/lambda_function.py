import json
import os
from collections import Counter
from decimal import Decimal

import boto3


dynamodb = boto3.resource("dynamodb")
table = dynamodb.Table(os.environ["CENTRAL_TABLE_NAME"])

ALLOWED_ORIGIN = os.environ.get(
    "ALLOWED_ORIGIN",
    "https://guardiao-dashboard.vercel.app"
).rstrip("/")

REQUIRED_GROUP = "SOC_GUARDIAO"


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
    authorizer = (
        event.get("requestContext", {})
        .get("authorizer", {})
    )

    return (
        authorizer.get("jwt", {}).get("claims")
        or authorizer.get("claims")
        or {}
    )


def get_user_groups(claims):
    groups_raw = claims.get("cognito:groups", [])

    if isinstance(groups_raw, list):
        return {
            str(group).strip()
            for group in groups_raw
            if str(group).strip()
        }

    if isinstance(groups_raw, str):
        normalized = (
            groups_raw
            .replace("[", "")
            .replace("]", "")
            .replace(",", " ")
        )

        return {
            group.strip()
            for group in normalized.split()
            if group.strip()
        }

    return set()


def lambda_handler(event, context):
    method = (
        event.get("requestContext", {})
        .get("http", {})
        .get("method", "GET")
    )

    if method == "OPTIONS":
        return make_response(204, {})

    if method != "GET":
        return make_response(405, {
            "message": "Método não permitido."
        })

    claims = get_jwt_claims(event)
    user_groups = get_user_groups(claims)

    if REQUIRED_GROUP not in user_groups:
        print(
            "ACESSO_CENTRAL_NEGADO",
            {
                "sub": claims.get("sub", "desconhecido"),
                "groups": sorted(user_groups)
            }
        )

        return make_response(403, {
            "message": "Você não possui permissão para acessar o Guardião Central."
        })

    try:
        result = table.scan(Limit=100)
        incidents = result.get("Items", [])

        query_params = event.get("queryStringParameters") or {}
        company_id = query_params.get("company_id")

        if company_id:
            incidents = [
                item for item in incidents
                if item.get("company_id") == company_id
            ]

        incidents.sort(
            key=lambda item: item.get(
                "central_received_at",
                item.get("received_at", 0)
            ),
            reverse=True
        )

        companies = Counter(
            item.get("company_id", "empresa-desconhecida")
            for item in incidents
        )

        return make_response(200, {
            "count": len(incidents),
            "companies_count": len(companies),
            "companies": dict(companies),
            "items": incidents
        })

    except Exception as error:
        print(f"ERRO_CENTRAL_DASHBOARD: {str(error)}")

        return make_response(500, {
            "message": "Não foi possível consultar os incidentes centrais."
        })