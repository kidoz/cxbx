# Game compatibility

This table records an automated boot smoke test performed on 2026-07-26 with
the current local emulator build. Each game was observed for a nominal
30-second run unless it exited earlier. An unresponsive window could extend the
wall-clock duration of the capture attempt.

These results describe startup behavior only. They do not prove that a game is
playable from beginning to end.

| Game | Result | Observation |
|---|---|---|
| Arx Fatalis | Crashes | Exits during startup without a confirmed game frame. |
| Beyond Good And Evil | Unresponsive | Remains running, but the window becomes unresponsive and produces no confirmed game frame. |
| Big Mutha Truckers | Crashes | Exits during startup without a confirmed game frame. |
| Bloody Roar Extreme | Crashes | Exits before the first frame can be captured. |
| Capcom Fighting Evolution | Crashes | Exits during startup without a confirmed game frame. |
| King of Fighters 2002 | Crashes | Shows a black screen before exiting. |
| Metal Slug 3 | Black screen | Remains running for the full test without visible game output. |
| Mortal Kombat: Deception Kollector's Edition | Crashes | Exits before the first frame can be captured. |
| Need For Speed: Underground | Crashes | Becomes unresponsive, shows a black screen, and then exits. |
| Samurai Showdown V | Crashes | Exits during startup without creating a capturable game window. |
| Soul Calibur 2 | Black screen | Remains running for the full test without visible game output. |
| Turok - Evolution | Reaches frontend | Completes the intro sequence, reaches the interactive save prompt, and remains running for the full test. |
| WhiteOut | Blank output | Remains running after showing a solid-blue frame and then a black screen. |

## Result meanings

- **Crashes**: the guest process exits with an unhandled error during startup.
- **Unresponsive**: the guest remains alive, but its window does not respond to
  capture attempts.
- **Black screen**: the guest remains alive without producing visible game
  content.
- **Blank output**: the guest presents only solid-color or empty frames.
- **Reaches frontend**: recognizable title frontend content renders and the
  guest remains alive, but gameplay was not exercised.
