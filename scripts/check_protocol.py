#!/usr/bin/env python3
"""Exercise both HTTP adapters and JSONL/CLI boundaries using a local fake provider."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer

build = Path(sys.argv[1] if len(sys.argv) > 1 else "build").resolve()
requests = []
provider = "openai"

class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        requests.append(body)
        tool = next(t for t in body["tools"] if (t.get("function", t))["name"] == "echo")
        schema = tool["function"]["parameters"] if provider == "openai" else tool["input_schema"]
        assert schema["additionalProperties"] is False
        if len(requests) > 1:
            assert body["tools"] == requests[0]["tools"]
        if provider == "openai":
            if len(requests) == 1:
                message = {"content": None, "tool_calls": [{"id": "call1", "type": "function",
                    "function": {"name": "echo", "arguments": '{"text":"ping"}'}},
                    {"id": "call2", "type": "function",
                     "function": {"name": "echo", "arguments": '{"text":"second"}'}}]}
                response = {"choices": [{"finish_reason": "tool_calls", "message": message}]}
            else:
                results = [m for m in body["messages"] if m["role"] == "tool"]
                calls = [m for m in body["messages"] if m.get("tool_calls")]
                assert [m["tool_call_id"] for m in results] == ["call1", "call2"]
                assert [c["id"] for c in calls[0]["tool_calls"]] == ["call1", "call2"]
                assert [json.loads(m["content"])["content"] for m in results] == ["ping", "second"]
                response = {"choices": [{"finish_reason": "stop", "message": {"content": "pong"}}]}
        else:
            if len(requests) == 1:
                response = {"stop_reason": "tool_use", "content": [{"type": "tool_use", "id": "call1",
                    "name": "echo", "input": {"text": "ping"}},
                    {"type": "tool_use", "id": "call2", "name": "echo", "input": {"text": "second"}}]}
            else:
                results = [b for m in body["messages"] for b in m["content"] if b["type"] == "tool_result"]
                assert [b["tool_use_id"] for b in results] == ["call1", "call2"]
                assert [json.loads(b["content"])["content"] for b in results] == ["ping", "second"]
                response = {"stop_reason": "end_turn", "content": [{"type": "text", "text": "pong"}]}
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(json.dumps(response).encode())

server = HTTPServer(("127.0.0.1", 0), Handler)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
try:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        env = dict(os.environ, AGENT_API_KEY="fake-local-test-key",
                   AGENT_ENDPOINT=f"http://127.0.0.1:{server.server_port}/messages")
        for provider in ("openai", "anthropic"):
            requests.clear()
            config = root / "config.json"
            config.write_text(json.dumps({"runtime": {"data_dir": str(root / provider),
                "default_workspace": str(root / "workspace")}, "model": {"provider": provider, "model": "fake-model"}}))
            result = subprocess.run([str(build / "agent_cli"), "--config", str(config), "echo ping"],
                                    env=env, text=True, capture_output=True, timeout=10)
            assert result.returncode == 0, result.stderr
            assert result.stdout == "pong\n", result.stdout
            assert len(requests) == 2
            assert "fake-local-test-key" not in result.stdout + result.stderr
            transcript = (root / provider / "sessions/default/transcript.jsonl").read_text()
            assert "fake-local-test-key" not in transcript
            records = [json.loads(line) for line in transcript.splitlines()]
            assert sum(r["type"] == "user_message" for r in records) == 1
            assert [r["provider_call_id"] for r in records if r["type"] == "tool_result"] == ["call1", "call2"]
            # Reopen the saved session through a fresh CLI process and validate replay at the provider.
            result = subprocess.run([str(build / "agent_cli"), "--config", str(config), "continue"],
                                    env=env, text=True, capture_output=True, timeout=10)
            assert result.returncode == 0, result.stderr
            assert result.stdout == "pong\n", result.stdout
            assert len(requests) == 3
        result = subprocess.run([str(build / "agentd"), "--config", str(config)], input='[]\n{"message":1}\n',
                                env=env, text=True, capture_output=True, timeout=10)
        assert result.returncode == 0, result.stderr
        events = [json.loads(line) for line in result.stdout.splitlines()]
        assert len(events) == 2 and all(e["type"] == "error" for e in events), events
    print("Local provider tool loops, CLI output, transcript replay and malformed JSONL: passed")
finally:
    server.shutdown()
    server.server_close()
    thread.join(timeout=2)
