set shell := ["powershell.exe", "-NoLogo", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command"]

build_dir := "build-min"

default:
    just --list

init:
    uv sync --dev

format:
    $files = git ls-files 'src/*' 'include/*' 'tests/*' | Where-Object { $_ -match '\.(c|cc|cpp|cxx|h|hh|hpp|hxx)$' }; if ($files) { clang-format -i --style=file --fallback-style=LLVM $files }; uv run ruff format tools/xtest

lint:
    if (!(Test-Path '{{build_dir}}\compile_commands.json')) { meson setup {{build_dir}} }; .github/scripts/clang_tidy_diff.ps1 -BuildDirectory '{{build_dir}}' -BaseRevision 'origin/master'; uv run ruff check tools/xtest; uv run mypy tools/xtest

lint-full:
    if (!(Test-Path '{{build_dir}}\compile_commands.json')) { meson setup {{build_dir}} }; $files = git ls-files 'src/*' 'tests/*' | Where-Object { $_ -match '\.(c|cc|cpp|cxx)$' }; if ($files) { clang-tidy -p {{build_dir}} $files }; uv run ruff check tools/xtest; uv run mypy tools/xtest

build:
    meson compile -C {{build_dir}}

# Debug-trace build in its own directory: function-entry/patch traces,
# EmuWarning bodies, and the szFuncName table entries. Setup is skipped when
# the directory already exists, so this is safe to re-run.
build-debug:
    if (!(Test-Path build-debug)) { meson setup build-debug "-Dcpp_args=['-D_DEBUG_TRACE','-D_DEBUG_WARNINGS']" "-Dc_args=['-D_DEBUG_TRACE','-D_DEBUG_WARNINGS']" }
    meson compile -C build-debug
