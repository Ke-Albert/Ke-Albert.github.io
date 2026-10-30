"""Protocol/affinity/argument checks; no assertions about machine-specific speed."""
import csv
import io
import os
from pathlib import Path
import subprocess
import unittest

HERE = Path(__file__).resolve().parent
BIN = str(HERE / "lde_bench")


def run(*args, ok=True):
    result = subprocess.run([BIN, *args], capture_output=True, text=True, timeout=20)
    if ok and result.returncode:
        raise AssertionError(result.stderr)
    return result


def rows(text):
    return list(csv.DictReader(io.StringIO("\n".join(
        line for line in text.splitlines() if not line.startswith("#")))))


class BenchmarkTests(unittest.TestCase):
    def test_round_trips_and_switch_counts(self):
        result = run("--iterations", "2000", "--syscalls", "5000", "--warmup", "100",
                     "--timer-samples", "1000", "--trials", "2")
        data = rows(result.stdout)
        self.assertEqual(len(data), 6)
        for trial in ("1", "2"):
            group = [r for r in data if r["trial"] == trial]
            self.assertEqual({r["benchmark"] for r in group},
                             {"read_zero", "pipe_pair", "ping_pong"})
        for r in data:
            count, elapsed = int(r["iterations"]), int(r["elapsed_ns"])
            self.assertGreater(elapsed, 0)
            self.assertAlmostEqual(float(r["ns_per_iteration"]), elapsed / count, delta=.001)
            if r["benchmark"] == "ping_pong":
                self.assertEqual(r["parent_cpu"], r["child_cpu"])
                switches = sum(int(r[k]) for k in ("parent_voluntary", "parent_involuntary",
                                                    "child_voluntary", "child_involuntary"))
                # Allow scheduler/measurement-boundary variation, but require real switching.
                self.assertGreaterEqual(switches, count)
        self.assertIn("# timer=gettimeofday", result.stdout)
        self.assertIn("# timer=monotonic_raw", result.stdout)

    def test_restricted_affinity_selects_allowed_cpu(self):
        cpu = max(os.sched_getaffinity(0))
        def restrict():
            os.sched_setaffinity(0, {cpu})
        result = subprocess.run([BIN, "--iterations", "20", "--syscalls", "20",
                                 "--warmup", "0", "--timer-samples", "10", "--trials", "1"],
                                preexec_fn=restrict, capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)
        for r in rows(result.stdout):
            self.assertEqual(int(r["parent_cpu"]), cpu)
            if r["benchmark"] == "ping_pong":
                self.assertEqual(int(r["child_cpu"]), cpu)

    def test_rejects_invalid_arguments(self):
        for args in (("--iterations", "0"), ("--trials", "-1"),
                     ("--syscalls", "abc"), ("--timer-samples", "0"),
                     ("--cpu", "9999999"), ("--unknown", "1"), ("--cpu",)):
            with self.subTest(args=args):
                self.assertNotEqual(run(*args, ok=False).returncode, 0)

    def test_help(self):
        self.assertIn("Usage:", run("--help").stdout)


if __name__ == "__main__":
    unittest.main()
