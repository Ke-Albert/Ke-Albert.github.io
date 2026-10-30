"""Run the benchmark and retain raw measurements plus a readable report."""
import argparse
import csv
import datetime
import hashlib
import io
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output-dir", default="results")
parser.add_argument("benchmark_args", nargs=argparse.REMAINDER)
args = parser.parse_args()
extra = args.benchmark_args
if extra[:1] == ["--"]:
    extra = extra[1:]
command = [str(HERE / "lde_bench"), *extra]
completed = subprocess.run(command, capture_output=True, text=True, timeout=600, check=True)
output = Path(args.output_dir)
if not output.is_absolute():
    output = HERE / output
output.mkdir(parents=True, exist_ok=True)
(output / "raw.csv").write_text(completed.stdout)
if completed.stderr:
    (output / "stderr.txt").write_text(completed.stderr)
metadata = [line for line in completed.stdout.splitlines() if line.startswith("#")]
rows = list(csv.DictReader(io.StringIO("\n".join(
    line for line in completed.stdout.splitlines() if not line.startswith("#")))))
models = [line.split(":", 1)[1].strip() for line in Path("/proc/cpuinfo").read_text().splitlines()
          if line.startswith("model name")]
clocksource = Path("/sys/devices/system/clocksource/clocksource0/current_clocksource")
env = {
    "recorded_at": datetime.datetime.now().astimezone().isoformat(),
    "command": command,
    "kernel": platform.platform(),
    "cpu_model": models[0] if models else "unknown",
    "runner_allowed_cpus": sorted(os.sched_getaffinity(0)),
    "clocksource": clocksource.read_text().strip() if clocksource.exists() else "unavailable",
    "compiler": subprocess.check_output(["gcc", "--version"], text=True).splitlines()[0],
    "cflags": "-std=c11 -Wall -Wextra -Werror -pedantic -O2",
    "source_sha256": {name: hashlib.sha256((HERE / name).read_bytes()).hexdigest()
                       for name in ("lde_bench.c", "Makefile", "run_experiment.py", "test_bench.py")},
    "benchmark_metadata": metadata,
}
(output / "environment.json").write_text(json.dumps(env, ensure_ascii=False, indent=2) + "\n")
groups = {name: [r for r in rows if r["benchmark"] == name]
          for name in ("read_zero", "pipe_pair", "ping_pong")}
assert all(groups.values()), "Missing benchmark results"
summary = ["# LDE 实测结果", "", f"记录时间：{env['recorded_at']}", "",
           f"CPU：{env['cpu_model']}；内核：{env['kernel']}；时钟源：{env['clocksource']}。",
           "", "命令：", "", "```sh", " ".join(command), "```", "",
           "编译选项：`" + env["cflags"] + "`。", "", "## 每轮结果", "",
           "| 轮次 | 零字节 read（ns/次） | 单进程管道写+读（ns/对） | 双进程往返（ns/轮） | 往返/2（ns） | 实际切换合计 | 合计/(2N) |",
           "| --- | ---: | ---: | ---: | ---: | ---: | ---: |"]
for r in groups["ping_pong"]:
    trial = r["trial"]
    null = next(x for x in groups["read_zero"] if x["trial"] == trial)
    pipe = next(x for x in groups["pipe_pair"] if x["trial"] == trial)
    switches = sum(int(r[k]) for k in ("parent_voluntary", "parent_involuntary",
                                      "child_voluntary", "child_involuntary"))
    summary.append(f"| {trial} | {float(null['ns_per_iteration']):.2f} | "
                   f"{float(pipe['ns_per_iteration']):.2f} | {float(r['ns_per_iteration']):.2f} | "
                   f"{float(r['ns_per_iteration']) / 2:.2f} | {switches} | "
                   f"{switches / (2 * int(r['iterations'])):.5f} |")
summary += ["", "## 跨轮统计", "", "| 指标 | 最小 | 中位数 | 均值 | 最大 |",
            "| --- | ---: | ---: | ---: | ---: |"]
for name, label, scale in (("read_zero", "零字节 read：ns/次", 1),
                           ("pipe_pair", "单进程管道写+读：ns/对", 1),
                           ("ping_pong", "双进程往返：ns/轮", 1),
                           ("ping_pong", "往返/2：ns/切换相关估计", .5)):
    values = [float(r["ns_per_iteration"]) * scale for r in groups[name]]
    summary.append(f"| {label} | {min(values):.2f} | {statistics.median(values):.2f} | "
                   f"{statistics.mean(values):.2f} | {max(values):.2f} |")
summary += ["", "## 计时器观测与参数", "", "```text", *metadata, "```", "",
            "相邻 gettimeofday 差值以 1000ns 为单位；零差值不代表调用没有成本。",
            "min_positive 是观测到的最小正间隔，不是绝对准确度；相邻读钟间隔也包含读钟开销。",
            "CLOCK_MONOTONIC_RAW 用于批量计时，每个批次只在首尾读钟。", "",
            "## 如何解释", "",
            "- 往返包含父→子与子→父两次交接，用 T/(2N) 估计每次切换相关的平均开销。",
            "- 这还包括管道读写、唤醒、调度、循环及缓存影响，不能称为纯 context switch 成本。",
            "- pipe_pair 是同进程无须互相等待的写+读参照，路径不同；不直接相减宣称获得纯切换成本。",
            "- 实际切换合计来自父子 getrusage 的主动和被动切换差值，边界与计时略有差异；",
            "  还可能包含与其他进程的切换，应结合合计/(2N) 判断模型是否近似成立。",
            "- CPU 绑定限制运行位置，但不独占 CPU。虚拟机/WSL、宿主调度和后台负载会影响结果。",
            "", "原始数据见 [raw.csv](raw.csv)，环境与源码哈希见 [environment.json](environment.json)。", ""]
text = "\n".join(summary)
(output / "summary.md").write_text(text)
print(text)
