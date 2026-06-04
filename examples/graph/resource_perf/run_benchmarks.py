#!/usr/bin/env python3
"""
Orchestrate building and running resource_perf benchmarks

Usage:
    python run_benchmarks.py --workload chain_scaling
    python run_benchmarks.py --workload genie_siblings --output genie_results.json
    python run_benchmarks.py --list-workloads
"""

import argparse
import subprocess
import json
import sys
from dataclasses import dataclass, field
from typing import List
from datetime import datetime
from pathlib import Path

@dataclass
class BenchmarkConfig:
    """Configuration for a single benchmark run"""
    benchmark: str          # e.g., "chain_bench"
    use_mode: int          # 0, 1, or 2
    args: List[str]        # Command-line args for benchmark

    # Optional metadata
    description: str = ""
    expected_behavior: str = ""
    trace_mode: str = "notrace"  # "notrace", "eager", or "lazy"

@dataclass
class BenchmarkResult:
    """Result from running a benchmark configuration"""
    config: BenchmarkConfig
    stdout: str
    stderr: str
    returncode: int
    execution_time_s: float
    timestamp: str = field(default_factory=lambda: datetime.now().isoformat())

@dataclass
class Workload:
    """A workload from workloads.md"""
    name: str
    description: str
    configs: List[BenchmarkConfig]
    expected_outcomes: str = ""

# Build directory relative to script location
SCRIPT_DIR = Path(__file__).parent
ROOT_DIR = SCRIPT_DIR.parent.parent.parent  # Up to oneTBB_resource_limited/
BUILD_DIR = ROOT_DIR / "build"

def get_target_name(benchmark: str, use_mode: int, trace_mode: str = "notrace") -> str:
    """Generate target name matching CMakeLists.txt conventions

    Args:
        benchmark: Benchmark name (e.g., "genie_bench")
        use_mode: 0, 1, or 2
        trace_mode: "notrace", "eager", or "lazy"
    """
    if use_mode == 2:
        # Mode 2 uses local_ts_notify configuration
        target_name = f"{benchmark}_mode2_local_ts_notify_{trace_mode}"
    else:
        target_name = f"{benchmark}_mode{use_mode}_{trace_mode}"
    return target_name

def get_executable_path(benchmark: str, use_mode: int, config: str = "Release", trace_mode: str = "notrace") -> Path:
    """Get path to built executable"""
    target_name = get_target_name(benchmark, use_mode, trace_mode)

    # Windows MSVC: build/msvc_*_release/target_name.exe or build/msvc_*_debug/target_name.exe
    # Windows: build/Release/target_name.exe
    # Linux/Mac: build/target_name
    if sys.platform == "win32":
        # Try MSVC-specific output directory first
        import glob
        config_suffix = "release" if config == "Release" else "debug"
        msvc_dirs = list(BUILD_DIR.glob(f"msvc_*_{config_suffix}"))
        if msvc_dirs:
            # Prefer MSVC directory if it exists (CMake will build executables there)
            exe_path = msvc_dirs[0] / f"{target_name}.exe"
            return exe_path

        # Fall back to standard Release/Debug directory
        exe_path = BUILD_DIR / config / f"{target_name}.exe"
    else:
        exe_path = BUILD_DIR / target_name

    return exe_path

def build_benchmark(benchmark: str, use_mode: int, config: str = "Release",
                   force_rebuild: bool = False, trace_mode: str = "notrace") -> Path:
    """Build a specific benchmark configuration"""
    target_name = get_target_name(benchmark, use_mode, trace_mode)
    exe_path = get_executable_path(benchmark, use_mode, config, trace_mode)

    # Check if already built (unless force_rebuild)
    if exe_path.exists() and not force_rebuild:
        print(f"  Using existing: {exe_path.name}")
        return exe_path

    print(f"  Building: {target_name}...")

    # Build using CMake from ROOT_DIR/build
    build_cmd = [
        "cmake",
        "--build", str(BUILD_DIR),
        "--target", target_name,
        "--config", config
    ]

    result = subprocess.run(build_cmd, cwd=str(ROOT_DIR),
                          capture_output=True, text=True)

    if result.returncode != 0:
        print(f"ERROR: Build failed for {target_name}")
        print(result.stderr)
        raise RuntimeError(f"Build failed for {target_name}")

    if not exe_path.exists():
        raise FileNotFoundError(f"Built executable not found: {exe_path}")

    return exe_path

def run_benchmark(exe_path: Path, args: List[str]) -> BenchmarkResult:
    """Run a benchmark executable with given arguments"""
    import time

    cmd = [str(exe_path)] + args
    print(f"  Running: {exe_path.name} {' '.join(args)}")

    start_time = time.time()
    result = subprocess.run(cmd, capture_output=True, text=True)
    execution_time = time.time() - start_time

    return BenchmarkResult(
        config=None,  # Will be filled by caller
        stdout=result.stdout,
        stderr=result.stderr,
        returncode=result.returncode,
        execution_time_s=execution_time
    )

# Define workloads based on workloads.md sections
WORKLOADS = {
    # Root-Genie Siblings: Test r ~= 1, 2, 4, 8, infinity
    "genie_siblings": Workload(
        name="Root-Genie Siblings - Arrival Rate Sweep",
        description="Test arrival rates r ~= 1, 2, 4, 8, infinity as defined in workloads.md section 'Root-Genie Siblings'",
        expected_outcomes="Expect starvation of Histo-generating node for r > 1 in modes 0 and 1",
        configs=[
            BenchmarkConfig("genie_bench", mode,
                          ["1", "100", str(rate)],
                          description=f"Mode {mode}, r ~= {rate}",
                          expected_behavior=f"{'Starvation expected' if rate > 1.0 and mode < 2 else 'Balanced expected'}")
            for mode in [0, 1, 2]
            for rate in [1.0, 2.0, 4.0, 8.0, 1000.0]  # 1000 ~= infinity
        ]
    ),

    # Root-Genie Dependence: Diamond topology with key_matching join
    "genie_dependence": Workload(
        name="Root-Genie Dependence - Diamond Topology Test",
        description="Test diamond topology with dependence (workloads.md 'Root-Genie Dependence')",
        expected_outcomes="Expect starvation of Histo-generating node for r > 1 in modes 0 and 1 due to multi-resource disadvantage",
        configs=[
            BenchmarkConfig("genie_diamond_bench", mode,
                          ["1", "100", str(rate)],
                          description=f"Mode {mode}, r ~= {rate}",
                          expected_behavior=f"{'Starvation expected' if rate > 1.0 and mode < 2 else 'Balanced expected'}")
            for mode in [0, 1, 2]
            for rate in [1.0, 2.0, 4.0, 8.0, 1000.0]  # 1000 ~= infinity
        ]
    ),

    # Baseline Cycle: Overhead measurement with varying graph size
    "baseline_cycle_overhead": Workload(
        name="Baseline Cycle - Resource Acquisition Overhead",
        description="Measure overhead of resource acquisition with no contention (workloads.md 'Baseline Cycle')",
        expected_outcomes="Measure overhead growth with number of consumers and handles",
        configs=[
            BenchmarkConfig("baseline_cycle_bench", mode,
                          ["1", "64", str(n_f), "1.0", str(num_limiters)],
                          description=f"Mode {mode}, N_F={n_f}, L={num_limiters}")
            for mode in [0, 1, 2]
            for n_f in [1, 3, 15, 31]  # From workloads.md table
            for num_limiters in [1, 2, 4, 8]  # Test limiter scaling
        ]
    ),

    # Chain: Resource sweep to expose pipeline parallelism
    "chain_scaling": Workload(
        name="Chain Scaling - Resource Sweep",
        description="Vary num_resources from 1 to N to expose pipeline parallelism (workloads.md 'Performance of a Chain')",
        expected_outcomes="With r=2 and R_L>=2, expect message-based parallelism/pipelining",
        configs=[
            # r ~= 1: No parallelism, baseline
            BenchmarkConfig("chain_bench", mode,
                          ["1", "100", "10", "1.0", "1"],
                          description=f"Mode {mode}, r~=1, R_L=1 (baseline, no parallelism)")
            for mode in [0, 1, 2]
        ] + [
            # r ~= 2: Test with R_L = 1, 2 to see pipelining effect
            BenchmarkConfig("chain_bench", mode,
                          ["1", "100", "10", "2.0", str(r_l)],
                          description=f"Mode {mode}, r~=2, R_L={r_l}",
                          expected_behavior=f"{'Serialized' if r_l == 1 else 'Pipelining expected'}")
            for mode in [0, 1, 2]
            for r_l in [1, 2]
        ] + [
            # r ~= N (N=10): Full pipeline with varying resources
            BenchmarkConfig("chain_bench", mode,
                          ["1", "100", "10", "10.0", str(r_l)],
                          description=f"Mode {mode}, r~=N=10, R_L={r_l}",
                          expected_behavior=f"Full pipelining if R_L>={r_l}")
            for mode in [0, 1, 2]
            for r_l in [1, 2, 10]
        ]
    ),

    # Siblings: Graph-based parallelism with contention
    "siblings_parallelism": Workload(
        name="Siblings - Graph-Based Parallelism",
        description="Test graph parallelism with varying resource counts (workloads.md 'Performance for Siblings')",
        expected_outcomes="With r=1 and R_L>=2, expect graph-based parallelism speedup",
        configs=[
            # r ~= 1: Graph parallelism only, vary R_L
            BenchmarkConfig("siblings_bench", mode,
                          ["1", "100", str(n_nodes), "1.0", str(r_l)],
                          description=f"Mode {mode}, r~=1, N={n_nodes}, R_L={r_l}",
                          expected_behavior=f"Speedup ~= {r_l}x expected")
            for mode in [0, 1, 2]
            for n_nodes in [5, 10]  # Number of sibling nodes
            for r_l in [1, 2, 4, n_nodes]  # R_L up to N
        ] + [
            # r > 1: Message + graph parallelism
            BenchmarkConfig("siblings_bench", mode,
                          ["1", "100", "10", str(rate), str(r_l)],
                          description=f"Mode {mode}, r~={rate}, R_L={r_l}")
            for mode in [0, 1, 2]
            for rate in [2.0, 10.0]
            for r_l in [1, 2, 10]
        ]
    ),

    # Tree: Hybrid chain/siblings structure
    "tree_parallelism": Workload(
        name="Tree - Hybrid Parallelism",
        description="Binary tree structure testing hybrid parallelism (workloads.md 'Performance of a Tree')",
        expected_outcomes="With r=1 and R_L>=2^D, expect parallelism up to tree width",
        configs=[
            # r ~= 1: Tree parallelism based on depth
            BenchmarkConfig("tree_bench", mode,
                          ["1", "100", str(num_nodes), "1.0", str(r_l)],
                          description=f"Mode {mode}, r~=1, nodes~={num_nodes}, R_L={r_l}")
            for mode in [0, 1, 2]
            for num_nodes in [7, 15, 31]  # Depth 3, 4, 5 (2^D - 1 nodes)
            for r_l in [1, 2, 4, 8]
        ]
    ),

    # Trace Collection: Generate Google Trace JSON files for specific scenarios
    "genie_trace_rate4": Workload(
        name="Genie Trace Collection - Rate 4.0",
        description="Collect Google traces for genie_bench at rate=4.0 across all modes (generates .json trace files)",
        expected_outcomes="Trace files will be generated in build directory showing execution timeline",
        configs=[
            BenchmarkConfig("genie_bench", mode,
                          ["1", "100", "4.0", "14"],  # 1 exec, 100 inputs, rate=4.0, 14 threads
                          description=f"Mode {mode}, r=4.0, trace=lazy",
                          trace_mode="lazy")
            for mode in [0, 1, 2]
        ]
    ),

    "diamond_trace_rate4": Workload(
        name="Diamond Trace Collection - Rate 4.0",
        description="Collect Google traces for genie_diamond_bench at rate=4.0 across all modes (generates .json trace files)",
        expected_outcomes="Trace files will be generated in build directory showing execution timeline",
        configs=[
            BenchmarkConfig("genie_diamond_bench", mode,
                          ["1", "100", "4.0", "14"],  # 1 exec, 100 inputs, rate=4.0, 14 threads
                          description=f"Mode {mode}, r=4.0, trace=lazy",
                          trace_mode="lazy")
            for mode in [0, 1, 2]
        ]
    ),

    "dining_trace_5phil": Workload(
        name="Dining Philosophers Trace Collection - 5 Philosophers",
        description="Collect Google traces for dining_philosophers_bench with 5 philosophers across all modes (generates .json trace files)",
        expected_outcomes="Trace files will be generated showing fairness/starvation patterns across modes",
        configs=[
            BenchmarkConfig("dining_philosophers_bench", mode,
                          ["1", "5", "10", "100.0"],  # 1 exec, 5 philosophers, 10 times, 100ms work
                          description=f"Mode {mode}, 5 philosophers, trace=lazy",
                          trace_mode="lazy")
            for mode in [0, 1, 2]
        ]
    ),

    "chain_trace_full_pipeline": Workload(
        name="Chain Trace Collection - Full Pipeline",
        description="Collect trace for chain_bench at r=10 with full pipelining (R_L=10)",
        expected_outcomes="Trace files will be generated showing full pipelining behavior with r=10, R_L=10",
        configs=[
            BenchmarkConfig("chain_bench", mode,
                          ["1", "100", "10", "10.0", "10"],  # 1 exec, 100 inputs, 10 nodes, r=10.0, R_L=10
                          description=f"Mode {mode}, r=10, R_L=10, trace=lazy",
                          trace_mode="lazy")
            for mode in [0, 1, 2]
        ]
    ),

    "chain_trace_slammed_pipeline": Workload(
        name="Chain Trace Collection - Slammed Pipeline",
        description="Collect trace for chain_bench at r=1000 with pipelining (R_L=10)",
        expected_outcomes="Trace files will be generated showing pipelining behavior with r=1000, R_L=10",
        configs=[
            BenchmarkConfig("chain_bench", mode,
                          ["1", "100", "10", "1000.0", "10"],  # 1 exec, 100 inputs, 10 nodes, r=1000.0, R_L=10
                          description=f"Mode {mode}, r=1000, R_L=10, trace=lazy",
                          trace_mode="lazy")
            for mode in [0, 1, 2]
        ]
    ),

    "baseline_trace": Workload(
        name="Baseline Cycle Trace Collection",
        description="Collect trace for baseline_cycle_bench with N_F=15, L=2",
        expected_outcomes="Trace files showing overhead measurement with no contention",
        configs=[
            BenchmarkConfig("baseline_cycle_bench", mode,
                          ["1", "64", "15", "1.0", "2"],  # 1 exec, 64 inputs, 15 nodes, r=1.0, L=2
                          description=f"Mode {mode}, N_F=15, L=2, trace=lazy",
                          trace_mode="lazy")
            for mode in [0, 1, 2]
        ]
    ),

    "siblings_trace": Workload(
        name="Siblings Trace Collection",
        description="Collect trace for siblings_bench with graph parallelism (r=1, N=10, R_L=10)",
        expected_outcomes="Trace files showing graph-based parallelism across sibling nodes",
        configs=[
            BenchmarkConfig("siblings_bench", mode,
                          ["1", "100", "10", "1.0", "10"],  # 1 exec, 100 inputs, 10 siblings, r=1.0, R_L=10
                          description=f"Mode {mode}, r=1, N=10, R_L=10, trace=lazy",
                          trace_mode="lazy")
            for mode in [0, 1, 2]
        ]
    ),

    "tree_trace": Workload(
        name="Tree Trace Collection - Low Rate",
        description="Collect trace for tree_bench at r=1 with sufficient resources (N=15, R_L=10)",
        expected_outcomes="Trace files showing hybrid parallelism with serial message arrival",
        configs=[
            BenchmarkConfig("tree_bench", mode,
                          ["1", "100", "15", "1.0", "10"],  # 1 exec, 100 inputs, 15 nodes (depth 4), r=1.0, R_L=10
                          description=f"Mode {mode}, r=1, N=15, R_L=10, trace=lazy",
                          trace_mode="lazy")
            for mode in [0, 1, 2]
        ]
    ),

    "tree_trace_high_rate": Workload(
        name="Tree Trace Collection - High Rate",
        description="Collect trace for tree_bench at r=1000 with sufficient resources (N=15, R_L=10)",
        expected_outcomes="Trace files showing hybrid parallelism with maximum contention",
        configs=[
            BenchmarkConfig("tree_bench", mode,
                          ["1", "100", "15", "1000.0", "10"],  # 1 exec, 100 inputs, 15 nodes, r=1000.0, R_L=10
                          description=f"Mode {mode}, r=1000, N=15, R_L=10, trace=lazy",
                          trace_mode="lazy")
            for mode in [0, 1, 2]
        ]
    ),

    "tree_trace_high_rate_rl2": Workload(
        name="Tree Trace Collection - High Rate with Limited Resources",
        description="Collect trace for tree_bench at r=1000 with only 2 resources (N=15, R_L=2)",
        expected_outcomes="Trace files showing extreme contention in tree structure with resource shortage",
        configs=[
            BenchmarkConfig("tree_bench", mode,
                          ["1", "100", "15", "1000.0", "2"],  # 1 exec, 100 inputs, 15 nodes, r=1000.0, R_L=2
                          description=f"Mode {mode}, r=1000, N=15, R_L=2, trace=lazy",
                          trace_mode="lazy")
            for mode in [0, 1, 2]
        ]
    ),

    "siblings_trace_high_rate": Workload(
        name="Siblings Trace Collection - High Rate",
        description="Collect trace for siblings_bench at r=1000 (all messages submitted up front)",
        expected_outcomes="Trace files showing maximum contention with all messages in flight simultaneously",
        configs=[
            BenchmarkConfig("siblings_bench", mode,
                          ["1", "100", "10", "1000.0", "10"],  # 1 exec, 100 inputs, 10 siblings, r=1000.0, R_L=10
                          description=f"Mode {mode}, r=1000, N=10, R_L=10, trace=lazy",
                          trace_mode="lazy")
            for mode in [0, 1, 2]
        ]
    ),

    "siblings_trace_high_rate_rl2": Workload(
        name="Siblings Trace Collection - High Rate with Limited Resources",
        description="Collect trace for siblings_bench at r=1000 with only 2 resources (R_L=2)",
        expected_outcomes="Trace files showing extreme contention with 10 siblings competing for 2 resources",
        configs=[
            BenchmarkConfig("siblings_bench", mode,
                          ["1", "100", "10", "1000.0", "2"],  # 1 exec, 100 inputs, 10 siblings, r=1000.0, R_L=2
                          description=f"Mode {mode}, r=1000, N=10, R_L=2, trace=lazy",
                          trace_mode="lazy")
            for mode in [0, 1, 2]
        ]
    ),

    # Comprehensive trace collection: All trace workloads combined for latency analysis
    "all_traces": Workload(
        name="All Trace Workloads - Comprehensive Collection",
        description="Run all trace collection workloads for comprehensive latency analysis across all benchmarks and scenarios",
        expected_outcomes="Complete set of trace files for analyze_latency.py to compare modes, workloads, and resource configurations",
        configs=[
            # Genie benchmark - rate 4.0
            *[BenchmarkConfig("genie_bench", mode,
                            ["1", "100", "4.0", "14"],
                            description=f"Genie: Mode {mode}, r=4.0",
                            trace_mode="lazy")
              for mode in [0, 1, 2]],

            # Genie diamond benchmark - rate 4.0
            *[BenchmarkConfig("genie_diamond_bench", mode,
                            ["1", "100", "4.0", "14"],
                            description=f"Diamond: Mode {mode}, r=4.0",
                            trace_mode="lazy")
              for mode in [0, 1, 2]],

            # Dining philosophers - 5 philosophers
            *[BenchmarkConfig("dining_philosophers_bench", mode,
                            ["1", "5", "10", "100.0"],
                            description=f"Dining: Mode {mode}, 5 phil",
                            trace_mode="lazy")
              for mode in [0, 1, 2]],

            # Chain - high rate, concurrency=1
            *[BenchmarkConfig("chain_bench", mode,
                            ["1", "100", "10", "1000.0", "10", "1"],
                            description=f"Chain: Mode {mode}, r=1000, R_L=10, conc=1",
                            trace_mode="lazy")
              for mode in [0, 1, 2]],

            # Chain - high rate, concurrency=0 (unlimited)
            *[BenchmarkConfig("chain_bench", mode,
                            ["1", "100", "10", "1000.0", "10", "0"],
                            description=f"Chain: Mode {mode}, r=1000, R_L=10, conc=0",
                            trace_mode="lazy")
              for mode in [0, 1, 2]],

            # Baseline cycle
            *[BenchmarkConfig("baseline_cycle_bench", mode,
                            ["1", "64", "15", "1.0", "2"],
                            description=f"Baseline: Mode {mode}, N_F=15, L=2",
                            trace_mode="lazy")
              for mode in [0, 1, 2]],

            # Siblings - low rate
            *[BenchmarkConfig("siblings_bench", mode,
                            ["1", "100", "10", "1.0", "10"],
                            description=f"Siblings: Mode {mode}, r=1, R_L=10",
                            trace_mode="lazy")
              for mode in [0, 1, 2]],

            # Siblings - high rate, concurrency=1
            *[BenchmarkConfig("siblings_bench", mode,
                            ["1", "100", "10", "1000.0", "10", "1"],
                            description=f"Siblings: Mode {mode}, r=1000, R_L=10, conc=1",
                            trace_mode="lazy")
              for mode in [0, 1, 2]],

            # Siblings - high rate, concurrency=0 (unlimited)
            *[BenchmarkConfig("siblings_bench", mode,
                            ["1", "100", "10", "1000.0", "10", "0"],
                            description=f"Siblings: Mode {mode}, r=1000, R_L=10, conc=0",
                            trace_mode="lazy")
              for mode in [0, 1, 2]],

            # Siblings - high rate, limited resources
            *[BenchmarkConfig("siblings_bench", mode,
                            ["1", "100", "10", "1000.0", "2"],
                            description=f"Siblings: Mode {mode}, r=1000, R_L=2",
                            trace_mode="lazy")
              for mode in [0, 1, 2]],

            # Tree - low rate
            *[BenchmarkConfig("tree_bench", mode,
                            ["1", "100", "15", "1.0", "10"],
                            description=f"Tree: Mode {mode}, r=1, R_L=10",
                            trace_mode="lazy")
              for mode in [0, 1, 2]],

            # Tree - high rate, concurrency=1
            *[BenchmarkConfig("tree_bench", mode,
                            ["1", "100", "15", "1000.0", "10", "1"],
                            description=f"Tree: Mode {mode}, r=1000, R_L=10, conc=1",
                            trace_mode="lazy")
              for mode in [0, 1, 2]],

            # Tree - high rate, concurrency=0 (unlimited)
            *[BenchmarkConfig("tree_bench", mode,
                            ["1", "100", "15", "1000.0", "10", "0"],
                            description=f"Tree: Mode {mode}, r=1000, R_L=10, conc=0",
                            trace_mode="lazy")
              for mode in [0, 1, 2]],

            # Tree - high rate, limited resources
            *[BenchmarkConfig("tree_bench", mode,
                            ["1", "100", "15", "1000.0", "2"],
                            description=f"Tree: Mode {mode}, r=1000, R_L=2",
                            trace_mode="lazy")
              for mode in [0, 1, 2]],
        ]
    ),

    # Dining Philosophers: Balanced servicing test
    "dining_philosophers": Workload(
        name="Dining Philosophers - Balanced Servicing Test",
        description="Test fairness across philosophers with varying sizes and work times (workloads.md 'Dining Philosophers')",
        expected_outcomes="Expect balanced servicing; Mode 2 should avoid starvation better than modes 0/1",
        configs=[
            # Small w_n (10ms): Quick test
            BenchmarkConfig("dining_philosophers_bench", mode,
                          ["1", str(n_phil), "10", "10.0"],
                          description=f"Mode {mode}, N={n_phil}, small work (10ms)")
            for mode in [0, 1, 2]
            for n_phil in [5, 10, 15]
        ] + [
            # Medium w_n (100ms): Standard test
            BenchmarkConfig("dining_philosophers_bench", mode,
                          ["1", str(n_phil), "10", "100.0"],
                          description=f"Mode {mode}, N={n_phil}, medium work (100ms)")
            for mode in [0, 1, 2]
            for n_phil in [5, 10, 15]
        ] + [
            # Large w_n (1000ms): Show overhead impact
            BenchmarkConfig("dining_philosophers_bench", mode,
                          ["1", "5", "10", "1000.0"],
                          description=f"Mode {mode}, N=5, large work (1000ms)")
            for mode in [0, 1, 2]
        ]
    ),
}

def list_workloads():
    """Print available workloads"""
    print("Available workloads:\n")
    for key, workload in WORKLOADS.items():
        print(f"  {key}")
        print(f"    Name: {workload.name}")
        print(f"    Description: {workload.description}")
        print(f"    Configs: {len(workload.configs)} benchmark runs")
        print()

def save_results(results: List[BenchmarkResult], output_file: Path):
    """Save results to JSON file"""
    output_data = {
        "timestamp": datetime.now().isoformat(),
        "total_runs": len(results),
        "results": [
            {
                "benchmark": r.config.benchmark,
                "mode": r.config.use_mode,
                "args": r.config.args,
                "description": r.config.description,
                "returncode": r.returncode,
                "execution_time_s": r.execution_time_s,
                "stdout": r.stdout,
                "stderr": r.stderr if r.stderr else None,
            }
            for r in results
        ]
    }

    with open(output_file, "w") as f:
        json.dump(output_data, f, indent=2)

    print(f"\nResults saved to: {output_file}")

def rename_trace_file_with_rate(stdout: str, args: List[str]):
    """Rename trace file to include generation rate in filename"""
    import re

    # Parse the trace filename from stdout
    # Look for pattern like "Lazy trace written to <filename>.json"
    match = re.search(r'(?:Lazy|Eager) trace written to (.+\.json)', stdout)
    if not match:
        return  # No trace file found in output

    original_filename = match.group(1).strip()

    # Check if file exists in resource_perf directory
    trace_path = SCRIPT_DIR / original_filename
    if not trace_path.exists():
        return  # Trace file not found

    # Extract generation rate and num_resources from args
    # For most benchmarks: args[3] is generation_rate, args[4] is num_resources
    # Format: [num_executions, num_inputs, num_nodes/num_philosophers, generation_rate, num_resources, ...]
    generation_rate = None
    num_resources = None
    try:
        if len(args) >= 4:
            generation_rate = float(args[3])
        if len(args) >= 5:
            num_resources = int(args[4])
    except (ValueError, IndexError):
        return  # Can't determine rate or resources

    if generation_rate is None:
        return

    # Create new filename with rate and optionally num_resources
    # Insert suffix before the .json extension
    base_name = original_filename.replace('.json', '')

    # Format rate as integer if close to whole number, otherwise with decimals
    if abs(generation_rate - round(generation_rate)) < 0.01:
        rate_str = f"_r{int(round(generation_rate))}"
    else:
        rate_str = f"_r{generation_rate:.1f}"

    # Add num_resources suffix if available
    suffix = rate_str
    if num_resources is not None:
        suffix += f"_rl{num_resources}"

    new_filename = f"{base_name}{suffix}.json"
    new_path = SCRIPT_DIR / new_filename

    # Rename the file
    try:
        trace_path.rename(new_path)
        print(f"  Renamed trace: {original_filename} -> {new_filename}")
    except Exception as e:
        print(f"  Warning: Could not rename trace file: {e}")

def run_workload(workload: Workload, force_rebuild: bool = False,
                config: str = "Release") -> List[BenchmarkResult]:
    """Run all configurations in a workload"""
    print(f"\n{'='*60}")
    print(f"Running workload: {workload.name}")
    print(f"Description: {workload.description}")
    print(f"Total configurations: {len(workload.configs)}")
    print(f"{'='*60}\n")

    results = []

    for i, bench_config in enumerate(workload.configs, 1):
        print(f"\n[{i}/{len(workload.configs)}] {bench_config.description}")

        # Build the benchmark
        try:
            exe_path = build_benchmark(bench_config.benchmark, bench_config.use_mode,
                                      config, force_rebuild, bench_config.trace_mode)
        except Exception as e:
            print(f"  SKIP: Build failed - {e}")
            continue

        # Run the benchmark
        try:
            result = run_benchmark(exe_path, bench_config.args)
            result.config = bench_config
            results.append(result)

            if result.returncode != 0:
                print(f"  WARNING: Non-zero exit code {result.returncode}")
                if result.stderr:
                    print(f"  stderr: {result.stderr[:200]}")

            # Rename trace file to include generation rate if this is a trace workload
            if bench_config.trace_mode and bench_config.trace_mode != "notrace":
                rename_trace_file_with_rate(result.stdout, bench_config.args)

        except Exception as e:
            print(f"  ERROR: Run failed - {e}")

    return results

def main():
    parser = argparse.ArgumentParser(
        description="Run resource_perf benchmarks based on workloads.md",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python run_benchmarks.py --workload chain_scaling
  python run_benchmarks.py --workload genie_siblings --output genie_results.json
  python run_benchmarks.py --list-workloads
        """
    )

    parser.add_argument("--workload", choices=list(WORKLOADS.keys()),
                       help="Predefined workload from workloads.md")
    parser.add_argument("--list-workloads", action="store_true",
                       help="List available workloads and exit")
    parser.add_argument("--output", type=Path, default=Path("results.json"),
                       help="Output file for results (default: results.json)")
    parser.add_argument("--force-rebuild", action="store_true",
                       help="Force rebuild even if executable exists")
    parser.add_argument("--config", default="Release", choices=["Debug", "Release"],
                       help="Build configuration (default: Release)")

    args = parser.parse_args()

    if args.list_workloads:
        list_workloads()
        return 0

    if not args.workload:
        parser.print_help()
        print("\nERROR: --workload required (use --list-workloads to see options)")
        return 1

    # Run the workload
    workload = WORKLOADS[args.workload]
    results = run_workload(workload, args.force_rebuild, args.config)

    # Save results
    if results:
        save_results(results, args.output)
        print(f"\nCompleted: {len(results)}/{len(workload.configs)} runs succeeded")
    else:
        print("\nNo results to save")
        return 1

    return 0

if __name__ == "__main__":
    sys.exit(main())
