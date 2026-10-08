# Changelog

## 1.0.0 — 2026-10-08

Initial public stable release.

### Added

- Native Win32 modern interface.
- Chinese / English language switching.
- Microsoft YaHei UI font across the application.
- Light, Dark, Ocean, Emerald, Rose and Custom Accent themes.
- Multiple simultaneous SSH session management.
- Multiple `-L` local forwards per SSH session.
- Full `-L` mapping parsing and local browser entry selection.
- Password and SSH private-key authentication.
- Per-session SSH_ASKPASS / Named Pipe credential handoff.
- Dedicated Server History page.
- Dedicated Tunnel Favorites page.
- Optional plaintext SSH password storage in Tunnel Favorites.
- Session, port-conflict, public-bind and close/disconnect safeguards.
- `Ctrl+A` Select All support in text controls and Activity log.
- Bounded Activity log and session cleanup for long-running use.
- Multi-file Visual Studio source layout.
- UTF-8 MSVC build configuration.
- GitHub Actions build and tagged-release workflows.

### Notes

- The first-launch SSH command field is intentionally empty.
- SSHLite is a connection/tunnel manager, not an embedded terminal emulator.
- Windows OpenSSH (`ssh.exe`) is used as the SSH transport backend.
