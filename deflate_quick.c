/*
 * The deflate_quick deflate strategy, designed to be used when cycles are
 * at a premium.
 *
 * Copyright (C) 2013 Intel Corporation. All rights reserved.
 * Authors:
 *  Wajdi Feghali   <wajdi.k.feghali@intel.com>
 *  Jim Guilford    <james.guilford@intel.com>
 *  Vinodh Gopal    <vinodh.gopal@intel.com>
 *     Erdinc Ozturk   <erdinc.ozturk@intel.com>
 *  Jim Kukunas     <james.t.kukunas@linux.intel.com>
 *
 * Portions are Copyright (C) 2016 12Sided Technology, LLC.
 * Author:
 *  Phil Vachon     <pvachon@12sidedtech.com>
 *
 * For conditions of distribution and use, see copyright notice in zlib.h
 */

#include "zbuild.h"
#include "deflate.h"
#include "deflate_p.h"
#include "functable.h"
#include "trees_emit.h"

extern const ct_data static_ltree[L_CODES+2];
extern const ct_data static_dtree[D_CODES];

#define QUICK_START_BLOCK(s, last) { \
    zng_tr_emit_tree(s, STATIC_TREES, last); \
    s->block_open = 1 + last; \
    s->block_start = s->strstart; \
}

#define QUICK_END_BLOCK(s, last) { \
    if (s->block_open) { \
        zng_tr_emit_end_block(s, static_ltree, last); \
        s->block_open = 0; \
        s->block_start = s->strstart; \
        flush_pending(s->strm); \
    } \
} 

ZLIB_INTERNAL block_state deflate_quick(deflate_state *s, int flush) {
    Pos hash_head;
    unsigned dist, match_len, last;


    last = (flush == Z_FINISH) ? 1 : 0;
    if (UNLIKELY(last && s->block_open != 2)) {
        /* Emit end of previous block */
        QUICK_END_BLOCK(s, 0);
        /* Emit start of last block */
        QUICK_START_BLOCK(s, last);
    } else if (UNLIKELY(s->block_open == 0 && s->lookahead > 0)) {
        /* Start new block only when we have lookahead data, so that if no
           input data is given an empty block will not be written */
        QUICK_START_BLOCK(s, last);
    }

    do {
        if (UNLIKELY(s->pending + ((BIT_BUF_SIZE + 7) >> 3) >= s->pending_buf_size)) {
            flush_pending(s->strm);
            if (s->strm->avail_out == 0 && flush != Z_FINISH) {
                return need_more;
            }
        }

        if (UNLIKELY(s->lookahead < MIN_LOOKAHEAD)) {
            fill_window(s);
            if (UNLIKELY(s->lookahead < MIN_LOOKAHEAD && flush == Z_NO_FLUSH)) {
                return need_more;
            }
            if (UNLIKELY(s->lookahead == 0))
                break;

            if (UNLIKELY(s->block_open == 0)) {
                /* Start new block when we have lookahead data, so that if no
                   input data is given an empty block will not be written */
                QUICK_START_BLOCK(s, last);
            }
        }

        if (LIKELY(s->lookahead >= MIN_MATCH)) {
            hash_head = functable.quick_insert_string(s, s->strstart);
            dist = s->strstart - hash_head;

            if (dist > 0 && dist < MAX_DIST(s)) {
                match_len = functable.compare258(s->window + s->strstart, s->window + hash_head);

                if (match_len >= MIN_MATCH) {
                    if (UNLIKELY(match_len > s->lookahead))
                        match_len = s->lookahead;

                    check_match(s, s->strstart, hash_head, match_len);

                    zng_tr_emit_dist(s, static_ltree, static_dtree, match_len - MIN_MATCH, dist);
                    s->lookahead -= match_len;
                    s->strstart += match_len;
                    continue;
                }
            }
        }

        zng_tr_emit_lit(s, static_ltree, s->window[s->strstart]);
        s->strstart++;
        s->lookahead--;
    } while (s->strm->avail_out != 0);

    s->insert = s->strstart < MIN_MATCH-1 ? s->strstart : MIN_MATCH-1;

    if (UNLIKELY(last)) {
        if (s->strm->avail_out == 0)
            return s->strm->avail_in == 0 ? finish_started : need_more;

        QUICK_END_BLOCK(s, 1);
        return finish_done;
    }

    QUICK_END_BLOCK(s, 0);
    return block_done;
}
