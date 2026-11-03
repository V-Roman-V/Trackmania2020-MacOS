.PHONY: help install build package check probes

PROBES := $(patsubst tests/probes/%.c,.build/probes/%.exe,$(wildcard tests/probes/*.c))
SCRIPTS := install.sh bin/tm2020 tools/build-runtime.sh tools/package.sh tests/test-cli.sh tests/test-install.sh

help:
	@echo 'make install  Install the runtime, Ubisoft Connect and the Trackmania 2020 app'
	@echo 'make build    Build the runtime from source'
	@echo 'make package  Pack the built runtime and its source for a release'
	@echo 'make check    Validate the scripts and run the offline tests'
	@echo 'make probes   Build the Windows test programs into .build/probes'

install:
	./install.sh

build:
	./tools/build-runtime.sh

package:
	./tools/package.sh

check:
	@for script in $(SCRIPTS); do bash -n "$$script" || exit 1; done
	python3 -I -m py_compile tools/session.py
	shellcheck $(SCRIPTS)
	./tests/test-cli.sh
	./tests/test-install.sh

probes: $(PROBES)

.build/probes/%.exe: tests/probes/%.c
	@mkdir -p .build/probes
	x86_64-w64-mingw32-gcc -O2 -o $@ $< -ld3d11 -ldxgi -ldxguid -luuid -luser32 -lsynchronization
