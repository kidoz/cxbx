"""Give the launcher workflow test an isolated output directory."""

import os
import subprocess
import sys
import tempfile
from pathlib import Path

with tempfile.TemporaryDirectory(prefix="cxbx-launcher-test-") as directory:
    environment = os.environ.copy()
    log = Path(directory) / "session.log"
    environment["CXBX_LOG_FILE"] = str(log)
    environment["CXBX_LOG_UNBUFFERED"] = "1"
    result = subprocess.run([sys.argv[1], directory], check=False, timeout=30, env=environment)
    if log.exists():
        print(log.read_text(errors="replace"))
    if result.returncode == 0 and len(sys.argv) > 2:
        result = subprocess.run(
            [sys.argv[2], "--ui-smoke-test", str(Path(directory) / "source.xbe")],
            check=False,
            timeout=20,
            env=environment,
        )
    sys.exit(0 if result.returncode == 0 else 1)
