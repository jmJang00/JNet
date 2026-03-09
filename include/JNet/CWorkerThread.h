#pragma once
#include <atomic>
#include <vector>
#include <JCore/CThread.h>
#include <JNet/Types.h>
#include <JNet/CMonitorThread.h>

class CSession;
class INetworkEntity;

struct WorkerMetrics
{
	double cpuUsage = 0;
};

class IWorkerObserver
{
public:
	virtual void OnWorkerStart() = 0;
	virtual ~IWorkerObserver() = default;
};

class CWorkerThread : public CThread
{
public:
	enum
	{
		SEND_START = 0xfff1,
		RELEASE_SESSION = 0xfff2,
		POST_MESSAGE = 0xfff3,
		POST_CONTENT = 0xfff4,
	};

	static const int ASSEMBLE_LIMIT = 5;

	CWorkerThread(int threadCnt, int concurrentThreadCnt);

	virtual ~CWorkerThread();

	void Start(const std::vector<IWorkerObserver*>& observers);

	void Stop();

	void Shutdown();

	bool PostStatus(uintptr_t compKey, OVERLAPPED* ov, unsigned int transferred = 0);

	bool Register(CSession* session, SOCKET sock);

	void RecvProc(CSession* session);

	void RecvProcDecoding(CSession* session);

	void SendProc(CSession* session);

	void RefreshStatistics()
	{
		InterlockedExchange(&_recvBytes, 0);
		InterlockedExchange(&_sendBytes, 0);
		InterlockedExchange(&_sendMessageCnt, 0);
		InterlockedExchange(&_recvMessageCnt, 0);
	}

	long GetRecvMessageCnt() { return InterlockedExchange(&_recvMessageCnt, 0); }
	long GetSendMessageCnt() { return InterlockedExchange(&_sendMessageCnt, 0); }
	long GetRecvBytes() { return InterlockedExchange(&_recvBytes, 0); }
	long GetSendBytes() { return InterlockedExchange(&_sendBytes, 0); }
	bool8 IsRunning() { return _isRunning; }

	void OnPrintExternal(CMonitorTable* table);
	void OnCollectExternal(MetricsCollector& collector);

	void WorkerThread();

	HANDLE _hIOCP;
	std::atomic<bool8> _isRunning;

	unsigned int _prevTime;
	std::vector<IWorkerObserver*> _workerObservers;
	std::vector<WorkerMetrics> _workerMetrics;
	int _threadCnt;
	long _recvBytes;
	long _sendBytes;
	long _recvMessageCnt;
	long _sendMessageCnt;
	long _chatResCnt;
};
