# Kairo

Real version: pressing **O** in-game shows/hides a Kairo panel with five empty
categories (Player, Movement, Visual, World, Misc) — no cheats, ready for you
to add real QoL modules into later.

## How it works (quick recap)
- **Rendering**: hooks `eglSwapBuffers` (the standard per-frame "present"
  call) to draw the ImGui panel right before the frame is shown.
- **Input**: reads the `O` key directly from Linux's raw input layer
  (`/dev/input/eventX`), independent of Minecraft's own input handling.
- Logs everything to `~/kairo_log.txt` so we can debug from real output
  instead of guessing.

## One-time setup: input device permissions

Reading `/dev/input/eventX` normally requires being in the `input` group.
Run this once:

```bash
sudo usermod -aG input $USER
```

Then **log all the way out and back in** (or reboot) — group membership
only applies to new login sessions.

## Build it

1. Push these files to your `kairo` GitHub repo (same as before — this
   replaces the old `main.cpp`/`CMakeLists.txt`/`build.yml` from the test
   mod; keep the same repo).
2. Go to the **Actions** tab, run the workflow, wait ~1-2 minutes.
3. Download the `kairo-mod-arm64` artifact and unzip it — you'll get
   `libkairo.so`.

## Install it

1. Delete the old `libkairo_hello.so` from your mods folder (the test one).
2. Copy `libkairo.so` into that same mods folder.
3. Launch Minecraft through mcpelauncher.
4. Get into a world (not just the main menu) and press **O**.

## If it doesn't show up

Run this right after trying, while the game is still open if possible:
```bash
cat ~/kairo_log.txt
```

Paste me exactly what it says — this tells us precisely which stage failed:

- **No file at all** → the mod isn't loading; double check it's the new
  `.so` in the mods folder, not the old test one.
- **"Kairo mod loaded." but no "Watching N input device(s)" line** →
  the input-watcher thread didn't start; paste the log, we'll check for a
  crash.
- **"Watching 0 input device(s)"** or **"No input devices were readable"**
  → the group permission change didn't take effect. Confirm with:
  ```bash
  groups
  ```
  `input` should be in that list. If it's not, the logout/login didn't
  register the new group — try a full reboot.
- **"Watching N input device(s)"** (N > 0) but pressing O does nothing and
  no new log lines appear → we're not reading the right device, or the key
  isn't reaching evdev the way we expect on your keyboard; paste the log
  and I'll adjust the device-matching logic.
- **Game crashes on launch** → the `eglSwapBuffers` hook likely isn't
  compatible with your exact Minecraft version's rendering setup; paste
  any crash output/log you can find (mcpelauncher usually prints one to
  the terminal you launched it from) and we'll adjust.

Every one of those failure modes is fixable — just send me what the log
says and we'll go from there.
