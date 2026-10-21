#!/usr/bin/env python3
"""Turn schema and font data into compile-time byte arrays."""
import pathlib
import sys

schema, regular, bold, output = map(pathlib.Path, sys.argv[1:])
items = {
    "schema": schema.read_bytes(),
    "noto_regular": regular.read_bytes(),
    "noto_bold": bold.read_bytes(),
}
lines = ["#pragma once", "#include <cstddef>", "namespace lmdraw::embedded {"]
for name, data in items.items():
    values = ",".join(f"0x{byte:02x}" for byte in data)
    lines.append(f"inline constexpr unsigned char {name}[] = {{{values}}};")
    lines.append(f"inline constexpr std::size_t {name}_size = sizeof({name});")
lines.append("}")
output.write_text("\n".join(lines) + "\n", encoding="ascii")
