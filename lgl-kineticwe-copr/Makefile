# COPR SCM build driver for KineticWE.
#
# Works whether invoked from the repo root or this directory:
#   make -f lgl-kineticwe-copr/Makefile srpm   (from repo root)
#   make srpm                                   (from lgl-kineticwe-copr/)
#
# COPR SCM usage: set Subdirectory = lgl-kineticwe-copr, Make targets = srpm

SELF_DIR  := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
REPO_ROOT := $(abspath $(SELF_DIR)/..)

PACKAGE := kineticwe
SPECFILE := $(SELF_DIR)kineticwe.spec
VERSION  := $(shell awk '/^Version:/ {print $$2; exit}' $(SPECFILE))
OUTDIR   := $(SELF_DIR)results

SOURCE_PREFIX    := $(PACKAGE)-$(VERSION)
KINETICWE_SRC    := $(PACKAGE)-$(VERSION).tar.gz

KGLOBALACCELD_REF ?= 5cc88399d8e2a7951798f85127e23f73f1fa0889
KGLOBALACCELD_SRC := kglobalacceld-$(KGLOBALACCELD_REF).tar.gz
KGLOBALACCELD_URL := https://invent.kde.org/plasma/kglobalacceld/-/archive/$(KGLOBALACCELD_REF)/$(KGLOBALACCELD_SRC)

# Pinned kdecoration master commit: kinetic-we needs KDecoration3::Style
# (server-side drop shadows for CSD windows), not yet in a released version.
KDECORATION_REF ?= d13049250c0ea1afc279aa8dc99243565c0d83e8
KDECORATION_SRC := kdecoration-$(KDECORATION_REF).tar.gz
KDECORATION_URL := https://invent.kde.org/plasma/kdecoration/-/archive/$(KDECORATION_REF)/$(KDECORATION_SRC)

# Pinned KConfig commit containing the kconfig_compiler enum-class setter fix
# (KConfig 41592cc, first released in 6.30). Build-time only; drop once the
# buildroot ships KConfig >= 6.30.
KCONFIG_COMPILER_REF ?= 41592ccfd9748be82c83ce98912037bffb73954e
KCONFIG_SRC := kconfig-$(KCONFIG_COMPILER_REF).tar.gz
KCONFIG_URL := https://invent.kde.org/frameworks/kconfig/-/archive/$(KCONFIG_COMPILER_REF)/$(KCONFIG_SRC)

SRPM := $(OUTDIR)/$(PACKAGE)-$(VERSION)-1.src.rpm

srpm: $(SRPM)

$(SRPM): $(OUTDIR)/$(KINETICWE_SRC) $(OUTDIR)/$(KGLOBALACCELD_SRC) $(OUTDIR)/$(KDECORATION_SRC) $(OUTDIR)/$(KCONFIG_SRC) $(OUTDIR)/kineticwe.desktop $(OUTDIR)/kglobalacceld-mask.conf
	rpmbuild -bs \
		--define "_sourcedir $(OUTDIR)" \
		--define "_srcrpmdir $(OUTDIR)" \
		--define "_specdir $(SELF_DIR)" \
		--define "kglobalacceld_ref $(KGLOBALACCELD_REF)" \
		--define "kdecoration_ref $(KDECORATION_REF)" \
		--define "kconfig_compiler_ref $(KCONFIG_COMPILER_REF)" \
		$(SPECFILE)

$(OUTDIR)/$(KINETICWE_SRC):
	mkdir -p $(OUTDIR)
	if command -v git >/dev/null 2>&1; then \
		git -C $(REPO_ROOT) archive \
			--prefix=$(SOURCE_PREFIX)/ \
			-o $(OUTDIR)/$(KINETICWE_SRC) \
			HEAD; \
	else \
		tar czf $(OUTDIR)/$(KINETICWE_SRC) \
			--exclude='.git' \
			--exclude='lgl-kineticwe-copr/results' \
			--transform "s|^\./|$(SOURCE_PREFIX)/|" \
			-C $(REPO_ROOT) .; \
	fi

$(OUTDIR)/$(KGLOBALACCELD_SRC):
	mkdir -p $(OUTDIR)
	curl -fsSL -o $(OUTDIR)/$(KGLOBALACCELD_SRC) $(KGLOBALACCELD_URL) || \
		wget -q -O $(OUTDIR)/$(KGLOBALACCELD_SRC) $(KGLOBALACCELD_URL)

$(OUTDIR)/$(KDECORATION_SRC):
	mkdir -p $(OUTDIR)
	curl -fsSL -o $(OUTDIR)/$(KDECORATION_SRC) $(KDECORATION_URL) || \
		wget -q -O $(OUTDIR)/$(KDECORATION_SRC) $(KDECORATION_URL)

$(OUTDIR)/$(KCONFIG_SRC):
	mkdir -p $(OUTDIR)
	curl -fsSL -o $(OUTDIR)/$(KCONFIG_SRC) $(KCONFIG_URL) || \
		wget -q -O $(OUTDIR)/$(KCONFIG_SRC) $(KCONFIG_URL)

$(OUTDIR)/kineticwe.desktop: $(SELF_DIR)SOURCES/kineticwe.desktop
	mkdir -p $(OUTDIR)
	cp $< $@

$(OUTDIR)/kglobalacceld-mask.conf: $(SELF_DIR)SOURCES/kglobalacceld-mask.conf
	mkdir -p $(OUTDIR)
	cp $< $@

clean:
	rm -rf $(OUTDIR)

# The kineticwe source archive is keyed on the repo's current HEAD, not on
# whether the tarball file already exists on disk — without this, once the
# tarball has been generated once, `make srpm` would keep reusing that stale
# snapshot forever, even after new commits land on master.
.PHONY: srpm clean $(OUTDIR)/$(KINETICWE_SRC)
