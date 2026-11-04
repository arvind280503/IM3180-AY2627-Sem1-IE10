# IM3180-AY2627-Sem1-IE10

This project is a chess-like game with an SFML interface and a configurable
search engine.

## Build with CMake

Requirements: CMake 3.28 or newer, Git, GCC targeting x86-64 with native
128-bit CAS support (`-mcx16`), a CPU supporting CMPXCHG16B, and SQLite3
headers/library. The coherent CAS transposition table deliberately has no
MSVC, Clang, ARM, or mutex fallback; unsupported compiler configurations fail
during configuration and unsupported CPUs are rejected when an Engine is created.
The project targets C++17 explicitly, including its tests.

The default configuration builds the GUI and fetches SFML 3.0.2 during the
first configure, which requires network access. Core-only builds do not fetch
SFML. On non-Windows systems, OpenSSL Crypto is also required for SHA-256;
the GUI additionally needs SFML's platform dependencies. Windows uses BCrypt.

### Windows: MSYS2 UCRT64

Install these packages from the MSYS2 UCRT64 shell (CMake 3.28+ and Git must
also be available):

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make mingw-w64-ucrt-x86_64-sqlite3
```

From PowerShell in a clean clone, set the matching toolchain/runtime PATH
before configuring, building, testing, or running:

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe -DCMAKE_PREFIX_PATH=C:/msys64/ucrt64
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
.\chess.exe
```

If using Git Bash instead of PowerShell, prepend PATH using shell syntax:

```sh
export PATH="/c/msys64/ucrt64/bin:$PATH"
```

Do not reuse a build directory configured with another compiler or generator.
A Windows `0xc0000139` startup error can indicate mismatched runtime DLLs;
use the UCRT64 PATH above for CTest as well as the application.

For a core-only Release build using the same PowerShell PATH:

```powershell
cmake -S . -B build-core -G "MinGW Makefiles" -DCHESS_BUILD_GUI=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe -DCMAKE_PREFIX_PATH=C:/msys64/ucrt64
cmake --build build-core --parallel 4
ctest --test-dir build-core --output-on-failure
```

VS Code's default **Build Chess** task runs the same CMake configure/build
pipeline, including SFML fetching and the CAS compiler check. Its UCRT64 path
is configured for the standard `C:/msys64` installation; adjust it if necessary.
No prebuilt executable, local database, or existing build directory is required.

### Tests and current limitation

Core CTest runs the TT, root-parallel search, Board/Engine, and storage suites.
GUI builds add account/history and UI-worker integration suites. The existing
UI-worker history fixture still expects the older after-move/auto-resume
behavior, while the current UI restores the selected move's parent and pauses
until RESUME; that known test failure is not a compilation failure and has not
been hidden or disabled. Storage tests use isolated build-directory databases.

The older `board_tests.cpp` suite targets the former Board-owned engine API and
is intentionally not part of the CMake test target while it is migrated. The
default test target uses the new public Board/Engine API.

## Architecture

- `Board` owns the authoritative position, turn/action rules, legal move
  generation, move rollback, terminal state, and position hash.
- `Engine` owns one side's search depth, negamax implementation, counters,
  best move, transposition table, and an evaluator object.
- `Evaluator` owns evaluation logic and any playstyle-specific state. The
  built-in `StandardEvaluator` uses `EngineConfig`; custom playstyles can
  derive from `Evaluator` without changing the search implementation.
- `ChessUI` owns one `Board` plus independent White and Black `Engine` objects.
  Player-vs-Player, Player-vs-Engine, and Engine-vs-Engine modes use the same
  Board while selecting the engine associated with the current side.

Each engine can receive its own `EngineConfig`, so evaluation metrics are not
shared accidentally between competing strategies.

To add a new playstyle, derive from `Evaluator` and inject it into an `Engine`:

```cpp
class KingSafetyEvaluator : public Evaluator {
public:
    int evaluate(const Board& board) const override {
        // Calculate and return a score from the side-to-move perspective.
        return 0;
    }
};

Engine white(0, 3, std::make_unique<KingSafetyEvaluator>());
```

`Engine::find_best_move` searches on a private copy of the supplied `Board`, so
the UI's authoritative position is not modified by search or evaluation.
It searches completed-turn depths 1 through the requested depth, retaining the
transposition table between iterations. A matching shallow entry supplies only
its best-move ordering hint; cached scores and bounds require sufficient depth.
Node counters include all iterations. The shared engine/UI default depth is 3;
an explicit depth (including 4) can still be passed to either constructor or search.
`Engine::set_search_threads(n)` opts into root splitting; `n` includes the caller
and defaults to 1. The first ordered root move is searched serially, then the
remaining branches are distributed among bounded participants. Board/evaluator,
killer/history, and counter state stay private; the group shares one coherent
native-CAS transposition table. Independent Engines keep separate tables.
Configure participant count only while idle. Custom evaluators need a `clone()`
override preserving their dynamic type and configuration for parallel search;
sequential custom evaluators do not need to implement it. The built-in Standard,
Aggressive, Positional, and Old evaluators support cloning. No UI thread/depth
default is changed by this API.
The same evaluator injection is available through `ChessUI`'s optional White
and Black evaluator constructor arguments; omitting them keeps the standard
evaluator for both sides.

## Variant FEN (Board API)

`Board::import_fen(const std::string&)` returns `false` for invalid input;
`Board::export_fen() const` returns a canonical six-field position string:

```text
piece_placement side_to_move castling actions_left halfmove_clock fullmove_number
rnbkqbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBKQBNR w KQkq 1 0 1
```

This is a **custom variant FEN, not standard/Lichess-compatible FEN**. Field 4
is exactly `1` or `2` remaining actions, not an en-passant square. Standard `-`
and square tokens such as `e3` are rejected in that field. The actual starting
back rank is `RNBKQBNR`: king on column d, queen on column e. Placement lists
rank 8 first (Board row 7) and rank 1 last (row 0), with uppercase White pieces.
The opening White turn has one action; later turns have two. Captures require
two remaining actions and immediately end the turn. The game ends by king
capture, not checkmate; this API does not change those rules.

Import requires exactly six whitespace-separated fields and eight ranks of
eight squares, using only `PNBRQK/pnbrqk` and empty runs `1`–`8` (no adjacent
digits). Side is `w` or `b`; castling is `-` or unique `KQkq` letters in any
order, exported in `KQkq` order. Counters accept unsigned decimal integers
through `18446744073709551615` (`uint64_t`); halfmove must be at least zero and
fullmove at least one. Leading zeroes and surrounding/inter-field whitespace
are accepted and normalized; signs, overflow, missing fields, and extra fields
are rejected.

The halfmove clock resets on every pawn action or capture, otherwise increases
on **each action**, including castling. The fullmove number increases only when
Black's turn ends, by capture or its last quiet action, not after its first quiet
action. Both counters saturate at the `uint64_t` maximum and are restored by
rollback. They are metadata only: neither affects hashing, search, or draw rules.

Validation is structural, not orthodox chess legality or a proof of reachability.
Each side may have at most one king; both missing is invalid. Both present means
ongoing, while one missing means a win for the side whose king remains. Adjacent
kings and castling flags without corresponding home pieces are accepted, including
flags left after a terminal king capture. Existing move validation still governs
whether castling or any other action can actually be played.

A failed import leaves the entire Board unchanged, including counters, hash,
last move, and existing undo history. A successful import starts a new root:
it clears undo history and last move, derives terminal status, recomputes the
position hash using the Board's existing hash tables, and preserves movement
patterns. Rollback at that imported root does nothing. FEN stores a position,
not action history, repetition history, controllers, clocks, accounts, or saved
matches. There is no clipboard, UI, or database integration in this Board API.

## Running the game

After building, run `chess.exe` from the repository root (or the equivalent executable path for
the selected generator). The UI starts at the menu, where the game mode and
engine side can be selected. The optional `ChessUI` constructor arguments still
set the default engine side and search depth for code that constructs the UI
directly:

```cpp
ChessUI ui(1, Engine::kDefaultSearchDepth); // default: Black, depth 3
```

## Local accounts and match history

Open the profile button at the top right of the main menu to sign up or sign in.
Accounts and completed games persist in `data/chess.db` under the repository root
used during CMake configuration. The directory is created on startup and ignored
by Git. No database server is needed.

The database has three tables:
- `accounts`: unique username and plain SHA-256 password digest.
- `games`: date, result, White/Black labels, and the selected main-line history.
- `player_games`: username, game ID, and played side (`0` = White, `1` = Black).

This is local project/demo authentication, not production password storage.
Usernames are case-sensitive. The existing form accepts up to 40 characters per field.
Sign-out clears the in-memory identity; accounts remain available after restart.

Only matches started while signed in are recorded. Player-vs-Engine uses the chosen
human side. Player-vs-Player records the single signed-in account as White and the
other player as Guest. Guest games and Engine-vs-Engine are not assigned to an account.
The identity is captured when the match starts, so changing accounts mid-game does
not reassign its history.

A completed match stores only the path to its terminal history node, not abandoned
branches. Each history line contains five integers:
`from_row from_col to_row to_col special_move`. Special-move codes use `SpecialMove`
from `board.hpp`; every action is stored in order, including same-turn actions.
Open Profile > Match History to fetch the signed-in account's games, newest first.
The screen remains a summary list; replay is not implemented.

CTest includes storage regressions and, with the GUI enabled, account/history UI
integration tests. These use separate temporary databases inside the build directory,
not the application's `data/chess.db`.
