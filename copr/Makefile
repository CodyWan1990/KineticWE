# COPR SCM Build Driver
# =============================================================================
# When COPR builds from SCM, it runs:  make -f copr/Makefile srpm
# This generates an SRPM from the current source tree.
# =============================================================================

PACKAGE := kineticwe
VERSION := $(shell grep '^Version:' ../kwin-we.spec | awk '{print $$2}')
SPECFILE := kwin-we.spec
OUTDIR := $(shell pwd)/results
TARNAME := $(PACKAGE)-$(VERSION)

srpm: $(OUTDIR)/$(TARNAME).src.rpm

$(OUTDIR)/$(TARNAME).src.rpm: $(OUTDIR)/$(TARNAME).tar.gz
	@echo "Building SRPM..."
	rpmbuild -bs \
		--define "_sourcedir $(OUTDIR)" \
		--define "_srcrpmdir $(OUTDIR)" \
		--define "_specdir $(shell pwd)/.." \
		$(shell pwd)/../$(SPECFILE)

$(OUTDIR)/$(TARNAME).tar.gz:
	@echo "Creating source tarball from git archive..."
	mkdir -p $(OUTDIR)
	git -C .. archive \
		--prefix=$(TARNAME)/ \
		-o $(OUTDIR)/$(TARNAME).tar.gz \
		HEAD
	# kglobalacceld source is fetched by rpmbuild from Source1 URL in the spec,
	# so we don't need to bundle it here.

clean:
	rm -rf $(OUTDIR)

.PHONY: srpm clean
