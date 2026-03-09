#pragma once
#include <string>
#include <list>
#include <iterator>
#include <JNet/CPacketBuffer.h>
#include <JNet/PacketHeader.h>
#include <JNet/CStreamReader.h>

class CPacketView : public CStreamReader<CPacketView>
{
public:
	CPacketView()
		: CStreamReader<CPacketView>(nullptr, 0, 0)
		, _packetBuffer(nullptr)
		, _recvTime(0)
	{
		CRASH(true);
	}

	CPacketView(CPacketBuffer* buffer, int readPos, int writePos)
		: CStreamReader<CPacketView>(buffer->_buffer, readPos, writePos)
		, _packetBuffer(buffer)
	{
		_recvTime = timeGetTime();
	}

	~CPacketView()
	{
	}

	unsigned short GetReadPos() { return _headPos; }
	void MoveReadPos(unsigned int size) { _headPos += size; }

	unsigned short GetBufferSize() { return _endPos - _headPos; }
	char* GetBufferPtr() { return _buffer + _headPos; }

	CPacketBuffer* GetPacketBuffer() { return _packetBuffer; }

	CPacketView& operator=(const CPacketView& sourcePacket);

	void Clear();

	bool Decode(unsigned char randomKey);

protected:
	CPacketBuffer* _packetBuffer;
	unsigned int _recvTime;
};

