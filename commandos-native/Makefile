# Commandos: Behind Enemy Lines, native arm64 macOS port.
# Run in the commandos-native conda env (". tools/env.sh").
#
#   make tools     build SRW and llasm (once)
#   make           translate comandos.exe and build build/Commandos
#
# GAME_DIR: folder with the original game (comandos.exe is read from it).

SHELL       := /bin/bash
.SHELLFLAGS := -o pipefail -c

GAME_DIR ?= $(HOME)/Games/Commandos Behind Enemy Lines/Game Data
B        := build
GEN      := $(B)/gen
OBJ      := $(B)/obj
JOBS     ?= 8

LLASMFLAGS := -m64 -inline-idiv -inline-float -pic -ptrofs
LLVMFLAGS  := -mtriple=arm64-apple-darwin --relocation-model=pic
CXX        := clang++
CC         := clang
SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS   := $(shell pkg-config --libs sdl2)
CXXFLAGS   := -arch arm64 -x c++ -std=c++17 -fpie -DPTROFS_64BIT -O2 -g -Wall -Wno-unused \
              -Iruntime -Iruntime/llasm $(SDL_CFLAGS)
CFLAGS     := -arch arm64 -fpie -O2 -g
LDFLAGS    := -arch arm64 -Wl,-pie

RUNTIME_SRC := $(wildcard runtime/*.c) $(wildcard runtime/llasm/*.c)
RUNTIME_OBJ := $(patsubst runtime/%.c,$(OBJ)/rt/%.o,$(RUNTIME_SRC))
GLUE_SRC    := $(wildcard runtime/llasm/*.llasm)
GLUE_OBJ    := $(patsubst runtime/llasm/%.llasm,$(OBJ)/glue/%.o,$(GLUE_SRC)) $(OBJ)/glue/glue-asm.o

.PHONY: all tools srw clean
all: $(B)/Commandos

tools:
	./tools/build-tools.sh

# 1. Translate the exe (SRW) -> build/gen/*.llasm
$(B)/srw/comandos.exe:
	mkdir -p $(B)/srw
	cp "$(GAME_DIR)/comandos.exe" $@

$(GEN)/Comandos.llasm: $(B)/srw/comandos.exe tools/gen_relocs.py tools/gen_extern.py \
                       $(wildcard srw/llasm/*.sci) srw/SR.cfg $(wildcard runtime/llasm/*.llinc)
	./tools/run-srw.sh

$(GEN)/glue-asm.llasm $(GEN)/stubs.c: runtime/imports.spec tools/gen_glue.py $(GEN)/Comandos.llasm $(GLUE_SRC)
	python tools/gen_glue.py runtime/imports.spec runtime/llasm $(GEN)/extern.llinc $(GEN)

# 2. Game code: llasm -> LLVM IR -> 16 parts -> arm64 objects (parallel)
$(OBJ)/game.stamp: $(GEN)/Comandos.llasm
	mkdir -p $(OBJ)/game
	rm -f $(OBJ)/game/*
	cd $(GEN) && $(CURDIR)/$(B)/bin/llasm Comandos.llasm -O $(LLASMFLAGS) > Comandos.ll
	llvm-split -j 16 -o $(OBJ)/game/part $(GEN)/Comandos.ll
	ls $(OBJ)/game/part* | xargs -P $(JOBS) -I{} sh -c \
	  'opt -O3 {} -o {}.bc && llc -O=3 -filetype=obj $(LLVMFLAGS) {}.bc -o {}.o && rm {}.bc'
	touch $@

# 3. Glue procedures (llasm) and runtime (C, compiled as C++ like the SR ports)
$(OBJ)/glue/%.o: runtime/llasm/%.llasm $(wildcard runtime/llasm/*.llinc)
	mkdir -p $(dir $@)
	cd runtime/llasm && $(CURDIR)/$(B)/bin/llasm $(notdir $<) -O $(LLASMFLAGS) | opt -O3 | llc -O=3 -filetype=obj $(LLVMFLAGS) -o $(CURDIR)/$@

$(OBJ)/glue/glue-asm.o: $(GEN)/glue-asm.llasm
	mkdir -p $(dir $@)
	cd $(GEN) && $(CURDIR)/$(B)/bin/llasm glue-asm.llasm -O $(LLASMFLAGS) | opt -O3 | llc -O=3 -filetype=obj $(LLVMFLAGS) -o $(CURDIR)/$@

$(OBJ)/rt/%.o: runtime/%.c $(wildcard runtime/*.h runtime/llasm/*.h)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ)/stubs.o: $(GEN)/stubs.c
	$(CC) $(CFLAGS) -c $< -o $@

# 4. Link
$(B)/Commandos: $(OBJ)/game.stamp $(GLUE_OBJ) $(RUNTIME_OBJ) $(OBJ)/stubs.o
	$(CXX) $(LDFLAGS) -o $@ $(OBJ)/game/*.o $(GLUE_OBJ) $(RUNTIME_OBJ) $(OBJ)/stubs.o $(SDL_LIBS)

clean:
	rm -rf $(GEN) $(OBJ) $(B)/Commandos
