#!/usr/bin/env python3
"""3-Way Benchmark Runner: Lua vs Gravity vs Zephyr

Runs identical algorithms across three scripting languages and compares
wall-clock execution times.

Usage:
    python runner.py [--iterations N] [--output PATH]
    python runner.py --zephyr PATH --lua PATH --gravity PATH
    python runner.py --telemetry   # merge zephyr stats into JSON output
"""

import argparse
import json
import math
import os
import statistics
import subprocess
import sys
import time
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent

# Default binary paths (relative to bench/3way/)
DEFAULTS = {
    "zephyr": str(SCRIPT_DIR / ".." / ".." / "build" / "Release" / "zephyr.exe"),
    "lua": str(SCRIPT_DIR / "lua-5.5.0" / "lua.exe"),
    "gravity": str(SCRIPT_DIR / "gravity" / "bin" / "gravity.exe"),
}

CASES = [
    {
        "name": "fibonacci(35)",
        "scripts": {
            "zephyr": "scripts/fib.zph",
            "lua": "scripts/fib.lua",
            "gravity": "scripts/fib.gravity",
        },
    },
    {
        "name": "hot_loop(1M)",
        "scripts": {
            "zephyr": "scripts/loop.zph",
            "lua": "scripts/loop.lua",
            "gravity": "scripts/loop.gravity",
        },
    },
    {
        "name": "array_sum(100K)",
        "scripts": {
            "zephyr": "scripts/array.zph",
            "lua": "scripts/array.lua",
            "gravity": "scripts/array.gravity",
        },
    },
    {
        "name": "closure(100K)",
        "scripts": {
            "zephyr": "scripts/closure.zph",
            "lua": "scripts/closure.lua",
            "gravity": "scripts/closure.gravity",
        },
    },
    {
        "name": "struct(100K)",
        "scripts": {
            "zephyr": "scripts/struct.zph",
            "lua": "scripts/struct.lua",
            "gravity": "scripts/struct.gravity",
        },
    },
    {
        "name": "entity_update(1M)",
        "scripts": {
            "zephyr": "scripts/entity_update.zph",
            "lua": "scripts/entity_update.lua",
            "gravity": "scripts/entity_update.gravity",
        },
    },
    {
        "name": "event_dispatch(100K)",
        "scripts": {
            "zephyr": "scripts/event_dispatch.zph",
            "lua": "scripts/event_dispatch.lua",
            "gravity": "scripts/event_dispatch.gravity",
        },
    },
    {
        "name": "vector_math(1M)",
        "scripts": {
            "zephyr": "scripts/vector_math.zph",
            "lua": "scripts/vector_math.lua",
            "gravity": "scripts/vector_math.gravity",
        },
    },
    {
        "name": "coroutine(100K)",
        "scripts": {
            "zephyr": "scripts/coroutine.zph",
            "lua": "scripts/coroutine.lua",
            "gravity": "scripts/coroutine.gravity",
        },
    },
    {
        "name": "native_callback(10K)",
        "scripts": {
            "zephyr": "scripts/native_callback.zph",
            "lua": "scripts/native_callback.lua",
            "gravity": "scripts/native_callback.gravity",
        },
    },
    {
        "name": "string_ops(10K)",
        "scripts": {
            "zephyr": "scripts/string_ops.zph",
            "lua": "scripts/string_ops.lua",
            "gravity": "scripts/string_ops.gravity",
        },
    },
    {
        "name": "method_dispatch(100K)",
        "scripts": {
            "zephyr": "scripts/method_dispatch.zph",
            "lua": "scripts/method_dispatch.lua",
            "gravity": "scripts/method_dispatch.gravity",
        },
    },
]


@dataclass
class TimingResult:
    times_ms: list[float] = field(default_factory=list)

    @property
    def mean_ms(self) -> float:
        return statistics.mean(self.times_ms) if self.times_ms else 0.0

    @property
    def min_ms(self) -> float:
        return min(self.times_ms) if self.times_ms else 0.0

    @property
    def max_ms(self) -> float:
        return max(self.times_ms) if self.times_ms else 0.0

    @property
    def p50_ms(self) -> float:
        if not self.times_ms:
            return 0.0
        return statistics.median(self.times_ms)

    @property
    def p95_ms(self) -> float:
        if not self.times_ms:
            return 0.0
        sorted_t = sorted(self.times_ms)
        idx = int(math.ceil(len(sorted_t) * 0.95)) - 1
        return sorted_t[min(idx, len(sorted_t) - 1)]

    def to_dict(self) -> dict:
        return {
            "min_ms": round(self.min_ms, 2),
            "p50_ms": round(self.p50_ms, 2),
            "p95_ms": round(self.p95_ms, 2),
            "mean_ms": round(self.mean_ms, 2),
            "samples": len(self.times_ms),
        }


def build_command(lang: str, binary: str, script: str) -> list[str]:
    """Build the CLI command for each language."""
    if lang == "zephyr":
        return [binary, "run", script]
    elif lang == "lua":
        return [binary, script]
    elif lang == "gravity":
        return [binary, script]
    return []


def run_once(cmd: list[str]) -> float:
    """Run a command and return wall-clock time in milliseconds."""
    start = time.perf_counter_ns()
    result = subprocess.run(
        cmd,
        capture_output=True,
        timeout=300,
    )
    elapsed_ns = time.perf_counter_ns() - start
    if result.returncode != 0:
        stderr = result.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(f"Command failed (exit {result.returncode}): {' '.join(cmd)}\n{stderr}")
    return elapsed_ns / 1_000_000  # -> ms


def check_binaries(binaries: dict[str, str]) -> dict[str, str]:
    """Check which binaries exist, return available ones."""
    available = {}
    for lang, path in binaries.items():
        resolved = str(Path(path).resolve())
        if os.path.isfile(resolved):
            available[lang] = resolved
        else:
            print(f"  [SKIP] {lang}: binary not found at {resolved}")
    return available


def collect_telemetry(zephyr_binary: str) -> dict | None:
    """Run 'zephyr stats' and return parsed JSON, or None on failure."""
    try:
        result = subprocess.run(
            [zephyr_binary, "stats"],
            capture_output=True,
            timeout=10,
        )
        if result.returncode == 0:
            output = result.stdout.decode("utf-8", errors="replace").strip()
            if output:
                return json.loads(output)
    except (subprocess.TimeoutExpired, json.JSONDecodeError, FileNotFoundError, OSError):
        pass
    return None


def run_benchmark(
    binaries: dict[str, str],
    iterations: int = 5,
    warmup: int = 2,
) -> list[dict]:
    """Run all benchmark cases and return results."""
    results = []

    for case in CASES:
        print(f"\n  {case['name']}")
        print(f"  {'─' * 60}")
        case_results = {}

        for lang, script_rel in case["scripts"].items():
            if lang not in binaries:
                continue

            script = str(SCRIPT_DIR / script_rel)
            cmd = build_command(lang, binaries[lang], script)
            timing = TimingResult()

            # Warmup (not counted in results)
            for _ in range(warmup):
                try:
                    run_once(cmd)
                except RuntimeError as e:
                    print(f"    {lang:>8}: WARMUP ERROR - {e}")
                    break

            # Measure
            for i in range(iterations):
                try:
                    ms = run_once(cmd)
                    timing.times_ms.append(ms)
                except RuntimeError as e:
                    print(f"    {lang:>8}: ERROR on iteration {i+1} - {e}")
                    break

            if timing.times_ms:
                case_results[lang] = timing
                print(
                    f"    {lang:>8}: min={timing.min_ms:>8.2f}ms  "
                    f"p50={timing.p50_ms:>8.2f}ms  "
                    f"p95={timing.p95_ms:>8.2f}ms"
                )
            else:
                print(f"    {lang:>8}: FAILED")

        # Determine winner by minimum time (most stable metric)
        winner = ""
        if case_results:
            winner = min(case_results, key=lambda k: case_results[k].min_ms)

        results.append({
            "name": case["name"],
            "results": {k: v.to_dict() for k, v in case_results.items()},
            "winner": winner,
        })

    return results


def print_summary(results: list[dict], binaries: dict[str, str]):
    """Print a formatted summary table with min times and percentage comparison."""
    langs = ["lua", "gravity", "zephyr"]
    available = [l for l in langs if l in binaries]

    # Use first available language as baseline for percentage comparison
    baseline_lang = available[0] if available else None

    print("\n")
    print("=" * 80)
    print("  3-Way Benchmark: Lua vs Gravity vs Zephyr")
    print("  (primary metric: min wall-clock time)")
    print("=" * 80)

    # Header
    header = f"  {'Case':<22}"
    for lang in available:
        header += f"  {lang.capitalize():>10}"
        if lang != baseline_lang:
            header += f" {'%':>6}"
    header += f"  {'Winner':>8}"
    print(header)
    sep = f"  {'─' * 20}  "
    for lang in available:
        sep += "─" * 10 + "  "
        if lang != baseline_lang:
            sep += "─" * 6 + "  "
    sep += "─" * 8
    print(sep)

    # Rows
    for case in results:
        row = f"  {case['name']:<22}"
        baseline_min = None
        if baseline_lang and baseline_lang in case["results"]:
            baseline_min = case["results"][baseline_lang]["min_ms"]

        for lang in available:
            if lang in case["results"]:
                ms = case["results"][lang]["min_ms"]
                row += f"  {ms:>8.2f}ms"
                if lang != baseline_lang:
                    if baseline_min and baseline_min > 0:
                        pct = ((ms / baseline_min) - 1.0) * 100
                        sign = "+" if pct >= 0 else ""
                        row += f" {sign}{pct:>4.0f}%"
                    else:
                        row += f" {'N/A':>6}"
            else:
                row += f"  {'N/A':>10}"
                if lang != baseline_lang:
                    row += f" {'':>6}"
        winner = case.get("winner", "")
        row += f"  {winner.capitalize():>8}"
        print(row)

    print("=" * 80)


def save_json(
    results: list[dict],
    binaries: dict[str, str],
    iterations: int,
    warmup: int,
    output: str,
    telemetry: dict | None = None,
):
    """Save results to JSON."""
    report = {
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "iterations": iterations,
        "warmup": warmup,
        "runtimes": binaries,
        "cases": results,
    }
    if telemetry is not None:
        report["telemetry"] = telemetry

    out_path = Path(output)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    print(f"\n  Results saved to: {out_path.resolve()}")


def main():
    parser = argparse.ArgumentParser(description="3-Way Benchmark: Lua vs Gravity vs Zephyr")
    parser.add_argument("--zephyr", default=DEFAULTS["zephyr"], help="Path to zephyr binary")
    parser.add_argument("--lua", default=DEFAULTS["lua"], help="Path to lua binary")
    parser.add_argument("--gravity", default=DEFAULTS["gravity"], help="Path to gravity binary")
    parser.add_argument("--iterations", "-n", type=int, default=5, help="Measurement iterations (default: 5)")
    parser.add_argument("--warmup", "-w", type=int, default=2, help="Warmup iterations (default: 2)")
    parser.add_argument("--output", "-o", default=str(SCRIPT_DIR / "results" / "comparison.json"), help="Output JSON path")
    parser.add_argument("--telemetry", action="store_true", help="Merge 'zephyr stats' output into JSON results")
    args = parser.parse_args()

    binaries_requested = {
        "zephyr": args.zephyr,
        "lua": args.lua,
        "gravity": args.gravity,
    }

    print("  Checking binaries...")
    binaries = check_binaries(binaries_requested)

    if not binaries:
        print("\n  ERROR: No binaries found. Cannot run benchmarks.")
        print("  Place binaries at:")
        for lang, path in DEFAULTS.items():
            print(f"    {lang}: {Path(path).resolve()}")
        sys.exit(1)

    print(f"\n  Available: {', '.join(b.capitalize() for b in binaries)}")
    print(f"  Iterations: {args.iterations} (warmup: {args.warmup})")

    results = run_benchmark(binaries, iterations=args.iterations, warmup=args.warmup)
    print_summary(results, binaries)

    # Collect telemetry if requested
    telemetry_data = None
    if args.telemetry and "zephyr" in binaries:
        print("\n  Collecting telemetry from 'zephyr stats'...")
        telemetry_data = collect_telemetry(binaries["zephyr"])
        if telemetry_data:
            print("  Telemetry collected successfully.")
        else:
            print("  Telemetry collection failed or no data returned.")

    save_json(results, binaries, args.iterations, args.warmup, args.output, telemetry_data)


if __name__ == "__main__":
    main()
