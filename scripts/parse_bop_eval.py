#!/usr/bin/env python3
import argparse
import csv
from pathlib import Path


CONFIGS = ["no_pref", "nextline", "stream", "stride", "bop"]


def read_stats(path):
  stats = {}
  if not path.exists():
    return stats
  with path.open(newline="") as f:
    for row in csv.reader(f):
      if len(row) < 3:
        continue
      name = row[0].strip()
      if not name or name == "Core":
        continue
      try:
        stats[name] = float(row[2].strip())
      except ValueError:
        continue
  return stats


def get(stats, name):
  return stats.get(f"{name}_total_count", stats.get(name, 0.0))


def safe_div(num, den):
  return num / den if den else 0.0


def parse_run(run_dir):
  core = read_stats(run_dir / "core.stat.0.csv")
  memory = read_stats(run_dir / "memory.stat.0.csv")
  pref = read_stats(run_dir / "pref.stat.0.csv")
  stream = read_stats(run_dir / "stream.stat.0.csv")
  l2l1 = read_stats(run_dir / "l2l1pref.stat.0.csv")
  bop = read_stats(run_dir / "pref_bop.stat.0.csv")

  inst = get(core, "NODE_INST_COUNT")
  cycles = get(core, "NODE_CYCLE")
  dcache_miss = get(memory, "DCACHE_MISS")
  dcache_hit = get(memory, "DCACHE_HIT")
  mlc_miss = get(memory, "MLC_MISS")
  mlc_hit = get(memory, "MLC_HIT")

  queue_issued = get(pref, "PREF_UL1REQ_QUEUE_SENTREQ") + get(pref, "PREF_UMLC_REQ_QUEUE_SENTREQ")
  issued = max(
      queue_issued,
      get(bop, "BOP_PREF_ISSUED"),
      get(l2l1, "L2NEXT_PREF_REQ"),
      get(stream, "STREAM_BUFFER_REQ"),
  )
  useful = max(
      get(pref, "L1_PREF_UNIQUE_HIT"),
      get(pref, "L1_PREF_HIT"),
      get(l2l1, "L2NEXT_PREF_HIT_DATA_REQ") + get(l2l1, "L2NEXT_PREF_HIT_DATA_IN_CACHE"),
  )

  return {
      "completed": 1.0 if (run_dir / "memory.stat.0.csv").exists() else 0.0,
      "instructions": inst,
      "cycles": cycles,
      "ipc": safe_div(inst, cycles),
      "dcache_hit": dcache_hit,
      "dcache_miss": dcache_miss,
      "dcache_miss_rate": safe_div(dcache_miss, dcache_miss + dcache_hit),
      "mlc_hit": mlc_hit,
      "mlc_miss": mlc_miss,
      "mlc_miss_rate": safe_div(mlc_miss, mlc_miss + mlc_hit),
      "pref_issued": issued,
      "pref_useful": useful,
      "pref_accuracy": safe_div(useful, issued),
      "bop_pref_issued": get(bop, "BOP_PREF_ISSUED"),
      "bop_rr_fill_updates": get(bop, "BOP_RR_FILL_UPDATES"),
      "bop_rr_hit_score_inc": get(bop, "BOP_RR_HIT_SCORE_INC"),
      "bop_phase_on": get(bop, "BOP_PHASE_PREF_ON"),
      "bop_phase_off": get(bop, "BOP_PHASE_PREF_OFF"),
      "bop_dropped_page": get(bop, "BOP_PREF_DROPPED_PAGE"),
  }


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument("simhome", type=Path)
  parser.add_argument("--experiment", default="bop_eval")
  parser.add_argument("--output", type=Path, required=True)
  args = parser.parse_args()

  rows = []
  for workload_dir in sorted(p for p in args.simhome.iterdir() if p.is_dir()):
    workload = workload_dir.name
    base_dir = workload_dir / args.experiment
    if not base_dir.exists():
      continue

    parsed = {config: parse_run(base_dir / config) for config in CONFIGS}
    no_pref_ipc = parsed["no_pref"]["ipc"]
    no_pref_dcache_miss = parsed["no_pref"]["dcache_miss"]
    no_pref_mlc_miss = parsed["no_pref"]["mlc_miss"]

    for config in CONFIGS:
      data = parsed[config]
      row = {
          "workload": workload,
          "config": config,
          **data,
          "speedup_vs_no_pref": safe_div(data["ipc"], no_pref_ipc),
          "dcache_miss_reduction_vs_no_pref": 1.0 - safe_div(data["dcache_miss"], no_pref_dcache_miss)
          if no_pref_dcache_miss
          else 0.0,
          "mlc_miss_reduction_vs_no_pref": 1.0 - safe_div(data["mlc_miss"], no_pref_mlc_miss)
          if no_pref_mlc_miss
          else 0.0,
          "pref_coverage_vs_no_pref": safe_div(data["pref_useful"], no_pref_dcache_miss),
      }
      rows.append(row)

  fieldnames = [
      "workload",
      "config",
      "completed",
      "instructions",
      "cycles",
      "ipc",
      "speedup_vs_no_pref",
      "dcache_hit",
      "dcache_miss",
      "dcache_miss_rate",
      "dcache_miss_reduction_vs_no_pref",
      "mlc_hit",
      "mlc_miss",
      "mlc_miss_rate",
      "mlc_miss_reduction_vs_no_pref",
      "pref_issued",
      "pref_useful",
      "pref_accuracy",
      "pref_coverage_vs_no_pref",
      "bop_pref_issued",
      "bop_rr_fill_updates",
      "bop_rr_hit_score_inc",
      "bop_phase_on",
      "bop_phase_off",
      "bop_dropped_page",
  ]

  args.output.parent.mkdir(parents=True, exist_ok=True)
  with args.output.open("w", newline="") as f:
    writer = csv.DictWriter(f, fieldnames=fieldnames)
    writer.writeheader()
    writer.writerows(rows)

  print(f"Wrote {len(rows)} rows to {args.output}")


if __name__ == "__main__":
  main()
