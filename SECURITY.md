# Security

SSHLite delegates SSH protocol handling and cryptography to the Windows OpenSSH Client.

## Plaintext passwords in Tunnel Favorites

Password-authentication Tunnel Favorites may intentionally persist an SSH password in plaintext in:

```text
SSHLite.ini
```

This value is not encrypted and is not protected by Windows Credential Manager.

Anyone who can read `SSHLite.ini` can read a stored favorite password.

SSHLite mitigates accidental leakage by:

- warning before plaintext-password storage;
- excluding `SSHLite.ini` in `.gitignore`;
- clearing stale Favorite sections when favorites are rewritten or deleted;
- never writing passwords to Server History;
- never persisting private-key passphrases.

Leave the Favorite password field empty if you do not want the password persisted.

## Runtime AskPass secrets

Non-persisted credentials are supplied to `ssh.exe` through the SSHLite AskPass helper and a per-session local Named Pipe.

## Host keys

SSHLite uses:

```text
-o StrictHostKeyChecking=accept-new
```

New hosts can be added automatically to the user's OpenSSH `known_hosts`, while changed keys remain subject to OpenSSH verification.

## Reports

Never include passwords, private keys, `SSHLite.ini`, private server addresses, tokens, or other secrets in a public issue.
