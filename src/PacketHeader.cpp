#include "pch.h"
#include <JNet/PacketHeader.h>

unsigned char PacketHeader::SERVER_CODE = 0x77;
unsigned char PacketHeader::SERVER_STATIC_KEY = 0x32;

void PacketHeader::SetServerCode(unsigned char byte)
{
	SERVER_CODE = byte;
}

void PacketHeader::SetServerStaticKey(unsigned char byte)
{
	SERVER_STATIC_KEY = byte;
}

