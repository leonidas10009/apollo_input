"""Local visual tester server for ApolloInput Nova.

The browser renders the controller, while this server exposes the C++ controller
tester so the page can create virtual pads, run self-tests, measure drift and
inspect connected devices.

The server binds to 127.0.0.1 only and is intended for local diagnostics.
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
import threading
import time
import webbrowser
from collections import deque
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

HERE = Path(__file__).resolve().parent


def find_tester() -> Path:
    """Locate the controller tester in a standalone or Apollo staging checkout."""
    env = os.environ.get("APOLLO_TESTER_EXE")
    if env:
        return Path(env).expanduser().resolve()
    candidates = [
        HERE.parent / "bin" / "apollo-input-controller-tester.exe",
        HERE.parent / "cmake-build-apollo-input" / "tools" / "apollo-input-controller-tester.exe",
        HERE.parent / "build" / "tools" / "apollo-input-controller-tester.exe",
        HERE / "bin" / "apollo-input-controller-tester.exe",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate.resolve()
    return candidates[0].resolve()


EXE = find_tester()
HOST = "127.0.0.1"
PORT = int(os.environ.get("APOLLO_TESTER_PORT", "8765"))
URL = f"http://{HOST}:{PORT}/"

CREATE_NO_WINDOW = 0x08000000 if os.name == "nt" else 0

state_lock = threading.Lock()
process: subprocess.Popen[str] | None = None
process_kind = ""
process_started = 0.0
output_lines: deque[str] = deque(maxlen=800)
return_code: int | None = None


def append_output(line: str) -> None:
    line = line.rstrip("\r\n")
    if line:
        with state_lock:
            output_lines.append(line)


def read_process(proc: subprocess.Popen[str], kind: str) -> None:
    global return_code, process
    assert proc.stdout is not None
    for line in proc.stdout:
        append_output(line)
    code = proc.wait()
    with state_lock:
        if process is proc:
            return_code = code
            # Do not call append_output() here: it acquires state_lock again.
            output_lines.append(f"[{kind}] terminado con código {code}")


def start_process(kind: str, args: list[str]) -> tuple[bool, str]:
    global process, process_kind, process_started, return_code
    with state_lock:
        if process is not None and process.poll() is None:
            return False, "Ya hay una prueba en ejecución."
        if not EXE.exists():
            return False, f"No se encontró el tester: {EXE}"
        output_lines.append(f"$ {EXE.name} {' '.join(args)}")
        return_code = None
        process = subprocess.Popen(
            [str(EXE), *args],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            creationflags=CREATE_NO_WINDOW,
        )
        process_kind = kind
        process_started = time.time()
        threading.Thread(target=read_process, args=(process, kind), daemon=True).start()
    return True, "Prueba iniciada."


def stop_process() -> tuple[bool, str]:
    global process
    with state_lock:
        proc = process
    if proc is None or proc.poll() is not None:
        return True, "No hay ninguna prueba en ejecución."
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=3)
    append_output("[servidor] prueba detenida")
    return True, "Prueba detenida."


def run_list() -> tuple[bool, str]:
    if not EXE.exists():
        return False, f"No se encontró el tester: {EXE}"
    try:
        result = subprocess.run(
            [str(EXE), "list"],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=15,
            creationflags=CREATE_NO_WINDOW,
        )
        text = (result.stdout or "") + (result.stderr or "")
        return result.returncode == 0, text.strip()
    except Exception as exc:  # noqa: BLE001 - diagnostic API should report failures
        return False, str(exc)


class Handler(SimpleHTTPRequestHandler):
    server_version = "ApolloInputNovaTester/0.2.0"

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(HERE), **kwargs)

    def log_message(self, fmt: str, *args) -> None:
        # Keep the server console quiet; the UI has its own log.
        return

    def send_json(self, status: int, payload: dict) -> None:
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:  # noqa: N802 - http.server API
        if self.path.startswith("/api/status"):
            with state_lock:
                proc = process
                running = proc is not None and proc.poll() is None
                payload = {
                    "running": running,
                    "kind": process_kind,
                    "return_code": return_code,
                    "elapsed": round(time.time() - process_started, 1) if process_started else 0,
                    "output": list(output_lines),
                }
            self.send_json(200, payload)
            return
        if self.path.startswith("/api/devices"):
            ok, text = run_list()
            self.send_json(200 if ok else 500, {"ok": ok, "text": text})
            return
        if self.path.startswith("/api/health"):
            self.send_json(200, {"ok": True, "tester": str(EXE), "tester_exists": EXE.exists()})
            return
        super().do_GET()

    def do_POST(self) -> None:  # noqa: N802 - http.server API
        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length) if length else b"{}"
        try:
            body = json.loads(raw.decode("utf-8") or "{}")
        except json.JSONDecodeError:
            self.send_json(400, {"ok": False, "error": "JSON inválido"})
            return

        if self.path.startswith("/api/stop"):
            ok, message = stop_process()
            self.send_json(200 if ok else 409, {"ok": ok, "message": message})
            return

        if self.path.startswith("/api/clear"):
            with state_lock:
                output_lines.clear()
            self.send_json(200, {"ok": True})
            return

        if not self.path.startswith("/api/start"):
            self.send_json(404, {"ok": False, "error": "Ruta desconocida"})
            return

        kind = str(body.get("kind", ""))
        profile = str(body.get("profile", "xbox_series"))
        source = str(body.get("source", "xinput"))
        index = int(body.get("index", 0))
        seconds = max(1, min(int(body.get("seconds", 60)), 3600))

        if kind == "bridge":
            args = ["bridge", source, str(index), "--profile", profile, "--seconds", str(seconds)]
        elif kind == "selftest":
            args = ["selftest", "--profile", profile, "--seconds", str(seconds)]
        elif kind == "verify":
            args = ["verify"]
        elif kind == "drift":
            args = ["drift", source, str(index), "--seconds", str(seconds)]
        else:
            self.send_json(400, {"ok": False, "error": f"Tipo desconocido: {kind}"})
            return

        ok, message = start_process(kind, args)
        self.send_json(200 if ok else 409, {"ok": ok, "message": message})


def main() -> int:
    if not EXE.exists():
        print(f"AVISO: no se encontró el tester en {EXE}", file=sys.stderr)
    server = ThreadingHTTPServer((HOST, PORT), Handler)
    no_browser = "--no-browser" in sys.argv or os.environ.get("APOLLO_TESTER_NO_BROWSER") == "1"
    if not no_browser:
        threading.Timer(0.5, lambda: webbrowser.open(URL)).start()
    print(f"ApolloInput Nova Visual Tester en {URL}")
    if no_browser:
        print("Modo sin navegador: abre la URL manualmente.")
    print("Cierra esta ventana para detener el servidor.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
