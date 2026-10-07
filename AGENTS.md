# AGENTS.md — Agent Rules for KineticWE

These rules apply to every AI agent (orchestrator and sub-agents) working in this
repository. They override any conflicting default behavior.

---

## 1. Golden rule: NEVER run builds

The user builds and tests locally on their own machine. An agent's job ends at
"code is written and statically verified." Under no circumstances run anything
that compiles, links, packages, installs, or launches binaries.

Always tell me the exact build command to run after changes: `./build-rpm-dev.sh --kwin-only` for kwin-only, `./build-rpm-dev.sh --noct-only` for noctalia-only, or `./build-rpm-dev.sh --binary` / `--full` for the full build. On Arch: `./install-arch.sh --update` (rebuilds + pacman -U all packages, only if something changed), `./install-arch.sh` (all packages), or `./install-arch.sh --local-dev --update` for dev builds into ~/.local (drop `--update` to rebuild everything; add `--no-greeter` to skip the greeter in either mode).

### Forbidden — never run, in any form

- **Build tools:** `cmake --build`, `ninja`, `make`, `meson compile`,
  `cargo build`, `npm run build` / `yarn build` / `pnpm build`, `nix build`,
  `makepkg`, `dpkg-buildpackage`, `debuild`, `rpmbuild`, `mock`, `copr-cli build`.
- **Distrobox builds:** never run `build-deb-dev.sh`, `build-rpm-dev.sh`,
  `build-kinetic-noctalia.sh` (superseded by `build-rpm-dev.sh --noct-only`),
  `install-build-deps.sh`, `debian-deps.sh`, and
  never `distrobox enter` (or `podman`/`docker exec`) into a container to build.
  Distrobox containers are the user's build environment — hands off.
- **Arch native builds:** never run `install-arch.sh` (formerly
  `build-arch-native.sh`) in any mode, including `--reset-local`; it builds,
  installs with sudo/pacman, or deletes installed files. `--help` is the only
  exception. Its build dirs `build.arch-native*/` and `build.arch-pkg/` are
  user-owned like the ones below.
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
(e.g. `./build-rpm-dev.sh --kwin-only`, `./build-rpm-dev.sh --noct-only`, or `./build-rpm-dev.sh --binary` / `--full`), and
(4) anything that could not be verified without a build.

---

## 2. Plan first, get approval before implementing

No code changes happen without a plan. This applies to every change, no
matter how small or large — a one-line typo fix, a comment, a refactor, a
new feature.

- **Plan before any edit:** before writing, editing, or deleting any file, the
  agent must present a plan stating: the goal, the exact files to touch, the
  approach, and any risks or open questions. Read-only investigation (code
  search, reading files, running allowed static checks) is fine before and
  during planning — that is not a change.
- **Get approval:** wait for explicit user approval before implementing. Do
  not start editing until the user says go. If the user approves with
  amendments, implement the amended plan.
- **Plan deviations:** if, during implementation, the agent discovers the plan
  is wrong or incomplete (e.g. the real fix touches different files than
  planned), stop and present the revised plan for approval before continuing —
  do not improvise.
- **Sub-agents:** the orchestrator must pass the approved plan to every
  sub-agent in the delegation; sub-agents must not deviate from it. If a
  sub-agent hits an unexpected snag, it reports back to the orchestrator
  rather than improvising, and the orchestrator returns to the user for
  approval if the plan needs to change.
- **Exceptions:** none. Even trivial changes (comments, formatting, typos)
  get a plan and approval first. A plan can be a single sentence for tiny
  changes, but it must exist and be approved before any edit.

---

## 3. Sub-agentic workflow

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

## 4. Repository map

| Path | What it is |
|---|---|
| `src/`, `autotests/`, `CMakeLists.txt` | KineticWE compositor (KWin fork, C++/CMake) |
| `shell/noctalia/` | Vendored Noctalia shell fork (QML); `kinetic-settings/` module is C++/meson |
| `portal/xdg-desktop-portal-kwe/` | Portal backend (xdg-desktop-portal-kde fork) |
| `scripts/` | Session/systemd scripts — never execute |
| `docs/kineticwe-2.0.md` | The KineticWE 2.0 plan — source of truth for direction |
| `build-*.sh`, `*-deps.sh` | User-only distrobox build scripts — never run |
| `install-arch.sh`, `PKGBUILD`, `arch/`, `ci/` | Arch native build script and packaging — edit allowed, never run |
| `flake.nix`, `nix/`, `PKGBUILD`, `kineticwe.spec`, `debian/` | Packaging — edit allowed, build forbidden |

## 5. General conventions

- Follow `.clang-format` and existing KDE/KWin code style; match the
  surrounding code in `shell/noctalia` (QML) and the meson module.
- Development happens on the `kineticwe-2.0` branch; `master` stays untouched.
- Never commit or push unless the user explicitly asks.
- Use absolute paths when referring to files.
