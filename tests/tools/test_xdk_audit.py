#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/xdkaudit"))

import xdk_audit  # noqa: E402


def write_xbe(path: Path, libraries: list[tuple[str, int, int, int, int]]) -> None:
    image_base = 0x00010000
    versions_offset = 0x180
    data = bytearray(versions_offset + len(libraries) * 16)
    data[:4] = b"XBEH"
    struct.pack_into("<I", data, 0x104, image_base)
    struct.pack_into("<I", data, 0x160, len(libraries))
    struct.pack_into("<I", data, 0x164, image_base + versions_offset)
    for index, (name, major, minor, build, flags) in enumerate(libraries):
        encoded_name = name.encode("ascii").ljust(8, b"\0")
        struct.pack_into(
            "<8sHHHH",
            data,
            versions_offset + index * 16,
            encoded_name,
            major,
            minor,
            build,
            flags,
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


class XdkAuditTests(unittest.TestCase):
    def test_audit_aggregates_metadata_and_hle_coverage(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            xdk = root / "xdk"
            repo = root / "repo"
            (xdk / "xbox/lib").mkdir(parents=True)
            (xdk / "Samples/Xbox/Graphics/Test").mkdir(parents=True)
            (repo / "src/cxbx/src/hle/dispatch").mkdir(parents=True)
            (xdk / "xbox/lib/d3d8.lib").write_bytes(b"archive")
            (xdk / "xbox/lib/d3d8i.lib").write_bytes(b"archive")
            (xdk / "Samples/Xbox/Graphics/Test/Test.vcproj").write_text(
                '<VisualStudioProject><Tool AdditionalDependencies="d3d8.lib xapilib.lib"/>'
                "</VisualStudioProject>",
                encoding="utf-8",
            )
            (repo / "src/cxbx/src/hle/dispatch/hle_database.cpp").write_text(
                '{ "D3D8", 1, 0, 5849, D3D8_1_0_5849, D3D8_1_0_5849_SIZE },',
                encoding="utf-8",
            )
            write_xbe(
                xdk / "bin/default.xbe",
                [("D3D8", 1, 0, 5849, 0), ("XACTENG", 1, 0, 5849, 0)],
            )

            report = xdk_audit.audit_xdk(xdk, repo)

        self.assertEqual(report["inventory"]["libraries"], 2)
        self.assertEqual(report["inventory"]["sample_projects"], 1)
        self.assertEqual(report["sample_dependencies"][0], {"library": "d3d8.lib", "projects": 1})
        requirements = {row["library"]: row for row in report["xbe_requirements"]}
        self.assertEqual(requirements["D3D8"]["classification"], "covered")
        self.assertEqual(requirements["D3D8"]["table"], "D3D8_1_0_5849")
        self.assertEqual(requirements["XACTENG"]["classification"], "candidate")

    def test_rejects_truncated_xbe_library_table(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bad.xbe"
            data = bytearray(0x180)
            data[:4] = b"XBEH"
            struct.pack_into("<I", data, 0x104, 0x00010000)
            struct.pack_into("<I", data, 0x160, 1)
            struct.pack_into("<I", data, 0x164, 0x00010180)
            path.write_bytes(data)
            with self.assertRaisesRegex(xdk_audit.AuditError, "truncated"):
                xdk_audit.parse_xbe_libraries(path)


if __name__ == "__main__":
    unittest.main()
