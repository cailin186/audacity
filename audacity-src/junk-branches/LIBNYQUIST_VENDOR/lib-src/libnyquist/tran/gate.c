#include "stdio.h"
#ifndef mips
#include "stdlib.h"
#endif
#include "xlisp.h"
#include "sound.h"

#include "falloc.h"
#include "cext.h"
#include "gate.h"

void gate_free();


typedef struct gate_susp_struct {
    snd_susp_node susp;
    long terminate_cnt;
    sound_type signal;
    long signal_cnt;
    sample_block_values_type signal_ptr;

    double rise_time;
    double fall_time;
    double floor;
    double threshold;
    long on_count;
    long off_count;
    double rise_factor;
    double fall_factor;
    long start_fall;
    long start_rise;
    long stop_count;
    long delay_len;
    int state;
    double value;
} gate_susp_node, *gate_susp_type;

#define ST_HOLD 0
#define ST_FALL 1
#define ST_FALL_UNTIL 2
#define ST_OFF 3
#define ST_OFF_UNTIL 4
#define ST_RISE 5

/* Overview:
This operation generates an exponential rise and decay suitable for implementing a 
noise gate. The decay starts when the signal drops below threshold and stays there
for longer than lookahead. 
Decay continues until the value reaches floor, at which point the decay stops
and the value is held constant. Either during the decay or after the floor is reached,
if the signal goes above threshold, then the output value will rise to 1.0 (0dB) at
the point the signal crosses the threshold. Again, lookahead is used, so the rise
actually starts before the signal crosses the threshold. The rise rate is constant
and set so that a rise from floor to 0dB occurs in the specified risetime.  Similarly,
the fall rate is constant such that a fall from 0dB to the floor takes falltime.

Rather than looking ahead, the output actually lags the input by lookahead. The caller
should advance the time of the input signal in order to get a correct output signal,
and this will be taken care of in Lisp code.

The implementation is a finite-state machine that simultaneously computes the value
and scans ahead for threshold crossings. Time points, remembered as sample counts are
saved in variables:
    on_count -- the time at which the rise should complete
    off_count -- the time at which the fall should begin
    rise_factor -- multiply by this to get exponential rise
    fall_factor -- multiply by this to get exponential fall
    rise_time -- number of samples for a full rise
    fall_time -- number of samples for a full fall
    floor -- the lowest value to output
    threshold -- compare the signal s to this value
    start_rise -- the sample count at which a rise begins
    delay_len -- number of samples to look ahead, length of buffer
    state -- the current state of finite state machine
        (see the individual 'case' statements for description of states)
    value -- the current output value
    
computing fall_factor:
    factor ^ (sample_rate * time) == floor
    log(factor) * sample_rate * time == log(floor)
    log(factor) == log(floor) / (sample_rate * time)
    factor == exp(log(floor) / (sample_rate * time))
    
*/

void compute_start_rise(gate_susp_type susp)
{
    /* to compute when to start rise to achieve 0dB at on_count:
    By similar triangles:
        truncated rise time    truncated fall time
        ------------------- == -------------------
            full rise time         full fall time
    when you enter ST_FALL, set start_fall = now
    then if (on_count - start_fall) < (rise_time + fall_time)
    then start rise at
    on_time - rise_time * (on_count-start_fall)/(rise_time+fall_time)
    */
    long total = (long) (susp->rise_time + susp->fall_time);
    if ((susp->on_count - susp->start_fall) < total) {
        susp->start_rise = (long) (susp->on_count - 
            (susp->rise_time * susp->on_count - susp->start_fall) / total);
    } else susp->start_rise = (long) (susp->on_count - susp->rise_time);
}


void gate_n_fetch(register gate_susp_type susp, snd_list_type snd_list)
{
    int cnt = 0; /* how many samples computed */
    int togo;
    int n;
    sample_block_type out;
    register sample_block_values_type out_ptr;

    register sample_block_values_type out_ptr_reg;

    register double threshold_reg;
    register long off_count_reg;
    register long stop_count_reg;
    register long delay_len_reg;
    register int state_reg;
    register double value_reg;
    register sample_block_values_type signal_ptr_reg;
    falloc_sample_block(out, "gate_n_fetch");
    out_ptr = out->samples;
    snd_list->block = out;

    while (cnt < max_sample_block_len) { /* outer loop */
    /* first compute how many samples to generate in inner loop: */
    /* don't overflow the output sample block: */
    togo = max_sample_block_len - cnt;

    /* don't run past the signal input sample block: */
    susp_check_term_samples(signal, signal_ptr, signal_cnt);
    togo = min(togo, susp->signal_cnt);

    /* don't run past terminate time */
    if (susp->terminate_cnt != UNKNOWN &&
        susp->terminate_cnt <= susp->susp.current + cnt + togo) {
        togo = susp->terminate_cnt - (susp->susp.current + cnt);
        if (togo == 0) break;
    }

    n = togo;
    threshold_reg = susp->threshold;
    off_count_reg = susp->off_count;
    stop_count_reg = susp->stop_count;
    delay_len_reg = susp->delay_len;
    state_reg = susp->state;
    value_reg = susp->value;
    signal_ptr_reg = susp->signal_ptr;
    out_ptr_reg = out_ptr;
    if (n) do { /* the inner sample computation loop */
{
        sample_type future = *signal_ptr_reg++;
        long now = susp->susp.current + cnt + togo - n;
        
        switch (state_reg) {
          /* hold at 1.0 and look for the moment to begin fall: */
          case ST_HOLD:
              if (future >= threshold_reg) {
                  off_count_reg = now + delay_len_reg;
              } else if (now >= off_count_reg) {
                  state_reg = ST_FALL;
                  stop_count_reg = (long) (now + susp->fall_time);
                  susp->start_fall = now;
              }
              break;
          /* fall until stop_count_reg while looking for next rise time */
          case ST_FALL:
            if (future >= threshold_reg) {
                off_count_reg = susp->on_count = now + delay_len_reg;
                compute_start_rise(susp);
                state_reg = ST_FALL_UNTIL;
            } else if (now == stop_count_reg) {
                state_reg = ST_OFF;
                value_reg = susp->floor;
            } else value_reg *= susp->fall_factor;
            break;
          /* fall until start_rise while looking for next fall time */
          case ST_FALL_UNTIL:
            value_reg *= susp->fall_factor;
            if (future >= threshold_reg) {
                   off_count_reg = now + delay_len_reg;
               }
               if (now >= susp->start_rise) {
                   state_reg = ST_RISE;
               } else if (now >= stop_count_reg) {
                   state_reg = ST_OFF_UNTIL;
                   value_reg = susp->floor;
               }
               break;
          /* hold at floor (minimum value_reg) and look for next rise time */
          case ST_OFF:
            if (future >= threshold_reg) {
                off_count_reg = susp->on_count = now + delay_len_reg;
                compute_start_rise(susp);
                state_reg = ST_OFF_UNTIL;
            }
            break;
          /* hold at floor until start_rise while looking for next fall time */
          case ST_OFF_UNTIL:
              if (future >= threshold_reg) {
                  off_count_reg = now + delay_len_reg;
              }
              if (now >= susp->start_rise) {
                  state_reg = ST_RISE;
              }
              break;
          /* rise while looking for fall time */
          case ST_RISE:
            value_reg *= susp->rise_factor;
            if (future >= threshold_reg) {
                off_count_reg = now + delay_len_reg;
            }
            if (now >= susp->on_count) {
                value_reg = 1.0;
                state_reg = ST_HOLD;
            }
            break;
          }
          *out_ptr_reg++ = (sample_type) value_reg;
      };
    } while (--n); /* inner loop */

    togo -= n;
    susp->off_count = off_count_reg;
    susp->stop_count = stop_count_reg;
    susp->state = state_reg;
    susp->value = value_reg;
    /* using signal_ptr_reg is a bad idea on RS/6000: */
    susp->signal_ptr += togo;
    out_ptr += togo;
    susp_took(signal_cnt, togo);
    cnt += togo;
    } /* outer loop */

    /* test for termination */
    if (togo == 0 && cnt == 0) {
    snd_list_terminate(snd_list);
    } else {
    snd_list->block_len = cnt;
    susp->susp.current += cnt;
    }
} /* gate_n_fetch */


void gate_toss_fetch(susp, snd_list)
  register gate_susp_type susp;
  snd_list_type snd_list;
{
    long final_count = susp->susp.toss_cnt;
    time_type final_time = susp->susp.t0;
    long n;

    /* fetch samples from signal up to final_time for this block of zeros */
    while ((round((final_time - susp->signal->t0) * susp->signal->sr)) >=
       susp->signal->current)
    susp_get_samples(signal, signal_ptr, signal_cnt);
    /* convert to normal processing when we hit final_count */
    /* we want each signal positioned at final_time */
    n = round((final_time - susp->signal->t0) * susp->signal->sr -
         (susp->signal->current - susp->signal_cnt));
    susp->signal_ptr += n;
    susp_took(signal_cnt, n);
    susp->susp.fetch = susp->susp.keep_fetch;
    (*(susp->susp.fetch))(susp, snd_list);
}


void gate_mark(gate_susp_type susp)
{
    sound_xlmark(susp->signal);
}


void gate_free(gate_susp_type susp)
{
    sound_unref(susp->signal);
    ffree_generic(susp, sizeof(gate_susp_node), "gate_free");
}


void gate_print_tree(gate_susp_type susp, int n)
{
    indent(n);
    stdputstr("signal:");
    sound_print_tree_1(susp->signal, n);
}


sound_type snd_make_gate(sound_type signal, time_type lookahead, double risetime, double falltime, double floor, double threshold)
{
    register gate_susp_type susp;
    rate_type sr = signal->sr;
    time_type t0 = 0.0;
    int interp_desc = 0;
    sample_type scale_factor = 1.0F;
    time_type t0_min = t0;
    /* combine scale factors of linear inputs (SIGNAL) */
    scale_factor *= signal->scale;
    signal->scale = 1.0F;

    /* try to push scale_factor back to a low sr input */
    if (signal->sr < sr) { signal->scale = scale_factor; scale_factor = 1.0F; }

    falloc_generic(susp, gate_susp_node, "snd_make_gate");
    susp->rise_time = signal->sr * risetime + 0.5;
    susp->fall_time = signal->sr * falltime + 0.5;
    susp->floor = floor; floor = log(floor);;
    susp->threshold = threshold;
    susp->on_count = 0;
    susp->off_count = 0;
    susp->rise_factor = exp(- floor / susp->rise_time);
    susp->fall_factor = exp(floor / susp->fall_time);
    susp->start_fall = 0;
    susp->start_rise = 0;
    susp->stop_count = 0;
    susp->delay_len = max(1, round(signal->sr * lookahead));
    susp->state = ST_OFF;
    susp->value = susp->floor;
    susp->susp.fetch = gate_n_fetch;
    susp->terminate_cnt = UNKNOWN;
    /* handle unequal start times, if any */
    if (t0 < signal->t0) sound_prepend_zeros(signal, t0);
    /* minimum start time over all inputs: */
    t0_min = min(signal->t0, t0);
    /* how many samples to toss before t0: */
    susp->susp.toss_cnt = (long) ((t0 - t0_min) * sr + 0.5);
    if (susp->susp.toss_cnt > 0) {
    susp->susp.keep_fetch = susp->susp.fetch;
    susp->susp.fetch = gate_toss_fetch;
    }

    /* initialize susp state */
    susp->susp.free = gate_free;
    susp->susp.sr = sr;
    susp->susp.t0 = t0;
    susp->susp.mark = gate_mark;
    susp->susp.print_tree = gate_print_tree;
    susp->susp.name = "gate";
    susp->susp.log_stop_cnt = UNKNOWN;
    susp->susp.current = 0;
    susp->signal = signal;
    susp->signal_cnt = 0;
    return sound_create((snd_susp_type)susp, t0, sr, scale_factor);
}


sound_type snd_gate(sound_type signal, time_type lookahead, double risetime, double falltime, double floor, double threshold)
{
    sound_type signal_copy = sound_copy(signal);
    return snd_make_gate(signal_copy, lookahead, risetime, falltime, floor, threshold);
}
