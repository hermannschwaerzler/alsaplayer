/*

    TiMidity -- Experimental MIDI to WAVE converter
    Copyright (C) 1995 Tuukka Toivonen <toivonen@clinet.fi>

 *  This file is part of the MIDI input plugin for AlsaPlayer.
 *
 *  The MIDI input plugin for AlsaPlayer is free software;
 *  you can redistribute it and/or modify it under the terms of the
 *  GNU General Public License as published by the Free Software
 *  Foundation; either version 3 of the License, or (at your option)
 *  any later version.
 *
 *  The MIDI input plugin for AlsaPlayer is distributed in the hope that
 *  it will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *  warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.

    I bet they'll be amazed.

    mix.c */

#include <math.h>
#include <stdio.h>
#ifdef __FreeBSD__
#include <stdlib.h>
#else
#include <malloc.h>
#endif
#include "gtim.h"
#include "common.h"
#include "instrum.h"
#include "playmidi.h"
#include "effects.h"
#include "md.h"
#include "output.h"
#include "controls.h"
#include "tables.h"
#include "resample.h"
#include "mix.h"

/* Returns 1 if envelope runs out */
int recompute_envelope(int v, struct md *d)
{
  int stage;

  stage = d->voice[v].envelope_stage;

#if 0
if (d->voice[v].envelope_offset[stage] >> 23 > 127)
fprintf(stderr, "offset %d\n", d->voice[v].envelope_offset[stage]>>23);
#endif

  if (stage>5)
    {
      /* Envelope ran out. */
      if (!(d->voice[v].status & VOICE_FREE))
	{
          d->voice[v].status = VOICE_FREE;
	  ctl->note(v);
	}
      return 1;
    }

  /**if (d->voice[v].sample->modes & MODES_ENVELOPE)**/
  if ((d->voice[v].sample->modes & MODES_ENVELOPE) && (d->voice[v].sample->modes & MODES_SUSTAIN))
    {
      if (d->voice[v].status & (VOICE_ON | VOICE_SUSTAINED))
	{
	  if (stage>2)
	    {
	      /* Freeze envelope until note turns off. Trumpets want this. */
	      d->voice[v].envelope_increment=0;
	      return 0;
	    }
	}
    }
  d->voice[v].envelope_stage=stage+1;

#if 0
fprintf(stderr, "v=%d stage %d, inc %ld[%ld], vol %ld[%ld], offset %ld[%ld]\n", v,
stage, d->voice[v].envelope_increment, d->voice[v].envelope_increment>>23,
 d->voice[v].envelope_volume, d->voice[v].envelope_volume>>23,
 d->voice[v].envelope_offset[stage], d->voice[v].envelope_offset[stage]>>23);
#endif


#ifdef tplus
  if (d->voice[v].envelope_volume==(int)d->voice[v].envelope_offset[stage] ||
      (stage > 2 && d->voice[v].envelope_volume < (int)d->voice[v].envelope_offset[stage]))
#else
  if (d->voice[v].envelope_volume==d->voice[v].envelope_offset[stage])
#endif

#if 0
  if ( (d->voice[v].envelope_increment >= 0 && d->voice[v].envelope_volume >= d->voice[v].envelope_offset[stage])
    || (d->voice[v].envelope_increment < 0  && d->voice[v].envelope_volume <= d->voice[v].envelope_offset[stage]) )
#endif
    return recompute_envelope(v, d);
  d->voice[v].envelope_target=d->voice[v].envelope_offset[stage];
  d->voice[v].envelope_increment = d->voice[v].envelope_rate[stage];
  if ((int)d->voice[v].envelope_target<d->voice[v].envelope_volume)
    d->voice[v].envelope_increment = -d->voice[v].envelope_increment;

#if 0
fprintf(stderr, "	cont:v=%d, stage %d, inc %ld[%ld], vol %ld[%ld], offset %ld[%ld]\n", v,
stage, d->voice[v].envelope_increment, d->voice[v].envelope_increment>>23,
 d->voice[v].envelope_volume, d->voice[v].envelope_volume>>23,
 d->voice[v].envelope_offset[stage], d->voice[v].envelope_offset[stage]>>23);
#endif




  return 0;
}

#ifdef tplus
int apply_envelope_to_amp(int v, struct md *d)
#else
void apply_envelope_to_amp(int v, struct md *d)
#endif
{
  FLOAT_T lamp=d->voice[v].left_amp, ramp;
  int32 la,ra;
  if (d->voice[v].panned == PANNED_MYSTERY)
    {
      ramp=d->voice[v].right_amp;
      if (d->voice[v].tremolo_phase_increment)
	{
	  lamp *= d->voice[v].tremolo_volume;
	  ramp *= d->voice[v].tremolo_volume;
	}
      if (d->voice[v].sample->modes & MODES_ENVELOPE)
	{
	  lamp *= d->vol_table[d->voice[v].envelope_volume>>23];
	  ramp *= d->vol_table[d->voice[v].envelope_volume>>23];
#if 0
if (d->voice[v].envelope_volume>>23 > 127)
fprintf(stderr,"env vol %d >>23 = %d\n", d->voice[v].envelope_volume,
d->voice[v].envelope_volume >> 23);
#endif
	}

      la = (int32)FRSCALE(lamp,AMP_BITS);
      
      if (la>MAX_AMP_VALUE)
	la=MAX_AMP_VALUE;

      ra = (int32)FRSCALE(ramp,AMP_BITS);
      if (ra>MAX_AMP_VALUE)
	ra=MAX_AMP_VALUE;

#ifdef tplus
      if ((d->voice[v].status & (VOICE_OFF | VOICE_DIE | VOICE_FREE | VOICE_SUSTAINED))
	  && (la | ra) <= MIN_AMP_VALUE)
      {
	  if (!(d->voice[v].status & VOICE_FREE))
	    {
	      d->voice[v].status = VOICE_FREE;
	      ctl->note(v);
	    }
	  return 1;
      }
#endif
 
      d->voice[v].left_mix=FINAL_VOLUME(la);
      d->voice[v].right_mix=FINAL_VOLUME(ra);
    }
  else
    {
      if (d->voice[v].tremolo_phase_increment)
	lamp *= d->voice[v].tremolo_volume;
      if (d->voice[v].sample->modes & MODES_ENVELOPE)
	lamp *= d->vol_table[d->voice[v].envelope_volume>>23];

      la = (int32)FRSCALE(lamp,AMP_BITS);

      if (la>MAX_AMP_VALUE)
	la=MAX_AMP_VALUE;

#ifdef tplus
      if ( (d->voice[v].status & (VOICE_OFF | VOICE_DIE | VOICE_FREE | VOICE_SUSTAINED))
	 && la <= MIN_AMP_VALUE)
      {
	  if (!(d->voice[v].status & VOICE_FREE))
	    {
	      d->voice[v].status = VOICE_FREE;
	      ctl->note(v);
	    }
	  return 1;
      }
#endif

      d->voice[v].left_mix=FINAL_VOLUME(la);
    }
#ifdef tplus
    return 0;
#endif
}

static int update_envelope(int v, struct md *d)
{
  d->voice[v].envelope_volume += d->voice[v].envelope_increment;
  if (d->voice[v].envelope_volume<0) d->voice[v].envelope_volume = 0;
  /* Why is there no ^^ operator?? */
  if (((d->voice[v].envelope_increment < 0) &&
       (d->voice[v].envelope_volume <= (int)d->voice[v].envelope_target)) ||
      ((d->voice[v].envelope_increment > 0) &&
	   (d->voice[v].envelope_volume >= (int)d->voice[v].envelope_target)))
    {
      d->voice[v].envelope_volume = d->voice[v].envelope_target;
      if (recompute_envelope(v, d))
	return 1;
    }
  return 0;
}

static void update_tremolo(int v, struct md *d)
{
  int32 depth=d->voice[v].sample->tremolo_depth<<7;

  if (d->voice[v].tremolo_sweep)
    {
      /* Update sweep position */

      d->voice[v].tremolo_sweep_position += d->voice[v].tremolo_sweep;
      if (d->voice[v].tremolo_sweep_position>=(1<<SWEEP_SHIFT))
	d->voice[v].tremolo_sweep=0; /* Swept to max amplitude */
      else
	{
	  /* Need to adjust depth */
	  depth *= d->voice[v].tremolo_sweep_position;
	  depth >>= SWEEP_SHIFT;
	}
    }

  d->voice[v].tremolo_phase += d->voice[v].tremolo_phase_increment;

  /* if (d->voice[v].tremolo_phase >= (SINE_CYCLE_LENGTH<<RATE_SHIFT))
     d->voice[v].tremolo_phase -= SINE_CYCLE_LENGTH<<RATE_SHIFT;  */

  d->voice[v].tremolo_volume = 
    1.0 - FRSCALENEG((sine(d->voice[v].tremolo_phase >> RATE_SHIFT) + 1.0)
		    * depth * TREMOLO_AMPLITUDE_TUNING,
		    17);

  /* I'm not sure about the +1.0 there -- it makes tremoloed voices'
     volumes on average the lower the higher the tremolo amplitude. */
}

/* Returns 1 if the note died */
static int update_signal(int v, struct md *d)
{
  if (d->voice[v].envelope_increment && update_envelope(v, d))
    return 1;

  if (d->voice[v].tremolo_phase_increment)
    update_tremolo(v, d);

#ifdef tplus
  return apply_envelope_to_amp(v, d);
#else
  apply_envelope_to_amp(v, d);
  return 0;
#endif
}

#ifdef LOOKUP_HACK
#  define MIXATION(a)	*lp++ += mixup[(a<<8) | (uint8)s];
#else
#  define MIXATION(a)	*lp++ += (a)*s;
#endif

static void mix_mystery_signal(sample_t *sp, int32 *lp, int v, uint32 count, struct md *d)
{
  Voice *vp = d->voice + v;
  final_volume_t 
    left=vp->left_mix, 
    right=vp->right_mix;
  uint32 cc;
  sample_t s;

  if (!(cc = vp->control_counter))
    {
      cc = control_ratio;
      if (update_signal(v, d))
	return;	/* Envelope ran out */
      left = vp->left_mix;
      right = vp->right_mix;
    }
  
  while (count)
    if (cc < count)
      {
	count -= cc;
	while (cc--)
	  {
	    s = *sp++;
	    MIXATION(left);
	    MIXATION(right);
	  }
	cc = control_ratio;
	if (update_signal(v, d))
	  return;	/* Envelope ran out */
	left = vp->left_mix;
	right = vp->right_mix;
      }
    else
      {
	vp->control_counter = cc - count;
	while (count--)
	  {
	    s = *sp++;
	    MIXATION(left);
	    MIXATION(right);
	  }
	return;
      }
}

static void mix_center_signal(sample_t *sp, int32 *lp, int v, uint32 count, struct md *d)
{
  Voice *vp = d->voice + v;
  final_volume_t 
    left=vp->left_mix;
  uint32 cc;
  sample_t s;

  if (!(cc = vp->control_counter))
    {
      cc = control_ratio;
      if (update_signal(v, d))
	return;	/* Envelope ran out */
      left = vp->left_mix;
    }
  
  while (count)
    if (cc < count)
      {
	count -= cc;
	while (cc--)
	  {
	    s = *sp++;
	    MIXATION(left);
	    MIXATION(left);
	  }
	cc = control_ratio;
	if (update_signal(v, d))
	  return;	/* Envelope ran out */
	left = vp->left_mix;
      }
    else
      {
	vp->control_counter = cc - count;
	while (count--)
	  {
	    s = *sp++;
	    MIXATION(left);
	    MIXATION(left);
	  }
	return;
      }
}

static void mix_single_signal(sample_t *sp, int32 *lp, int v, uint32 count, struct md *d)
{
  Voice *vp = d->voice + v;
  final_volume_t 
    left=vp->left_mix;
  uint32 cc;
  sample_t s;
  
  if (!(cc = vp->control_counter))
    {
      cc = control_ratio;
      if (update_signal(v, d))
	return;	/* Envelope ran out */
      left = vp->left_mix;
    }
  
  while (count)
    if (cc < count)
      {
	count -= cc;
	while (cc--)
	  {
	    s = *sp++;
	    MIXATION(left);
	    lp++;
	  }
	cc = control_ratio;
	if (update_signal(v, d))
	  return;	/* Envelope ran out */
	left = vp->left_mix;
      }
    else
      {
	vp->control_counter = cc - count;
	while (count--)
	  {
	    s = *sp++;
	    MIXATION(left);
	    lp++;
	  }
	return;
      }
}

static void mix_mono_signal(sample_t *sp, int32 *lp, int v, uint32 count, struct md *d)
{
  Voice *vp = d->voice + v;
  final_volume_t 
    left=vp->left_mix;
  uint32 cc;
  sample_t s;
  
  if (!(cc = vp->control_counter))
    {
      cc = control_ratio;
      if (update_signal(v, d))
	return;	/* Envelope ran out */
      left = vp->left_mix;
    }
  
  while (count)
    if (cc < count)
      {
	count -= cc;
	while (cc--)
	  {
	    s = *sp++;
	    MIXATION(left);
	  }
	cc = control_ratio;
	if (update_signal(v, d))
	  return;	/* Envelope ran out */
	left = vp->left_mix;
      }
    else
      {
	vp->control_counter = cc - count;
	while (count--)
	  {
	    s = *sp++;
	    MIXATION(left);
	  }
	return;
      }
}

static void mix_mystery(sample_t *sp, int32 *lp, int v, uint32 count, struct md *d)
{
  final_volume_t 
    left=d->voice[v].left_mix, 
    right=d->voice[v].right_mix;
  sample_t s;
  
  while (count--)
    {
      s = *sp++;
      MIXATION(left);
      MIXATION(right);
    }
}

static void mix_center(sample_t *sp, int32 *lp, int v, uint32 count, struct md *d)
{
  final_volume_t 
    left=d->voice[v].left_mix;
  sample_t s;
  
  while (count--)
    {
      s = *sp++;
      MIXATION(left);
      MIXATION(left);
    }
}

static void mix_single(sample_t *sp, int32 *lp, int v, uint32 count, struct md *d)
{
  final_volume_t 
    left=d->voice[v].left_mix;
  sample_t s;
  
  while (count--)
    {
      s = *sp++;
      MIXATION(left);
      lp++;
    }
}

static void mix_mono(sample_t *sp, int32 *lp, int v, uint32 count, struct md *d)
{
  final_volume_t 
    left=d->voice[v].left_mix;
  sample_t s;
  
  while (count--)
    {
      s = *sp++;
      MIXATION(left);
    }
}

/* Ramp a note out in c samples */
static void ramp_out(sample_t *sp, int32 *lp, int v, uint32 c, struct md *d)
{

  /* should be final_volume_t, but uint8 gives trouble. */
  int32 left, right, li, ri;

  sample_t s=0; /* silly warning about uninitialized s */

  left=d->voice[v].left_mix;
  if (c < 1) return;
  li=-(left/c); /*NB: c can be 0 here */
  if (!li) li=-1;

  /* printf("Ramping out: left=%d, c=%d, li=%d\n", left, c, li); */

  if (!(play_mode->encoding & PE_MONO))
    {
      if (d->voice[v].panned==PANNED_MYSTERY)
	{
	  right=d->voice[v].right_mix;
	  ri=-(right/c);
	  while (c--)
	    {
	      left += li;
	      if (left<0)
		left=0;
	      right += ri;
	      if (right<0)
		right=0;
	      s=*sp++;
	      MIXATION(left);
	      MIXATION(right);
	    }
	}
      else if (d->voice[v].panned==PANNED_CENTER)
	{
	  while (c--)
	    {
	      left += li;
	      if (left<0)
		return;
	      s=*sp++;	
	      MIXATION(left);
	      MIXATION(left);
	    }
	}
      else if (d->voice[v].panned==PANNED_LEFT)
	{
	  while (c--)
	    {
	      left += li;
	      if (left<0)
		return;
	      s=*sp++;
	      MIXATION(left);
	      lp++;
	    }
	}
      else if (d->voice[v].panned==PANNED_RIGHT)
	{
	  while (c--)
	    {
	      left += li;
	      if (left<0)
		return;
	      s=*sp++;
	      lp++;
	      MIXATION(left);
	    }
	}
    }
  else
    {
      /* Mono output.  */
      while (c--)
	{
	  left += li;
	  if (left<0)
	    return;
	  s=*sp++;
	  MIXATION(left);
	}
    }
}

/**************** interface function ******************/

void mix_voice(int32 *buf, int v, uint32 c, struct md *d)
{
  Voice *vp=d->voice+v;
  sample_t *sp;
  uint32 count=c;
  if (vp->status&VOICE_DIE)
    {
/* this seems no longer useful: resample kill voices
   before they get here
*/
      if (count>=MAX_DIE_TIME)
	count=MAX_DIE_TIME;
      sp=resample_voice(v, &count, d);
      ramp_out(sp, buf, v, count, d);
      vp->status=VOICE_FREE;
    }
  else
    {
      sp=resample_voice(v, &count, d);
      if (count<1)
	{
          vp->status=VOICE_FREE;
	  return;
	}
      if (play_mode->encoding & PE_MONO)
	{
	  /* Mono output. */
	  if (vp->envelope_increment || vp->tremolo_phase_increment)
	    mix_mono_signal(sp, buf, v, count, d);
	  else
	    mix_mono(sp, buf, v, count, d);
	}
      else
	{
	  if (vp->panned == PANNED_MYSTERY)
	    {
	      if (vp->envelope_increment || vp->tremolo_phase_increment)
		mix_mystery_signal(sp, buf, v, count, d);
	      else
		mix_mystery(sp, buf, v, count, d);
	    }
	  else if (vp->panned == PANNED_CENTER)
	    {
	      if (vp->envelope_increment || vp->tremolo_phase_increment)
		mix_center_signal(sp, buf, v, count, d);
	      else
		mix_center(sp, buf, v, count, d);
	    }
	  else
	    { 
	      /* It's either full left or full right. In either case,
		 every other sample is 0. Just get the offset right: */
	      if (vp->panned == PANNED_RIGHT) buf++;
	      
	      if (vp->envelope_increment || vp->tremolo_phase_increment)
		mix_single_signal(sp, buf, v, count, d);
	      else 
		mix_single(sp, buf, v, count, d);
	    }
	}
    }
}
