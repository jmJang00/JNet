#pragma once
#include <JCore/CTlsMemoryPool.h> 

class CPacketBuffer
{
public:
	friend class CPacket;
	friend class CPacketView;
	friend class CSession;

	CPacketBuffer();

	~CPacketBuffer();

	CPacketBuffer(const CPacketBuffer&) = delete;

	void Clear()
	{
		_refCnt = 0;
		_readPos = 0;
		_writePos = 0;
	}

	void AddRef()
	{
		InterlockedIncrement(&_refCnt);
	}

	bool Release()
	{
		if (InterlockedDecrement(&_refCnt) == 0)
		{
			Clear();
			Free(this);
			return true;
		}
		return false;
	}

	char* GetBufferPtr() { return _buffer; }
	unsigned short GetBufferSize() { return (unsigned short)sizeof(_buffer); }

	char* GetDataPtr() { return _buffer + _readPos; }
	unsigned short GetDataSize() { return _writePos - _readPos; }
	unsigned short GetFreeSize() { return (unsigned short)sizeof(_buffer) - _writePos; }

	unsigned short GetWritePos() { return _writePos; }
	unsigned short GetReadPos() { return _readPos; }

	void SetWritePos(unsigned short writePos) { _writePos = writePos; }
	void SetReadPos(unsigned short readPos) { _readPos = readPos; }

	void MoveWritePos(unsigned short size) { _writePos += size; }
	void MoveReadPos(unsigned short size) { _readPos += size; }

	static CPacketBuffer* Alloc()
	{
		CPacketBuffer* buffer = _pool.Alloc();
		buffer->Clear();
		return buffer;
	}

	static void Free(CPacketBuffer* packetBuffer) { _pool.Free(packetBuffer); }

	static int GetPoolCount() { return _pool.GetPoolCount(); }
	static int GetAllocCount() { return _pool.GetAllocCount(); }

private:

	char _buffer[512];
	unsigned short _readPos;
	unsigned short _writePos;
	long _refCnt;
	static CTlsMemoryPool<CPacketBuffer> _pool;
};