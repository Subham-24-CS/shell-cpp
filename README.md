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

## Process Lifecycle Architecture

The shell interfaces directly with the Linux kernel using the standard POSIX process management model:

* **Process Isolation (`fork`)**: For every external command or pipeline stage, the shell spawns a distinct child process via `fork()` to ensure a crash or hang in a program doesn't compromise the parent shell loop.
* **Program Execution (`execvp` / `execv`)**: The child process replaces its memory space using `execvp()` to search through directories defined in the user's `$PATH` and execute the program binary with your parsed arguments. For custom programmable autocompletions, it handles background scripts via `execv()`.
* **Pipeline Synchronization (`pipe` & `dup2`)**: Multi-stage pipelines (e.g., `cmd1 | cmd2`) initialize raw unidirectional communication descriptors via `pipe()`. The shell manipulates standard file descriptors (`STDIN_FILENO`, `STDOUT_FILENO`) in the child processes using `dup2()` before triggering their respective `exec` routines.
* **Asynchronous Tracking (`waitpid`)**: For foreground instructions, the parent shell blocks using `waitpid()` until execution wraps up. For background tasks (`&`), the process ID is registered into an active job tracker list and periodically reaped non-blocking via `waitpid(..., WNOHANG)` right before a new prompt is generated to prevent zombie processes.

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

## Usage Guide

- Basic Commands
  ```text
  $ pwd
  /home/developer/workspace

  $ cd src
  $ pwd
  /home/developer/workspace/src

  $ echo "Hello from our custom C++ shell"
  Hello from our custom C++ shell

  $ echo "Hello    World"
  Hello    World

  $ echo 'Text with  single  quotes'
  Text with  single  quotes

  $ echo Embedded\ \ Spaces\ \ via\ \ Escapes
  Embedded  Spaces  via  Escapes
  ```
- Variable Declaration & Parameter Expansion
  ```text
  $declare MY_VAR="Project-Alpha"$ echo $MY_VAR
  Project-Alpha

  $ echo "Working on ${MY_VAR}_v2"
  Working on Project-Alpha_v2
  ```
- Pipelines & I/O Redirection
  ```text
  $type echo > output.txt$ cat output.txt
  echo is a shell builtin
  
  $ ls non_existent_file 2>> error.log
  
  $ history | grep echo
      1  echo "Hello from our custom C++ shell"
      2  declare MY_VAR="Project-Alpha"
      3  echo $MY_VAR
  ```
- Job Control (Background Execution)
  ```text
  $ sleep 10 &
  [1] 12345

  # View background jobs manually
  $ jobs
  [1]+  Running                 sleep 10 &

  # Upon process completion, right before the subsequent prompt renders:
  [1]+  Done                    sleep 10
  ```
- Session History Interactivity
  - Press the `Up` and `Down` arrow keys to seamlessly traverse previous commands.
  - Append History (`-a`): Appends newly executed session commands into your history file.
  - Read History (`-r`): Reads the contents of your history file and appends them to the current session history.
  - Write History (`-w`): Force save your in-memory history registry to an external file:
    ```text
    $ history -w backup_history.txt
    ```
- Custom Programmable Autocompletion
  You can program your shell to call external scripts to generate custom tab-completions for specific commands using the `-C` flag:
  ```text
  # Register an external script to handle completions for a custom command 'mycmd'
  $ complete -C /path/to/my_completion_script.sh mycmd

  # View currently registered custom programmable completions
  $ complete -p
  complete -C /path/to/my_completion_script.sh mycmd

  # Pressing [TAB] after typing 'mycmd ' will now safely execute your script in a child process to provide suggestions!

  # Remove the custom completion registration
  $ complete -r mycmd
  ```
