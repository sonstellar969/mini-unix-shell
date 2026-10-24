#!/usr/bin/env python3
"""Time built-in commands against external processes. Prints one trial table."""

import statistics
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MSH = str(ROOT / "msh")
TRIALS = 5


def time_script(script):
    start = time.perf_counter()
    result = subprocess.run(
        [MSH],
        input=script,
        text=True,
        capture_output=True,
        cwd=str(ROOT),
        timeout=120,
    )
    elapsed = time.perf_counter() - start
    if result.returncode != 0:
        raise SystemExit(result.stderr)
    return elapsed


def median_ms(command, count):
    script = "".join(f"{command}\n" for _ in range(count))
    samples = [time_script(script) for _ in range(TRIALS)]
    return statistics.median(samples) * 1000.0 / count, samples


def main():
    builtin_ms, builtin_samples = median_ms("pwd > /dev/null", 400)
    external_ms, external_samples = median_ms("/usr/bin/true", 200)
    pipeline_ms, pipeline_samples = median_ms("echo hello | cat > /dev/null", 100)
    pipe_bytes = subprocess.check_output([str(ROOT / "tests" / "measure_pipe")], text=True).strip()

    print(f"builtin_pwd_ms {builtin_ms:.3f}")
    print(f"external_true_ms {external_ms:.3f}")
    print(f"pipeline_ms {pipeline_ms:.3f}")
    print(f"pipe_capacity_bytes {pipe_bytes}")
    print(f"builtin_trials_s {' '.join(f'{s:.4f}' for s in builtin_samples)}")
    print(f"external_trials_s {' '.join(f'{s:.4f}' for s in external_samples)}")
    print(f"pipeline_trials_s {' '.join(f'{s:.4f}' for s in pipeline_samples)}")


if __name__ == "__main__":
    main()
