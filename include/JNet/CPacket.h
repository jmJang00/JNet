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
			buffer = CPacketBuffer::Alloc();
			_buffer = buffer->_buffer;
		}
		else
		{
			_buffer = buffer->_buffer;
		}

		_startPos = buffer->_writePos;
		_headPos = buffer->_writePos;
		_endPos = sizeof(buffer->_buffer);

		if (encoding)
		{
			MoveWritePos(sizeof(FHeaderEx));
		}
		else
		{
			MoveWritePos(sizeof(FHeader));
		}

		buffer->AddRef();
	}

	CPacket(const CPacket& serializer)
		: CStreamWriter(serializer._buffer, serializer._headPos, serializer._endPos)
		, _encoding(serializer._encoding)
		, _hasHeader(serializer._hasHeader)
		, _startPos(0)
	{
		((CPacketBuffer*)serializer._buffer)->AddRef();
	}

	CPacket& operator=(const CPacket& packet)
	{
		_buffer = packet._buffer;
		((CPacketBuffer*)packet._buffer)->AddRef();
		_endPos = packet._endPos;
		_headPos = packet._headPos;
		_startPos = packet._startPos;
		_hasHeader = packet._hasHeader;
		_encoding = packet._encoding;
		return *this;
	}

	void Clear()
	{
		_err = Default;
		_endPos = 0;
		_headPos = 0;
		_startPos = 0;
		_buffer = nullptr;
		_hasHeader = false;
		_encoding = false;
	}

	~CPacket()
	{
		((CPacketBuffer*)_buffer)->Release();
		Clear();
	}

	unsigned short GetBufferSize() { return _headPos - _startPos; }
	char* GetBufferPtr() { return _buffer + _startPos; }
	unsigned short GetWritePos() { return _headPos; }
	void MoveWritePos(unsigned short size) { _headPos += size; }
	CPacketBuffer* GetPacketBuffer() { return (CPacketBuffer*)_buffer; }
	int GetLastError() { return _err; }

	template <typename... Args>
	static CPacket* Alloc(Args&&... args) { return _pool.Alloc(std::forward<Args>(args)...); }
	static void Free(CPacket* serializer) { _pool.Free(serializer); }

	static int GetPoolCount() { return _pool.GetPoolCount(); }
	static int GetAllocCount() { return _pool.GetAllocCount(); }

	void Encode(unsigned char randomKey);
	bool Encoding() { return _encoding; }
	void Commit();

protected:
	unsigned short _startPos;
	bool _hasHeader;
	bool _encoding;

protected:
	static CTlsMemoryPool<CPacket> _pool;
};

