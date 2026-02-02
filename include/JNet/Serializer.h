#pragma once
#include <string>
#include <list>
#include <iterator>
#include <JCore/CTlsMemoryPool.h>
#include <JNet/PacketBuffer.h>
#include <JNet/PacketHeader.h>

class Serializer
{
public:
	enum Type
	{
		None,
		Send,
		Recv,
	};

	enum ErrCode
	{
		Default,
		ErrorSerialize,
		ErrorDeserialize,
		ErrorResize,
	};

	Serializer()
		: _packetBuffer(nullptr)
		, _readPos(0)
		, _writePos(0)
		, _err(Default)
		, _recvTime(0)
	{
		CRASH(true);
	}

	Serializer(PacketBuffer* buffer)
		: _packetBuffer(buffer)
		, _readPos(0)
		, _writePos(0)
		, _err(Default)
		, _recvTime(0)
	{
		_packetBuffer->AddRef();
	}

	Serializer(PacketBuffer* buffer, int readPos, int writePos)
		: _packetBuffer(buffer)
		, _readPos(readPos)
		, _writePos(writePos)
		, _err(Default)
		, _recvTime(0)
	{
		_packetBuffer->AddRef();
	}

	Serializer(const Serializer& serializer)
		: _packetBuffer(serializer._packetBuffer)
		, _readPos(serializer._readPos)
		, _writePos(serializer._writePos)
		, _err(Default)
		, _recvTime(0)
	{
		//TODO: 카피 잘 되는지 테스트
		_packetBuffer->AddRef();
	}


	~Serializer()
	{
		Clear();
		_packetBuffer->Release();
	}

	int	GetBufferSize() { return sizeof(_packetBuffer->_buffer); }

	int	GetDataSize() { return _writePos - _readPos; }

	int GetFreeSize() { return sizeof(_packetBuffer->_buffer) - _writePos; }

	char* GetDataPtr() { return _packetBuffer->_buffer + _readPos; }

	char* GetBufferPtr() { return _packetBuffer->_buffer; }

	PacketBuffer* GetPacketBuffer() { return _packetBuffer; }

	int GetReadPos() { return _readPos; }

	int GetWritePos() { return _writePos; }

	int	MoveWritePos(int size)
	{
		_writePos += size;
		return size;
	}

	int	MoveReadPos(int size)
	{
		_readPos += size;
		return size;
	}

	Serializer& operator=(const Serializer& sourcePacket);

	void Clear();

	//////////////////////////////////////////////////////////////////////////
	// 넣기.
	//////////////////////////////////////////////////////////////////////////
	Serializer& operator<<(char value);
	Serializer& operator<<(unsigned char value);

	Serializer& operator<<(short value);
	Serializer& operator<<(unsigned short value);

	Serializer& operator<<(unsigned int value);
	Serializer& operator<<(int value);

	Serializer& operator<<(unsigned long long value);
	Serializer& operator<<(long long value);

	Serializer& operator<<(float value);
	Serializer& operator<<(double value);

	Serializer& operator<<(std::wstring& value);
	Serializer& operator<<(std::string& value);

	//////////////////////////////////////////////////////////////////////////
	// 빼기.
	//////////////////////////////////////////////////////////////////////////
	Serializer& operator>>(char& value);
	Serializer& operator>>(unsigned char& value);

	Serializer& operator>>(short& value);
	Serializer& operator>>(unsigned short& value);

	Serializer& operator>>(int& value);
	Serializer& operator>>(unsigned int& value);

	Serializer& operator>>(long long& value);
	Serializer& operator>>(unsigned long long& value);

	Serializer& operator>>(float& value);
	Serializer& operator>>(double& value);

	int	GetData(char* dest, int size);
	int	PutData(const char* src, int size);
	int GetLastError();

	static Serializer* Alloc(bool encoding = false)
	{
		//PROFILER(L"Serializer::Alloc");
		Serializer* serializer = _pool.Alloc(PacketBuffer::Alloc());
		if (encoding)
		{
			serializer->MoveWritePos(sizeof(HeaderEx));
		}
		else
		{
			serializer->MoveWritePos(sizeof(Header));
		}

		return serializer;
	}

	static Serializer* Alloc(PacketBuffer* buffer)
	{
		Serializer* serializer = _pool.Alloc(buffer, 0, 0);
		return serializer;
	}

	static Serializer* Alloc(PacketBuffer* buffer, int readPos, int writePos)
	{
		Serializer* serializer = _pool.Alloc(buffer, readPos, writePos);
		return serializer;
	}

	static Serializer* Alloc(Serializer* buffer)
	{
		Serializer* serializer = _pool.Alloc(*buffer);
		return serializer;
	}

	static void Free(Serializer* serializer)
	{
		_pool.Free(serializer);
	}

	static int GetPoolCount()
	{
		return _pool.GetPoolCount();
	}

	static int GetAllocCount()
	{
		return _pool.GetAllocCount();
	}

	void Encode(unsigned char randomKey)
	{
		unsigned char* start = (unsigned char*)_packetBuffer->_buffer + _readPos + offsetof(HeaderEx, checkSum);
		unsigned char* end = (unsigned char*)_packetBuffer->_buffer + _writePos;
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

	bool Decode(unsigned char randomKey)
	{
		unsigned char* start = (unsigned char*)_packetBuffer->_buffer + _readPos + offsetof(HeaderEx, checkSum);
		unsigned char* end = (unsigned char*)_packetBuffer->_buffer + _writePos;

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

	void MakeHeader(bool encoding)
	{
		if (encoding)
		{
			HeaderEx* header = (HeaderEx*)GetBufferPtr();
			header->code = PacketHeader::SERVER_CODE;
			header->len = (unsigned short)(GetDataSize() - sizeof(*header));
			header->rkey = rand() & 0xFF;
			header->checkSum = 0;

			Encode(header->rkey);
		}
		else
		{
			Header* header = (Header*)GetBufferPtr();
			header->size = (unsigned short)(GetDataSize() - sizeof(*header));
		}

		_packetBuffer->_hasHeader = true;
	}

	bool HasHeader()
	{
		return _packetBuffer->_hasHeader;
	}

public:
	unsigned int _recvTime;

protected:
	PacketBuffer* _packetBuffer;
	int _err;
	int _readPos;
	int _writePos;
	static CTlsMemoryPool<Serializer> _pool;
	static CRWLock _poolLock;
};

