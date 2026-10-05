# Shared build rules for the native ports (static recompilation with M-HT/SR).
#
# A game folder has a Makefile with two lines:
#     COMMON := ../../common/native-mac
#     include $(COMMON)/mk/game.mk
# and these files:
#     game.conf        GAME_NAME (program name), GAME_EXE (exe in the game
#                      folder), GAME_EXE_SHA256 (the supported exe version),
#                      GAME_LLASM (name of the SRW output),
#                      GAME_DIR_DEFAULT (relative to $HOME)
#     runtime/game.h   values for the shared runtime (common/native-mac/runtime/game-info.h)
#     srw/             SR.cfg, llasm/*.sci; optional jump_tables.txt,
#                      data_in_text.txt, not_relocations.txt (gen_relocs.py)
# Optional: runtime/*.c, runtime/llasm/*.llasm|*.llinc|*.c (a file with the
# same name as a shared file replaces it), runtime/imports.spec (added to
# the shared spec).
#
#   make tools     build SRW and llasm into the shared build folder (once)
#   make           translate the exe and build build/$(GAME_NAME)
#
# Run in the conda env of the repository (". ../../common/native-mac/tools/env.sh").
# GAME_DIR: folder with the original game (the exe is read from it).

.DELETE_ON_ERROR:
SHELL       := /bin/bash
.SHELLFLAGS := -o pipefail -c

include game.conf

COMMON   ?= ../../common/native-mac
TOOLS    := $(COMMON)/tools
HOSTBIN  := $(COMMON)/../../build/bin
GAME_DIR ?= $(HOME)/$(GAME_DIR_DEFAULT)
B        := build
GEN      := $(B)/gen
OBJ      := $(B)/obj
STAGE    := $(GEN)/rt-llasm
JOBS     ?= 8

LLASMFLAGS := -m64 -inline-idiv -inline-float -pic -ptrofs
LLVMFLAGS  := -mtriple=arm64-apple-darwin --relocation-model=pic
CXX        := clang++
CC         := clang
# SDL2 comes from the conda env (. common/native-mac/tools/env.sh), never from Homebrew.
ifeq ($(CONDA_PREFIX),)
$(error CONDA_PREFIX is not set: run ". $(TOOLS)/env.sh" first)
endif
SDL_CONFIG := $(CONDA_PREFIX)/bin/sdl2-config
SDL_CFLAGS := $(shell $(SDL_CONFIG) --cflags)
# Link SDL2 by full path, so that -L does not also pick the env's libc++
# (the system libc++ is used). The rpath finds SDL2 in a development build;
# common/native-mac/macos/make-bundle.sh changes it to the copy in the app bundle.
SDL_LIBS   := $(CONDA_PREFIX)/lib/libSDL2main.a $(CONDA_PREFIX)/lib/libSDL2-2.0.0.dylib \
              -Wl,-rpath,$(CONDA_PREFIX)/lib -Wl,-framework,Cocoa \
              -Wl,-framework,CoreText -Wl,-framework,CoreGraphics -Wl,-framework,CoreFoundation \
              -Wl,-framework,AudioToolbox
# FreeType (GDI text) from the conda env; make-bundle.sh copies it and its libraries.
FT_CFLAGS  := -I$(CONDA_PREFIX)/include/freetype2
FT_LIBS    := $(CONDA_PREFIX)/lib/libfreetype.6.dylib
EXTRA_LIBS ?=
CXXFLAGS   := -arch arm64 -x c++ -std=c++17 -fpie -DPTROFS_64BIT -O2 -g -Wall -Wno-unused \
              -Iruntime -Iruntime/llasm -I$(COMMON)/runtime -I$(COMMON)/runtime/llasm $(SDL_CFLAGS) $(FT_CFLAGS) \
              $(EXTRA_CFLAGS)
CFLAGS     := -arch arm64 -fpie -O2 -g
LDFLAGS    := -arch arm64 -Wl,-pie

# Runtime C files: the game's own files replace shared files with the same name.
GAME_C      := $(wildcard runtime/*.c) $(wildcard runtime/llasm/*.c)
COMMON_C    := $(filter-out $(addprefix %/,$(notdir $(GAME_C))), \
               $(wildcard $(COMMON)/runtime/*.c) $(wildcard $(COMMON)/runtime/llasm/*.c))
RUNTIME_OBJ := $(addprefix $(OBJ)/rt/,$(notdir $(GAME_C:.c=.o) $(COMMON_C:.c=.o)))
vpath %.c runtime runtime/llasm $(COMMON)/runtime $(COMMON)/runtime/llasm
RUNTIME_H   := $(wildcard runtime/*.h runtime/llasm/*.h $(COMMON)/runtime/*.h $(COMMON)/runtime/llasm/*.h)

# llasm glue and include files, staged into one folder (game files last, so they win).
LLASM_SRC   := $(wildcard $(COMMON)/runtime/llasm/*.llasm $(COMMON)/runtime/llasm/*.llinc \
                          runtime/llasm/*.llasm runtime/llasm/*.llinc)
GLUE_NAMES  := $(sort $(notdir $(wildcard $(COMMON)/runtime/llasm/*.llasm runtime/llasm/*.llasm)))
GLUE_OBJ    := $(addprefix $(OBJ)/glue/,$(GLUE_NAMES:.llasm=.o)) $(OBJ)/glue/glue-asm.o
SPEC_SRC    := $(wildcard $(COMMON)/runtime/imports.spec runtime/imports.spec)
# COM interface lists (common/native-mac/tools/gen_com.py): glue + weak C stubs
COM_SPECS   := $(wildcard $(COMMON)/runtime/com/*.com runtime/com/*.com)
COM_OBJ     := $(addprefix $(OBJ)/com/,$(notdir $(COM_SPECS:.com=.o)))

.PHONY: all tools clean
all: $(B)/$(GAME_NAME)

tools:
	$(TOOLS)/build-tools.sh

$(STAGE)/stamp: $(LLASM_SRC)
	rm -rf $(STAGE) && mkdir -p $(STAGE)
	cp $(LLASM_SRC) $(STAGE)/
	touch $@

$(GEN)/com.stamp: $(COM_SPECS) $(TOOLS)/gen_com.py $(STAGE)/stamp
	python $(TOOLS)/gen_com.py $(STAGE) $(GEN)/com-stubs.c $(COM_SPECS)
	touch $@

$(GEN)/com-stubs.c: $(GEN)/com.stamp

$(GEN)/imports.spec: $(SPEC_SRC)
	mkdir -p $(GEN)
	cat $(SPEC_SRC) > $@

# 1. Translate the exe (SRW) -> build/gen/*.llasm
# The fixes in srw/ use fixed addresses of one exe version: GAME_EXE_SHA256
# in game.conf is that version. Another version stops the build here.
$(B)/srw/$(GAME_EXE):
	mkdir -p $(B)/srw
	@sum=$$(shasum -a 256 "$(GAME_DIR)/$(GAME_EXE)" | cut -d' ' -f1); \
	if [ -n "$(GAME_EXE_SHA256)" ] && [ "$$sum" != "$(GAME_EXE_SHA256)" ]; then \
	  echo "error: $(GAME_DIR)/$(GAME_EXE) is not the exe version that this port supports." >&2; \
	  echo "  sha256 found:    $$sum" >&2; \
	  echo "  sha256 expected: $(GAME_EXE_SHA256) (GAME_EXE_SHA256 in game.conf)" >&2; \
	  echo "  The fixes in srw/ are for that version only (see the README of the game)." >&2; \
	  exit 1; \
	fi
	cp "$(GAME_DIR)/$(GAME_EXE)" $@

$(GEN)/$(GAME_LLASM).llasm: $(B)/srw/$(GAME_EXE) $(TOOLS)/gen_relocs.py $(TOOLS)/gen_extern.py $(TOOLS)/run-srw.sh \
                            $(wildcard srw/llasm/*.sci) $(wildcard srw/*.txt) srw/SR.cfg $(STAGE)/stamp
	$(TOOLS)/run-srw.sh

$(GEN)/glue-asm.llasm $(GEN)/stubs.c: $(GEN)/imports.spec $(TOOLS)/gen_glue.py $(GEN)/$(GAME_LLASM).llasm $(STAGE)/stamp
	python $(TOOLS)/gen_glue.py $(GEN)/imports.spec $(STAGE) $(GEN)/extern.llinc $(GEN)

# 2. Game code: llasm -> LLVM IR -> 16 parts -> arm64 objects (parallel)
$(OBJ)/game.stamp: $(GEN)/$(GAME_LLASM).llasm
	mkdir -p $(OBJ)/game
	rm -f $(OBJ)/game/*
	cd $(GEN) && $(abspath $(HOSTBIN))/llasm $(GAME_LLASM).llasm -O $(LLASMFLAGS) > $(GAME_LLASM).ll
	llvm-split -j 16 -o $(OBJ)/game/part $(GEN)/$(GAME_LLASM).ll
	ls $(OBJ)/game/part* | xargs -P $(JOBS) -I{} sh -c \
	  'opt -O3 {} -o {}.bc && llc -O=3 -filetype=obj $(LLVMFLAGS) {}.bc -o {}.o && rm {}.bc'
	touch $@

# 3. Glue procedures (llasm) and runtime (C, compiled as C++ like the SR ports)
$(OBJ)/glue/%.o: $(STAGE)/stamp
	mkdir -p $(dir $@)
	cd $(STAGE) && $(abspath $(HOSTBIN))/llasm $*.llasm -O $(LLASMFLAGS) | opt -O3 | llc -O=3 -filetype=obj $(LLVMFLAGS) -o $(CURDIR)/$@

$(OBJ)/com/%.o: $(GEN)/com.stamp
	mkdir -p $(dir $@)
	cd $(STAGE) && $(abspath $(HOSTBIN))/llasm com-$*.llasm -O $(LLASMFLAGS) | opt -O3 | llc -O=3 -filetype=obj $(LLVMFLAGS) -o $(CURDIR)/$@

$(OBJ)/com-stubs.o: $(GEN)/com-stubs.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ)/glue/glue-asm.o: $(GEN)/glue-asm.llasm
	mkdir -p $(dir $@)
	cp $(GEN)/glue-asm.llasm $(STAGE)/
	cd $(STAGE) && $(abspath $(HOSTBIN))/llasm glue-asm.llasm -O $(LLASMFLAGS) | opt -O3 | llc -O=3 -filetype=obj $(LLVMFLAGS) -o $(CURDIR)/$@

$(OBJ)/rt/%.o: %.c $(RUNTIME_H)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ)/stubs.o: $(GEN)/stubs.c
	$(CC) $(CFLAGS) -c $< -o $@

# 4. Link
$(B)/$(GAME_NAME): $(OBJ)/game.stamp $(GLUE_OBJ) $(COM_OBJ) $(RUNTIME_OBJ) $(OBJ)/stubs.o $(OBJ)/com-stubs.o
	$(CXX) $(LDFLAGS) -o $@ $(OBJ)/game/*.o $(GLUE_OBJ) $(COM_OBJ) $(RUNTIME_OBJ) $(OBJ)/stubs.o $(OBJ)/com-stubs.o $(SDL_LIBS) $(FT_LIBS) $(EXTRA_LIBS)

clean:
	rm -rf $(GEN) $(OBJ) $(B)/$(GAME_NAME)
