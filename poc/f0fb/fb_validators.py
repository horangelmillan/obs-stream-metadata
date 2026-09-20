#!/usr/bin/env python3
"""FB-1 Step1 GREEN: validadores offline Facebook (T-070, PoC, no producto).

Solo stdlib. Sin red, sin secretos, sin persistencia.
Decisiones D1-D14 en docs/T0FB-RESEARCH.md; Graph API v25/v26.
"""

import urllib.parse

FB_TITLE_MAX = 254
GRAPH_VERSION = "v26.0"
OAUTH_DIALOG = f"https://www.facebook.com/{GRAPH_VERSION}/dialog/oauth"


def fb_title_ok(title):
    """Titulo 1-254 (D2). Retorna (ok, motivo)."""
    if not isinstance(title, str):
        return False, "tipo"
    if len(title) < 1:
        return False, "vacio"
    if len(title) > FB_TITLE_MAX:
        return False, "max-254"
    return True, ""


def fb_description_ok(desc):
    """Descripcion SI existe (D2). Sin limite inventado: solo tipo str."""
    if not isinstance(desc, str):
        return False, "tipo"
    return True, ""


def facebook_auth_url(client_id, redirect_uri, state, scope="publish_video"):
    """Flujo manual desktop (brief §1): dialog/oauth + state, sin secret."""
    q = urllib.parse.urlencode({
        "client_id": client_id,
        "redirect_uri": redirect_uri,
        "state": state,
        "scope": scope,
        "response_type": "code",
    })
    return f"{OAUTH_DIALOG}?{q}"


def fb_update_payload(title, description=""):
    """POST /{live-video-id}: solo title/description (D1)."""
    ok, why = fb_title_ok(title)
    if not ok:
        raise ValueError(f"titulo invalido: {why}")
    ok, why = fb_description_ok(description)
    if not ok:
        raise ValueError(f"descripcion invalida: {why}")
    payload = {"title": title}
    if description:
        payload["description"] = description
    return payload


def fb_privacy_payload(public=True):
    """Audiencia (D11): publico = privacy EVERYONE (hipotesis update)."""
    return {"privacy": {"value": "EVERYONE" if public else "SELF"}}


def fb_status_payload(go_live=False):
    """Go-live ON sobre video existente en vista previa (D6R)."""
    return {"status": "LIVE_NOW" if go_live else "UNPUBLISHED"}


def fb_end_live_payload():
    """Boton OFF (D7): POST /{id}?end_live_video=true."""
    return {"end_live_video": True}


def classify_fb(http_status, body):
    """Taxonomia §28 + tabla brief §5. body: dict con posible 'code'."""
    code = 0
    try:
        code = int(body.get("code", 0))
    except (AttributeError, ValueError, TypeError):
        code = 0
    if code in (1363120, 1363144):
        return "AUTHORIZATION"
    if code == 190:
        return "SESSION_EXPIRED"
    if code == 10:
        return "AUTHORIZATION"
    if code in (613, 4, 17):
        return "PROVIDER_RATE_LIMITED"
    if http_status == 200:
        return "Success"
    if http_status == 400:
        return "INVALID_REQUEST"
    if http_status in (401, 403):
        return "SESSION_EXPIRED" if http_status == 401 else "AUTHORIZATION"
    if http_status == 404:
        return "NOT_FOUND"
    if http_status == 409:
        return "CONFLICT"
    if http_status == 429:
        return "PROVIDER_RATE_LIMITED"
    if 500 <= http_status <= 599:
        return "PROVIDER_UNAVAILABLE"
    return "PROVIDER_UNAVAILABLE"


def fb_required_scopes(mode):
    """Scopes minimos (brief §1/D3). Jamas groups/email."""
    if mode == "page":
        return ["pages_manage_posts", "pages_read_engagement", "pages_show_list"]
    return ["publish_video"]
