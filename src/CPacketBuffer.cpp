#include "pch.h"
#include <JNet/CPacketBuffer.h>

CTlsMemoryPool<CPacketBuffer> CPacketBuffer::_pool(PoolRegistry::RegisterDebugSign("CPacketBuffer"), false);

CPacketBuffer::CPacketBuffer()
	: _refCnt(0)
	, _readPos(0)
	, _writePos(0)
{
	memset(_buffer, 0, sizeof(_buffer));
}

CPacketBuffer::~CPacketBuffer()
{
}

