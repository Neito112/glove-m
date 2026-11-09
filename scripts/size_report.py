"""Print the largest components from `idf.py size-components --json` output."""

import json
import sys

if sys.argv[1] == "-":
    data = json.load(sys.stdin)
else:
    with open(sys.argv[1], encoding="utf-8-sig") as handle:
        data = json.load(handle)

rows = sorted(data, key=lambda item: -item["flash"])
width = max(len(row["component"]) for row in rows)
print(f"{'component'.ljust(width)} {'flash':>9} {'iram':>8}")
for row in rows[:30]:
    print(f"{row['component'].ljust(width)} {row['flash']:9d} {row.get('iram', 0):8d}")
