# A Shell Game

Build a minimal Unix-style shell called `crash` that supports job control, background/foreground execution, signal handling, and job tracking.

## Summary

Implemented a command-line shell that:
- Parses input lines into commands
- Supports foreground and background execution (`&`)
- Manages multiple jobs concurrently
- Handles user signals like `Ctrl+C`, `Ctrl+Z`, and `Ctrl+\`
- Implements job control commands like `jobs`, `fg`, `bg`, and `nuke`

This project emphasizes **signal safety**, **concurrency**, and **process control** — core concepts in systems programming.

---

## Specification

### Supported Commands

| Command | Description |
|--------|-------------|
| `quit` | Exit the shell |
| `jobs` | List current background/suspended jobs |
| `nuke [PID/%JOBID ...]` | Kill one or more jobs or PIDs with SIGKILL |
| `fg [PID/%JOBID]` | Resume a job in the foreground |
| `bg [PID/%JOBID ...]` | Resume job(s) in the background |
| `program args` | Run a program in the foreground |
| `program args &` | Run a program in the background |

- Job IDs use `%N` format (e.g. `%3`)
- PIDs are direct numeric values
- Commands can be separated by `;` or `&`


### Job Execution

- Foreground jobs block the shell until finished or suspended.
- Background jobs allow continued shell use immediately.
- Each job receives a unique job ID starting from 1.
- Shell supports up to **32 concurrent jobs**.


### Signals

| Signal | Action |
|--------|--------|
| `Ctrl+C` (`SIGINT`) | Sends signal to foreground process |
| `Ctrl+Z` (`SIGTSTP`) | Suspends foreground process |
| `Ctrl+\` (`SIGQUIT`) | Quits the shell if no foreground job, else sends to foreground process |
| `Ctrl+D` (EOF) | Exits shell if idle |


### Output Formatting

Messages look like:

```bash
[1] (12345)  running    sleep
[1] (12345)  suspended  sleep
[1] (12345)  continued  sleep
[1] (12345)  finished   sleep
[1] (12345)  killed     sleep
[1] (12345)  killed (core dumped)  sleep
```
- Commands are displayed without arguments
- Output are **atomic** (no interleaved prints)


### ⚠ Error Messages

All error messages go to **standard error** and follow this format:

```bash
ERROR: fg needs exactly one argument
ERROR: no job 23
ERROR: cannot run foo
```

---

### Notes

- This shell does **not** support features like pipes (`|`), redirection (`>`, `<`), or quoting.
- Everything was implemented in **C**, in a single file: `crash.c`
- No external libraries used beyond standard `libc`
