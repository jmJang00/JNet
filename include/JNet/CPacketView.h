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
		, _recvTime(0)
	{
		CRASH(true);
	}

	CPacketView(CPacketBuffer* buffer, int readPos, int writePos)
		: CStreamReader<CPacketView>(buffer->_buffer, readPos, writePos)
		, _recvTime(0)
	{
	}

	CPacketView(const CPacketView& view)
		: CStreamReader<CPacketView>(view._buffer, view._headPos, view._endPos)
		, _recvTime(view._recvTime)
	{
	}

	~CPacketView()
	{
		Clear();
	}

	CPacketView& operator=(const CPacketView& packet)
	{
		_buffer = packet._buffer;
		_endPos = packet._endPos;
		_headPos = packet._headPos;
		_recvTime = packet._recvTime;
		return *this;
	}

	void Clear()
	{
		_buffer = nullptr;
		_endPos = 0;
		_headPos = 0;
		_recvTime = 0;
		_err = Default;
	}

	unsigned short GetReadPos() { return _headPos; }
	void MoveReadPos(unsigned int size) { _headPos += size; }

	unsigned short GetBufferSize() { return _endPos - _headPos; }
	char* GetBufferPtr() { return _buffer + _headPos; }

	CPacketBuffer* GetPacketBuffer() { return (CPacketBuffer*)_buffer; }

	bool Decode(unsigned char randomKey);

	template <typename... Args>
	static CPacketView* Alloc(Args&&... args) 
	{ 
		CPacketView* view = _pool.Alloc(std::forward<Args>(args)...); 
		((CPacketBuffer*)view->_buffer)->AddRef();
		return view;
	}

	static void Free(CPacketView* view) 
	{ 
		((CPacketBuffer*)view->_buffer)->Release();
		_pool.Free(view); 
	}

	unsigned int GetReceiveTime() { return _recvTime; }
	void SetReceiveTime(unsigned int recvTime) { _recvTime = recvTime; }

protected:
	unsigned int _recvTime;

private:
	static CTlsMemoryPool<CPacketView> _pool;
};

