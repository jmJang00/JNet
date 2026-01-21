#include "pch.h"
#include <JNet/CContent.h>
#include <JNet/CTimerThread.h>
#include <JNet/SystemMessage.h>
#include <JNet/CTimerManager.h>
#include <JNet/CAppServer.h>
#include "LogTag.h"

CTimerThread::CTimerThread(CAppServer* server)
	: CThread([this]() { TimerThread(); }, 1)
	, _minMs(INFINITE)
{
	_server = server;
	_hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	_hShutdownEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	Create(true);
}

void CTimerThread::Enqueue(FTimerNode* node)
{
	_lock.Lock();
	_timerQueue.push(node);
	uint64 minMs = _timerQueue.top()->reserveMs;
	if (minMs < _minMs)
	{
		_minMs = minMs;
		_lock.Unlock();
		SetEvent(_hEvent);
	}
	else
	{
		_lock.Unlock();
	}
}

uint64 CTimerThread::GetCurrentTick64()
{
    static int64_t frequency = 0;
    if (frequency == 0)
    {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        frequency = freq.QuadPart;
    }

    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);

    return (uint64_t)((counter.QuadPart * 1000) / frequency);
}

void CTimerThread::Shutdown()
{
	SetEvent(_hShutdownEvent);
}

void CTimerThread::TimerThread()
{
	uint64 tick;
	FTimerNode* top;
	unsigned int deltaTick = INFINITE;
	CTimerManager* timerMng = _server->Timer();
	HANDLE handles[2] = { _hShutdownEvent, _hEvent };

	while (1)
	{
		DWORD ret = WaitForMultipleObjects(2, handles, false, deltaTick);
		if (ret == WAIT_OBJECT_0)
		{
			break;
		}

		while (1)
		{
			{
				CCSGuard gurad(&_lock);

				if (_timerQueue.size() <= 0)
				{
					_minMs = INFINITE;
					deltaTick = INFINITE;
					break;
				}

				tick = GetCurrentTick64();
				top = _timerQueue.top();
				if (top->reserveMs > tick)
				{
					_minMs = top->reserveMs;
					deltaTick = (unsigned int)(top->reserveMs - tick);
					break;
				}

				_timerQueue.pop();
			}

			CContent* content = top->content;
			if (InterlockedExchange(&top->active, 0) == 1)
			{
				CInternalSession* context = content->GetContext();
				context->PostTask(top->lambda);
				context->PostLambda([content]()
					{
						content->Release();
						content->_memoryLog[InterlockedIncrement(&content->_index) % 100] = "Reserve Post Release";
					});
			}

			timerMng->Free(top);
		} 
	}
}
