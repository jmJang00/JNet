#include "pch.h"
#include <stdlib.h>
#include <memory.h>
#include <JNet/CPacketView.h>
#include <JNet/CPacket.h>



CTlsMemoryPool<CPacketView> CPacketView::_pool("CPacketView", true);

bool CPacketView::Decode(unsigned char randomKey)
{
	unsigned char* start = (unsigned char*)_buffer + _headPos + offsetof(FHeaderEx, checkSum);
	unsigned char* end = (unsigned char*)_buffer + _endPos;

	unsigned char* checksum = start;
	unsigned char* d = start;
	unsigned char p = 0;
	unsigned char np = 0;
	unsigned char e = 0;
	unsigned char cnt = 1;
	while (d < end)
	{
		np = *d ^ (e + PacketHeader::SERVER_STATIC_KEY + cnt);
		e = *d;
		*d = np ^ (p + randomKey + cnt);
		p = np;
		++d;
		++cnt;
	}

	unsigned char compare = 0;
	d = start + 1;
	while (d < end)
	{
		compare = (compare + *d) & (unsigned char)0xFF;
		++d;
	}

	return (*checksum == compare);
}
