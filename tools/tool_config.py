#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Shared local TOML configuration loader for tools/ scripts."""

import os
import tomllib
from pathlib import Path
from typing import Any

Config = dict[str, Any]

CONFIG_ENV = "CXBX_TOOLS_CONFIG"
TOOLS_DIR = Path(__file__).resolve().parent
ROOT = TOOLS_DIR.parent
DEFAULT_CONFIG = TOOLS_DIR / "config.toml"


def repo_root() -> Path:
    return ROOT


def config_path() -> Path:
    raw = os.environ.get(CONFIG_ENV)
    path = Path(raw).expanduser() if raw else DEFAULT_CONFIG
    return path if path.is_absolute() else (ROOT / path).resolve()


def load_config(required: bool = True) -> Config:
    path = config_path()
    if not path.exists():
        if required:
            raise FileNotFoundError(
                f"local tool config not found: {path}\n"
                "Copy tools/config.toml.example to tools/config.toml and adjust paths, "
                f"or set {CONFIG_ENV} to another local config file."
            )
        return {}
    with path.open("rb") as f:
        return tomllib.load(f)


def config_value(cfg: Config, *keys: str, default: Any = None, required: bool = False) -> Any:
    cur: Any = cfg
    for key in keys:
        if not isinstance(cur, dict) or key not in cur:
            if required:
                dotted = ".".join(keys)
                raise KeyError(f"missing required config value: {dotted}")
            return default
        cur = cur[key]
    if cur in (None, ""):
        if required:
            dotted = ".".join(keys)
            raise KeyError(f"missing required config value: {dotted}")
        return default
    return cur


def config_path_value(
    cfg: Config, *keys: str, default: str | Path | None = None, required: bool = False
) -> Path | None:
    value = config_value(cfg, *keys, default=default, required=required)
    if value is None:
        return None
    path = Path(str(value)).expanduser()
    return path if path.is_absolute() else (ROOT / path).resolve()
