# Security policy

## Reporting a vulnerability

Please report security problems **privately** through GitHub's private vulnerability reporting:
open the repository's **Security** tab and choose **Report a vulnerability**
(<https://github.com/christianmeier/irixglass-desktop/security/advisories/new>).
Don't open a public issue or pull request for an unfixed vulnerability.

Helpful details:

- what is affected: the installer (`scripts/`), the theme generators (`theme/`), or fm (`fm/`), and
  the commit or release you tested;
- how to reproduce it (a file, an archive, a web page or a command line), and what an attacker
  gains;
- your distribution, Plasma, Qt and KDE Frameworks versions, if it matters.

You should get an answer within a week. Once a fix is out, the advisory is published with credit
to you, unless you'd rather not be named.

## Scope

In scope:

- **fm**: anything a file, folder name, archive, web page, terminal output, D-Bus call
  (`org.freedesktop.FileManager1`) or remote (KIO) location can make fm do without the user asking
  for it: reading or writing files outside what the user picked, running programs, leaking data.
- **`scripts/install.sh` and `scripts/install-craft-app.sh`**: downloads that aren't verified,
  files written outside the user's home or the documented places, command injection.
- **This repository's GitHub Actions workflows.**

Out of scope: bugs in the upstream projects the installer downloads (WhiteSur themes, Apple fonts,
AppImages, Qt, KDE Frameworks); report those to their maintainers.

## Supported versions

Only the latest commit on `main` gets fixes.
