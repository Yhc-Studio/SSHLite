# SSHLite Usage Guide

## Quick Connect

Use Quick Connect to create and manage active SSH sessions. Each click on **New Connection** launches an independent Windows `ssh.exe` process.

The SSH command field is empty on first launch. Saved History or Favorites can repopulate it later.

### Multiple local forwards

A single session can contain multiple `-L` rules:

```bat
ssh -CN -L 6006:127.0.0.1:6006 -L 8800:127.0.0.1:8800 user@example.com -p 22
```

SSHLite parses every `-L` mapping and provides the corresponding local browser entries.

## Server History

History is automatically updated when a connection is launched.

- Up to 20 recent entries are retained.
- An entry can be restored to Quick Connect.
- SSH passwords are not stored in History.
- Private-key passphrases are not stored.

## Tunnel Favorites

Favorites are user-managed connection profiles.

They can store:

- profile name;
- SSH command;
- authentication mode;
- private-key path;
- automatic-browser option;
- an optional plaintext SSH password for password authentication.

A stored favorite password is written directly to `SSHLite.ini` and is not encrypted. Leave the field empty if you do not want to persist a password.

## Safety conventions

SSHLite intentionally blocks or warns on common mistakes:

- duplicate `-L` local ports;
- ports already used by another active SSHLite session;
- ports already listened to by another local application;
- public forwarding with `-g` / public bind addresses;
- duplicate active sessions;
- disconnect-all operations;
- deletion of stored profiles/history;
- closing the application while sessions are active.

## Plain SSH / port 22

`ssh user@example.com -p 22` is accepted, but SSHLite has no embedded terminal. It therefore treats a plain SSH connection as a non-interactive keepalive session after user confirmation.

Use Windows Terminal or PowerShell for an interactive shell.

## Keyboard shortcuts

Native text fields support standard clipboard shortcuts and application-level `Ctrl+A` Select All.
