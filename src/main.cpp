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

struct MyParseCallbacks : ParseCallbacks {
	uint64_t sample_count;
	MyParseCallbacks() : sample_count(0) {}

	virtual bool gotHeader(const VorbisIdHeader& header) {
		std::cout
		<< "Header: vorbis version: " << header.vorbis_version
		<< ", channels: " << (int) header.audio_channels
		<< ", sample rate: " << header.audio_sample_rate
		<< std::endl;
		return true;
	}
	virtual bool gotSetup(const VorbisStreamSetup& setup) {
		std::cout
		<< "Setup: num codebooks: " << setup.codebooks.size()
		<< ", num floors: " << setup.floors.size()
		<< ", num mappings: " << setup.mappings.size()
		<< ", num modes: " << setup.modes.size()
		<< ", num residues: " << setup.residues.size()
		<< std::endl;
		return true;
	}
	virtual bool gotPcmData(const std::vector<DataRange<const float>>& channelPcms) {
		assert(channelPcms.size() > 0);
		sample_count += channelPcms[0].size();
		return true;
	}
	virtual bool gotEof() {
		std::cout << "got eof. sample count: " << sample_count << std::endl;
		return true;
	}
};

int main(int argc, const char** argv) {
	ArgParser args;
	if(!args.parse_args(argc, argv))
		return 1;
	MyParseCallbacks callbacks;
	OggReader reader(callbacks);
	OkOrError result = reader.full_read(args.ogg_filename);
	if(result.is_error_) {
		std::cerr << "error: " << result.err_msg_ << std::endl;
		return 1;
	}
	std::cout << "ok" << std::endl;
	std::cout << "Ogg total packets count: " << reader.packet_counts_ << std::endl;
	return 0;
}
