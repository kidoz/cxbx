set shell := ["powershell.exe", "-NoLogo", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command"]

build_dir := "build-min"

default:
    just --list

init:
    uv sync --dev

format:
    $files = git ls-files 'src/*' 'include/*' 'tests/*' | Where-Object { $_ -match '\.(c|cc|cpp|cxx|h|hh|hpp|hxx)$' }; if ($files) { clang-format -i --style=file --fallback-style=LLVM $files }; uv run ruff format tools/xtest

lint:
    if (!(Test-Path '{{build_dir}}\compile_commands.json')) { meson setup {{build_dir}} }; $files = git ls-files 'src/*' 'tests/*' | Where-Object { $_ -match '\.(c|cc|cpp|cxx)$' }; if ($files) { clang-tidy -p {{build_dir}} $files }; uv run ruff check tools/xtest; uv run mypy tools/xtest

build:
    meson compile -C {{build_dir}}
