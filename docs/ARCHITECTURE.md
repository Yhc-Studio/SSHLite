# Architecture

SSHLite is a native Win32 frontend around the Windows OpenSSH Client.

## Source modules

### `src/main.cpp`
Minimal Windows subsystem entry point.

### `src/MainWindow.cpp` / `MainWindow.h`
Win32 Modern UI, pages, theme/language handling, history/favorites and session-view state.

### `src/SshBackend.cpp` / `SshBackend.h`
Owns `ssh.exe` child-process lifecycle, SSH_ASKPASS named-pipe handling and concurrent SessionManager state. This keeps blocking process/thread code out of the UI module.

### `src/AppServices.h`
Small bridge used by the backend to post session log/state events and query language/app directory without exposing MainWindow internals.

### `src/SshCommand.cpp` / `SshCommand.h`
OpenSSH command tokenization/normalization, endpoint parsing, complete `-L` mapping parsing, OpenSSH executable discovery and local TCP readiness checks.

### `src/AppIds.h`
Application version and Win32 control/menu/message IDs.

### `src/AppTypes.h`
Shared value types and session-state constants.

### `askpass/askpass.cpp`
Small SSH_ASKPASS helper. Runtime secrets move over a per-session local Named Pipe.

## Dependency policy

No vcpkg, libssh2, OpenSSL SDK, CMake, .NET or Windows App SDK dependency is required. SSH protocol and cryptography are delegated to Windows OpenSSH.
