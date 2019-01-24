/********************************************************************
 *                                                                  *
 * THIS FILE IS PART OF THE OggVorbis SOFTWARE CODEC SOURCE CODE.   *
 * USE, DISTRIBUTION AND REPRODUCTION OF THIS LIBRARY SOURCE IS     *
 * GOVERNED BY A BSD-STYLE SOURCE LICENSE INCLUDED WITH THIS SOURCE *
 * IN 'COPYING'. PLEASE READ THESE TERMS BEFORE DISTRIBUTING.       *
 *                                                                  *
 * THE OggVorbis SOURCE CODE IS (C) COPYRIGHT 1994-2007             *
 * by the Xiph.Org Foundation http://www.xiph.org/                  *
 *                                                                  *
 ********************************************************************

 function: simple example decoder using vorbisfile

 ********************************************************************/

/* Takes a vorbis bitstream from stdin and writes raw stereo PCM to
   stdout using vorbisfile. Using vorbisfile is much simpler than
   dealing with libvorbis. */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisfile.h>
#include <iostream>
#include "Callbacks.h"

#ifdef _WIN32 /* We need the following two to set stdin/stdout to binary */
#include <io.h>
#include <fcntl.h>
#endif

char pcmout[4096]; /* take 4k out of the data segment, not the stack */

int main(int argc, const char** argv) {
	ArgParser args;
	if(!args.parse_args(argc, argv))
		return 1;

#ifdef _WIN32 /* We need to set stdin/stdout to binary mode. Damn windows. */
	/* Beware the evil ifdef. We avoid these where we can, but this one we
	 cannot. Don't add any more, you'll probably go to hell if you do. */
	_setmode(_fileno(stdin), _O_BINARY);
	_setmode(_fileno(stdout), _O_BINARY);
#endif

	std::cout << "open file: " << args.ogg_filename << std::endl;
	FILE* file = fopen(args.ogg_filename.c_str(), "rb");
	if(!file) {
		std::cerr << "cannot open file" << std::endl;
		return 1;
	}
	OggVorbis_File vf;
	if(ov_open_callbacks(file, &vf, NULL, 0, OV_CALLBACKS_DEFAULT) < 0) {
		std::cerr << "Input does not appear to be an Ogg bitstream." << std::endl;
		return 1;
	}

	/* Throw the comments plus a few lines about the bitstream we're decoding */
	{
		char **ptr = ov_comment(&vf, -1)->user_comments;
		vorbis_info *vi = ov_info(&vf, -1);
		while(*ptr) {
			std::cout << *ptr << std::endl;
			++ptr;
		}
		std::cout
		<< std::endl
		<< "Bitstream is " << vi->channels << " channel, " << vi->rate << "Hz\n" << std::endl
		<< "Decoded length: " << (long)ov_pcm_total(&vf, -1) << " samples" << std::endl
		<< "Encoded by: " << ov_comment(&vf, -1)->vendor << std::endl
		<< std::endl;
	}

	size_t sample_count = 0;
	bool eof = false;
	while(!eof) {
		int current_section;
		long ret = ov_read(&vf, pcmout, sizeof(pcmout), 0, 2, 1, &current_section);
		if(ret == 0) {
			/* EOF */
			eof = true;
		} else if(ret < 0) {
			if(ret == OV_EBADLINK)
				std::cerr << "Corrupt bitstream section! Exiting." << std::endl;
			else
				std::cerr << "error reading: " << ret << std::endl;
			return 1;
		} else {
			/* we don't bother dealing with sample rate changes, etc, but you'll have to */
			//fwrite(pcmout, 1, ret, stdout);
			sample_count += ret / 2 / ov_info(&vf, -1)->channels;
		}
	}

	/* cleanup */
	ov_clear(&vf);

	std::cout << "Num samples: " << sample_count << std::endl;
	std::cout << "Done." << std::endl;
	return 0;
}
