#pragma once
#include <unordered_map>
#include <array>
#include <utility>
#include <JNet/Session.h>
#include <JNet/CInternalSession.h>
#include <JCore/CLockFreeQueue.h>
#include <JNet/CAppServer.h>
#include <JNet/CTimerThread.h>
#include <JNet/CTimerManager.h>
#include <JNet/SystemMessage.h>

class CInternalSession;
class CWorkerThread;
class CAppServer;

class CContent
{
public:
	friend class CContentManager;

	CContent(CAppServer* server, int frameMs);
	~CContent();
	virtual void OnRegister() = 0;
	virtual void OnUnregister() = 0;
	virtual void OnTick(int deltaTime) = 0;
	virtual void OnRecv(SessionId sessionId, Serializer* packet) = 0;
	virtual void OnEnter(SessionId sessionId, void* userData) = 0;
	virtual void OnLeave(SessionId sessionId, void*& userData) = 0;

	template <typename Lambda>
	bool Execute(Lambda&& func)
	{
		if (_shutdown)
		{
			return false;
		}

		_memoryLog[InterlockedIncrement(&_index) % 100] = "Execute AddRef";
		if (!AddRef())
		{
			Release();
			_memoryLog[InterlockedIncrement(&_index) % 100] = "Execute Release";
			return false;
		}

		_context->PostLambda(std::forward<Lambda>(func));
		_context->PostLambda([this]()
			{
				Release();
				_memoryLog[InterlockedIncrement(&_index) % 100] = "Execute Post Release";
			});

		_memoryLog[InterlockedIncrement(&_index) % 100] = "Execute Post Success";

		return true;
	}

	bool Enqueue(FSystemMessage* msg)
	{
		return _updateQ.Enqueue(*msg);
	}

	CInternalSession* GetContext()
	{
		return _context;
	}

	bool AddRef();
	bool Release();
	void WaitStartEvent();
	void WaitStopEvent();
	void BeginShutdown();
	void EndShutdown();

protected:
	template <typename Lambda>
	bool Reserve(int ms, Lambda&& lambda)
	{
		if (_shutdown)
		{
			return false;
		}

		_memoryLog[InterlockedIncrement(&_index) % 100] = "Reserve AddRef";
		if (!AddRef())
		{
			Release();
			_memoryLog[InterlockedIncrement(&_index) % 100] = "Reserve Release";
			return false;
		}

		FTimerHandle handle = _server->Timer()->PostAfterInternal(ms, this, std::forward<Lambda>(lambda));
		if (handle.handle == FTimerHandle::INVALID_HANDLE)
		{
			Release();
			_memoryLog[InterlockedIncrement(&_index) % 100] = "Reserve Release Post Fail";
			return false;
		}

		_memoryLog[InterlockedIncrement(&_index) % 100] = "Reserve Post Success";

		_timers.push_back(handle);

		return true;
	}

	void ClearInvalidHandles();

public:
	std::array<const char*, 100> _memoryLog;
	long _index = -1;

private:
	void Update();
	void Start();
	void Stop();

	CAppServer* _server;
	CInternalSession* _context;
	std::vector<FTimerHandle> _timers;
	HANDLE _startEvent;
	HANDLE _endEvent;
	bool _shutdown;
	unsigned int _frameTick;
	unsigned int _oldTick;
	int _frameMs;
	long _timerRefCnt;
	CLockFreeQueue<FSystemMessage> _updateQ;
	std::unordered_map<SessionId, Session*> _sessionMap;
};
