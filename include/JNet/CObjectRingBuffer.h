#pragma once
#include <atomic>
#include <JNet/Types.h>

class CObjectRingBuffer
{
public: 
	CObjectRingBuffer(int32 slotCnt, int32 slotSize)
	{
		_buffer = new int8[slotSize * (slotCnt + 1)];
		_capacity = slotSize * (slotCnt + 1);
		_front.store(0);
		_rear.store(0);
		_slotSize = slotSize;
	}

	~CObjectRingBuffer()
	{
		delete[] _buffer;
	}

	void* Reserve()
	{
		if (GetFreeSize() < _slotSize)
		{
			return nullptr;
		}

		return _buffer + _rear.load();
	}

	void Commit()
	{
		int32 rear = _rear.load();
		_rear.store((rear + _slotSize) % _capacity);
	}

	void* Peek()
	{
		if (GetUseSize() < _slotSize)
		{
			return nullptr;
		}

		return _buffer + _front.load();
	}

	void Pop()
	{
		int32 front = _front.load();
		_front.store((front + _slotSize) % _capacity);
	}

	bool8 Empty() const
	{
		return GetUseSize() == 0;
	}

	int32 GetUseSize() const
	{
		int32 front = _front.load();
		int32 rear = _rear.load();
		if (front <= rear)
		{
			return rear - front;
		}
		else
		{
			return _capacity - (front - rear);
		}
	}

	int32 GetFreeSize()
	{
		return _capacity - GetUseSize() - _slotSize;
	}

	int8* _buffer;
	int32 _capacity;
	int32 _slotSize;
	std::atomic<int32> _front;
	std::atomic<int32> _rear;
};
