# COPR SCM Build Driver (root dispatcher)
# COPR runs: make -f .copr/Makefile srpm from the repository root

srpm:
	$(MAKE) -f .copr/Makefile srpm

clean:
	$(MAKE) -f .copr/Makefile clean

.PHONY: srpm clean
