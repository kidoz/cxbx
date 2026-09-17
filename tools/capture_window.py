"""Capture a window owned by a specific guest PID to PNG using Win32 and the standard library.

PrintWindow can return a black D3D client; in that case capture the desktop client
rectangle after briefly raising the target. Use emulator backbuffer dumps when
the title does not expose meaningful pixels through either path.
"""

import argparse
import ctypes
import struct
import sys
import time
import zlib
from ctypes import wintypes
from pathlib import Path
from typing import Any


def save_png(path: Path, width: int, height: int, pixels: bytes) -> None:
    """Write top-down BGRA pixels as an RGB PNG without an image dependency."""
    rgb = bytearray(width * height * 3)
    rgb[0::3], rgb[1::3], rgb[2::3] = pixels[2::4], pixels[1::4], pixels[0::4]
    rows = b"".join(b"\0" + rgb[y * width * 3 : (y + 1) * width * 3] for y in range(height))

    def chunk(kind: bytes, data: bytes) -> bytes:
        return (
            struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
        )

    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(rows))
        + chunk(b"IEND", b"")
    )


def client_metrics(pixels: bytes, width: int, x: int, y: int, cw: int, ch: int) -> str:
    colors = set()
    samples = nonblack = 0
    min_x, min_y, max_x, max_y = cw, ch, -1, -1
    for cy in range(0, ch, 4):
        for cx in range(0, cw, 4):
            offset = ((y + cy) * width + x + cx) * 4
            rgb = int.from_bytes(pixels[offset : offset + 3], "little")
            colors.add(rgb)
            samples += 1
            if rgb:
                nonblack += 1
                min_x, min_y = min(min_x, cx), min(min_y, cy)
                max_x, max_y = max(max_x, cx), max(max_y, cy)
    bbox = f"{min_x},{min_y},{max_x},{max_y}" if nonblack else "none"
    ratio = nonblack / samples if samples else 0.0
    return f"samples={samples} nonblack={ratio:.6f} colors={len(colors)} bbox={bbox}"


class BitmapInfo(ctypes.Structure):
    _fields_ = [
        ("size", wintypes.DWORD),
        ("width", wintypes.LONG),
        ("height", wintypes.LONG),
        ("planes", wintypes.WORD),
        ("bit_count", wintypes.WORD),
        ("compression", wintypes.DWORD),
        ("image_size", wintypes.DWORD),
        ("x_ppm", wintypes.LONG),
        ("y_ppm", wintypes.LONG),
        ("used", wintypes.DWORD),
        ("important", wintypes.DWORD),
    ]


def capture(pid: int, destination: Path) -> str:
    if sys.platform != "win32":
        raise OSError("Window capture requires Windows.")
    user = ctypes.WinDLL("user32", use_last_error=True)
    gdi = ctypes.WinDLL("gdi32", use_last_error=True)
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    # Explicit pointer-sized signatures are required when Python is 64-bit.
    signatures: list[tuple[Any, type[Any], list[type[Any]]]] = [
        (user.EnumWindows, wintypes.BOOL, [callback_type, wintypes.LPARAM]),
        (
            user.GetWindowThreadProcessId,
            wintypes.DWORD,
            [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)],
        ),
        (user.IsWindowVisible, wintypes.BOOL, [wintypes.HWND]),
        (user.GetWindow, wintypes.HWND, [wintypes.HWND, wintypes.UINT]),
        (user.GetWindowRect, wintypes.BOOL, [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]),
        (user.GetClientRect, wintypes.BOOL, [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]),
        (user.ClientToScreen, wintypes.BOOL, [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]),
        (user.SetThreadDpiAwarenessContext, wintypes.HANDLE, [wintypes.HANDLE]),
        (user.GetDC, wintypes.HDC, [wintypes.HWND]),
        (user.ReleaseDC, ctypes.c_int, [wintypes.HWND, wintypes.HDC]),
        (user.PrintWindow, wintypes.BOOL, [wintypes.HWND, wintypes.HDC, wintypes.UINT]),
        (user.SetForegroundWindow, wintypes.BOOL, [wintypes.HWND]),
        (user.GetWindowLongW, wintypes.LONG, [wintypes.HWND, ctypes.c_int]),
        (
            user.SetWindowPos,
            wintypes.BOOL,
            [
                wintypes.HWND,
                wintypes.HWND,
                ctypes.c_int,
                ctypes.c_int,
                ctypes.c_int,
                ctypes.c_int,
                wintypes.UINT,
            ],
        ),
        (gdi.CreateCompatibleDC, wintypes.HDC, [wintypes.HDC]),
        (
            gdi.CreateDIBSection,
            wintypes.HBITMAP,
            [
                wintypes.HDC,
                ctypes.POINTER(BitmapInfo),
                wintypes.UINT,
                ctypes.POINTER(ctypes.c_void_p),
                wintypes.HANDLE,
                wintypes.DWORD,
            ],
        ),
        (gdi.SelectObject, wintypes.HANDLE, [wintypes.HDC, wintypes.HANDLE]),
        (gdi.DeleteObject, wintypes.BOOL, [wintypes.HANDLE]),
        (gdi.DeleteDC, wintypes.BOOL, [wintypes.HDC]),
        (
            gdi.BitBlt,
            wintypes.BOOL,
            [
                wintypes.HDC,
                ctypes.c_int,
                ctypes.c_int,
                ctypes.c_int,
                ctypes.c_int,
                wintypes.HDC,
                ctypes.c_int,
                ctypes.c_int,
                wintypes.DWORD,
            ],
        ),
        (gdi.GdiFlush, wintypes.BOOL, []),
    ]
    for function, result, arguments in signatures:
        function.restype = result
        function.argtypes = arguments
    windows: list[int] = []

    def visit(hwnd: int, unused: int) -> bool:
        owner = wintypes.DWORD()
        user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user.IsWindowVisible(hwnd) and not user.GetWindow(hwnd, 4):
            windows.append(hwnd)
            return False
        return True

    user.EnumWindows(callback_type(visit), 0)
    if not windows:
        return "NOWINDOW"
    hwnd = windows[0]
    previous_dpi = user.SetThreadDpiAwarenessContext(ctypes.c_void_p(-4))
    screen = dc = bitmap = previous = None
    try:
        rect, client, origin = wintypes.RECT(), wintypes.RECT(), wintypes.POINT()
        if not user.GetWindowRect(hwnd, ctypes.byref(rect)):
            raise ctypes.WinError(ctypes.get_last_error())
        width, height = rect.right - rect.left, rect.bottom - rect.top
        if width <= 0 or height <= 0:
            return "ZERORECT"
        if not user.GetClientRect(hwnd, ctypes.byref(client)) or not user.ClientToScreen(
            hwnd, ctypes.byref(origin)
        ):
            return "NOCLIENTRECT"
        x, y = origin.x - rect.left, origin.y - rect.top
        cw, ch = client.right - client.left, client.bottom - client.top
        if cw <= 0 or ch <= 0:
            return "ZEROCLIENT"
        if width * height > 64 * 1024 * 1024 or min(x, y) < 0 or x + cw > width or y + ch > height:
            raise ValueError("Invalid capture geometry.")
        screen = user.GetDC(None)
        dc = gdi.CreateCompatibleDC(screen)
        info = BitmapInfo(
            size=ctypes.sizeof(BitmapInfo), width=width, height=-height, planes=1, bit_count=32
        )
        bits = ctypes.c_void_p()
        bitmap = gdi.CreateDIBSection(screen, ctypes.byref(info), 0, ctypes.byref(bits), None, 0)
        if not screen or not dc or not bitmap or not bits.value:
            raise ctypes.WinError(ctypes.get_last_error())
        previous = gdi.SelectObject(dc, bitmap)
        printed = user.PrintWindow(hwnd, dc, 2)
        gdi.GdiFlush()
        pixels = ctypes.string_at(bits, width * height * 4)
        black = not any(
            pixels[((y + cy) * width + x + cx) * 4 + channel]
            for cy in range(0, ch, 16)
            for cx in range(0, cw, 16)
            for channel in range(3)
        )
        source = "printwindow"
        if not printed or black:
            was_topmost = bool(user.GetWindowLongW(hwnd, -20) & 0x00000008)
            user.SetForegroundWindow(hwnd)
            raised = user.SetWindowPos(hwnd, ctypes.c_void_p(-1), 0, 0, 0, 0, 0x0043)
            try:
                if not raised:
                    raise ctypes.WinError(ctypes.get_last_error())
                time.sleep(0.04)
                if not gdi.BitBlt(dc, x, y, cw, ch, screen, origin.x, origin.y, 0x00CC0020):
                    raise ctypes.WinError(ctypes.get_last_error())
                gdi.GdiFlush()
                pixels = ctypes.string_at(bits, width * height * 4)
            finally:
                if raised and not was_topmost:
                    user.SetWindowPos(hwnd, ctypes.c_void_p(-2), 0, 0, 0, 0, 0x0003)
            source = "desktopclient"
        metrics = client_metrics(pixels, width, x, y, cw, ch)
        save_png(destination, width, height, pixels)
        return (
            f"SAVED width={width} height={height} source={source} client={cw}x{ch} {metrics} "
            f"screen={origin.x},{origin.y} window={rect.left},{rect.top}"
        )
    finally:
        if previous:
            gdi.SelectObject(dc, previous)
        if bitmap:
            gdi.DeleteObject(bitmap)
        if dc:
            gdi.DeleteDC(dc)
        if screen:
            user.ReleaseDC(None, screen)
        if previous_dpi:
            user.SetThreadDpiAwarenessContext(previous_dpi)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target-pid", type=int, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    try:
        result = capture(args.target_pid, args.out)
    except (OSError, ValueError) as error:
        print(f"Capture failed: {error}", file=sys.stderr)
        return 1
    print(result)
    return 0 if result.startswith("SAVED ") else 1


if __name__ == "__main__":
    sys.exit(main())
