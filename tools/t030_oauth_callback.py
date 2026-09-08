#!/usr/bin/env python3
"""T-030: receptor local mínimo del callback OAuth (una sola petición).

Escucha solo en loopback, valida `state` contra el valor generado por el
operador, muestra el `code` por consola local (para el intercambio manual
con curl según docs/T030-POC.md) y se apaga. No guarda nada en disco,
no registra nada en ficheros.

Uso:
    python tools/t030_oauth_callback.py --host localhost --port 3000 --state <valor>
"""
import argparse
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import parse_qs, urlparse


def main() -> int:
    ap = argparse.ArgumentParser(description="Callback OAuth local T-030")
    ap.add_argument("--host", default="localhost")
    ap.add_argument("--port", type=int, default=3000)
    ap.add_argument("--state", required=True, help="state generado previamente")
    args = ap.parse_args()

    expected = args.state
    result = {}

    class H(BaseHTTPRequestHandler):
        def do_GET(self):  # noqa: N802
            q = parse_qs(urlparse(self.path).query)
            result["code"] = (q.get("code") or [""])[0]
            result["state"] = (q.get("state") or [""])[0]
            result["error"] = (q.get("error") or [""])[0]
            ok = result["state"] == expected and bool(result["code"])
            body = ("OK: vuelve a la terminal." if ok else "ERROR: state o code invalidos."
                    ).encode()
            self.send_response(200 if ok else 400)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *a):  # silencio: nada a disco ni detalle
            pass

    srv = HTTPServer((args.host, args.port), H)
    print(f"escuchando en http://{args.host}:{args.port} (una peticion, Ctrl+C cancela)",
          flush=True)
    srv.handle_request()  # una sola petición y cierra el listener
    srv.server_close()

    if result.get("error"):
        print(f"proveedor devolvio error: {result['error']}", flush=True)
        return 2
    if result.get("state") != expected:
        print("state NO coincide: posible CSRF, se ignora la respuesta.", flush=True)
        return 3
    if not result.get("code"):
        print("sin code en el callback.", flush=True)
        return 4
    # Se muestra solo por consola local para el intercambio manual
    # inmediato; no se escribe en ningún fichero ni log.
    print("state OK. code recibido (usalo una vez y cierra esta terminal):", flush=True)
    print(result["code"], flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
