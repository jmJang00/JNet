#include "pch.h"
#include <JNet/PacketBuffer.h>

CTlsMemoryPool<PacketBuffer> PacketBuffer::_pool(PoolRegistry::RegisterDebugSign("PacketBuffer"), false);

PacketBuffer::PacketBuffer()
	: _refCnt(0)
	, _hasHeader(false)
{
	memset(_buffer, 0, sizeof(_buffer));
}

PacketBuffer::~PacketBuffer()
{

}

void PacketBuffer::AddRef()
{
	InterlockedIncrement(&_refCnt);
}

bool PacketBuffer::Release()
{
	if (InterlockedDecrement(&_refCnt) == 0)
	{
		Clear();
		Free(this);
		return true;
	}
	return false;
}
