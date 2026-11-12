# Top-level convenience targets. See README.md for onboarding.

.PHONY: ci test-host test-web preview-web bootstrap setup check-prereqs setup-flashtool uart-console

test-host:
	$(MAKE) -C firmware/test test-host

test-web:
	$(MAKE) -C tools/web test-web

preview-web:
	$(MAKE) -C tools/web preview-web

bootstrap:
	./tools/bootstrap.sh

setup: bootstrap

setup-flashtool:
	./tools/setup-flashtool.sh

uart-console:
	@echo "Usage: ./tools/uart-console.sh /dev/ttyUSB0"

check-prereqs:
	./tools/check-prereqs.sh host
	./tools/check-prereqs.sh target || true

# Full local gate (host + sanitizers + web + ARM both banks + budgets).
.PHONY: ci
ci:
	./tools/ci.sh
