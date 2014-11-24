### paths ###
# clear dir vars if these programs are in path
VASM_DIR    = ../../sw/vasm/
HEX2MEM_DIR = ../../sw/hex2mem/
HEX2MIF_DIR = ../../sw/hex2mif/


### programs ###
ifneq (,$(findstring Windows,$(OS)))
    VASM    = $(VASM_DIR)vasmm68k_mot.exe
else
    VASM    = $(VASM_DIR)vasmm68k_mot
endif

XXD     = xxd
HEX2MEM = $(HEX2MEM_DIR)hex2mem.py
HEX2MIF = $(HEX2MIF_DIR)hex2mif.py


### flags ###
ASMFLAGS=-m68000 -showopt -Fbin
XXDFLAGS=-ps -c 1
HEXFLAGS=-a 8 -s 256 -w 2


### variables ###
BUILD_USER=\"$(USER)\"
BUILD_T=$$(date +%Y-%m-%d)
BUILD_TIME=\"$(BUILD_T)\"
BUILD_N=$$(cat build_num.txt)
BUILD_NUM=\"$(BUILD_N)\"
BUILD_R=$$(git rev-parse --verify HEAD)
BUILD_REV=\"$(BUILD_R)\"

BINDIR=bin
OBJDIR=obj


### files ###

# asm sources
ASM_SOURCES=ethernet_attr.asm


### build rules ###

# all
all:
	@echo Making ethernet attr ...
	@make bin
	@make bin/ethernet_attr.v
	@make bin/ethernet_attr.mif

bin:
	@echo $@
	@mkdir -p $(BINDIR)

bin/ethernet_attr.bin : Makefile bin $(ASM_SOURCES)
	@echo $@
	@$(VASM) $(ASMFLAGS) $(ASM_SOURCES) -o $@

bin/ethernet_attr.hex : Makefile bin bin/ethernet_attr.bin
	@echo $@
	@$(XXD) $(XXDFLAGS) $(@:.hex=.bin) > $@

bin/ethernet_attr.v : Makefile bin bin/ethernet_attr.hex
	@echo $@
	@$(HEX2MEM) $(HEXFLAGS) $(@:.v=.hex) $@

bin/ethernet_attr.mif : bin/ethernet_attr.hex
	@echo $@
	@$(HEX2MIF) -w 8 $(@:.mif=.hex) $@

# clean
clean:
	@echo clean
	@rm -rf ./$(BINDIR)

