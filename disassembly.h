/*
 * trdb - Trace Debugger Software for the PULP platform
 *
 * Copyright (C) 2018 ETH Zurich and University of Bologna
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file disassembly.h
 * @author Robert Balas (balasr@student.ethz.ch)
 * @date 25 Aug 2018
 * @brief Collection of disassembly routines using libopcdes and libbfd
 */

#ifndef __DISASSEMBLY_H__
#define __DISASSEMBLY_H__

#include <inttypes.h>
#include <stdbool.h>

#define PACKAGE "foo" /* quick hack for bfd if not using autotools */
#include "bfd.h"
#include "demangle.h"
#include "dis-asm.h"
#include "trace_debugger.h"

/* define functions which help figure out what function we are dealing with */
#define DECLARE_INSN(code, match, mask)                                        \
    static const uint32_t match_##code = match;                                \
    static const uint32_t mask_##code = mask;                                  \
    static bool is_##code##_instr(long instr)                                  \
    {                                                                          \
        return (instr & mask) == match;                                        \
    }

#include "riscv_encoding.h"
#undef DECLARE_INSN

/* ABI names for x-registers */
#define X_RA 1
#define X_SP 2
#define X_GP 3
#define X_TP 4
#define X_T0 5
#define X_T1 6
#define X_T2 7
#define X_T3 28

#define OP_MASK_RD 0x1f
#define OP_SH_RD 7
#define MASK_RD (OP_MASK_RD << OP_SH_RD)

#define OP_MASK_RS1 0x1f
#define OP_SH_RS1 15
#define MASK_RS1 (OP_MASK_RS1 << OP_SH_RS1)

#define RV_X(x, s, n) (((x) >> (s)) & ((1 << (n)) - 1))
#define ENCODE_ITYPE_IMM(x) (RV_X(x, 0, 12) << 20)
#define MASK_IMM ENCODE_ITYPE_IMM(-1U)

static bool is_really_c_jalr_instr(long instr)
{
    /* we demand that rd is nonzero */
    return is_c_jalr_instr(instr) && ((instr & MASK_RD) != 0);
}

static bool is_really_c_jr_instr(long instr)
{
    /* we demand that rd is nonzero */
    return is_c_jr_instr(instr) && ((instr & MASK_RD) != 0);
}

static bool is_c_ret_instr(long instr)
{
    return (instr & (MASK_C_JR | MASK_RD)) == (MATCH_C_JR | (X_RA << OP_SH_RD));
}

static bool is_ret_instr(long instr)
{
    return (instr & (MASK_JALR | MASK_RD | MASK_RS1 | MASK_IMM)) ==
           (MATCH_JALR | (X_RA << OP_SH_RS1));
}

enum trdb_ras { none = 0, ret = 1, call = 2, coret = 3 };

static enum trdb_ras is_jalr_funcall(uint32_t instr)
{
    /* According the the calling convention outlined in the risc-v spec we are
     * dealing with a function call if instr == jalr and rd=x1/x5=link.
     * Furthermore the following hints for the RAS (return address stack) are
     * encoded using rs and rd:
     *
     *(rd=!link && rs=link             pop aka ret)
     * rd=link && rs!=link             push
     * rd=link && rs=link && rd!=rs    push and pop, used for coroutines
     * rd=link && rs=link && rd==rs    push
     */
    bool is_jalr = is_jalr_instr(instr);
    uint32_t rd = (instr & MASK_RD) >> OP_SH_RD;
    uint32_t rs = (instr & MASK_RS1) >> OP_SH_RS1;
    bool is_rd_link = (rd == X_RA) || (rd == X_T0);
    bool is_rs_link = (rs == X_RA) || (rs == X_T0);
    bool is_eq_link = is_rd_link && is_rs_link && rd == rs;

    if (is_jalr && !is_rd_link && is_rs_link)
        return ret;
    else if (is_jalr && is_rd_link && !is_rs_link)
        return call;
    else if (is_jalr && is_rd_link && is_rs_link && !is_eq_link)
        return coret;
    else if (is_jalr && is_rd_link && is_rs_link && is_eq_link)
        return ret;
    else
        return none;
}

static enum trdb_ras is_c_jalr_funcall(uint32_t instr)
{
    /* C.JALR expands to jalr x1, rs1, 0 */
    bool is_c_jalr = is_really_c_jalr_instr(instr);
    uint32_t rs = (instr & MASK_RS1) >> OP_SH_RS1;
    bool is_rs_link = (rs == X_RA) || (rs == X_T0);
    if (is_c_jalr && is_rs_link && (X_RA != rs))
        return coret;
    else if (is_c_jalr && is_rs_link && (X_RA == rs))
        return ret;
    else
        return none;
}

static enum trdb_ras is_jal_funcall(uint32_t instr)
{
    /* if jal with rd=x1/x5 then we know it's a function call */
    bool is_jal = is_jal_instr(instr);
    bool is_rd_link = (instr & MASK_RD) == (X_RA << OP_SH_RD) ||
                      (instr & MASK_RD) == (X_T0 << OP_SH_RD);
    if (is_jal && is_rd_link)
        return call;
    else
        return none;
}

static enum trdb_ras is_c_jal_funcall(uint32_t instr)
{
    /* C.JAL expands to jal x1, offset[11:1] */
    bool is_c_jal = is_c_jal_instr(instr);
    if (is_c_jal)
        return call;
    else
        return none;
}

/**
 * Used to capture all information and functions needed to disassemble.
 */
struct disassembler_unit {
    disassembler_ftype disassemble_fn; /**< does the actual disassembly */
    struct disassemble_info *dinfo;    /**< context for disassembly */
};

#define TRDB_NO_ALIASES 1
#define TRDB_PREFIX_ADDRESSES 2
#define TRDB_DO_DEMANGLE 4
#define TRDB_DISPLAY_FILE_OFFSETS 8
#define TRDB_LINE_NUMBERS 16
#define TRDB_SOURCE_CODE 32
#define TRDB_FUNCTION_CONTEXT 64
#define TRDB_INLINES 128

struct trdb_ctx;

/**
 * Store disassembly configuration and context.
 */
struct trdb_disasm_aux {
    /* current disassembly context */
    bfd *abfd;
    asection *sec;
    bfd_boolean require_sec;

    arelent **dynrelbuf;
    long dynrelcount;

    arelent *reloc;

    /* symbols extracted from bfd */
    asymbol **symbols;
    long symcount;
    asymbol **dynamic_symbols;
    long dynsymcount;
    asymbol *synthethic_symbols;
    long synthcount;
    asymbol **sorted_symbols;
    long sorted_symcount;

    uint32_t config;       /**< stores the below settings as bitfield */
    bool no_aliases;       /**< set to true to always diassemble to most general
                              representation */
    bool prefix_addresses; /**< different address format */
    bool do_demangle;      /**< demangle symbols using bfd */
    bool display_file_offsets;  /**< display file offsets for displayed symbols
                                 */
    bool with_line_numbers;     /**< periodically show line numbers mixed with
                                   assembly */
    bool with_source_code;      /**< show source code mixed with assembly */
    bool with_function_context; /**< show when instruction lands on symbol value
                                   of a function */
    bool unwind_inlines;        /**< Print all inlines for source line */
};

/* The number of zeroes we want to see before we start skipping them. The number
 * is arbitrarily chosen.
 */

#define DEFAULT_SKIP_ZEROES 8

/* The number of zeroes to skip at the end of a section. If the number of zeroes
 * at the end is between SKIP_ZEROES_AT_END and SKIP_ZEROES, they will be
 * disassembled. If there are fewer than SKIP_ZEROES_AT_END, they will be
 * skipped. This is a heuristic attempt to avoid disassembling zeroes inserted
 * by section alignment.
 */

#define DEFAULT_SKIP_ZEROES_AT_END 3

/**
 * Computes the length of a the given RISC-V instruction instr.
 *
 * So far only up to 64 bit instructions are supported.
 *
 * @param instr raw instruction value up to 64 bit long
 * @return number of bytes which make up the instruction
 */
static inline unsigned int riscv_instr_len(uint64_t instr)
{
    if ((instr & 0x3) != 0x3) /* RVC.  */
        return 2;
    if ((instr & 0x1f) != 0x1f) /* Base ISA and extensions in 32-bit space.  */
        return 4;
    if ((instr & 0x3f) == 0x1f) /* 48-bit extensions.  */
        return 6;
    if ((instr & 0x7f) == 0x3f) /* 64-bit extensions.  */
        return 8;
    /* Longer instructions not supported at the moment.  */
    return 2;
}

/**
 * Initialize disassemble_info from libopcodes with hardcoded values for the
 * PULP platform.
 * @param dinfo filled with information of the PULP platform
 */
void trdb_init_disassemble_info_for_pulp(struct disassemble_info *dinfo);

/**
 * Initialize disassembler_unit with settings for the PULP platform.
 * @param dunit filled with information for PULP
 * @param options configure the formatting behaviour or NULL
 * @return 0 on success, -1 otherwise
 */
int trdb_init_disassembler_unit_for_pulp(struct disassembler_unit *dunit,
                                         char *options);

/**
 * Initialize disassemble_info from libopcodes by grabbing data out of @p abfd.
 *
 * The options string can be non-@c NULL to pass disassembler specific settings
 * to libopcodes. Currently supported is "no-aliases", which disassembles common
 * abbreviations for certain instructions.
 *
 * @param dinfo filled with information from @p abfd
 * @param abfd the bfd representing the binary
 * @param options disassembly options passed to libopcodes
 */
void trdb_init_disassemble_info_from_bfd(struct disassemble_info *dinfo,
                                         bfd *abfd, char *options);

/**
 * Initialize disassembler_unit.
 *
 * A side from calling init_disassemble_info_from_bfd(), it also sets
 * #disassembler_unit.disassemble_fn to the architecture matching @p abfd.
 *
 * @param dunit filled with information from @p abfd
 * @param abfd the bfd representing the binary
 * @param options disassembly options passed to libopcodes
 * @return 0 on success, a negative error code otherwise
 * @return -trdb_invalid if @p dunit or #disassembler_unit.dinfo in @p
 * dunit is NULL
 * @return -trdb_arch_support if architecture is not supported
 */
int trdb_init_disassembler_unit(struct disassembler_unit *dunit, bfd *abfd,
                                char *options);

/**
 * Initialize disassembler_unit and its containing disassemble_info from
 * libopcodes by grabbing meta data out of @p abfd. Furthermore it allocates
 * internal structures to allow #disassembler_unit.disassemble_fn in @p
 * dunit to resolve addresses to nearest symbols. TODO: trdb_ctx sets demangle,
 * disassembly options and more.
 *
 * @param c the trace debugger context containing settings
 * @param abfd the bfd representing the binary
 * @param dunit filled with information from @p abfd
 * @return 0 on success, a negative error code otherwise
 * @return -trdb_invalid if @p c, @p abfd or @p dunit is NULL
 * @return -trdb_nomem if out of memory
 * @return -trdb_arch_support if architecture is not supported
 */
int trdb_alloc_dinfo_with_bfd(struct trdb_ctx *c, bfd *abfd,
                              struct disassembler_unit *dunit);
/**
 * Free the memory allocated to @p abfd and @p dunit by a call to
 * trdb_alloc_dinfo_with_bfd().
 *
 * @param c the trace debugger context containing settings
 * @param abfd the bfd representing the binary
 * @param dunit filled with information from @p abfd
 */
void trdb_free_dinfo_with_bfd(struct trdb_ctx *c, bfd *abfd,
                              struct disassembler_unit *dunit);

/**
 * Configure the disassembler with flags. E.g. set @p settings to
 * TRDB_SOURCE_CODE | TRDB_LINE_NUMBERS to show the source code and file line
 * numbers mixed with disassembly.
 *
 * @param dunit the disassembler
 * @param settings configuration flags which can be set as described
 */
void trdb_set_disassembly_conf(struct disassembler_unit *dunit,
                               uint32_t settings);

/**
 * Read the disassembler configuration flags. Test for enabled features by
 * and'ing the returned value with e.g. TRDB_SOURCE_CODE.
 *
 * @param dunit the disassembler whose settings should be read
 * @param conf written with the disassembly configuration if successfull
 * @return 0 on success, a negative error code otherwise
 * @return -trdb_invalid if trdb_disasm_aux in @p dunit is NULL
 */
int trdb_get_disassembly_conf(struct disassembler_unit *dunit, uint32_t *conf);

/**
 * A #print_address_func used in disassemble_info, set by
 * trdb_alloc_dinfo_with_bfd(), which resolve addresses to symbols. This
 * callback is only well defined if its disassemble_info struct was initialized
 * using trdb_alloc_dinfo_with_bfd().
 *
 * This is a callback function, if registered, called by libopcodes to custom
 * format addresses. It can also be abused to print other information.
 *
 * @param vma the virtual memory address where this instruction is located at
 * @param dinfo context of disassembly
 */
void trdb_print_address(bfd_vma vma, struct disassemble_info *inf);

/**
 * A #symbol_at_address function used in disassemble_info, set by
 * trdb_alloc_dinfo_with_bfd().
 *
 * This is a callback function, which gives libopcodes information about
 * specific addresses. Determines if the given address has a symbol associated
 * with it.
 *
 * @param vma the virtual memory address where this instruction is located at
 * @param dinfo context of disassembly
 * @return
 */
int trdb_symbol_at_address(bfd_vma vma, struct disassemble_info *inf);

/**
 *  Print the bfd section header to stdout.
 *
 * @param abfd the bfd representing the binary
 * @param section which section to print
 * @param ignored is ignored and only for compatiblity
 */
void trdb_dump_section_header(bfd *abfd, asection *section, void *ignored);

/**
 * Print bfd architecuture information to stdout.
 *
 * @param abfd the bfd representing the binary
 */
void trdb_dump_bin_info(bfd *abfd);

/**
 * Print all section names of bfd to stdout.
 *
 * @param abfd the bfd representing the binary
 */
void trdb_dump_section_names(bfd *abfd);

/**
 * Print all supported targets of the used libopcodes to stdout.
 */
void trdb_dump_target_list();

/**
 * Default #print_address_func used in disassemble_info, set by
 * init_disassemble_info_for_pulp() or init_disassemble_info_from_bfd().
 *
 * This is a callback function, if registered, called by libopcodes to custom
 * format addresses. It can also be abused to print other information.
 *
 * @param vma the virtual memory address where this instruction is located at
 * @param dinfo context of disassembly
 */
void trdb_riscv32_print_address(bfd_vma vma, struct disassemble_info *dinfo);

/**
 * Return if vma is contained in the given section of abfd.
 *
 * @param abfd the bfd representing the binary
 * @parma section the section to test against
 * @param vma the virtual memory address to test for
 * @return whether @p vma is in contained in @p section
 */
bool trdb_vma_in_section(bfd *abfd, asection *section, bfd_vma vma);

/**
 * Return the section of @p abfd that contains the given @p vma or @c NULL.
 *
 * @param abfd the bfd representing the binary
 * @param vma the virtual memory address to
 * @return the section which contains @p vma or @c NULL
 */
asection *trdb_get_section_for_vma(bfd *abfd, bfd_vma vma);

/**
 * Disassemble the given asection and print it calling fprintf_func.
 *
 * @p inf must point to a disassembler_unit.
 *
 * @param abfd the bfd representing the binary
 * @param section the section to disassemble
 */
void trdb_disassemble_section(bfd *abfd, asection *section, void *inf);

/**
 * Disassemble @p len bytes of data pointed to by @p data with the given
 * @p dunit and print it calling fprintf_func.
 *
 * @param len number of bytes in @p data
 * @param data raw data block to disassemble
 * @param dunit disassembly context
 */
void trdb_disassemble_block(size_t len, bfd_byte *data,
                            struct disassembler_unit *dunit);

/**
 * Disassemble the given instr with the pretended address and disassembler_unit
 * and print it calling fprintf_func.
 *
 * @param instr the raw instruction value up to 32 bits
 * @param addr the address where the instruction is located at
 * @param dunit disassembly context
 */
void trdb_disassemble_single_instruction(uint32_t instr, uint32_t addr,
                                         struct disassembler_unit *dunit);

/**
 * Disassemble the given instr with the pretended address and print it calling
 * fprintf_func. Will configure a disassembler with default settings for the
 * PULP platform. This function is slow since we create a dissasembler for each
 * call. It is to be used sparingly for single instructions.
 *
 * @param instr the raw instruction value up to 32 bits
 * @param addr the address where the instruction is located at
 */
void trdb_disassemble_single_instruction_slow(uint32_t instr, uint32_t addr);

/**
 * Disassemble the given instruction, indicated by its address @p addr and the
 * bfd @p abfd which contains it, using the settings in @p dunit and print it by
 * calling fprintf_func.
 *
 * @param c the trace debugger context
 * @param abfd the bfd which contains the instruction
 * @param addr the address of the instruction
 * @param dunit disassembly context
 */
void trdb_disassemble_instruction_with_bfd(struct trdb_ctx *c, bfd *abfd,
                                           bfd_vma addr,
                                           struct disassembler_unit *dunit);

#endif
