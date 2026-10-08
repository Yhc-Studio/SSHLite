# SSHLite v1.0.0

SSHLite 1.0.0 is the first public stable release of the project.

## Highlights

- Native Win32 modern UI for Windows 10/11.
- Chinese / English live language switching.
- Microsoft YaHei UI typography.
- Light, Dark, Ocean, Emerald, Rose, and Custom Accent themes.
- Multiple simultaneous SSH sessions.
- Multiple `-L` local forwards in a single session.
- Full local-forward mapping display: local listener → remote target.
- Password authentication and SSH private-key authentication.
- Dedicated Server History and Tunnel Favorites pages.
- Optional plaintext password persistence for password-authentication favorites.
- Multiple local browser entries for forwarded services.
- Close/disconnect/delete confirmation safeguards.
- Port conflict and public-bind warnings.
- `Ctrl+A` Select All support for native text controls and Activity log.
- Empty SSH command field on first launch; no server-specific default configuration.
- Multi-file Visual Studio source layout.
- UTF-8 MSVC project configuration.
- Windows OpenSSH backend with no vcpkg, libssh2, OpenSSL SDK, CMake, .NET, or Windows App SDK dependency.

## Release files

```text
SSHLite.exe
SSHLiteAskPass.exe
README.md
RELEASE_NOTES.md
LICENSE
```

`SSHLite.exe` and `SSHLiteAskPass.exe` must remain in the same directory.

## Security note

A Tunnel Favorite may optionally store an SSH password as plaintext in `SSHLite.ini`. This file is ignored by Git, but users must not manually upload or share it.

Server History does not store SSH passwords. Private-key passphrases are never persisted.
