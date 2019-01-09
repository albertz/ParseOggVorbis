//
//  Utils.cpp
//  ParseOggVorbis
//
//  Created by Albert Zeyer on 09.01.19.
//  Copyright © 2019 Albert Zeyer. All rights reserved.
//

#include "Utils.hpp"
#include "crctable.h"


uint32_t update_crc(uint32_t crc, uint8_t* buffer, int size) {
	while(size >= 8) {
		crc ^= buffer[0]<<24 | buffer[1]<<16 | buffer[2]<<8 | buffer[3];
		
		crc = crc_lookup[7][ crc>>24      ] ^ crc_lookup[6][(crc>>16)&0xff] ^
		crc_lookup[5][(crc>> 8)&0xff] ^ crc_lookup[4][ crc     &0xff] ^
		crc_lookup[3][buffer[4]     ] ^ crc_lookup[2][buffer[5]     ] ^
		crc_lookup[1][buffer[6]     ] ^ crc_lookup[0][buffer[7]     ];
		
		buffer += 8;
		size -= 8;
	}
	
	while(size--)
		crc = (crc<<8) ^ crc_lookup[0][((crc>>24)&0xff) ^ *buffer++];
	return crc;
}
