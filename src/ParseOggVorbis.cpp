//
//  ParseOggVorbis.cpp
//  ParseOggVorbis
//
//  Created by Albert Zeyer on 05.02.19.
//  Copyright © 2019 Albert Zeyer. All rights reserved.
//

#include <string.h>
#include "ParseOggVorbis.hpp"

extern "C" int ogg_vorbis_full_read(const char* filename, const char** error_out) {
	ParseCallbacks dummy_callbacks;
	OggReader reader(dummy_callbacks);
	OkOrError result = reader.full_read(filename);
	if(result.is_error_) {
		if(error_out) {
			static char error_buf[255];
			strncpy(error_buf, result.err_msg_.c_str(), sizeof(error_buf));
			error_buf[sizeof(error_buf) - 1] = 0;
			*error_out = error_buf;
		}
		return 1;
	}
	return 0;
}
