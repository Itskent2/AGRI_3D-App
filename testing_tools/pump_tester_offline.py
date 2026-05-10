"""
pump_tester_offline.py
======================
Offline / Seed-only version of the AGRI-3D Pump Tester.

No ESP32, no WebSocket, no network required.
Supply a random seed → get a full trial schedule printed up front.
Then physically run each trial, type in the ml you measured, and
get the same CSV output as the live tester.

Usage:
    python pump_tester_offline.py
"""

import csv
import json
import os
import random
import time


# ─────────────────────────────────────────────────────────────────────────────
# Default calibrated flow rates (ml/sec).
# These are overridden by pump_calibration.json if it exists.
# ─────────────────────────────────────────────────────────────────────────────
DEFAULT_FLOW_RATES = {
    "Sector 1": 23.9,
    "Sector 2": 23.9,
    "Sector 3": 2.0,
    "Sector 4": 2.0,
}

CALIBRATION_FILE = "pump_calibration.json"


def load_calibration(flow_rates: dict) -> dict:
    """Load flow rates from pump_calibration.json if present."""
    if os.path.exists(CALIBRATION_FILE):
        try:
            with open(CALIBRATION_FILE, "r") as f:
                data = json.load(f)
                flow_rates.update(data)
                print(f"[✓] Loaded calibration from {CALIBRATION_FILE}")
        except Exception as e:
            print(f"[⚠] Error loading calibration: {e}")
    else:
        print(f"[i] No {CALIBRATION_FILE} found — using default flow rates.")
    return flow_rates


def prompt_int(msg: str, default: int) -> int:
    raw = input(msg).strip()
    try:
        return int(raw) if raw else default
    except ValueError:
        return default


def prompt_float(msg: str) -> float | None:
    raw = input(msg).strip()
    try:
        return float(raw)
    except ValueError:
        return None


def generate_schedule(pump_name: str, num_trials: int, seed: int | None) -> list[float]:
    """Return a list of random durations for each trial."""
    if seed is not None:
        random.seed(seed)
        print(f"  \033[92m[✓] Using random seed: {seed}\033[0m")
    else:
        print("  \033[33m[i] No seed set — using truly random sequence.\033[0m")

    durations = []
    for _ in range(num_trials):
        if pump_name == "Sector 1":
            d = round(random.uniform(1.0, 8.0), 2)
        elif pump_name == "Sector 2":
            d = 5.0
        elif pump_name == "Sector 3":
            d = round(random.uniform(1.0, 30.0), 2)
        elif pump_name == "Sector 4":
            d = 30.0
        else:
            d = 1.0
        durations.append(d)
    return durations


def print_schedule(durations: list[float], flow_rate: float, pump_name: str):
    """Print the full trial schedule so you can see it before starting."""
    print("\n" + "=" * 55)
    print(f"  FULL TRIAL SCHEDULE — {pump_name.upper()}")
    print(f"  Flow rate used : {flow_rate} ml/sec")
    print("=" * 55)
    print(f"  {'Trial':>5}  {'Duration (s)':>13}  {'Est. Volume (ml)':>18}")
    print("  " + "-" * 40)
    for i, d in enumerate(durations, start=1):
        est = d * flow_rate
        print(f"  {i:>5}  {d:>13.2f}  {est:>18.1f}")
    print("=" * 55)


def main():
    print("=" * 55)
    print("  AGRI-3D OFFLINE PUMP TESTER  (no ESP32 needed)")
    print("=" * 55)

    # ── Load calibration ──────────────────────────────────────────────────
    flow_rates = load_calibration(dict(DEFAULT_FLOW_RATES))

    print("\nSelect Test Mode:")
    print("  1. Sector 1: Water (Random 1-8s)")
    print("  2. Sector 2: Water (Fixed 5s - Consistency)")
    print("  3. Sector 3: Fertilizer (Random 1-30s)")
    print("  4. Sector 4: Fertilizer (Fixed 30s - Consistency)")
    choice = input("Choice (1-4) [default 1]: ").strip() or "1"
    
    mapping = {
        "1": "Sector 1",
        "2": "Sector 2",
        "3": "Sector 3",
        "4": "Sector 4"
    }
    pump_name = mapping.get(choice, "Sector 1")
    default_rate = flow_rates.get(pump_name, 23.9)
    print(f"\n\033[96m[INFO] Using default flow rate for {pump_name}: {default_rate} ml/sec\033[0m")

    # ── Manual flow rate override ─────────────────────────────────────────
    rate_raw = input(f"  Enter flow rate ml/sec [press ENTER to keep {default_rate}]: ").strip()
    if rate_raw:
        try:
            flow_rate = float(rate_raw)
            print(f"  \033[92m[✓] Using custom flow rate: {flow_rate} ml/sec\033[0m")
        except ValueError:
            flow_rate = default_rate
            print(f"  \033[33m[⚠] Invalid input — keeping {flow_rate} ml/sec\033[0m")
    else:
        flow_rate = default_rate
        print(f"  \033[90m[i] Keeping calibrated rate: {flow_rate} ml/sec\033[0m")

    # ── Seed ──────────────────────────────────────────────────────────────
    seed_raw = input("\nEnter random seed (press ENTER to skip): ").strip()
    seed: int | None = None
    if seed_raw:
        try:
            seed = int(seed_raw)
        except ValueError:
            print("  \033[31m[⚠] Invalid seed — using random sequence.\033[0m")

    # ── Number of trials ─────────────────────────────────────────────────
    num_trials = prompt_int("Number of trials [default 25]: ", 25)

    # ── Generate & display schedule ───────────────────────────────────────
    durations = generate_schedule(pump_name, num_trials, seed)
    print_schedule(durations, flow_rate, pump_name)

    # ── CSV output files ──────────────────────────────────────────────────
    timestamp = int(time.time())
    filename = f"pump_results_{pump_name.lower()}_{timestamp}.csv"
    dryrun_filename = f"pump_dryrun_{pump_name.lower()}_{timestamp}.csv"

    with open(dryrun_filename, mode="w", newline="") as df:
        df_writer = csv.writer(df)
        df_writer.writerow(["Trial", "Target_Seconds", "Target_ML"])
        for i, d in enumerate(durations, start=1):
            df_writer.writerow([i, d, round(d * flow_rate, 1)])

    print(f"\n[i] Dryrun schedule saved to: {dryrun_filename}")
    print(f"[i] Final results will be saved to: {filename}")
    input("\nPress ENTER when you are ready to begin the trials…")

    # ── Run trials ────────────────────────────────────────────────────────
    with open(filename, mode="w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["Trial", "Target_Seconds", "Target_ML", "Actual_ML", "ML_Per_Sec"])

        for trial, duration in enumerate(durations, start=1):
            target_ml = round(duration * flow_rate, 1)

            print("\n" + "=" * 50)
            print(f"  Trial {trial} of {num_trials}  —  {pump_name}")
            print(f"  Run pump for : \033[93m{duration:.2f} s\033[0m")
            print(f"  Target volume: \033[93m{target_ml:.1f} ml\033[0m")
            print("=" * 50)

            print(f"\n\033[93m[!] Physically run the pump for {duration:.2f} seconds, then measure.\033[0m")

            # Prompt for actual measured volume
            while True:
                ml = prompt_float("  > Enter measured volume (ml): ")
                if ml is not None:
                    break
                print("  [!] Not a valid number — try again.")

            ml_per_sec = round(ml / duration, 3)
            print(f"  \033[96m→ Flow rate this trial: {ml_per_sec} ml/sec\033[0m")

            # Also print the CSV-ready line (mirrors original script style)
            time_ms = int(duration * 1000)
            print(f"  \033[90m[CSV] {trial}, {duration:.2f}, {target_ml:.1f}, {ml:.1f}, {ml_per_sec}\033[0m")

            writer.writerow([trial, duration, target_ml, ml, ml_per_sec])
            f.flush()

    # ── Summary ───────────────────────────────────────────────────────────
    print("\n" + "=" * 55)
    print(f"  [✓] Testing complete! Results saved to:")
    print(f"      {filename}")
    print("=" * 55)

    # Quick stats
    print("\nQuick summary (from this session):\n")
    with open(filename, newline="") as f:
        rows = list(csv.DictReader(f))

    if rows:
        rates = [float(r["ML_Per_Sec"]) for r in rows]
        avg   = sum(rates) / len(rates)
        mn    = min(rates)
        mx    = max(rates)
        print(f"  Trials completed : {len(rows)}")
        print(f"  Avg ml/sec       : {avg:.3f}")
        print(f"  Min ml/sec       : {mn:.3f}")
        print(f"  Max ml/sec       : {mx:.3f}")
        print(f"\n  Recommended calibration value: \033[92m{avg:.3f} ml/sec\033[0m")


if __name__ == "__main__":
    main()
