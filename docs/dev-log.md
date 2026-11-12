# Development Log

Each entry records what was done, the commands used, and the outcome.
Only results that were actually executed are recorded here.

## Stage 1 — Research & environment (2026-11-12)

* Checked the tools: g++ 13.3, CMake 3.28, Ninja, Python 3 (matplotlib, pandas), Git. ns-3 was not installed.
* Codex CLI isn't installed and can't be installed here (package installs are blocked). All compiling and testing is done directly.
* `nsnam.org` and `gitlab.com` are blocked by the workspace proxy, so ns-3.49 was cloned from the official GitHub mirror:
  `git clone --depth 1 --branch ns-3.49 https://github.com/nsnam/ns-3-dev-git.git`
* Configured with only the required modules (see `docs/research.md`).
* Researched the packet-filter hook in the 3.49 source. Chose a custom `Ipv4RoutingProtocol` at the top of the router's `Ipv4ListRouting`.
* GitHub: the workspace's GitHub login is invalid and the account isn't linked to Claude, so nothing is pushed yet. Commits are kept locally, one per stage.
