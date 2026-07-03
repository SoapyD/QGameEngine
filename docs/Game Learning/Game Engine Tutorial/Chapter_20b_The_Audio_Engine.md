# Chapter 20b: The Audio Engine

## What You'll Learn
- Pulling a single-header C audio library (**miniaudio**) into a CMake build with `file(DOWNLOAD)` at configure time — no vendored blob, no submodule
- Why OGG needs a second header (**stb_vorbis**) and the exact include order that wires it into miniaudio
- Building the one implementation translation unit that compiles both libraries into a small static target
- Linking the Windows audio libraries (`ole32`, `winmm`) and folding the target into `qengine_lib`
- Wrapping miniaudio behind an `AudioEngine` class that uses the **pimpl** idiom to keep `miniaudio.h` out of the public header
- A dependency-free manifest parser, and the `play` / `playAt` / `playMusic` / `stopMusic` / `setListener` playback surface

---

## Where We Are

In **Chapter 20a: Audio Assets & the Manifest** we produced the sound assets and a
`manifest.json` that maps logical ids (`"weapon.shotgun"`, `"music.ambient"`) to file paths on
disk. That manifest is *data* — it names sounds but plays nothing. This chapter builds the
**backend** that reads that manifest and turns an id into audible output: the `AudioEngine`
class and the third-party library it wraps.

We are deliberately staying at the backend layer here. Nothing in this chapter knows about the
ECS, `SoundEvent`s, or when a shotgun actually fires — that wiring is **Chapter 20c: Wiring
Sound Events**. What we build now is a self-contained object you can hand a string id and get a
sound. Getting the dependency in, compiling it cleanly, and exposing a tidy interface is the
whole job.

Here's the shape of what we're building:

```
manifest.json  ──▶  AudioEngine::init()  ──▶  play("weapon.shotgun")  ──▶  miniaudio device
 (from 20a)         (parse ids → paths)       (id → path → ma_engine)      (+ stb_vorbis for .ogg)
```

---

## Step 1: Download miniaudio at Configure Time

miniaudio is a single-header cross-platform audio library — one `miniaudio.h` file, no build
system of its own. Rather than commit a ~90k-line header into our tree, we fetch it during
CMake's configure step. Open `CMakeLists.txt` and add, before the Jolt section:

```cmake
# ──────────────────────────────────────────────
# miniaudio — single-header audio backend (downloaded at configure time)
# ──────────────────────────────────────────────
set(MINIAUDIO_DIR ${CMAKE_BINARY_DIR}/miniaudio)
if(NOT EXISTS ${MINIAUDIO_DIR}/miniaudio.h)
	message(STATUS "Downloading miniaudio.h ...")
	file(DOWNLOAD
		https://raw.githubusercontent.com/mackron/miniaudio/0.11.21/miniaudio.h
		${MINIAUDIO_DIR}/miniaudio.h
		STATUS MA_DL_STATUS)
	list(GET MA_DL_STATUS 0 MA_DL_CODE)
	if(NOT MA_DL_CODE EQUAL 0)
		message(FATAL_ERROR "Failed to download miniaudio.h (${MA_DL_STATUS}). "
			"Place miniaudio.h at ${MINIAUDIO_DIR}/ manually and re-configure.")
	endif()
endif()
```

The header lands in the *build* directory (`${CMAKE_BINARY_DIR}/miniaudio`), not the source
tree, so it's a build artefact that a clean checkout regenerates. The `if(NOT EXISTS ...)` guard
means the download runs **once** — subsequent configures skip it, so you're not hitting GitHub
on every `cmake`.

> **Why pin an exact version (`0.11.21`) instead of `master`?** The URL points at a tagged
> release commit, not the moving branch tip. A build that fetches from `master` can break
> silently the day upstream changes an API — your source didn't move, but your dependency did.
> Pinning makes the build reproducible: the same checkout fetches the same bytes forever.

> **Why check `MA_DL_STATUS` and fail loudly?** `file(DOWNLOAD)` does *not* fail the configure
> on a network error by default — it would leave you with a missing or truncated header and a
> confusing compile error later. We read status code 0 (success) out of the returned list and
> `FATAL_ERROR` otherwise, with a message telling you exactly where to drop the file by hand if
> you're building offline.

---

## Step 2: Download stb_vorbis for OGG Support

miniaudio decodes WAV, MP3 and FLAC natively, but **not OGG/Vorbis**. Our background music is
`.ogg` (it's the compressed format that's free of the licensing baggage MP3 historically
carried, and far smaller than WAV). To teach miniaudio how to decode Ogg Vorbis, we bring in a
second single-file library, **stb_vorbis**, the same way:

```cmake
# stb_vorbis — gives miniaudio OGG/Vorbis decoding (our music is .ogg).
if(NOT EXISTS ${MINIAUDIO_DIR}/stb_vorbis.c)
	message(STATUS "Downloading stb_vorbis.c ...")
	file(DOWNLOAD
		https://raw.githubusercontent.com/nothings/stb/master/stb_vorbis.c
		${MINIAUDIO_DIR}/stb_vorbis.c
		STATUS SV_DL_STATUS)
	list(GET SV_DL_STATUS 0 SV_DL_CODE)
	if(NOT SV_DL_CODE EQUAL 0)
		message(FATAL_ERROR "Failed to download stb_vorbis.c (${SV_DL_STATUS}).")
	endif()
endif()
```

Same pattern: fetch into the build dir if absent, fail loudly on a network error. It sits beside
`miniaudio.h` in `${MINIAUDIO_DIR}`, so a single include path serves both. How the two libraries
actually connect is the subject of Step 4 — it comes down to include *order*.

---

## Step 3: The miniaudio Static Library and Its Link Libs

Both libraries are header-only, which means their code has to be compiled into exactly **one**
object file somewhere (compile it twice and you get duplicate-symbol link errors). We give them
their own small static library so that compilation happens once, isolated from everything else:

```cmake
add_library(miniaudio STATIC src/engine/audio/miniaudio_impl.cpp)
target_include_directories(miniaudio PUBLIC ${MINIAUDIO_DIR})
if(WIN32)
	target_link_libraries(miniaudio PUBLIC ole32 winmm)
else()
	find_package(Threads REQUIRED)
	target_link_libraries(miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
endif()
```

The library is built from a single source file, `miniaudio_impl.cpp` (Step 4). The
`target_include_directories(... PUBLIC ${MINIAUDIO_DIR})` means anything that links `miniaudio`
automatically gets `${MINIAUDIO_DIR}` on its include path — so `audio_engine.cpp` can write
`#include "miniaudio.h"` without knowing it lives in the build tree.

The platform-specific link libraries matter:

- **`ole32`** — miniaudio's Windows backend uses WASAPI, which is a COM API; `ole32` provides
  `CoInitializeEx` / `CoCreateInstance`. Without it you get unresolved COM symbols at link time.
- **`winmm`** — the Windows multimedia library, used for timing and the older audio backends
  miniaudio falls back to.

On non-Windows, miniaudio instead wants a threading library and the dynamic loader (`dl`) to
open the platform's audio backend at runtime; `Threads::Threads` and `${CMAKE_DL_LIBS}` supply
those. Marking these `PUBLIC` propagates them to `qengine_lib` and on to the final executables,
so nobody downstream has to re-declare them.

> **Why a separate `miniaudio` target instead of adding the `.cpp` straight into
> `qengine_lib`?** Two reasons. It quarantines the ~90k-line header's compile into one place
> (and one target's warning settings), keeping it out of the engine library's own build. And it
> lets the ole32/winmm link requirements live *with* the code that needs them, propagated
> automatically via `PUBLIC` rather than bolted onto the engine target by hand.

Finally, fold the wrapper source into the engine library and link the new target. In the
`qengine_lib` source list:

```cmake
	src/engine/audio/audio_engine.cpp
	src/engine/ecs/systems/audio/audio_system.cpp
```

and in its `target_link_libraries`:

```cmake
target_link_libraries(qengine_lib PUBLIC
	glfw
	glad
	glm
	entt
	stb
	miniaudio
	Jolt
)
```

(`audio_system.cpp` is the ECS bridge — that's Chapter 20c's concern, listed here only so the
build is complete.)

---

## Step 4: The Single Implementation TU — Include Order Is Everything

Header-only libraries expose declarations by default and only emit their *implementation* when
you define a magic macro before including them. We do all of that in exactly one file,
`src/engine/audio/miniaudio_impl.cpp`:

```cpp
// Single translation unit that compiles miniaudio + stb_vorbis.
// Everything else includes "miniaudio.h" for declarations only.
//
// stb_vorbis gives miniaudio OGG/Vorbis decoding (our music is .ogg). The
// header-only include must come BEFORE miniaudio's implementation so miniaudio
// detects Vorbis support; the stb_vorbis implementation must come AFTER.

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#undef STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
```

Read that top-to-bottom, because the order is the whole trick:

1. **`#define STB_VORBIS_HEADER_ONLY` then include `stb_vorbis.c`** — this pulls in only the
   stb_vorbis *declarations* (function prototypes, no bodies). miniaudio's implementation, when
   it compiles next, probes for those symbols and — finding them — switches on its Vorbis
   decoding path. If this include came *after* miniaudio, miniaudio would already have compiled
   with OGG support off.
2. **`#define MINIAUDIO_IMPLEMENTATION` then include `miniaudio.h`** — this emits the full
   miniaudio implementation, now wired to call into stb_vorbis for `.ogg` files.
3. **`#undef STB_VORBIS_HEADER_ONLY` then include `stb_vorbis.c` again** — with the
   header-only macro cleared, this third include finally emits the stb_vorbis *implementation*
   (the actual decoder bodies) so the prototypes from step 1 have definitions to link against.

> **Why include the same `.c` file twice?** stb-style libraries fold their header and their
> implementation into one file, gated by a macro. The first include (with
> `STB_VORBIS_HEADER_ONLY` defined) gives miniaudio the *prototypes* it needs at the moment it
> compiles, so it enables the Vorbis path; the second (with the macro undefined) supplies the
> *definitions* those prototypes resolve to. Split like this, the decoder is declared before
> miniaudio and defined after it — exactly the sandwich miniaudio's build expects. Get the order
> wrong and you either lose OGG support or get link errors.

Because these implementation macros appear only in this one `.cpp`, every other file that
includes `miniaudio.h` (just `audio_engine.cpp`, as it happens) sees declarations alone — no
duplicate symbols, fast compiles.

---

## Step 5: The AudioEngine Header — pimpl

Now the public interface, `src/engine/audio/audio_engine.h`. This is the header gameplay code
includes, and it deliberately reveals **nothing** about miniaudio:

```cpp
#pragma once

#include <glm/glm.hpp>

#include <memory>
#include <string>

// Thin wrapper over miniaudio: loads the sound manifest and plays clips by their
// logical id (e.g. "weapon.shotgun"). Pimpl keeps miniaudio.h out of this header,
// so gameplay code can queue sounds without pulling the whole backend in.
//
// Only the windowed build creates one; the headless harness never does (it just
// queues SoundEvents that are never drained). See ecs/systems/audio/audio_system.
class AudioEngine
{
	public:
		AudioEngine();
		~AudioEngine();

		// Start the device and load `manifestPath` (paths are resolved relative
		// to `soundsRoot`). Returns false if the audio device can't be opened.
		bool init(const std::string& soundsRoot, const std::string& manifestPath);
		void shutdown();
		bool isValid() const { return m_valid; }

		// Fire-and-forget one-shot by manifest id. `playAt` is the positional
		// variant (currently non-spatial; falls back to 2D).
		void play(const std::string& id);
		void playAt(const std::string& id, const glm::vec3& pos);

		// Streamed, looping background track (stops any current track first).
		void playMusic(const std::string& id, float volume = 0.6f);
		void stopMusic();

		// Position/orientation of the listener (the camera) for future 3D audio.
		void setListener(const glm::vec3& pos, const glm::vec3& forward);

	private:
		struct Impl;
		std::unique_ptr<Impl> m_impl;
		bool m_valid = false;
};
```

The key move is the last three lines: the class holds a `std::unique_ptr<Impl>` to a struct it
only **forward-declares** (`struct Impl;`). The definition of `Impl` — which is where the
`ma_engine`, `ma_sound` and the rest of the miniaudio types live — is hidden in the `.cpp`. This
is the **pimpl** (pointer-to-implementation) idiom.

> **Why pimpl here?** `miniaudio.h` is enormous, and its types would leak into every file that
> includes `audio_engine.h`. By putting all the miniaudio state behind an opaque `Impl` pointer,
> the public header depends only on `<memory>`, `<string>` and GLM. Gameplay code can hold an
> `AudioEngine`, call `play(...)`, and never drag the 90k-line audio backend (or its compile
> cost) into its own translation unit. It also means changing the audio internals never triggers
> a rebuild of everything that merely *uses* the engine.

Because `Impl` is incomplete in the header, `unique_ptr`'s destructor can't be generated there —
which is exactly why the `~AudioEngine()` destructor is *declared* in the header but
*defined* in the `.cpp` (Step 6), where `Impl` is a complete type.

Note also the comment's contract: only the windowed build ever constructs an `AudioEngine`; the
headless harness leaves sound events undrained. And `isValid()` / the `return false` from
`init` mean a machine with no audio device runs **silent**, never crashes — important for CI and
headless runs.

---

## Step 6: The Implementation — Impl, Manifest, Playback

Now `src/engine/audio/audio_engine.cpp`. This is the only file that includes `miniaudio.h` for
its declarations. Start with the includes and the hidden `Impl` struct:

```cpp
#include "engine/audio/audio_engine.h"

#include "miniaudio.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

// ─── pimpl ───────────────────────────────────────────────────────────────
struct AudioEngine::Impl
{
	ma_engine engine{};
	std::string root;                                  // e.g. "assets/sounds/"
	std::unordered_map<std::string, std::string> paths; // id -> full file path
	std::unordered_set<std::string> warned;            // ids warned-about once
	ma_sound music{};
	bool musicActive = false;
};
```

Everything miniaudio-flavoured lives here, out of sight of the header: the `ma_engine` device,
the `id → path` lookup built from the manifest, a `ma_sound` handle for the streamed music
track, and a `warned` set so an unknown-id message prints **once** rather than every frame the
game asks for it.

### The manifest parser — no JSON dependency

```cpp
namespace
{
	// Minimal manifest reader: pulls every `"id": "path"` pair whose value ends
	// in a known audio extension out of our generated manifest.json. Avoids a
	// JSON dependency for a file we control the shape of.
	void parseManifest(const std::string& file, const std::string& root,
	                   std::unordered_map<std::string, std::string>& out)
	{
		std::ifstream in(file);
		if (!in)
		{
			std::cerr << "[audio] manifest not found: " << file << "\n";
			return;
		}
		std::string line;
		while (std::getline(in, line))
		{
			// find two double-quoted tokens on the line
			std::size_t a = line.find('"');
			if (a == std::string::npos) continue;
			std::size_t b = line.find('"', a + 1);
			if (b == std::string::npos) continue;
			std::string key = line.substr(a + 1, b - a - 1);

			std::size_t c = line.find('"', b + 1);
			if (c == std::string::npos) continue;
			std::size_t d = line.find('"', c + 1);
			if (d == std::string::npos) continue;
			std::string val = line.substr(c + 1, d - c - 1);

			bool audio = val.size() > 4 &&
				(val.rfind(".wav") == val.size() - 4 ||
				 val.rfind(".ogg") == val.size() - 4 ||
				 val.rfind(".mp3") == val.size() - 4);
			if (audio && key.find('.') != std::string::npos)
				out[key] = root + val;
		}
	}
}
```

It reads the manifest line by line and grabs the first two double-quoted tokens on each line as
`key` and `val`. A line only counts as a sound entry if two things hold: the value ends in a
known audio extension (`.wav` / `.ogg` / `.mp3`), and the key contains a `.` — our ids are
dotted (`"weapon.shotgun"`), so that dot filters out incidental string pairs (schema fields,
version strings) that aren't sounds. Matching entries are stored as `id → root + path`, so
`play` gets a ready-to-open absolute-ish path.

> **Why hand-roll a parser instead of pulling in a JSON library?** We *generate* this manifest
> ourselves (Chapter 20a), so we control its exact shape — one `"id": "path"` pair per line.
> Parsing that with two `find('"')` scans is a dozen lines and zero dependencies. A full JSON
> library would be a heavyweight addition (build time, binary size, another fetch) to read a
> file whose format we already dictate. If the manifest ever grows genuinely nested structure,
> that trade-off changes; for a flat id→path map it isn't worth it.

Note this parser is intentionally forgiving: a missing manifest logs and returns (leaving an
empty map) rather than throwing, so a botched asset build degrades to silence, not a crash.

### Lifecycle: construct, init, shutdown

```cpp
// ─── lifecycle ───────────────────────────────────────────────────────────
AudioEngine::AudioEngine() : m_impl(std::make_unique<Impl>()) {}
AudioEngine::~AudioEngine() { shutdown(); }

bool AudioEngine::init(const std::string& soundsRoot, const std::string& manifestPath)
{
	m_impl->root = soundsRoot;
	if (!m_impl->root.empty() && m_impl->root.back() != '/' && m_impl->root.back() != '\\')
		m_impl->root += '/';

	if (ma_engine_init(nullptr, &m_impl->engine) != MA_SUCCESS)
	{
		std::cerr << "[audio] failed to init device — running silent\n";
		m_valid = false;
		return false;
	}

	parseManifest(manifestPath, m_impl->root, m_impl->paths);
	std::cout << "[audio] " << m_impl->paths.size() << " sounds loaded from manifest\n";
	m_valid = true;
	return true;
}

void AudioEngine::shutdown()
{
	if (!m_valid) return;
	stopMusic();
	ma_engine_uninit(&m_impl->engine);
	m_valid = false;
}
```

The constructor allocates the `Impl` (this is where `unique_ptr` needs the complete type, hence
the `.cpp` home). The destructor calls `shutdown()`, so an `AudioEngine` cleans up its device
whether or not you remember to. `init` normalises the sounds root to end in a slash, opens the
default device with `ma_engine_init(nullptr, ...)` (the `nullptr` config means "use sensible
defaults"), and — crucially — **returns `false` and runs silent if the device won't open**
rather than aborting. It then parses the manifest and prints a one-line diagnostic
(`"[audio] N sounds loaded from manifest"`) so you can confirm assets were found at startup.

### One-shots: play / playAt

```cpp
// ─── playback ────────────────────────────────────────────────────────────
void AudioEngine::play(const std::string& id)
{
	if (!m_valid) return;
	auto it = m_impl->paths.find(id);
	if (it == m_impl->paths.end())
	{
		if (m_impl->warned.insert(id).second)
			std::cerr << "[audio] unknown sound id '" << id << "'\n";
		return;
	}
	ma_engine_play_sound(&m_impl->engine, it->second.c_str(), nullptr);
}

void AudioEngine::playAt(const std::string& id, const glm::vec3& /*pos*/)
{
	// Positional playback is a follow-up; for now play non-spatially.
	play(id);
}
```

`play` looks the id up in the manifest map and hands the path to
`ma_engine_play_sound(...)` — miniaudio's fire-and-forget one-shot that loads, plays, and frees
the clip for you. If the id is unknown, the `warned.insert(id).second` guard prints the warning
only the first time (a `set::insert` returns `.second == true` only on a fresh insert), so a
mis-named sound requested every frame doesn't flood the log. The `if (!m_valid) return` at the
top makes every playback call a safe no-op on a silent engine.

`playAt` is the positional variant. It's a placeholder today — spatial audio is a follow-up — so
it drops the position and calls `play`. Keeping the signature now means the call sites in
Chapter 20c don't have to change when 3D audio lands.

### Music: streamed, looping, one track at a time

```cpp
void AudioEngine::playMusic(const std::string& id, float volume)
{
	if (!m_valid) return;
	auto it = m_impl->paths.find(id);
	if (it == m_impl->paths.end())
	{
		if (m_impl->warned.insert(id).second)
			std::cerr << "[audio] unknown music id '" << id << "'\n";
		return;
	}
	stopMusic();

	ma_uint32 flags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION;
	ma_result r = ma_sound_init_from_file(&m_impl->engine, it->second.c_str(), flags,
	                                      nullptr, nullptr, &m_impl->music);
	if (r != MA_SUCCESS)
	{
		std::cerr << "[audio] could not load music '" << it->second
		          << "' (ma_result " << r << ")\n";
		return;
	}

	ma_sound_set_looping(&m_impl->music, MA_TRUE);
	ma_sound_set_volume(&m_impl->music, volume);
	ma_sound_start(&m_impl->music);
	m_impl->musicActive = true;
	std::cout << "[audio] music playing: " << id << "\n";
}

void AudioEngine::stopMusic()
{
	if (m_valid && m_impl->musicActive)
	{
		ma_sound_uninit(&m_impl->music);
		m_impl->musicActive = false;
	}
}
```

Music is different from a one-shot in three ways, and the flags say so. It calls `stopMusic()`
first so a new track replaces the current one (only ever one background track). It initialises a
persistent `ma_sound` from the file with two flags:

- **`MA_SOUND_FLAG_STREAM`** — decode the file *as it plays* instead of loading it whole into
  memory. Music tracks are long; streaming keeps a multi-megabyte `.ogg` from being fully
  decoded into RAM up front.
- **`MA_SOUND_FLAG_NO_SPATIALIZATION`** — background music isn't positioned in the world; it
  plays flat at full presence regardless of where the listener is looking.

It then sets looping on, applies the requested volume (default `0.6` from the header), and
starts playback. The `musicActive` flag is the guard `stopMusic()` checks so it only ever
`ma_sound_uninit`s a track that was actually initialised — calling `stopMusic()` on silence is
harmless.

> **Why the explicit `ma_result` diagnostic on a failed music load?** This is the one place
> loading can realistically fail at runtime — a missing or corrupt `.ogg`, or the Vorbis path
> not being wired (which is exactly what the Step 4 include order guards against). Logging the
> path *and* the numeric `ma_result` turns "no music played" into an actionable message: you can
> see which file it tried and which miniaudio error code came back. The success path logs too
> (`"[audio] music playing: <id>"`), so the console tells you unambiguously whether the track is
> live.

### The listener

```cpp
void AudioEngine::setListener(const glm::vec3& pos, const glm::vec3& forward)
{
	if (!m_valid) return;
	ma_engine_listener_set_position(&m_impl->engine, 0, pos.x, pos.y, pos.z);
	ma_engine_listener_set_direction(&m_impl->engine, 0, forward.x, forward.y, forward.z);
}
```

`setListener` feeds the camera's position and facing to miniaudio's listener 0. Nothing is
spatialised yet (`play` is 2D, music is flagged no-spatialization), so this is groundwork: once
`playAt` becomes truly positional, the listener transform is already being kept current every
frame. Wiring this to the camera is Chapter 20c's job.

---

## What Changed — Summary

| File | Change |
|------|--------|
| `CMakeLists.txt` | **New audio section.** `file(DOWNLOAD)` fetches pinned `miniaudio.h` (0.11.21) and `stb_vorbis.c` into the build dir at configure time; a `miniaudio` STATIC target built from `miniaudio_impl.cpp`, linking `ole32`/`winmm` on Windows (Threads + dl elsewhere). Added `audio_engine.cpp` to `qengine_lib` and linked the `miniaudio` target. |
| `src/engine/audio/miniaudio_impl.cpp` | **New.** The single TU that compiles both libraries, with the stb_vorbis-header → miniaudio-impl → stb_vorbis-impl include sandwich that gives miniaudio OGG decoding. |
| `src/engine/audio/audio_engine.h` | **New.** The `AudioEngine` public interface — pimpl (`unique_ptr<Impl>`) keeps `miniaudio.h` out of the header; declares `init`/`shutdown`/`play`/`playAt`/`playMusic`/`stopMusic`/`setListener`. |
| `src/engine/audio/audio_engine.cpp` | **New.** Defines `Impl` (the miniaudio state), the dependency-free manifest parser, device lifecycle, one-shot and streamed-music playback, and the listener transform. |

---

## What You Should See

After adding the CMake section, reconfigure and build (the MSYS2 UCRT64 toolchain must be on
`PATH`, per Chapter 0):

```bash
cmake -S . -B build
cmake --build build
```

The first configure prints `Downloading miniaudio.h ...` and `Downloading stb_vorbis.c ...`;
subsequent configures skip both (the `if(NOT EXISTS ...)` guards). Both files appear under
`build/miniaudio/`. The `miniaudio` static library compiles from the single
`miniaudio_impl.cpp`, and `qengine_lib` links it.

At this point the engine *builds and links* against a real audio backend, but nothing calls it
yet — construction and playback are triggered by the ECS wiring in the next chapter. When that
lands, a machine with a working audio device will print, at startup:

```
[audio] N sounds loaded from manifest
```

and, once music starts, `[audio] music playing: music.ambient`. A machine with no audio device
prints `[audio] failed to init device — running silent` and carries on — the game never crashes
for want of a sound card.

---

## What's Next

The engine can now open a device, read the manifest, and play a clip by id — but it sits idle,
because nothing in the game ever calls `play()`. In **Chapter 20c: Wiring Sound Events**, we
plug this backend into the ECS: a `SoundEvent` queue that gameplay systems push onto (shotgun
fires, pickup collected, player hurt), an `audio_system` that drains that queue into
`AudioEngine::play` each frame, the camera feeding `setListener`, and `playMusic` kicking off
the ambient track at startup. That's the chapter that finally makes the room *sound* alive.
