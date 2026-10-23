# All targets run inside the courier-dev image (docker/dev.Dockerfile), so the
# host needs nothing but Docker. Build trees and caches live in named volumes.
PRESET ?= release
IMAGE ?= courier-dev
NPROCS ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu)
DOCKER_RUN = docker run --rm $(DOCKER_TTY) --network host \
	-v $(CURDIR):/src -w /src \
	-v courier-build:/src/build -v courier-ccache:/ccache -v courier-cpm:/cpm \
	-e CPM_SOURCE_CACHE=/cpm --user ubuntu $(DOCKER_EXTRA) $(IMAGE)
DOCKER_TTY = $(shell test -t 0 && echo -it)

.PHONY: image build test unittest shell
image:
	docker build -f docker/dev.Dockerfile -t $(IMAGE) .

build:
	$(DOCKER_RUN) sh -c 'cmake --preset $(PRESET) && cmake --build build/$(PRESET) -j $(NPROCS)'

test: build
	$(DOCKER_RUN) sh -c 'cd build/$(PRESET) && ctest --output-on-failure'

shell:
	$(DOCKER_RUN) bash
