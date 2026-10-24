#!/usr/bin/env python3
"""Ctrl+C must stop the foreground command and leave a background job alive."""

import os
import pty
import re
import select
import signal
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MSH = str(ROOT / "msh")


def read_until(fd, needles, timeout):
    data = b""
    deadline = time.time() + timeout
    wanted = needles if isinstance(needles, (list, tuple)) else (needles,)
    while time.time() < deadline:
        remaining = max(0.05, deadline - time.time())
        ready, _, _ = select.select([fd], [], [], min(0.2, remaining))
        if not ready:
            continue
        try:
            chunk = os.read(fd, 4096)
        except OSError:
            break
        if not chunk:
            break
        data += chunk
        if any(item in data for item in wanted):
            return data
    return data


def main():
    pid, fd = pty.fork()
    if pid == 0:
        os.execv(MSH, [MSH])

    shell_pid = pid
    try:
        banner = read_until(fd, b"msh:", 3)
        if b"msh:" not in banner:
            raise SystemExit(f"shell did not prompt\n{banner!r}")

        os.write(fd, b"sleep 30 &\n")
        read_until(fd, b"msh:", 3)
        os.write(fd, b"sleep 30\n")
        time.sleep(0.4)
        os.write(fd, b"\x03")
        after = read_until(fd, b"msh:", 3)
        if b"msh:" not in after:
            raise SystemExit(f"foreground sleep survived Ctrl+C\n{after!r}")

        os.write(fd, b"jobs\n")
        listed = read_until(fd, b"running", 3)
        if b"running" not in listed or b"sleep 30 &" not in listed:
            raise SystemExit(f"background job was killed with the foreground\n{listed!r}")

        match = re.search(br"pgid=(\d+)", listed)
        if not match:
            raise SystemExit(f"job list has no pgid\n{listed!r}")
        os.kill(int(match.group(1)), signal.SIGTERM)

        os.write(fd, b"printf SURVIVED\\n\n")
        survived = read_until(fd, b"SURVIVED", 3)
        if b"SURVIVED" not in survived:
            raise SystemExit(f"shell did not accept a command after Ctrl+C\n{survived!r}")

        os.write(fd, b"exit\n")
        _, status = os.waitpid(shell_pid, 0)
        shell_pid = 0
        if os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0:
            print("signal test passed")
            return
        raise SystemExit(f"shell exit status {status}")
    finally:
        if shell_pid:
            subprocess.run(["pkill", "-P", str(shell_pid)], check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            try:
                os.kill(shell_pid, signal.SIGTERM)
            except OSError:
                pass
            try:
                os.waitpid(shell_pid, 0)
            except OSError:
                pass
        try:
            os.close(fd)
        except OSError:
            pass


if __name__ == "__main__":
    main()
