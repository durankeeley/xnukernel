#!/usr/bin/env python3
"""Host-side job server for the macOS guest agent.
  GET  /next            -> next queued job script (204 if none); header X-Job-Id
  PUT  /result/<id>     -> store job output
  GET  /files/<name>    -> serve ../build/agent/files/<name>
  PUT  /upload/<name>   -> store into ../build/agent/files/<name>
Run: scripts/agent-server.py   (listens on 0.0.0.0:8000; guest reaches it at 10.0.2.2)
"""
import http.server, os, sys
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'build', 'agent')
J, R, F = (os.path.join(ROOT, d) for d in ('jobs', 'results', 'files'))
for d in (J, R, F): os.makedirs(d, exist_ok=True)

class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def _send(self, code, body=b'', hdr=None):
        self.send_response(code)
        for k, v in (hdr or {}).items(): self.send_header(k, v)
        self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)
    def do_GET(self):
        if self.path == '/next':
            jobs = sorted(f for f in os.listdir(J) if not f.endswith('.taken'))
            if not jobs: return self._send(204)
            p = os.path.join(J, jobs[0]); body = open(p, 'rb').read(); os.rename(p, p + '.taken')
            return self._send(200, body, {'X-Job-Id': jobs[0]})
        if self.path.startswith('/files/'):
            p = os.path.join(F, os.path.basename(self.path))
            if os.path.isfile(p): return self._send(200, open(p, 'rb').read())
        self._send(404)
    def do_PUT(self):
        n = int(self.headers.get('Content-Length', 0)); data = self.rfile.read(n)
        if self.path.startswith('/result/'): p = os.path.join(R, os.path.basename(self.path))
        elif self.path.startswith('/upload/'): p = os.path.join(F, os.path.basename(self.path))
        else: return self._send(404)
        open(p, 'wb').write(data); self._send(200)

http.server.ThreadingHTTPServer(('0.0.0.0', 8000), H).serve_forever()
