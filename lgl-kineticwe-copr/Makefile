# COPR SCM build driver for KineticWE.
#
# Expected COPR SCM command from the repository root:
#   make -f lgl-kineticwe-copr/Makefile srpm

PACKAGE := kineticwe
SPECFILE := lgl-kineticwe-copr/kineticwe.spec
VERSION := $(shell awk '/^Version:/ {print $$2; exit}' $(SPECFILE))
OUTDIR := $(shell pwd)/lgl-kineticwe-copr/results
SOURCE_PREFIX := $(PACKAGE)-$(VERSION)
KINETICWE_SRC := $(PACKAGE)-$(VERSION).tar.gz

# Override this in COPR if a newer fixed kglobalacceld ref is required:
#   make -f lgl-kineticwe-copr/Makefile srpm KGLOBALACCELD_REF=<commit-or-tag>
KGLOBALACCELD_REF ?= master
KGLOBALACCELD_SRC := kglobalacceld-$(KGLOBALACCELD_REF).tar.gz
KGLOBALACCELD_URL := https://invent.kde.org/plasma/kglobalacceld/-/archive/$(KGLOBALACCELD_REF)/$(KGLOBALACCELD_SRC)

SRPM := $(OUTDIR)/$(PACKAGE)-$(VERSION)-1.src.rpm

srpm: $(SRPM)

$(SRPM): $(OUTDIR)/$(KINETICWE_SRC) $(OUTDIR)/$(KGLOBALACCELD_SRC) $(OUTDIR)/start-kineticwe $(OUTDIR)/kineticwe.desktop
	rpmbuild -bs \
		--define "_sourcedir $(OUTDIR)" \
		--define "_srcrpmdir $(OUTDIR)" \
		--define "_specdir $(shell pwd)/lgl-kineticwe-copr" \
		--define "kglobalacceld_ref $(KGLOBALACCELD_REF)" \
		$(SPECFILE)

$(OUTDIR)/$(KINETICWE_SRC):
	mkdir -p $(OUTDIR)
	git archive \
		--prefix=$(SOURCE_PREFIX)/ \
		-o $(OUTDIR)/$(KINETICWE_SRC) \
		HEAD

$(OUTDIR)/$(KGLOBALACCELD_SRC):
	mkdir -p $(OUTDIR)
	curl -fsSL -o $(OUTDIR)/$(KGLOBALACCELD_SRC) $(KGLOBALACCELD_URL) || \
		wget -q -O $(OUTDIR)/$(KGLOBALACCELD_SRC) $(KGLOBALACCELD_URL)

$(OUTDIR)/start-kineticwe: lgl-kineticwe-copr/SOURCES/start-kineticwe
	mkdir -p $(OUTDIR)
	cp $< $@

$(OUTDIR)/kineticwe.desktop: lgl-kineticwe-copr/SOURCES/kineticwe.desktop
	mkdir -p $(OUTDIR)
	cp $< $@

clean:
	rm -rf $(OUTDIR)

.PHONY: srpm clean
