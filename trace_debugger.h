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

/*
 * Author: Robert Balas (balasr@student.ethz.ch)
 * Description: Software model for the hardware trace debugger
 */

#ifndef __TRACE_DEBUGGER_H__
#define __TRACE_DEBUGGER_H__

#define PACKAGE "foo" /* quick hack for bfd if not using autotools */
#include <stdbool.h>
#include <inttypes.h>
#include "bfd.h"
#include "list.h"

#define XLEN 32
#define CAUSELEN 5
#define PRIVLEN 5
#define ILEN 32

struct instr_sample {
    /* bool valid; */
    bool exception;
    bool interrupt;
    uint32_t cause : CAUSELEN; /* CAUSELEN */
    uint32_t tval : XLEN;      /* XLEN */
    uint32_t priv : PRIVLEN;   /* PRIVLEN */
    uint32_t iaddr : XLEN;     /* XLEN */
    uint32_t instr : ILEN;     /* ILEN */
};

#define F_BRANCH_FULL 0
#define F_BRANCH_DIFF 1
#define F_ADDR_ONLY 2
#define F_SYNC 3

#define SF_START 0
#define SF_EXCEPTION 1
#define SF_CONTEXT 2

struct tr_packet {
    uint32_t msg_type : 2;
    uint32_t format : 2;

    uint32_t branches : 5;
    uint32_t branch_map;

    uint32_t subformat : 2;
    uint32_t context;
    uint32_t privilege : PRIVLEN;
    /* we need this if the first instruction of an exception is a branch, since
     * that won't be reorcded into the branch map
     */
    bool branch;
    uint32_t address : XLEN;
    uint32_t ecause : CAUSELEN;
    bool interrupt;
    uint32_t tval : XLEN;

    struct list_head list;
};

#define ALLOC_INIT_PACKET(name)                                                \
    struct tr_packet *name = malloc(sizeof(*name));                            \
    if (!name) {                                                               \
        perror("malloc");                                                      \
        goto fail_malloc;                                                      \
    }                                                                          \
    *name = (struct tr_packet){.msg_type = 2};


#define INIT_PACKET(p)                                                         \
    do {                                                                       \
        *p = (struct tr_packet){0};                                            \
    } while (false)

struct list_head *trdb_compress_trace(struct list_head *packet_list,
                                      struct instr_sample[1], size_t len);

/* packet from bitstream where parse single packet */
/* packet from bitstream whole decode function */

char *trdb_decompress_trace(bfd *abfd, struct list_head *packet_list);

void trdb_dump_packet_list(struct list_head *packet_list);

void trdb_free_packet_list(struct list_head *packet_list);

/* TODO: encoder and decoder state structs */

/* inline uint32_t cause(struct instr_sample *instr) */
/* { */
/*     return instr->cause & MASK_FROM(CAUSELEN); */
/* } */

/* inline uint32_t tval(struct instr_sample *instr) */
/* { */
/*     return instr->tval & MASK_FROM(XLEN); */
/* } */

/* inline uint32_t priv(struct instr_sample *instr) */
/* { */
/*     return instr->priv & MASK_FROM(PRIVLEN); */
/* } */

/* inline uint32_t iaddr(struct instr_sample *instr) */
/* { */
/*     return instr->iaddr & 0xffffffff; */
/* } */

/* inline uint32_t instr(struct instr_sample *instr) */
/* { */
/*     return instr->instr & 0xffffffff; */
/* } */

/* struct packet0 {
 *     uint32_t msg_type : 2;
 *     uint32_t format : 2;   // 00
 *     uint32_t branches : 5;
 *     uint32_t branch_map;
 *     uint32_t address;
 * };
 *
 * struct packet1 {
 *     uint32_t msg_type : 2;
 *     uint32_t format : 2;   // 01
 *     uint32_t branches : 5;
 *     uint32_t branch_map;
 *     uint32_t address;
 * };
 *
 * struct packet2 {
 *     uint32_t msg_type : 2;
 *     uint32_t format : 2;
 *     uint32_t address;
 * };
 *
 * struct packet3 {
 *     uint32_t msg_type : 2;
 *     uint32_t format : 2;
 *     uint32_t subformat : 2;
 *     uint32_t context;
 *     uint32_t privilege : PRIVLEN;
 *     bool branch : 1;
 *     uint32_t address : XLEN;
 *     uint32_t ecause : CAUSELEN;
 *     bool interrupt : 1;
 *     uint32_t tval : XLEN;
 * };
 */

#endif
