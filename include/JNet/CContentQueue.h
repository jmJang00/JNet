#pragma once
#include <functional>
#include <utility>
#include <future>
#include <JCore/CLockFreeQueue.h>
#include <JCore/JWindows.h>
#include <JNet/Types.h>
#include <JNet/CWorkerThread.h>

class CContent;

class CContentQueue
{
public:
	using Func = void (CContent::*)(void);

	CContentQueue(CWorkerThread* context, CContent* content, int32 maxTaskCnt)
		: _buffer(maxTaskCnt)
		, _context(context)
		, _processing(0)
		, _content(content)
	{
	}

	~CContentQueue()
	{
		Clear();
	}

	void PostJob(Func func)
	{
		_buffer.ForceEnqueue(func);

		if (InterlockedExchange8(&_processing, 1) == 0)
		{
			_context->PostStatus((uintptr_t)this, (OVERLAPPED*)CWorkerThread::POST_CONTENT);
		}
	}

	void Clear()
	{
		Func func;
		while (_buffer.Dequeue(&func))
		{
		}
	}

	void Execute()
	{
		Func func = nullptr;
		while (_buffer.Dequeue(&func))
		{
			(_content->*func)();
		}

		InterlockedExchange8(&_processing, 0);

		if (_buffer.GetSize() > 0)
		{
			if (InterlockedExchange8(&_processing, 1) == 0)
			{
				_context->PostStatus((uintptr_t)this, (OVERLAPPED*)CWorkerThread::POST_CONTENT);
			}
		}
	}

private:
	CContent* _content;
	CWorkerThread* _context;
	CLockFreeQueue<Func> _buffer;
	int8 _processing;
};


