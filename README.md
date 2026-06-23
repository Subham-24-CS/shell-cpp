# Custom C++ Shell Implementation

A fully functional, POSIX-compliant minimalist shell environment written from scratch in C++. It features an interactive command-line interface leveraging the `GNU Readline` library to provide tab completion, history tracking, built-in commands, standard I/O redirection, pipeline chains, and basic background job control.

---

## Key Features

- **Interactive Command Line**: Uses `readline` for real-time user input with full support for line editing and command-history retrieval.
- **Robust Argument Parsing**: Supports single quotes (`'...'`), double quotes (`"..."`), and backslash escaping (`\`), contextually handling embedded spaces and special characters.
- **Built-in Shell Commands**: Includes full native implementations of standard utilities:
  - `cd` — Change directory (supports `~` expansion).
  - `pwd` — Print current working directory.
  - `echo` — Output arguments to stdout.
  - `type` — Identify commands as builtins or external path executables.
  - `declare` — Create and list shell variables (`declare -p`).
  - `history` — Retrieve, append (`-a`), read (`-r`), or write (`-w`) session command logs.
  - `jobs` — Inspect currently active asynchronous processes.
  - `complete` — Manage programmable completions (`-C`, `-p`, `-r`).
  - `exit` — Cleanly exit the shell session and persist history logs.
- **Parameter Expansion**: Automatically replaces variables matching `$VAR` or `${VAR}` with registered values from the shell scope.
- **Redirection (Standard & Error Paths)**:
  - Output redirection: `>` or `1>` (overwrite), `>>` or `1>>` (append).
  - Error redirection: `2>` (overwrite), `2>>` (append).
- **Pipeline Processing**: Chains multiple commands via pipes (`|`) utilizing standard Unix multi-fork processing.
- **Asynchronous Background Processing**: Appending `&` to a command schedules execution asynchronously as a background process, tracked with localized numeric job IDs and status checks before each new prompt.
- **Advanced Autocompletion System**:
  - Context-aware tab completion for builtins and binaries inside the user's `PATH`.
  - Fallback completion handler targeting local directories and filenames.
  - Extensible programmable auto-completions mirroring standard bash hooks (`complete -C`).

---

## Directory Architecture

```text
.
├── src/
│   └── main.cpp                  # Primary source code containing core shell loop
├── CMakeLists.txt                # Build orchestration file
├── vcpkg.json                    # Dependency declaration for GNU Readline package
├── vcpkg-configuration.json      # Version locking parameters
├── codecrafters.yml              # Deployment integration configurations
├── your_program.sh               # Initialization/execution launcher script
└── README.md                     # Documentation

```

## Compilation and Setup Guide 

The project relies on a modern C++ compiler supporting C++17 or higher, along with the `GNU Readline` package library. Package management is natively orchestrated using `vcpkg` and built using `CMake`

### Prerequisites

Ensure you have your toolchain installed (For Ubuntu/Debian users)

```text
sudo apt-get update
sudo apt-get install build-essential cmake libreadline-dev
```

### Installation Steps

- **Build Project** : Initialize configuration and build the target binary from the workspace root
  ```text
  cmake -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build
  ```
- **Run Environment** : Launch the shell via the convenience runner script
  ```text
  ./your_program.sh
  ```
