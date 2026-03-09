#include "pch.h"
#include <stdlib.h>
#include <memory.h>
#include <JNet/CPacket.h>

CTlsMemoryPool<CPacket> CPacket::_pool(PoolRegistry::RegisterDebugSign("CPacket"), true);
CRWLock CPacket::_poolLock;

CPacket& CPacket::operator=(const CPacket& clSrcPacket)
{
	_packetBuffer->Release();
	_packetBuffer = clSrcPacket._packetBuffer;
	_packetBuffer->AddRef();
	_endPos = clSrcPacket._endPos;
	_headPos = clSrcPacket._headPos;
	return *this;
}

void CPacket::Clear()
{
	_err = Default;
	_endPos = 0;
	_headPos = 0;
	_buffer = nullptr;
	_hasHeader = false;
	_encoding = false;
}

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

	_packetBuffer->SetWritePos(_headPos);
}
