#pragma once
#include <atomic>
#include <JCore/CThread.h>
#include <JNet/Types.h>

class Session;
class INetworkEntity;

class CWorkerThread : public CThread
{
public:
	enum
	{
		SEND_START = 0xfff1,
		RELEASE_SESSION = 0xfff2,
		POST_MESSAGE = 0xfff3,
	};

	CWorkerThread(int threadCnt, int concurrentThreadCnt);

	virtual ~CWorkerThread();

	void Start();

	void Stop();

	void Shutdown();

	bool PostStatus(uintptr_t compKey, OVERLAPPED* ov, unsigned int transferred = 0);

	bool Register(Session* session, SOCKET sock);

	void RecvProc(Session* session);

	void RecvProcDecoding(Session* session);

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

	void WorkerThread();

	HANDLE _hIOCP;
	std::atomic<bool8> _isRunning;

	int _threadCnt;
	long _recvBytes;
	long _sendBytes;
	long _recvMessageCnt;
	long _sendMessageCnt;
};
