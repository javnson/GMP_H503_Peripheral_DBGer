"""Convert the InterConnect Studio Intel HEX image to a checked C array."""

from pathlib import Path

HERE = Path(__file__).resolve().parent
START, END = 0x200, 0x400
image = bytearray(END - START)
seen = set()
eof = False

for line_no, line in enumerate((HERE / "program.hex").read_text().splitlines(), 1):
    line = line.strip()
    if not line:
        continue
    if not line.startswith(":"):
        raise ValueError(f"line {line_no}: missing colon")
    record = bytes.fromhex(line[1:])
    # InterConnect Studio 1.8 omits the address MSB from its data-record
    # checksum (all 0x02xx records sum to 2, all 0x03xx records sum to 3).
    # Accept that known format as well as ordinary Intel HEX checksums.
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

lines = [
    "/* Generated from cpld/pa1_to_pa0/program.hex; do not edit by hand. */",
    "#ifndef TPLD2001_IMAGE_H",
    "#define TPLD2001_IMAGE_H",
    "#include <stdint.h>",
    "#define TPLD2001_IMAGE_SIZE 512U",
    "static const uint8_t tpld2001_image[TPLD2001_IMAGE_SIZE] = {",
]
for offset in range(0, len(image), 16):
    lines.append("  " + ", ".join(f"0x{value:02X}" for value in image[offset:offset + 16]) + ",")
lines += ["};", "#endif", ""]
(HERE / "../../H503_Peripheral_DBGer/Core/Inc/tpld2001_image.h").resolve().write_text(
    "\n".join(lines), encoding="ascii"
)
