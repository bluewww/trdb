CC 		= gcc
CPPFLAGS	=
CFLAGS		= -std=gnu11 -Wall -O2 -fno-strict-aliasing -Wno-unused-function
CFLAGS_DEBUG	= -std=gnu11 -Wall -g -fno-strict-aliasing -Wno-unused-function

LIB_PATHS       = /scratch/balasr/pulp-riscv-binutils-gdb/opcodes \
		/scratch/balasr/pulp-riscv-binutils-gdb/bfd \
		/scratch/balasr/pulp-riscv-binutils-gdb/libiberty \
		/scratch/balasr/pulp-riscv-binutils-gdb/zlib
INCLUDE_PATHS   = /scratch/balasr/pulp-riscv-binutils-gdb/include
MORE_TAG_PATHS  = /scratch/balasr/pulp-riscv-binutils-gdb/bfd

LDFLAGS 	= $(addprefix -L, $(LIB_PATHS))
LDLIBS		= -l:libbfd.a -l:libopcodes.a -l:libiberty.a -l:libz.a -ldl

SRCS		= $(wildcard *.c)
OBJS		= $(SRCS:.c=.o)
INCLUDES	= $(addprefix -I, $(INCLUDE_PATHS))
BIN 		= disasm

CTAGS 		= ctags


all: $(BIN)

debug: CFLAGS = $(CFLAGS_DEBUG)
debug: all

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $(BIN) $(LDFLAGS) $(OBJS) $(LDLIBS)
	rm -f $(OBJS)


# $@ = name of target
# $< = first dependency
%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES)  $(LDFLAGS) -c $< -o $@ $(LDLIBS)

.PHONY: run
run:
	./$(BIN)

TAGS:
	$(CTAGS) -R -e -h=".c.h" --tag-relative=always . $(LIB_PATHS) \
	$(INCLUDE_PATHS) $(MORE_TAG_PATHS)

.PHONY: clean
clean:
	rm -rf $(BIN) $(OBJS)

.PHONY: distclean
distclean: clean
	rm -f TAGS

