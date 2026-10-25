/* ps5_game.c -- Policenauts PS5 defaults (see psxrecomp runtime/src/ps5_platform.c).
 * Generated from game.toml by hand-run script; edit game.toml, not this. */
const char* const psx_ps5_boot_disc = "disc/disc1.cue";
const char* const psx_ps5_default_files[] = {
    "game.toml",
    "# PS5 copy, written to /data/policenauts/game.toml on first launch.\n"
    "# Disc images: /data/policenauts/disc/disc1.bin and disc2.bin.\n"
    "# Policenauts (Japan) (English Patched v1.0) — PSXRecomp game config\n"
    "# Two-disc title: Disc 1 (boot) + Disc 2 (continuation).\n"
    "# Boot EXE lives under NAUTS/ on the disc (cdrom:\\NAUTS\\SLPS_002.15;1).\n"
    "# Moved into recomps\\ on 2026-10-06 from the 2026-10-03 snapshot (game.toml, seeds,\n"
    "# generated, tools\\extract_bin_dpk.py) so RUN-ALL builds it on the shared framework.\n"
    "\n"
    "[game]\n"
    "name = \"Policenauts\"\n"
    "id = \"SLPS-00215\"\n"
    "players = 1\n"
    "exe = \"disc/SLPS_002.15\"\n"
    "discs = [\n"
    "  \"disc/disc1.cue\",\n"
    "  \"disc/disc2.cue\",\n"
    "]\n"
    "disc_serials = [\"SLPS-00215\", \"SLPS-00216\"]\n"
    "disc = \"disc/disc1.cue\"\n"
    "load_address = \"0x80010000\"\n"
    "entry_pc = \"0x800127B4\"\n"
    "text_size = \"0x00023800\"\n"
    "stack_base = \"0x801FFFF0\"\n"
    "\n"
    "[recompiler]\n"
    "seeds = \"seeds/ghidra_funcs.txt\"\n"
    "out_dir = \"generated\"\n"
    "strict = true\n"
    "\n"
    "[runtime]\n"
    "window_title = \"Policenauts Recompiled\"\n"
    "memcard_dir = \"saves\"\n"
    "\n"
    "\n"
    "[controller]\n"
    "mouse = true\n"
    "# Where the game keeps its cursor X/Y and the area it moves in (found 2026-10-09 by diffing\n"
    "# save states): touch tap-to-point steers by the real cursor.\n"
    "mouse_cursor = \"0x80068928,0x8006892A,320,240\"\n"
    "\n"
    "[video]\n"
    "# PS5 SDL2 offers only the software presenter.\n"
    "renderer = \"software\"\n"
    "",
    "disc/disc1.cue",
    "FILE \"disc1.bin\" BINARY\n"
    "  TRACK 01 MODE2/2352\n"
    "    INDEX 01 00:00:00\n"
    "",
    "disc/disc2.cue",
    "FILE \"disc2.bin\" BINARY\n"
    "  TRACK 01 MODE2/2352\n"
    "    INDEX 01 00:00:00\n"
    "",
    0, 0,
};
