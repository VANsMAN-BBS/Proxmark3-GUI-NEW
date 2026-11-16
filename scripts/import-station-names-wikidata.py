# Import zh/en station names from Wikidata by exact Japanese label match.
# Usage: python scripts/import-station-names-wikidata.py
# Requires network access to query.wikidata.org. The result is merged with the
# existing CSV, preserving verified community entries.

import csv
import json
import pathlib
import urllib.parse
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
CODES = ROOT / "resources" / "felica" / "station_codes.csv"
OUT = ROOT / "resources" / "felica" / "station_names_i18n.csv"

def load_names():
    names = set()
    with CODES.open(encoding="utf-8-sig", newline="") as f:
        for row in csv.reader(f):
            if len(row) >= 6:
                name = row[5].strip()
                if name:
                    names.add(name)
    return names

def query(batch):
    values = " ".join('"%s"@ja' % n.replace('"', '\\"') for n in batch)
    sparql = (
        "SELECT ?ja ?zh ?en WHERE {"
        " ?item wdt:P17 wd:Q17 ."
        " ?item wdt:P31/wdt:P279* wd:Q55488 ."
        " ?item rdfs:label ?ja . FILTER(LANG(?ja)=\"ja\")"
        " OPTIONAL { ?item rdfs:label ?zh . FILTER(LANG(?zh)=\"zh\") }"
        " OPTIONAL { ?item rdfs:label ?en . FILTER(LANG(?en)=\"en\") }"
        " VALUES ?ja { " + values + " } }"
    )
    url = "https://query.wikidata.org/sparql?" + urllib.parse.urlencode(
        {"format": "json", "query": sparql})
    req = urllib.request.Request(url, headers={"User-Agent": "Proxmark3GUI/0.4.1"})
    with urllib.request.urlopen(req, timeout=120) as response:
        return json.loads(response.read().decode("utf-8"))["results"]["bindings"]

def main():
    names = sorted(load_names())
    found = {}
    for start in range(0, len(names), 80):
        batch = names[start:start + 80]
        try:
            rows = query(batch)
        except Exception as error:
            print("batch failed:", error)
            continue
        for row in rows:
            ja = row["ja"]["value"]
            if ja not in names:
                continue
            found[ja] = (row.get("zh", {}).get("value", ""),
                         row.get("en", {}).get("value", ""))
        print("processed", start + len(batch), "/", len(names), "matched", len(found))

    existing = {}
    if OUT.exists():
        with OUT.open(encoding="utf-8", newline="") as f:
            for row in csv.reader(f):
                if len(row) >= 5 and row[0] != "ja":
                    existing[row[0]] = row

    merged = []
    for ja, row in existing.items():
        merged.append(row)
    for ja, (zh, en) in found.items():
        if ja in existing:
            continue
        merged.append([ja, zh, en, "wikidata", "community"])
    merged.sort(key=lambda r: r[0])

    with OUT.open("w", encoding="utf-8", newline="\n") as f:
        writer = csv.writer(f, lineterminator="\n")
        writer.writerow(["ja", "zh", "en", "source", "verified"])
        writer.writerows(merged)
    print("wrote", OUT, "rows", len(merged))

if __name__ == "__main__":
    main()
