#!/usr/bin/env python3
"""Convert supplier daily XLSX exports to Multical 21 history CSV."""

import argparse
import calendar
import csv
import re
import zipfile
import xml.etree.ElementTree as ET
from pathlib import Path


MONTHS = {
    "januar": 1,
    "februar": 2,
    "marts": 3,
    "april": 4,
    "maj": 5,
    "juni": 6,
    "juli": 7,
    "august": 8,
    "september": 9,
    "oktober": 10,
    "november": 11,
    "december": 12,
}


def column_index(cell_ref: str) -> int:
    letters = ""
    for char in cell_ref:
        if char.isalpha():
            letters += char.upper()
        else:
            break
    index = 0
    for char in letters:
        index = index * 26 + (ord(char) - ord("A") + 1)
    return index - 1


def read_first_sheet_rows(path: Path):
    namespace = {"x": "http://schemas.openxmlformats.org/spreadsheetml/2006/main"}
    with zipfile.ZipFile(path) as archive:
        with archive.open("xl/worksheets/sheet1.xml") as handle:
            root = ET.parse(handle).getroot()

    rows = []
    for row in root.findall(".//x:sheetData/x:row", namespace):
        values = {}
        for cell in row.findall("x:c", namespace):
            ref = cell.attrib.get("r", "")
            value_node = cell.find("x:v", namespace)
            if not ref or value_node is None or value_node.text is None:
                continue
            values[column_index(ref)] = value_node.text
        if values:
            max_col = max(values)
            rows.append([values.get(i) for i in range(max_col + 1)])
    return rows


def parse_month_file(path: Path):
    name = path.name.lower()
    match = None
    month = None
    for month_name, month_number in MONTHS.items():
        match = re.search(rf" - {month_name} (\d{{4}})(?: \(\d+\))?\.xlsx$", name)
        if match:
            month = month_number
            break
    if not match:
        return []

    year = int(match.group(1))
    max_day = calendar.monthrange(year, month)[1]
    rows = []
    for row in read_first_sheet_rows(path)[1:]:
        try:
            day = int(float(row[0])) if len(row) > 0 and row[0] is not None else None
            usage = float(row[2]) if len(row) > 2 and row[2] is not None else None
        except ValueError:
            continue
        if day is not None and usage is not None and 1 <= day <= max_day:
            rows.append((f"{year:04d}-{month:02d}-{day:02d}", round(usage, 3)))
    return rows


def main():
    parser = argparse.ArgumentParser(description="Build date,usage_m3 CSV from supplier XLSX exports.")
    parser.add_argument("input", type=Path, help="Folder with 77513579 monthly .xlsx files")
    parser.add_argument("output", type=Path, help="Output CSV path")
    args = parser.parse_args()

    daily = {}
    for path in sorted(args.input.glob("77513579 - *.xlsx")):
        for date, usage in parse_month_file(path):
            daily[date] = usage

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["date", "usage_m3"])
        for date in sorted(daily):
            writer.writerow([date, f"{daily[date]:.3f}"])

    print(f"Wrote {len(daily)} days to {args.output}")
    print(f"Total {sum(daily.values()):.3f} m3")


if __name__ == "__main__":
    main()
