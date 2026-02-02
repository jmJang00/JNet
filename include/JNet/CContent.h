#pragma once
#include <unordered_map>
#include <array>
#include <utility>
#include <JNet/Session.h>
#include <JNet/CContentQueue.h>
#include <JCore/CLockFreeQueue.h>
#include <JCore/ScopedLock.h>
#include <JNet/CAppServer.h>
#include <JNet/CTimerThread.h>
#include <JNet/CTimerManager.h>
#include <JNet/SystemMessage.h>
#include <JNet/CInternalSession.h>

class CInternalSession;
class CWorkerThread;
class CAppServer;
class CContentManager;
struct FContentNode;

class CContent
{
public:
	friend class CContentManager;
	friend struct FContentNode;

	enum EContentStatus : long
	{
		Cleared = 0,
		Initializing = 1,
		Running = 2,
		Closing = 4,
	};

	CContent(CAppServer* server, int frameMs);
	~CContent();
	virtual void OnRegister() = 0;
	virtual void OnUnregister() = 0;
	virtual void OnTick(int deltaTime) = 0;
	virtual void OnRecv(SessionId sessionId, Serializer* packet) = 0;
	virtual void OnEnter(SessionId sessionId, void* userData) = 0;
	virtual void OnLeave(SessionId sessionId, void*& userData) = 0;

	template <typename Lambda>
	bool RequestExternal(Lambda&& func)
	{
		if (_shutdown)
		{
			return false;
		}

		FInternalTask* task = FInternalTask::CreateTask(std::forward<Lambda>(func));

		if (!_requestQ.Enqueue(task))
		{
			FInternalTask::ReleaseTask(task);
			return false;
		}

		return true;
	}

public:
	FContentHandle GetHandle();
	void Init(FContentNode* node);
	bool AddRef();
	void Release();
	void WaitStartEvent();
	void WaitStopEvent();
	void BeginShutdown();
	void EndShutdown();
	void Enqueue(FSystemMessage* msg) { _updateQ.ForceEnqueue(*msg); }
	CContentQueue* GetContext() { return _context; }

protected:
	bool Reserve(CContentQueue::Func func);
	bool Reserve(int ms, CContentQueue::Func func);

private:
	void Enter(Session* session);
	void Leave(Session* session);
	void Update();
	void Start();
	void ClearInvalidHandles();
	void ClearRequest();
	void ProcessRequest();
	void ProcessUpdateQueue();
	void ClearSession();
	void Reset();

private:
	CAppServer* _server;
	CContentQueue* _context;
	FContentNode* _node;
	HANDLE _startEvent;
	HANDLE _endEvent;
	bool _shutdown;
	long _registered;
	unsigned int _frameTick;
	unsigned int _oldTick;
	int _frameMs;
	long _timerRefCnt;
	CLockFreeQueue<FSystemMessage> _updateQ;
	CLockFreeQueue<FTimerHandle> _timers;
	CLockFreeQueue<FInternalTask*> _requestQ;
	std::unordered_map<SessionId, Session*> _sessionMap;
};
