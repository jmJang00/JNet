#include "pch.h"
#include <stdlib.h>
#include <memory.h>
#include <JNet/CPacketView.h>
#include <JNet/CPacket.h>

CPacketView& CPacketView::operator=(const CPacketView& clSrcPacket)
{
	_packetBuffer = clSrcPacket._packetBuffer;
	_endPos = clSrcPacket._endPos;
	_headPos = clSrcPacket._headPos;
	return *this;
}

void CPacketView::Clear()
{
	_err = Default;
	_endPos = 0;
	_headPos = 0;
}

bool CPacketView::Decode(unsigned char randomKey)
{
	unsigned char* start = (unsigned char*)_packetBuffer->_buffer + _headPos + offsetof(FHeaderEx, checkSum);
	unsigned char* end = (unsigned char*)_packetBuffer->_buffer + _endPos;

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
