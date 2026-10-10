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
response_override = None

class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        requests.append(body)
        if response_override is not None:
            status, payload = response_override
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(payload)
            return
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

        # Exercise error responses through the actual HTTP adapters, without credentials
        # or network access beyond loopback. Provider error bodies must stay private.
        for provider in ("openai", "anthropic"):
            config.write_text(json.dumps({"runtime": {"data_dir": str(root / "errors"),
                "default_workspace": str(root / "workspace")},
                "model": {"provider": provider, "model": "fake-model"}}))
            for status, payload, diagnostic in (
                (401, b'{"error":"fake-local-test-key"}', "Provider HTTP status 401"),
                (200, b'not json', "Failed to parse"),
                (200, b'{"error":"fake-local-test-key"}', "Provider returned an API error"),
                (200, b'{}', "no choices" if provider == "openai" else "no content"),
            ):
                response_override = (status, payload)
                result = subprocess.run([str(build / "agent_cli"), "--config", str(config),
                                         "--session", "error", "--workspace", str(root / "workspace"), "hello"],
                                        env=env, text=True, capture_output=True, timeout=10)
                assert result.returncode == 1, result
                assert diagnostic in result.stderr, result.stderr
                assert "fake-local-test-key" not in result.stdout + result.stderr

            # Token-limit responses still contain a usable final answer and usage.
            payload = ({"usage": {"prompt_tokens": 3, "completion_tokens": 2},
                        "choices": [{"finish_reason": "length", "message": {"content": "limited"}}]}
                       if provider == "openai" else
                       {"id": "local", "usage": {"input_tokens": 3, "output_tokens": 2},
                        "stop_reason": "max_tokens", "content": [{"type": "text", "text": "limited"}]})
            response_override = (200, json.dumps(payload).encode())
            result = subprocess.run([str(build / "agentd"), "--config", str(config)],
                                    input='\nnot json\n{}\n{"type":"user_message"}\n'
                                          '{"type":"user_message","message":"hello"}\n',
                                    env=env, text=True, capture_output=True, timeout=10)
            assert result.returncode == 0, result.stderr
            events = [json.loads(line) for line in result.stdout.splitlines()]
            assert sum(e["type"] == "error" for e in events) == 3, events
            assert any(e["type"] == "assistant_final" and e["text"] == "limited" for e in events), events
        response_override = None

        # Argument and configuration failures should be deterministic at both entry points.
        invalid_config = root / "invalid.json"
        invalid_config.write_text('{')
        for binary in ("agent_cli", "agentd"):
            for arguments in (["--help"], ["-h"], ["--unknown"], ["--config"],
                              ["--config", str(invalid_config), "hello"] if binary == "agent_cli"
                              else ["--config", str(invalid_config)]):
                result = subprocess.run([str(build / binary), *arguments],
                                        env=env, text=True, capture_output=True, timeout=10)
                assert result.returncode == (0 if arguments[0] in ("--help", "-h") else 1), result
            missing = root / "missing.json"
            arguments = ["--config", str(missing)] + (["hello"] if binary == "agent_cli" else [])
            no_key = dict(env, AGENT_API_KEY="")
            result = subprocess.run([str(build / binary), *arguments], input='', env=no_key,
                                    cwd=root, text=True, capture_output=True, timeout=10)
            assert result.returncode == (1 if binary == "agent_cli" else 0), result
        result = subprocess.run([str(build / "agent_cli")], capture_output=True, timeout=10)
        assert result.returncode == 1
    print("Local provider tool loops, CLI output, transcript replay and malformed JSONL: passed")
finally:
    server.shutdown()
    server.server_close()
    thread.join(timeout=2)
