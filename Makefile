.PHONY: help build check probes

PROBES := $(patsubst tests/probes/%.c,.build/probes/%.exe,$(wildcard tests/probes/*.c))

help:
	@echo 'make build    Build the runtime from source'
	@echo 'make check    Validate the scripts and run the offline tests'
	@echo 'make probes   Build the Windows test programs into .build/probes'

build:
	./tools/build-runtime.sh

check:
	bash -n bin/tm2020
	bash -n tools/build-runtime.sh
	bash -n tests/test-cli.sh
	shellcheck bin/tm2020 tools/build-runtime.sh tests/test-cli.sh
	./tests/test-cli.sh

probes: $(PROBES)

.build/probes/%.exe: tests/probes/%.c
	@mkdir -p .build/probes
	x86_64-w64-mingw32-gcc -O2 -o $@ $< -ld3d11 -ldxgi -ldxguid -luuid
