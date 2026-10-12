"""Convert a TI InterConnect Studio HEX image into the firmware C array."""

import argparse
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
START, END = 0x200, 0x400

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument(
    "source",
    nargs="?",
    type=Path,
    default=REPO / "cpld/led_direct/program.hex",
    help="InterConnect Studio program.hex (default: cpld/led_direct/program.hex)",
)
parser.add_argument(
    "--output",
    type=Path,
    default=REPO / "H503_Peripheral_DBGer/Core/Inc/tpld2001_image.h",
)
args = parser.parse_args()
source = args.source.resolve()
output = args.output.resolve()
image = bytearray(END - START)
seen = set()
eof = False

for line_no, line in enumerate(source.read_text().splitlines(), 1):
    line = line.strip()
    if not line:
        continue
    if not line.startswith(":"):
        raise ValueError(f"line {line_no}: missing colon")
    record = bytes.fromhex(line[1:])
    # InterConnect Studio 1.8 omits the address MSB from its data-record
    # checksum. Accept that known format and standard Intel HEX checksums.
    if len(record) != 5 + record[0] or (sum(record) & 0xFF) not in (0, record[1]):
        raise ValueError(f"line {line_no}: invalid length or checksum")
    count = record[0]
    address = (record[1] << 8) | record[2]
    kind = record[3]
    if kind == 1:
        if count:
            raise ValueError(f"line {line_no}: invalid EOF")
        eof = True
        break
    if kind != 0 or address < START or address + count > END:
        raise ValueError(f"line {line_no}: unexpected record")
    for offset, value in enumerate(record[4:4 + count]):
        target = address + offset
        if target in seen:
            raise ValueError(f"line {line_no}: duplicate address {target:#x}")
        seen.add(target)
        image[target - START] = value

if not eof or len(seen) != END - START:
    raise ValueError(f"image incomplete: {len(seen)} / {END - START} bytes")

source_name = source.relative_to(REPO).as_posix() if source.is_relative_to(REPO) else source.name
lines = [
    f"/* Generated from {source_name}; do not edit by hand. */",
    "#ifndef TPLD2001_IMAGE_H",
    "#define TPLD2001_IMAGE_H",
    "#include <stdint.h>",
    "#define TPLD2001_IMAGE_SIZE 512U",
    "static const uint8_t tpld2001_image[TPLD2001_IMAGE_SIZE] = {",
]
for offset in range(0, len(image), 16):
    lines.append("  " + ", ".join(f"0x{value:02X}" for value in image[offset:offset + 16]) + ",")
lines += ["};", "#endif", ""]
output.write_text("\n".join(lines), encoding="ascii")
print(f"Wrote {output} from {source_name}")
