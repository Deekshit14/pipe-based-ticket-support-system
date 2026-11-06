#!/usr/bin/env python3
"""Smoke integration checks for advanced Linux IPC and encryption demos."""

import pathlib
import subprocess
import sys


def run(executable: pathlib.Path, mode: str, data: str) -> str:
    result = subprocess.run(
        [str(executable), mode], input=data, text=True, capture_output=True,
        timeout=15, check=False,
    )
    output = result.stdout + result.stderr
    if result.returncode != 0:
        raise AssertionError(f"{mode} exited {result.returncode}:\n{output}")
    return output


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} PATH_TO_IPC_ADVANCED", file=sys.stderr)
        return 2
    executable = pathlib.Path(sys.argv[1]).resolve()

    room = run(executable, "rooms", "hello room\n/quit\n")
    assert "support room received: hello room" in room

    server = run(executable, "server", "/send 2 reset password\n/quit\n")
    assert "agent-2: reset password" in server

    signal_chat = run(executable, "signal-chat", "hello signal\n/quit\n")
    assert "signal child received: hello signal" in signal_chat

    encrypted = run(executable, "encrypt", "test-passphrase\nsecret message\n")
    assert "AES-256-GCM encrypted payload" in encrypted
    assert "Authenticated plaintext: secret message" in encrypted

    print("Advanced IPC and encryption integration checks passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
