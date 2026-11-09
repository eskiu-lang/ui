"""A static server for a web build in the host tests (tests/web/*.py): a thread, a free port,
the module types, no caching, and a hook that may rewrite a file as it is served."""
import functools
import http.server
from pathlib import Path
import socket
import threading


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map,
                      ".mjs": "text/javascript", ".js": "text/javascript", ".wasm": "application/wasm",
                      ".json": "application/json", ".txt": "text/plain"}
    rewrite = None
    cors = False

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        if self.cors:
            self.send_header("Access-Control-Allow-Origin", "*")
        super().end_headers()

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        # A route the app shows (/tasks/42) is the page itself, as ui's own server serves it.
        if "." not in path.rsplit("/", 1)[-1]:
            path = "/index.html"
            self.path = "/index.html"
        if self.rewrite and path == "/index.html":
            body = self.rewrite((Path(self.directory) / "index.html").read_text()).encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        super().do_GET()

    def log_message(self, *_):
        pass


def serve(directory, rewrite=None, cors=False):
    """Serves `directory` on 127.0.0.1; (the server, its URL). `rewrite(text)` changes
    index.html as it is served; set server.handler.rewrite to change it later."""
    handler = type("H", (Handler,), {"rewrite": staticmethod(rewrite) if rewrite else None, "cors": cors})
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(handler, directory=str(directory)))
    server.handler = handler
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server, f"http://127.0.0.1:{server.server_address[1]}/"
