# Copyright 2019 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
MAKEFILE_DIR := $(realpath $(dir $(lastword $(MAKEFILE_LIST))))
TEST_DATA_URL := https://github.com/google-coral/edgetpu/raw/master/test_data

.PHONY: wasm download zip server reset clean

COMPILATION_MODE ?= dbg
ifeq ($(filter $(COMPILATION_MODE),opt dbg),)
$(error COMPILATION_MODE must be opt or dbg)
endif

ifneq (,$(wildcard /.dockerenv))
  BAZEL_OPTIONS += --output_base=/output/base --output_user_root=/output/user_root
endif

wasm:
	bazel $(BAZEL_OPTIONS) build \
  --distdir=$(MAKEFILE_DIR)/.distdir \
  --verbose_failures \
  --sandbox_debug \
  --subcommands \
  --experimental_repo_remote_exec \
  --compilation_mode=$(COMPILATION_MODE) \
  --define darwinn_portable=1 \
  --action_env PYTHON_BIN_PATH=$(shell which python3) \
  --features=use_pthreads \
  --linkopt=-sPTHREAD_POOL_SIZE=8 \
  --linkopt=-sINITIAL_MEMORY=67108864 \
  --linkopt="-sEXTRA_EXPORTED_RUNTIME_METHODS=['cwrap']" \
  --linkopt=-sASYNCIFY \
  --linkopt=-sASYNCIFY_STACK_SIZE=16384 \
  --linkopt="-sASYNCIFY_IMPORTS=['emscripten_receive_on_main_thread_js','emscripten_asm_const_int_sync_on_main_thread']" \
  //tflite:interpreter-wasm && \
  cp -f "$(MAKEFILE_DIR)/bazel-bin/tflite/interpreter-wasm/interpreter.data" \
        "$(MAKEFILE_DIR)/bazel-bin/tflite/interpreter-wasm/interpreter.js" \
        "$(MAKEFILE_DIR)/bazel-bin/tflite/interpreter-wasm/interpreter.wasm" \
        "$(MAKEFILE_DIR)/bazel-bin/tflite/interpreter-wasm/interpreter.worker.js" \
        "$(MAKEFILE_DIR)/site"

%.tflite:
	mkdir -p $(dir $@) && cd $(dir $@) && wget "$(TEST_DATA_URL)/$(notdir $@)"

download: site/models/mobilenet_v1_1.0_224_quant.tflite \
          site/models/mobilenet_v1_1.0_224_quant_edgetpu.tflite \
          site/models/ssd_mobilenet_v2_coco_quant_postprocess.tflite \
          site/models/ssd_mobilenet_v2_coco_quant_postprocess_edgetpu.tflite \
          site/models/ssd_mobilenet_v2_face_quant_postprocess.tflite \
          site/models/ssd_mobilenet_v2_face_quant_postprocess_edgetpu.tflite

third_party/dfu-util/src/dfu-util:
	git -C third_party clone git://git.code.sf.net/p/dfu-util/dfu-util || true
	cd third_party/dfu-util && ./autogen.sh && ./configure && make

reset: third_party/dfu-util/src/dfu-util
	third_party/dfu-util/src/dfu-util \
    -D third_party/libedgetpu/driver/usb/apex_latest_single_ep.bin \
    -d 1a6e:089a -R || true

zip:
	zip -r site.zip site

server:
	cd "$(MAKEFILE_DIR)/site" && python3 -m http.server

clean:
	rm -f $(MAKEFILE_DIR)/site/interpreter.* \
        $(MAKEFILE_DIR)/site/models/*.tflite

################################################################################
# Docker commands
################################################################################
DOCKER_CONTEXT_DIR := $(MAKEFILE_DIR)/docker
DOCKER_WORKSPACE := $(MAKEFILE_DIR)
DOCKER_CONTAINER_WORKSPACE := /workspace
DOCKER_CPUS ?= k8 armv7a armv6 aarch64
DOCKER_TARGETS ?=
DOCKER_IMAGE ?= debian:buster
DOCKER_TAG_BASE ?= webcoral
DOCKER_TAG := "$(DOCKER_TAG_BASE)-$(subst :,-,$(DOCKER_IMAGE))"
DOCKER_SHELL_COMMAND ?=

DOCKER_MAKE_COMMAND := \
for cpu in $(DOCKER_CPUS); do \
    make CPU=\$${cpu} -C $(DOCKER_CONTAINER_WORKSPACE) $(DOCKER_TARGETS) || exit 1; \
done

define docker_run_command
chmod a+w /; \
groupadd --gid $(shell id -g) $(shell id -g -n); \
useradd -m -e '' -s /bin/bash --gid $(shell id -g) --uid $(shell id -u) $(shell id -u -n); \
echo '$(shell id -u -n) ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers; \
su $(shell id -u -n) $(if $(1),-c '$(1)',)
endef

docker-image:
	docker build $(DOCKER_IMAGE_OPTIONS) -t $(DOCKER_TAG) \
      --build-arg IMAGE=$(DOCKER_IMAGE) $(DOCKER_CONTEXT_DIR)

docker-shell: docker-image
	docker run --rm -i --tty --workdir $(DOCKER_CONTAINER_WORKSPACE) \
      --tmpfs /output:rw,exec,nr_inodes=0,size=20G \
      -v $(DOCKER_WORKSPACE):$(DOCKER_CONTAINER_WORKSPACE) \
      $(DOCKER_TAG) /bin/bash -c "$(call docker_run_command,$(DOCKER_SHELL_COMMAND))"

docker-build: docker-image
	docker run --rm -i $(shell tty -s && echo --tty) \
      -v $(DOCKER_WORKSPACE):$(DOCKER_CONTAINER_WORKSPACE) \
      $(DOCKER_TAG) /bin/bash -c "$(call docker_run_command,$(DOCKER_MAKE_COMMAND))"
