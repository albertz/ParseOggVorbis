//
//  main.cpp
//  ParseOggVorbis
//
//  Created by Albert Zeyer on 07.01.19.
//  Copyright © 2019 Albert Zeyer. All rights reserved.
//

#include <iostream>
#include "ParseOggVorbis.hpp"
#include "Utils.hpp"
#include "Callbacks.h"

int main(int argc, const char** argv) {
	ArgParser args;
	if(!args.parse_args(argc, argv))
		return 1;
	OggReader reader;
	OkOrError result = reader.full_read(args.ogg_filename);
	if(result.is_error_)
		std::cerr << "error: " << result.err_msg_ << std::endl;
	else
		std::cout << "ok" << std::endl;
	return (int) result.is_error_;
}
