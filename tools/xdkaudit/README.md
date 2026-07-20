# XDK audit workflow

`xdk_audit.py` inventories a locally available Xbox XDK and compares the
library-version records in its prebuilt XBEs with CXBX's registered HLE tables.
It also counts library dependencies across the SDK sample projects.

The tool emits metadata only. It does not copy SDK headers, libraries, samples,
or executable contents into the repository.

## Configure

Either pass the SDK root explicitly:

```powershell
python tools/xdkaudit/xdk_audit.py --xdk-root "<XDK_ROOT>"
```

or set the local-only configuration value shown in `tools/config.toml.example`.

## Use

Human-readable audit:

```powershell
python tools/xdkaudit/xdk_audit.py
```

Stable JSON for comparison or scripting:

```powershell
python tools/xdkaudit/xdk_audit.py --json > xdk-audit.json
```

Treat `candidate` as an investigation queue, not permission to add a table.
CRT, kernel, and application-private library records are not XDK HLE APIs.
Every new HLE entry still requires a focused probe, a unique signature, and an
independent patch-address check.
