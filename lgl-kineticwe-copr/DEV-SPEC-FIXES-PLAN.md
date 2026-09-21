# Plan For Build-Script Fixes

## Purpose

The build script should handle build setup and source packaging inputs. The RPM
spec should handle package metadata, file ownership, and install-time actions.

Portal source changes are deliberately out of scope for this plan. They will be
managed separately and must not be added to `build-rpms.sh` or folded into the
main build-script work.

## Keep In The Spec

The following are RPM rules and should stay in `kineticwe.spec`:

- `Requires:` and `Recommends:` entries
- Package and subpackage definitions
- `%files` ownership and `%exclude` rules
- `%posttrans` install messages
- The portal packaging workaround in `%prep`
- The greeter tmpfiles ownership correction

These rules describe the final package layout or installation behavior.

## Build-Script Work

`build-rpms.sh` should:

- Find its packaging directory from the script location.
- Find the source repository from the packaging directory's parent.
- Write generated files to the ignored `results/` directory.
- Copy the required `SOURCES` files into the RPM input directory.
- Build the SRPM and binary RPMs from the same spec and source inputs.
- Fail clearly when a required input is missing.

The script must not edit the portal source tree or apply portal translation or
logging-category changes. Those changes are managed separately.

## Required Build Inputs

The current spec requires these packaging inputs:

- `SOURCES/kineticwe.desktop`
- `SOURCES/kglobalacceld-mask.conf`
- `kineticwe-setup-greeter.sh`
- `start-kineticwe.sh`

The build script should check for these files before starting the build.

## Validation

- Run the build from `lgl-kineticwe-copr/`, not from a fixed home directory.
- Confirm output is written to `results/`.
- Build the release 7 SRPM and binary RPMs.
- Check that all expected subpackages are produced.
- Check package file ownership and dependency metadata.
- Test installation beside the stock KDE portal.
- Confirm the RPM transaction completes without the nested RPM lock error.

## Completion Criteria

The build works from the moved packaging directory, uses no user-specific
output path, keeps portal source maintenance separate, and produces the full
release 7 package set from the committed source and packaging files.
