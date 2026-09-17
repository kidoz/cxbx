"""Regenerate checked-in SPIR-V from the adjacent GLSL sources.

Requires glslangValidator and spirv-val from a Vulkan SDK on PATH, or explicit
--compiler and --validator paths. Ordinary builds use the checked-in header
and do not need these tools.
"""

import argparse
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


def run_tool(command: list[str]) -> None:
    subprocess.run(
        command,
        check=True,
        timeout=60,
        creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0,
    )


def generate_shaders(compiler: str, validator: str) -> None:
    source_directory = Path(__file__).resolve().parent
    lines = [
        "// Generated from fixed_function.vert and combiner.frag by generate_shaders.py.",
        "// Target: Vulkan 1.3. Edit GLSL sources, then regenerate; do not edit SPIR-V.",
        "#ifndef CXBX_HLE_D3D8_VULKAN_SHADER_SPIRV_H",
        "#define CXBX_HLE_D3D8_VULKAN_SHADER_SPIRV_H",
        "#include <cstdint>",
    ]
    with tempfile.TemporaryDirectory(prefix="cxbx-shaders-") as temporary_directory:
        for name, source in (("Vertex", "fixed_function.vert"), ("Fragment", "combiner.frag")):
            binary = Path(temporary_directory) / f"{name}.spv"
            run_tool(
                [
                    compiler,
                    "-V",
                    "--target-env",
                    "vulkan1.3",
                    # Avoid requiring the shaderDemoteToHelperInvocation feature.
                    "--discard-is-terminate",
                    str(source_directory / source),
                    "-o",
                    str(binary),
                ]
            )
            run_tool([validator, "--target-env", "vulkan1.3", str(binary)])
            data = binary.read_bytes()
            words = struct.unpack(f"<{len(data) // 4}I", data)
            lines.append(f"static constexpr std::uint32_t k{name}ShaderSpirv[] = {{")
            for offset in range(0, len(words), 8):
                lines.append(
                    "    " + ", ".join(f"0x{word:08X}" for word in words[offset : offset + 8]) + ","
                )
            lines.append("};")
            lines.append(f"static constexpr unsigned int k{name}ShaderSpirvCount = {len(words)};")
    lines.append("#endif")
    # Preserve the existing header if either shader fails compilation or validation.
    (source_directory / "shader_spirv.h").write_text(
        "\n".join(lines) + "\n", encoding="utf-8", newline="\n"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="glslangValidator", help="GLSL compiler executable")
    parser.add_argument("--validator", default="spirv-val", help="SPIR-V validator executable")
    args = parser.parse_args()
    try:
        generate_shaders(args.compiler, args.validator)
    except (OSError, subprocess.SubprocessError, struct.error) as error:
        print(f"Shader generation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
