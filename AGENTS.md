# AGENTS.md — Agent Rules for KineticWE

These rules apply to every AI agent (orchestrator and sub-agents) working in this
repository. They override any conflicting default behavior.

---

## 1. Golden rule: NEVER run builds

The user builds and tests locally on their own machine. An agent's job ends at
"code is written and statically verified." Under no circumstances run anything
that compiles, links, packages, installs, or launches binaries.

### Forbidden — never run, in any form

- **Build tools:** `cmake --build`, `ninja`, `make`, `meson compile`,
  `cargo build`, `npm run build` / `yarn build` / `pnpm build`, `nix build`,
  `makepkg`, `dpkg-buildpackage`, `debuild`, `rpmbuild`, `mock`, `copr-cli build`.
- **Distrobox builds:** never run `build-deb-dev.sh`, `build-rpm-dev.sh`,
  `build-kinetic-noctalia.sh`, `install-build-deps.sh`, `debian-deps.sh`, and
  never `distrobox enter` (or `podman`/`docker exec`) into a container to build.
  Distrobox containers are the user's build environment — hands off.
- **User-owned build dirs:** `build/`, `build.deb-quick/`,
  `build.deb-quick-noctalia/`, `build.rpm-quick/`, `build.rpm-quick-noctalia/`,
  `build-nix-dev/`, `build.TI47zXjW/`, `result-kwin` are the user's build
  outputs. Never create, write to, clean, or delete them. Reading (e.g. to
  inspect a compile_commands.json the user generated) is fine.
- **Install / run:** no `make install`, `ninja install`, `meson install`,
  `sudo` anything, no running the compositor, shell, or session scripts
  (`scripts/start-kineticwe.sh`, `scripts/dev-session.sh`), and no running
  autotests — they require built binaries.

### Allowed — fast, read-only verification only

Compile *checks* are fine as long as they parse/configure/lint and finish in
seconds without producing binaries:

- **Configure-only:** `cmake -S . -B <fresh-tmp-dir> -G Ninja` (stop after
  configure) or `meson setup <fresh-tmp-dir>` — never follow with a compile
  step, and always use a fresh temp dir, never the user's build dirs.
- **Syntax / lint:** `clang++ -fsyntax-only` (when include paths are known),
  `qmllint`, `qmlformat --check`, `bash -n`, `shellcheck`, `xmllint --noout`,
  `clang-format --dry-run -Werror`, `cargo check`, `cargo clippy`,
  `tsc --noEmit`, `npm run lint` (only if already defined in package.json).
- **Static analysis:** `cppcheck` (repo ships `cppcheck-suppressions.xml`),
  code search, `git` inspection, reading logs/error output the user pastes.

**Rule of thumb:** if a command compiles, links, packages, installs, or
executes a built artifact — do not run it. If it only parses, configures, or
lints — it is allowed. When in doubt, don't run it; hand the user the exact
command to run locally instead.

### After making code changes

Always end with: (1) what changed and why, (2) which static checks were run and
their results, (3) the exact build/test command for the user to run locally
(e.g. `./build-rpm-dev.sh --quick` or `./build-kinetic-noctalia.sh`), and
(4) anything that could not be verified without a build.

---

## 2. Sub-agentic workflow

Work in this repo follows an orchestrator / sub-agent model.

- **Orchestrator (main agent)** owns: understanding the request, planning,
  decomposing work into tasks, delegating to sub-agents, reviewing and
  integrating their output, and the final summary. The orchestrator does not
  do large implementation work inline when it can be delegated.
- **Task shape:** each delegated task is small, focused, and independent where
  possible. Every delegation must specify: the goal, the exact files/areas to
  touch, the conventions to follow, the acceptance criteria, and a reminder
  that builds are forbidden (Section 1 applies to sub-agents too).
- **Parallelism:** run independent tasks (e.g. compositor change vs. shell QML
  change vs. docs) concurrently; serialize tasks that touch the same files or
  depend on each other's output.
- **Integration:** sub-agents return diffs plus a short summary; the
  orchestrator reviews for correctness, style, and cross-component consistency
  before accepting. The orchestrator is responsible for the final coherent
  state of the tree.
- **Verification:** sub-agents verify with the allowed static checks only and
  report exactly what they ran. "It compiles" is never a claim an agent can
  make — only "it passes static checks; build verification is up to the user."

---

## 3. Repository map

| Path | What it is |
|---|---|
| `src/`, `autotests/`, `CMakeLists.txt` | KineticWE compositor (KWin fork, C++/CMake) |
| `shell/noctalia/` | Vendored Noctalia shell fork (QML); `kinetic-settings/` module is C++/meson |
| `portal/xdg-desktop-portal-kwe/` | Portal backend (xdg-desktop-portal-kde fork) |
| `scripts/` | Session/systemd scripts — never execute |
| `docs/kineticwe-2.0.md` | The KineticWE 2.0 plan — source of truth for direction |
| `build-*.sh`, `*-deps.sh` | User-only distrobox build scripts — never run |
| `flake.nix`, `nix/`, `PKGBUILD`, `kineticwe.spec`, `debian/` | Packaging — edit allowed, build forbidden |

## 4. General conventions

- Follow `.clang-format` and existing KDE/KWin code style; match the
  surrounding code in `shell/noctalia` (QML) and the meson module.
- Development happens on the `kineticwe-2.0` branch; `master` stays untouched.
- Never commit or push unless the user explicitly asks.
- Use absolute paths when referring to files.
