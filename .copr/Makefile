# COPR SCM Build Driver
# =============================================================================
# When COPR builds from SCM, it clones the repo and runs:
#     make -f .copr/Makefile srpm outdir=<path> spec=<path>
# from the repository root directory.
# =============================================================================

PACKAGE := kineticwe
# COPR passes 'outdir' and 'spec' as make variables; fall back to defaults
OUTDIR := $(or $(outdir),$(shell pwd)/results)
SPECFILE := $(or $(spec),kwin-we.spec)
VERSION := $(shell grep '^Version:' $(SPECFILE) | awk '{print $2}')
SRCTARBALL := v$(VERSION).tar.gz        # must match basename of Source0 in spec
KGA_SRC := kglobalacceld-master.tar.gz
KGA_URL := https://invent.kde.org/plasma/kglobalacceld/-/archive/master/$(KGA_SRC)
SRPM := $(OUTDIR)/$(PACKAGE)-$(VERSION).src.rpm

srpm: $(SRPM)

$(SRPM): $(OUTDIR)/$(SRCTARBALL) $(OUTDIR)/$(KGA_SRC)
	@echo "Building SRPM..."
	rpmbuild -bs \
		--define "_sourcedir $(OUTDIR)" \
		--define "_srcrpmdir $(OUTDIR)" \
		--define "_specdir $(dir $(abspath $(SPECFILE)))" \
		$(SPECFILE)

$(OUTDIR)/$(SRCTARBALL):
	@echo "Creating source tarball from git archive..."
	mkdir -p $(OUTDIR)
	git archive \
		--prefix=$(PACKAGE)/ \
		-o $(OUTDIR)/$(SRCTARBALL) \
		HEAD

$(OUTDIR)/$(KGA_SRC):
	@echo "Downloading kglobalacceld source..."
	mkdir -p $(OUTDIR)
	curl -fsSL -o $(OUTDIR)/$(KGA_SRC) $(KGA_URL) || \
		wget -q -O $(OUTDIR)/$(KGA_SRC) $(KGA_URL)

clean:
	rm -rf $(OUTDIR)

.PHONY: srpm clean
