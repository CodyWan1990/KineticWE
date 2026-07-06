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

KGLOBALACCELD_REF ?= master
KGLOBALACCELD_SRC := kglobalacceld-$(KGLOBALACCELD_REF).tar.gz
KGLOBALACCELD_URL := https://invent.kde.org/plasma/kglobalacceld/-/archive/$(KGLOBALACCELD_REF)/$(KGLOBALACCELD_SRC)

SRPM := $(OUTDIR)/$(PACKAGE)-$(VERSION)-1.src.rpm

srpm: $(SRPM)

$(SRPM): $(OUTDIR)/$(KINETICWE_SRC) $(OUTDIR)/$(KGLOBALACCELD_SRC) $(OUTDIR)/start-kineticwe $(OUTDIR)/kineticwe.desktop $(OUTDIR)/kglobalacceld-mask.conf
	rpmbuild -bs \
		--define "_sourcedir $(OUTDIR)" \
		--define "_srcrpmdir $(OUTDIR)" \
		--define "_specdir $(SELF_DIR)" \
		--define "kglobalacceld_ref $(KGLOBALACCELD_REF)" \
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

$(OUTDIR)/start-kineticwe: $(SELF_DIR)SOURCES/start-kineticwe
	mkdir -p $(OUTDIR)
	cp $< $@

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
