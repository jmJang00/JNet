#pragma once
#include <string>
#include <list>
#include <iterator>
#include <JCore/CTlsMemoryPool.h>
#include <JNet/CPacketBuffer.h>
#include <JNet/PacketHeader.h>
#include <JNet/CPacketView.h>
#include <JNet/CStreamWriter.h>

class CRingBuffer;

class CPacket : public CStreamWriter<CPacket>
{
public:

	CPacket()
		: CStreamWriter(nullptr, 0, 0)
		, _packetBuffer(nullptr)
		, _hasHeader(false)
		, _encoding(false)
		, _startPos(0)
	{
		CRASH(true);
	}

	CPacket(bool encoding, CPacketBuffer* buffer = nullptr)
		: CStreamWriter(nullptr, 0, 0)
		, _hasHeader(false)
		, _encoding(encoding)
		, _startPos(0)
	{
		if (buffer == nullptr)
		{
			_packetBuffer = CPacketBuffer::Alloc();
		}
		else
		{
			_packetBuffer = buffer;
		}

		_buffer = _packetBuffer->_buffer;
		_startPos = _packetBuffer->_writePos;
		_headPos = _packetBuffer->_writePos;
		_endPos = sizeof(_packetBuffer->_buffer);

		if (encoding)
		{
			MoveWritePos(sizeof(FHeaderEx));
		}
		else
		{
			MoveWritePos(sizeof(FHeader));
		}
		_packetBuffer->AddRef();
	}

	CPacket(const CPacket& serializer)
		: CStreamWriter(serializer._buffer, serializer._headPos, serializer._endPos)
		, _packetBuffer(serializer._packetBuffer)
		, _encoding(serializer._encoding)
		, _hasHeader(serializer._hasHeader)
		, _startPos(0)
	{
		_packetBuffer->AddRef();
	}

	~CPacket()
	{
		Clear();
		_packetBuffer->Release();
	}

	unsigned short GetBufferSize() { return _headPos - _startPos; }
	char* GetBufferPtr() { return _buffer + _startPos; }

	unsigned short GetWritePos() { return _headPos; }
	void MoveWritePos(unsigned short size) { _headPos += size; }

	CPacketBuffer* GetPacketBuffer() { return _packetBuffer; }

	CPacket& operator=(const CPacket& sourcePacket);

	void Clear();

	int GetLastError() { return _err; }
	void SetLastError(int err) { _err = err; }

	template <typename... Args>
	static CPacket* Alloc(Args&&... args) { return _pool.Alloc(std::forward<Args>(args)...); }
	static void Free(CPacket* serializer) { _pool.Free(serializer); }

	static int GetPoolCount() { return _pool.GetPoolCount(); }
	static int GetAllocCount() { return _pool.GetAllocCount(); }

	void Encode(unsigned char randomKey);
	void Commit();

protected:
	CPacketBuffer* _packetBuffer;
	unsigned short _startPos;
	bool _hasHeader;
	bool _encoding;

protected:
	static CTlsMemoryPool<CPacket> _pool;
	static CRWLock _poolLock;
};

