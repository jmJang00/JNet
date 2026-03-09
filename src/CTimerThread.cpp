#include "pch.h"
#include <JNet/CContent.h>
#include <JNet/CTimerThread.h>
#include <JNet/CTimerManager.h>
#include <JNet/CAppServer.h>
#include <JNet/CContentQueue.h>
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

bool CTimerThread::Enqueue(FTimerNode* node)
{
	_lock.Lock();
	if (GetContextPtr()->_bShutdown)
	{
		_lock.Unlock();
		return false;
	}

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

	return true;
}

void CTimerThread::Clear()
{
	CTimerManager* timerMng = _server->TimerMng();
	CCSGuard gurad(&_lock);
	while (_timerQueue.size())
	{
		FTimerNode* top = _timerQueue.top();
		_timerQueue.pop();
		timerMng->CancelPost(top->handle);
		timerMng->Free(top);
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
	CCSGuard gurad(&_lock);
	CThread::Shutdown();
	SetEvent(_hShutdownEvent);
}

void CTimerThread::TimerThread()
{
	uint64 tick;
	FTimerNode* top;
	unsigned int deltaTick = INFINITE;
	CTimerManager* timerMng = _server->TimerMng();
	HANDLE handles[2] = { _hShutdownEvent, _hEvent };

	while (1)
	{
		DWORD ret = WaitForMultipleObjects(2, handles, false, deltaTick);
		if (ret == WAIT_OBJECT_0)
		{
			Clear();
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
				CContentQueue* context = content->GetContext();
				context->PostJob(top->lambda);
				context->PostJob(&CContent::Release);
			}

			timerMng->Free(top);
		} 
	}
}
