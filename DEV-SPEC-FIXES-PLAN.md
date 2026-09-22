# Plan For Spec Fixes That Belong In The Build Script

## Purpose

The spec currently does two different jobs:

1. It describes the RPM packages.
2. It changes source files during `%prep` so the packages build correctly.

The first job belongs in the spec. Some parts of the second job could be done
by `build-rpms.sh` before it creates the source archives. This plan identifies
those changes and explains the intended result.

The goal is not to move every spec change into the script. The goal is to move
source changes into one clear staging step, while keeping RPM rules in the
spec.

## Important Distinction

The portal coexistence design is the long-term solution. The KDE and KWE
backends must keep separate names, D-Bus services, portal metadata, locale
files, and logging-category files.

Release 7 does not yet implement that source rename in the main build script.
The current spec applies it during `%prep`, using substitutions that match the
current pinned source layout. That is expected to work for the current source,
but it is not yet build-tested and may need maintenance after a source update.

This plan moves the source transformation into a controlled staging step. It
does not change the coexistence design or remove the RPM ownership rules.

## Fix 1: Portal Translation Names

### What Is The Problem?

The KineticWE portal is based on the KDE portal. Its source still uses the KDE
translation name, so it can build files named:

```text
xdg-desktop-portal-kde.mo
```

The stock Fedora KDE portal owns the same file.

### Why Does It Matter?

RPM cannot install two packages that own the same locale file. Installing or
upgrading KineticWE beside the stock KDE portal can therefore fail.

### What Does The Spec Do Now?

During `%prep`, the spec changes the translation domain, renames the `.po`
files, updates their project names, and excludes the generated KWE locale file
from the main package. The portal subpackage owns the renamed file.

### How Should The Build Script Handle It?

`build-rpms.sh` should export `HEAD` to a temporary staging directory, apply
the same deterministic rename there, and create the main source archive from
that directory. The source checkout must not be changed.

After this is working and verified, remove the duplicate locale transformation
from `%prep`. Keep the `%files` ownership and exclusion rules in the spec,
because they describe the RPM layout. Until then, the release 7 spec patch is
the active implementation.

## Fix 2: Portal Logging Category Name

### What Is The Problem?

The portal source also installs `xdp-kde.categories`, which uses the KDE name
and can conflict with the stock KDE portal's logging-category file.

### Why Does It Matter?

The locale fix alone is not enough. A second file conflict can still stop the
RPM transaction.

### What Does The Spec Do Now?

The spec changes the source installation name to `xdp-kwe.categories`, excludes
that file from the main package, and assigns it to `xdg-desktop-portal-kwe`.

### How Should The Build Script Handle It?

Apply the logging-category rename in the same temporary source staging step as
the translation rename. Keep the final `%files` ownership in the spec. Until
that staging step exists, the release 7 `%prep` substitution remains the active
implementation.

These two portal changes should be moved together because they solve the same
package conflict and must always use the same KWE naming scheme.

## Fix 3: Greeter Account Name

### What Is The Problem?

The greeter source provides a tmpfiles entry using the generic account
`greeter:greeter`. Fedora's `greetd` package uses `greetd:greetd` instead.

### Why Does It Matter?

The wrong account name can prevent tmpfiles from creating the greeter's runtime
directory with the correct owner.

### What Does The Spec Do Now?

After unpacking the pinned greeter archive, `%install` changes the account and
group names in the generated tmpfiles file before it is packaged.

### How Could The Build Script Handle It?

The build script could patch the pinned greeter archive while staging it, before
the spec sees the archive. This is optional and should only be done if the
staging code can preserve the pinned source and apply the patch repeatably.

If the greeter archive is left unchanged, keeping this small install-time fix
in the spec is also reasonable. It is not part of the portal conflict and does
not need to move for release 7.

## Fixes That Should Stay In The Spec

These changes describe RPM behavior, not source preparation:

- `Requires:` and `Recommends:` entries
- `%files` ownership and `%exclude` rules
- Package and subpackage definitions
- `%posttrans` install messages
- Removal of files that conflict with Fedora-owned KWin data
- The final greeter tmpfiles ownership rule, unless the external archive patch
  is deliberately moved later

Moving these into `build-rpms.sh` would make the package layout harder to see
and would mix RPM policy with source export logic.

## Build Script Design

The source-fix stage should:

1. Export committed `HEAD` to a temporary directory.
2. Check that each expected source file exists.
3. Apply the portal translation and logging-category changes.
4. Optionally apply the greeter archive patch.
5. Create the `kineticwe` source archive from the staged directory.
6. Remove the temporary directory after the archive is created.

The stage should fail clearly if an expected file or source pattern is missing.
That prevents a future upstream source change from silently producing KDE-named
files again.

## Validation

After implementing the staging step:

- Confirm `/home/lgl/kineticwe/` is unchanged by the build.
- Inspect the source archive and confirm it contains KWE names.
- Build the SRPM and binary RPMs.
- Confirm the main package does not contain the portal locale or category file.
- Confirm `xdg-desktop-portal-kwe` contains both files.
- Install it beside the stock KDE portal.
- Repeat the build using cached downloads.

## Completion Criteria

The portal source fixes are applied by `build-rpms.sh`, the spec keeps the RPM
ownership rules, and release 7 builds and installs without conflicts. The
greeter source patch may remain in the spec unless there is a clear benefit to
moving it into the same staging system. Until this work is complete, release 7
should be described as a current-source fix, not a permanently update-safe
solution.
