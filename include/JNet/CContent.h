#pragma once
#include <unordered_set>
#include <array>
#include <utility>
#include <JCore/CLockFreeQueue.h>
#include <JCore/ScopedLock.h>
#include <JNet/CAppServer.h>
#include <JNet/CTimerThread.h>
#include <JNet/CTimerManager.h>
#include <JNet/FSystemMessage.h>
#include <JNet/CLambdaPipe.h>
#include <JNet/CSession.h>
#include <JNet/CContentQueue.h>

class CPacketView;
class CLambdaPipe;
class CWorkerThread;
class CAppServer;
class CContentManager;
struct FContentNode;
class CMonitorTable;

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

	CContent(CAppServer* server, int frameMs, bool disconnectOnExit = true);
	~CContent();
	virtual void OnRegister() = 0;
	virtual void OnUnregister() = 0;
	virtual void OnTick(int deltaTime) = 0;
	virtual void OnRecv(FSessionId sessionId, CPacketView* packet) = 0;
	virtual void OnEnter(FSessionId sessionId, void* userData) = 0;
	virtual void OnLeave(FSessionId sessionId, void*& userData) = 0;

	virtual void OnCollectExternal() = 0;
	virtual void OnPrintExternal(CMonitorTable* table) = 0;

	template <typename Lambda>
	bool RequestExternal(Lambda&& func)
	{
		if (_shutdown)
		{
			return false;
		}

		FLambdaTask* task = FLambdaTask::CreateTask(std::forward<Lambda>(func));

		if (!_requestQ.Enqueue(task))
		{
			FLambdaTask::ReleaseTask(task);
			return false;
		}

		return true;
	}

	void MoveSession(FContentHandle handle);

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
	void Enter(CSession* session);
	void Leave(CSession* session);
	void Update();
	void Start();
	void ClearInvalidHandles();
	void ClearRequest();
	void ProcessRequestQueue();
	void ProcessUpdateQueue();
	void DisconnectSession();
	void Reset();

private:
	CAppServer* _server;
	CContentQueue* _context;
	FContentNode* _node;
	HANDLE _startEvent;
	HANDLE _endEvent;
	bool _shutdown;
	bool _disconnectOnExit;
	long _registered;
	unsigned int _frameTick;
	unsigned int _oldTick;
	int _frameMs;
	long _timerRefCnt;
	CLockFreeQueue<FSystemMessage> _updateQ;
	CLockFreeQueue<FTimerHandle> _timers;
	CLockFreeQueue<FLambdaTask*> _requestQ;
	std::unordered_set<FSessionId> _sessions;
};
