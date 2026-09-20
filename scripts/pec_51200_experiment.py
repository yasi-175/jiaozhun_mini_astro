#!/usr/bin/env python3
"""Run the 14.4-minute / 51200-step PEC experiment (3 + 3 periods)."""
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))
from pec_25600_experiment import main  # noqa: E402

if __name__ == "__main__":
    # Keep this invocation unambiguous and store data separately from the
    # earlier 25600-step experiment.
    sys.argv[1:] += ["--period-steps", "51200", "--cycles", "3"]
    if "--output" not in sys.argv:
        sys.argv[1:] += ["--output", "/home/q/workspace/jiaozhun_mini_astro/pec_51200_experiment"]
    raise SystemExit(main())
