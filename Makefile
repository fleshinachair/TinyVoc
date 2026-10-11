# TinyVoice. Thin front for the scripts (they are the source of truth and also
# run on their own: `bash scripts/test.sh`). Needs bash, zig (https://ziglang.org),
# and node for the OLED widget test.
#
#   make test      native engine tests + the canvas.js widget test
#   make arm       cross-compile for the Move (aarch64, glibc 2.35) with
#                  -Wall -Wextra -Werror -pedantic, and package build/TinyVoice.tar.gz
#   make install   build and sideload to move.local (MOVE_HOST / MOVE_USER override)
#   make json      regenerate module.json and src/dsp/module_meta.h
#   make clean
.PHONY: all test arm install json clean

all: test arm

test:
	bash scripts/test.sh

arm:
	bash scripts/build.sh

install:
	bash scripts/install.sh --build

json:
	python scripts/gen_module_json.py

clean:
	rm -rf build/tinyvoice build/tests build/TinyVoice.tar.gz
