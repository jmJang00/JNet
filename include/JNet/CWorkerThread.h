#pragma once
#include <atomic>
#include <vector>
#include <JCore/CThread.h>
#include <JCore/IWorkerObserver.h>
#include <JNet/Types.h>
#include <JNet/CMonitorThread.h>
#include <JNet/CLambdaPipe.h>

class CSession;
class INetworkEntity;
struct FOverlappedEx;

struct WorkerMetrics
{
	double cpuUsage = 0;
};

class CWorkerThread : public CThread
{
public:
	enum
	{
		SEND_START,
		RELEASE_SESSION,
		JOB_POST,
		PIPE_POST,
		CONTENT_POST,
		SEND_POST,
		RECV_POST,
	};

	static const int ASSEMBLE_LIMIT = 5;

	CWorkerThread(int threadCnt, int concurrentThreadCnt);

	virtual ~CWorkerThread();

	void Start(const std::vector<IWorkerObserver*>& observers);

	void Start();

	void Stop();

	void Shutdown();

	template <typename Lambda>
	bool PostJob(Lambda&& func)
	{
		FLambdaTask* task = FLambdaTask::CreateTask<Lambda>(std::forward<Lambda>(func));
		return PostStatus((uintptr_t)task, sPostJobOverlapped);
	}

	bool PostStatus(uintptr_t compKey, FOverlappedEx* ov, unsigned int transferred = 0);

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

public:
	static FOverlappedEx* sSendStartOverlapped;
	static FOverlappedEx* sReleaseSessionOverlapped;
	static FOverlappedEx* sPostMessageOverlapped;
	static FOverlappedEx* sPostContentOverlapped;
	static FOverlappedEx* sPostJobOverlapped;
};
