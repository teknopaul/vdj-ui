# The trakcrate command line tools (doc/trakcrate/cli.md, .planning/plans/trakcrate-cli_PLAN.md)
#
#   make          build the helper
#   make test     C unit tests (snip) and the bash tests
#   make deb      build target/trakcrate-cli_<version>_<arch>.deb (no root needed)
#   make clean

all:
	$(MAKE) -C c

test:
	$(MAKE) -C c test
	$(MAKE) -C c test-asan
	test/run.sh

deb:
	deploy/build-deb.sh

clean:
	$(MAKE) -C c clean
	rm -rf target

.PHONY: all test deb clean
