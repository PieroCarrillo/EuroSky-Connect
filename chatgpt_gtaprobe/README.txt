GTAProbe v3 - GTA San Andreas 1.0 US x86 diagnostic probe

Purpose:
- Observe the real CBmx::LaunchBunnyHopCB callback at 0x6C0390.
- Record GTA timer/frame data plus BMX move/turn speed before and after the vanilla callback.
- Does NOT unlock FPS.
- Does NOT synthesize keyboard/mouse input.
- Does NOT add impulses.
- Does NOT change animations or gameplay values.

Install:
1. Put GTAProbe_v3.asi next to gta_sa.exe.
2. Disable SelectiveFPS.SA.asi and older GTAProbe ASIs.
3. You may keep sensfix.asi, RefreshRateFixByDarkP1xel32.ASI and fix.black_roads.asi.
4. Launch GTA/SA-MP.
5. GTAProbe.log should appear next to gta_sa.exe and contain HOOK_OK + READY.

Test:
A) 98 FPS: perform ~20-30 bunny-hop attempts under the same conditions, exit normally, rename GTAProbe.log to GTAProbe_98.log.
B) ~300 FPS unlocked: repeat the same test and rename to GTAProbe_300.log.
C) Send both logs back for analysis.

If the log says target_already_hooked_or_redirected, another plugin hooked 0x6C0390. Disable it and retry.
