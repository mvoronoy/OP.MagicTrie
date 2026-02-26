import json
import sys

def get_test_map(data):
    """Flattens the nested suite/cases into a map: {(suite_id, case_id): duration_ns}"""
    test_map = {}
    for suite in data.get("suites", []):
        s_id = suite.get("id")
        for case in suite.get("cases", []):
            c_id = case.get("id")
            test_map[(s_id, c_id)] = case.get("duration", {}).get("ns", 0)
    return test_map

def compare_results(old_file, new_file):
    with open(old_file, 'r') as f:
        old_map = get_test_map(json.load(f))
    with open(new_file, 'r') as f:
        new_data = json.load(f)
    
    print(f"{'SUITE.CASE':<40} | {'OLD (ms)':>10} | {'NEW (ms)':>10} | {'DIFF %':>10}")
    print("-" * 80)

    for suite in new_data.get("suites", []):
        s_id = suite.get("id")
        for case in suite.get("cases", []):
            c_id = case.get("id")
            new_ns = case.get("duration", {}).get("ns", 0)
            old_ns = old_map.get((s_id, c_id))

            if old_ns is not None:
                # Convert ns to ms for readability
                old_ms = old_ns / 1_000_000
                new_ms = new_ns / 1_000_000
                
                # Calculate % deviation: ((new - old) / old) * 100
                diff_pct = ((new_ns - old_ns) / old_ns * 100) if old_ns > 0 else 0
                
                # Highlight regressions (> 5% slower)
                flag = "!" if diff_pct > 5 else " "
                
                print(f"{s_id + '.' + c_id:<40} | {old_ms:>10.2f} | {new_ms:>10.2f} | {diff_pct:>9.1f}% {flag}")
            else:
                print(f"{s_id + '.' + c_id:<40} | {'NEW TEST':>35}")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python compare.py old.json new.json")
    else:
        compare_results(sys.argv[1], sys.argv[2])
