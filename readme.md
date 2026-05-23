```
  ____            _       _   _       ____             _
 |  _ \  __ _ ___| |__   | \ | |     | __ )  __ _ _ __| | __
 | | | |/ _` / __| '_ \  |  \| |_____|  _ \ / _` | '__| |/ /
 | |_| | (_| \__ \ | | | | |\  |_____| |_) | (_| | |  |   <
 |____/ \__,_|___/_| |_| |_| \_|     |____/ \__,_|_|  |_|\_\
```

A C++20 Discord voice bot built on [D++](https://github.com/brainboxdotcc/DPP) and [NVIDIA stdexec](https://github.com/NVIDIA/stdexec). It plays audio from streaming sources, mixes user-uploaded soundpads, applies pitch/voice effects, and can record and parrot voice channels.

## Features

- **Streaming playback** — fetches via `yt-dlp` (Bilibili, YouTube, etc.) and decodes through a custom audio-mixer pipeline.
- **Soundpads & join effects** — per-guild clip libraries with searchable names.
- **Voice effects** — pitch shifting via Bungee, real-time DSP on the mixer pool.
- **Recording & parrot** — capture voice and replay it back into the channel.
- **Slash commands** — `/stream`, `/search`, `/add`, `/join`, `/leave`, `/skip`, `/playlist`, `/soundpad`, `/parrot`, `/joineffect`.

## Architecture

- Coroutine-based command dispatch on a `exec::static_thread_pool`. Each slash command becomes a stdexec sender pipeline.
- `BotRouter` wires D++ events to handlers; `ToolInterface` owns the audio + persistence side.
- `CrashHandler` installs signal handlers that dump per-thread stack traces (via [cpptrace](https://github.com/jeremy-rifkin/cpptrace)) on `SIGSEGV` / `SIGABRT` / unhandled exception, and on `SIGUSR1` for live deadlock inspection.

## Build

Requires CMake ≥ 3.25 and a C++20 compiler.

```bash
apt install libtbb-dev libssl-dev sqlite3 libsqlite3-dev meson libopus-dev ffmpeg

git submodule update --init --recursive
cmake -B cmake-build-debug
cmake --build cmake-build-debug -j
```

Vendored deps (under `libs/`): DPP, stdexec, spdlog, soci, cpr, json, bungee. cpptrace is fetched at configure time.

## Run

Drop a `config.json` next to the binary (see `testdata/config.json` for the schema — bot token, working directories, etc.) and launch:

```bash
./cmake-build-debug/Dash-N-Bark
```

In Debug builds the working directory defaults to `../testdata/`.

## Tracing

Coroutine and scope timings can be emitted at compile time:

```bash
cmake -B cmake-build-debug -DCMAKE_CXX_FLAGS=-DDNB_TRACE_ENABLED
```

Convert the resulting log into a Chrome-tracing JSON for `chrome://tracing` or `ui.perfetto.dev`:

```bash
scripts/trace_graph.py logs/output_*.log -o trace.json
```

See `Trace.h` for the macro and the sender wrapper.

## Tests

```bash
cmake --build cmake-build-debug --target crash_handler_test
./cmake-build-debug/tests/crash_handler/crash_handler_test <segv|abort|terminate|deadlock|hang>
```

Each scenario writes a trace dump under `logs/`.

## Layout

```
BotRouter.{h,cpp}       Discord event → command dispatch (stdexec)
ToolInterface.{h,cpp}   Audio mixer, soundpad, recording, search
CrashHandler.{h,cpp}    Signal handlers + per-thread stack dumps
Trace.h                 Optional coroutine/scope tracing (opt-in)
Commands/               Slash-command handler classes
Audio-Mixer/            Mixer, voice changer, recorder, soundpad
Stream-Fetch/           yt-dlp wrapper + HTTP fetch pipeline
scripts/                trace_graph.py, watchdog.sh
tests/crash_handler/    Crash + deadlock scenario harness
```
