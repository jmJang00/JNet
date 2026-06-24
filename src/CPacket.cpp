#include "pch.h"
#include <stdlib.h>
#include <memory.h>
#include <JNet/CPacket.h>

CTlsMemoryPool<CPacket> CPacket::_pool(PoolRegistry::RegisterDebugSign("CPacket"), true);

void CPacket::Encode(unsigned char randomKey)
{
	unsigned char* start = (unsigned char*)_buffer + _startPos + offsetof(FHeaderEx, checkSum);
	unsigned char* end = (unsigned char*)_buffer + _headPos;
	unsigned char* checksum = start;
	unsigned char* d = start + 1;
	while (d < end)
	{
		*checksum = (*checksum + *d) & (unsigned char)0xFF;
		++d;
	}

	d = checksum;
	unsigned char p = 0;
	unsigned char cnt = 1;
	unsigned char e = 0;
	while (d < end)
	{
		p = *d ^ (p + randomKey + cnt);
		*d = p ^ (e + PacketHeader::SERVER_STATIC_KEY + cnt);
		e = *d;
		++d;
		++cnt;
	}
}

void CPacket::Commit()
{
	if (_hasHeader)
		return;

	if (_encoding)
	{
		FHeaderEx* header = (FHeaderEx*)GetBufferPtr();
		header->code = PacketHeader::SERVER_CODE;
		header->len = (unsigned short)(GetBufferSize() - sizeof(*header));
		header->rkey = rand() & 0xFF;
		header->checkSum = 0;

		Encode(header->rkey);
	}
	else
	{
		FHeader* header = (FHeader*)GetBufferPtr();
		header->size = (unsigned short)(GetBufferSize() - sizeof(*header));
	}

	_hasHeader = true;

	((CPacketBuffer*)_buffer)->SetWritePos(_headPos);
}
