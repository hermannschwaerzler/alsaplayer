//---------------------------------------------------------------------------
//  alsaplayer flac input plugin handlers.
//
//  Copyright (c) 2002 by Drew Hess <dhess@bothan.net>
//
/*  This file is part of AlsaPlayer.
 *
 *  AlsaPlayer is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  AlsaPlayer is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
//--------------------------------------------------------------------------

#if !defined(FLAC_API_VERSION_CURRENT) || FLAC_API_VERSION_CURRENT < 8
#define LEGACY_FLAC
#else
#undef LEGACY_FLAC
#endif

#include <string>
#include <cstdio>
#include <stdlib.h>
#include <cstring>
#include <cmath>

#include "config.h"

#include "input_plugin.h"
#include "alsaplayer_error.h"
#include "ap_string.h"

#include "FlacStream.h"
#include "FlacEngine.h"
#include "FlacSeekableStream.h"
#include "FlacTag.h"
#ifdef HAVE_LIBOGGFLC
#include "OggFlacStream.h"
#endif

static int
flac_channels (input_object * obj)
{
    return obj->nr_channels;
}


static int
flac_sample_rate (input_object * obj)
{
    if (!obj)
	return 0;

    Flac::FlacStream * f = (Flac::FlacStream *) obj->local_data;
    if (!f)
	return 0;
    return (int) f->sampleRate ();
}


static int
flac_block_size (input_object * obj)
{
    if (!obj)
	return 0;
    return obj->block_size;
}


static int
flac_nr_blocks (input_object * obj)
{
    if (!obj)
	return 0;
    return obj->nr_blocks;
}

static int64_t
flac_frame_count (input_object * obj)
{
    if (!obj)
	return 0;
    return ((int64_t) obj->nr_blocks) * obj->block_size;
}


static long
flac_block_to_centisec (input_object * obj, int block)
{
    if (!obj)
	return 0;

    Flac::FlacStream * f = (Flac::FlacStream *) obj->local_data;
    if (!f)
	return 0;

    return (long) f->engine ()->blockTime (block) * 100;
}


static int
flac_block_seek (input_object * obj, int block)
{
    if (!obj)
	return 0;

    Flac::FlacStream * f = (Flac::FlacStream *) obj->local_data;
    if (!f)
      return 0;
    return f->engine ()->seekToBlock (block);
}


static int
flac_play_block (input_object * obj, short * buf)
{
    if (!obj || !buf)
	return 0;

    Flac::FlacStream * f = (Flac::FlacStream *) obj->local_data;
    if (!f)
	return 0;
    return f->engine ()->decodeBlock (buf);
}


static int
flac_open (input_object * obj, const char * name)
{
    if (!obj)
	return 0;
    if (!name)
	return 0;

    reader_type * rdr = reader_open (name, NULL, NULL);
    if (!rdr)
    {
	alsaplayer_error ("flac_open: reader_open failed");
	return 0;
    }

    obj->flags = 0;
    Flac::FlacStream * f = 0;
    try
    {
	if (Flac::FlacStream::isFlacStream (name))
	{
	    if (reader_seekable (rdr))
	    {
		f = new Flac::FlacSeekableStream (name, rdr);
		obj->flags |= P_SEEK | P_PERFECTSEEK;
	    }
	    else
		f = new Flac::FlacStream (name, rdr);
	}
#ifdef HAVE_LIBOGGFLC
	else
	{
	    f = new Flac::OggFlacStream (name, rdr);
	}
#endif
    }
    catch (...)
    {
	alsaplayer_error ("flac_open: unable to allocate memory for plugin.");
	delete f;
	reader_close (rdr);
	return 0;
    }

    if (f && f->open ())
    {
	obj->block_size  = f->engine ()->apBlockSize ();

	// attach a song info tag

	if (Flac::FlacTag::hasTag (f->name ()))
	{
	    Flac::FlacTag * t = Flac::FlacTag::newTag (f->name ());
	    f->setTag (t);
	}

	if (strncasecmp (name, "http://", 7) == 0)
	    obj->flags |= P_STREAMBASED;
	else
	    obj->flags |= P_FILEBASED;
	obj->nr_channels  = f->engine ()->apChannels ();
	obj->flags       |= P_REENTRANT;
	obj->nr_blocks    = f->engine ()->apBlocks ();
	obj->nr_tracks    = 1;
	obj->ready        = 1;
	obj->local_data   = (void *) f;
	return 1;
    }
    else
    {
	alsaplayer_error ("flac_open: unable to open flac stream or "
			  "unsupported flac stream (%s)", name);
	delete f;
	obj->block_size  = 0;
	obj->nr_channels = 0;
	obj->flags       = 0;
	obj->nr_blocks   = 0;
	obj->nr_tracks   = 0;
	obj->ready       = 0;
	obj->local_data  = 0;
	return 0;
    }
}


static int
flac_stream_info (input_object * obj, stream_info * info)
{
    if (!obj || !info)
	return 0;

    Flac::FlacStream * f = (Flac::FlacStream *) obj->local_data;
    if (!f)
	return 0;

    snprintf (info->stream_type, sizeof (info->stream_type), "%d-bit %dKhz %s flac",
	     f->bps (), f->sampleRate () / 1000,
	     f->channels () == 1 ? "mono" :
	     f->channels () == 2 ? "stereo" : "multi-channel");

    Flac::FlacTag * t = f->tag ();
    if (t && ! t->title ().empty ())
    {
	ap_strlcpy (info->artist, t->artist ().c_str (), sizeof (info->artist));
	ap_strlcpy (info->title, t->title ().c_str (), sizeof (info->title));
	ap_strlcpy (info->album, t->album ().c_str (), sizeof (info->album));
	ap_strlcpy (info->genre, t->genre ().c_str (), sizeof (info->genre));
	ap_strlcpy (info->year, t->year ().c_str (), sizeof (info->year));
	ap_strlcpy (info->track, t->track ().c_str (), sizeof (info->track));
	ap_strlcpy (info->comment, t->comment ().c_str (), sizeof (info->comment));
    }
    else
    {
	// use stream name
	const char * fname = strrchr (f->name ().c_str (), '/');
	if (fname)
	{
	    fname++;
	    ap_strlcpy (info->title, fname, sizeof (info->title));
	}
	else
	    info->title[0] = 0;

	info->artist[0]  = 0;
	info->title[0]   = 0;
	info->album[0]   = 0;
	info->genre[0]   = 0;
	info->year[0]    = 0;
	info->track[0]   = 0;
	info->comment[0] = 0;
    }
    info->status[0] = 0;

    return 1;
}


static float
flac_can_handle (const char * name)
{
	float res = 0.0;

	if (strncmp(name, "http://", 7) == 0) {
		return 0.0;
	}
	const char *ext = strrchr(name, '.');
	if (!ext)
		return 0.0;
	ext++;
	if (strcasecmp(ext, "flac") == 0) /* Always support .flac files */
		return 0.8;
	if (strcasecmp(ext, "ogg")) /* Ignore all non .ogg files */
		return 0.0;

	res = Flac::FlacStream::isFlacStream (name);
#ifdef HAVE_LIBOGGFLC
	if (res != 1.0) {
		res = Flac::OggFlacStream::isOggFlacStream (name);
	}
#endif
	return res;
}


static void
flac_close (input_object * obj)
{
    if (!obj)
	return;

    Flac::FlacStream * f = (Flac::FlacStream *) obj->local_data;
    delete f;
    f = 0;
}


static int
flac_init ()
{
    return 1;
}


static void
flac_shutdown ()
{
    return;
}


static input_plugin flac_plugin;

extern "C"
{

input_plugin *
input_plugin_info (void)
{
    memset (&flac_plugin, 0, sizeof(input_plugin));

    flac_plugin.version      = INPUT_PLUGIN_VERSION;
#ifdef LEGACY_FLAC
    flac_plugin.name         = (char *)"flac player v1.1.2";
#else
    flac_plugin.name         = (char *)"flac player v1.1.3/1.1.4/1.2";
#endif
    flac_plugin.author       = "Drew Hess";
    flac_plugin.init         = flac_init;
    flac_plugin.shutdown     = flac_shutdown;
    flac_plugin.can_handle   = flac_can_handle;
    flac_plugin.open         = flac_open;
    flac_plugin.close        = flac_close;
    flac_plugin.play_block   = flac_play_block;
    flac_plugin.block_seek   = flac_block_seek;
    flac_plugin.block_size   = flac_block_size;
    flac_plugin.nr_blocks    = flac_nr_blocks;
    flac_plugin.frame_count  = flac_frame_count;
    flac_plugin.block_to_sec = flac_block_to_centisec;
    flac_plugin.sample_rate  = flac_sample_rate;
    flac_plugin.channels     = flac_channels;
    flac_plugin.stream_info  = flac_stream_info;

    return & flac_plugin;
}

}
