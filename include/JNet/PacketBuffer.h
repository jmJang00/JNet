#pragma once
#include <JCore/CTlsMemoryPool.h>

class PacketBuffer
{
public:
	friend class Serializer;
	friend class Session;

	PacketBuffer();

	~PacketBuffer();

	PacketBuffer(const PacketBuffer&) = delete;

	void Clear()
	{
		_refCnt = 0;
		_packetType = 0;
		_hasHeader = false;
	}

	void AddRef();

	bool Release();

	static PacketBuffer* Alloc()
	{
		//PROFILER(L"PacketBuffer::Alloc");
		PacketBuffer* buffer = _pool.Alloc();
		buffer->Clear();
		return buffer;
	}

	static int GetPoolCount()
	{
		return _pool.GetPoolCount();
	}

	static int GetAllocCount()
	{
		return _pool.GetAllocCount();
	}

public:
	int _packetType;

private:

	static void Free(PacketBuffer* packetBuffer)
	{
		//PROFILER(L"PacketBuffer::Free");
		_pool.Free(packetBuffer);
	}

	char _buffer[1024];
	bool _hasHeader;
	long _refCnt;
	static CTlsMemoryPool<PacketBuffer> _pool;
};