#include "stdio.h"
#ifndef mips
#include "stdlib.h"
#endif
#include "xlisp.h"
#include "sound.h"

#include "falloc.h"
#include "cext.h"
#include "clip.h"

void clip_free();


typedef struct clip_susp_struct {
    snd_susp_node susp;
    long terminate_cnt;
    boolean logically_stopped;
    sound_type s;
    long s_cnt;
    sample_block_values_type s_ptr;

    double lev;
} clip_susp_node, *clip_susp_type;


void clip_n_fetch(register clip_susp_type susp, snd_list_type snd_list)
{
    int cnt = 0; /* how many samples computed */
    int togo;
    int n;
    sample_block_type out;
    register sample_block_values_type out_ptr;

    register sample_block_values_type out_ptr_reg;

    register double lev_reg;
    register sample_block_values_type s_ptr_reg;
    falloc_sample_block(out, "clip_n_fetch");
    out_ptr = out->samples;
    snd_list->block = out;

    while (cnt < max_sample_block_len) { /* outer loop */
    /* first compute how many samples to generate in inner loop: */
    /* don't overflow the output sample block: */
    togo = max_sample_block_len - cnt;

    /* don't run past the s input sample block: */
    susp_check_term_log_samples(s, s_ptr, s_cnt);
    togo = min(togo, susp->s_cnt);

    /* don't run past terminate time */
    if (susp->terminate_cnt != UNKNOWN &&
        susp->terminate_cnt <= susp->susp.current + cnt + togo) {
        togo = susp->terminate_cnt - (susp->susp.current + cnt);
        if (togo == 0) break;
    }


    /* don't run past logical stop time */
    if (!susp->logically_stopped && susp->susp.log_stop_cnt != UNKNOWN) {
        int to_stop = susp->susp.log_stop_cnt - (susp->susp.current + cnt);
        /* break if to_stop == 0 (we're at the logical stop)
         * AND cnt > 0 (we're not at the beginning of the
         * output block).
         */
        if (to_stop < togo) {
        if (to_stop == 0) {
            if (cnt) {
            togo = 0;
            break;
            } else /* keep togo as is: since cnt == 0, we
                    * can set the logical stop flag on this
                    * output block
                    */
            susp->logically_stopped = true;
        } else /* limit togo so we can start a new
                * block at the LST
                */
            togo = to_stop;
        }
    }

    n = togo;
    lev_reg = susp->lev;
    s_ptr_reg = susp->s_ptr;
    out_ptr_reg = out_ptr;
    if (n) do { /* the inner sample computation loop */
double x = *s_ptr_reg++; *out_ptr_reg++ = (sample_type) (x > lev_reg ? lev_reg : (x < -lev_reg ? -lev_reg : x));
    } while (--n); /* inner loop */

    susp->lev = lev_reg;
    /* using s_ptr_reg is a bad idea on RS/6000: */
    susp->s_ptr += togo;
    out_ptr += togo;
    susp_took(s_cnt, togo);
    cnt += togo;
    } /* outer loop */

    /* test for termination */
    if (togo == 0 && cnt == 0) {
    snd_list_terminate(snd_list);
    } else {
    snd_list->block_len = cnt;
    susp->susp.current += cnt;
    }
    /* test for logical stop */
    if (susp->logically_stopped) {
    snd_list->logically_stopped = true;
    } else if (susp->susp.log_stop_cnt == susp->susp.current) {
    susp->logically_stopped = true;
    }
} /* clip_n_fetch */


void clip_toss_fetch(susp, snd_list)
  register clip_susp_type susp;
  snd_list_type snd_list;
{
    long final_count = susp->susp.toss_cnt;
    time_type final_time = susp->susp.t0;
    long n;

    /* fetch samples from s up to final_time for this block of zeros */
    while ((round((final_time - susp->s->t0) * susp->s->sr)) >=
       susp->s->current)
    susp_get_samples(s, s_ptr, s_cnt);
    /* convert to normal processing when we hit final_count */
    /* we want each signal positioned at final_time */
    n = round((final_time - susp->s->t0) * susp->s->sr -
         (susp->s->current - susp->s_cnt));
    susp->s_ptr += n;
    susp_took(s_cnt, n);
    susp->susp.fetch = susp->susp.keep_fetch;
    (*(susp->susp.fetch))(susp, snd_list);
}


void clip_mark(clip_susp_type susp)
{
    sound_xlmark(susp->s);
}


void clip_free(clip_susp_type susp)
{
    sound_unref(susp->s);
    ffree_generic(susp, sizeof(clip_susp_node), "clip_free");
}


void clip_print_tree(clip_susp_type susp, int n)
{
    indent(n);
    stdputstr("s:");
    sound_print_tree_1(susp->s, n);
}


sound_type snd_make_clip(sound_type s, double level)
{
    register clip_susp_type susp;
    rate_type sr = s->sr;
    time_type t0 = s->t0;
    int interp_desc = 0;
    sample_type scale_factor = 1.0F;
    time_type t0_min = t0;
    /* combine scale factors of linear inputs (S) */
    scale_factor *= s->scale;
    s->scale = 1.0F;

    /* try to push scale_factor back to a low sr input */
    if (s->sr < sr) { s->scale = scale_factor; scale_factor = 1.0F; }

    falloc_generic(susp, clip_susp_node, "snd_make_clip");
    susp->lev = level * s->scale;
    susp->susp.fetch = clip_n_fetch;
    susp->terminate_cnt = UNKNOWN;
    /* handle unequal start times, if any */
    if (t0 < s->t0) sound_prepend_zeros(s, t0);
    /* minimum start time over all inputs: */
    t0_min = min(s->t0, t0);
    /* how many samples to toss before t0: */
    susp->susp.toss_cnt = (long) ((t0 - t0_min) * sr + 0.5);
    if (susp->susp.toss_cnt > 0) {
    susp->susp.keep_fetch = susp->susp.fetch;
    susp->susp.fetch = clip_toss_fetch;
    }

    /* initialize susp state */
    susp->susp.free = clip_free;
    susp->susp.sr = sr;
    susp->susp.t0 = t0;
    susp->susp.mark = clip_mark;
    susp->susp.print_tree = clip_print_tree;
    susp->susp.name = "clip";
    susp->logically_stopped = false;
    susp->susp.log_stop_cnt = logical_stop_cnt_cvt(s);
    susp->susp.current = 0;
    susp->s = s;
    susp->s_cnt = 0;
    return sound_create((snd_susp_type)susp, t0, sr, scale_factor);
}


sound_type snd_clip(sound_type s, double level)
{
    sound_type s_copy = sound_copy(s);
    return snd_make_clip(s_copy, level);
}
