#pragma once
#include <map>
#include <vector>
#include <stack>
#include <array>
#include <JCore/CLockFreeStack.h>
#include <JCore/CLockFreeQueue.h>
#include <JNet/CSession.h>
#include <JNet/INetworkEntity.h>
#include <JNet/PacketHeader.h>
#include <JNet/CWorkerThread.h>
#include <JNet/CMonitorThread.h>

struct FServerConfig
{
	std::string serverIp;
	std::string serverPort;
	int workerThreadNum = 0;
	int concurrentThreadNum = 0;
	int maxSessions = 0;
	int sendBufZero = 0;
	int encoding = 0;
	int nagle = 0;
};

struct ServerMetrics
{
	int acceptTPS = 0;
	float acceptUsage = 0;
	int recvMessageTPS = 0;
	int sendMessageTPS = 0;
	int disconnectCnt = 0;
	int acceptTotal = 0;
	int recvBytes = 0;
	int sendBytes = 0;
	int sessionCnt = 0;
};

class CSession;
class CPacketBuffer;
class CMonitorThread;
class CLambdaPipe;
class CPacketView;
class CMonitorTable;

class CNetServer : public INetworkEntity
{
public:
	friend class CMonitorThread;
	friend class CSession;

	CNetServer(int concurrentThreadCnt, int totalThreadCnt, int maxSession);
	virtual ~CNetServer();
	
	bool Start(const char* ip, const char* port, bool nagle, bool encoding, bool sendBufZero);
	virtual void Stop();

	virtual bool Disconnect(FSessionId sessionId);
	bool SendPacket(FSessionId sessionId, CPacketBuffer* message);
	bool SendPacketUnsafe(FSessionId sessionId, CPacketBuffer* message);
	bool SendPacketMultiCast(FSessionId* group, int count, CPacketBuffer* message);

	virtual bool OnConnectionRequest(const wchar_t* ip, unsigned short port) = 0;
	virtual void OnAccept(FSessionId sessionId, const wchar_t* ip, unsigned short port, void*& userData) = 0;
	virtual void OnRelease(FSessionId sessionId, void* userData) = 0;
	virtual void OnRecv(FSessionId sessionId, CPacketView* packet) = 0;
	virtual void OnError(ENetError errCode, const char* errMsg);
	virtual void OnPrintExternal(CMonitorTable* table);
	virtual void OnCollectExternal(MetricsCollector& collector);

	void AddWorkerObserver(IWorkerObserver* obs);
	CLambdaPipe* GetServerContext();
	CSession* CreateSession(SOCKET sock) override;
	bool ReleaseSession(CSession* session) override;
	int GetSessionCount() { return _sessionCnt; }

	long GetAcceptTPS() 
	{ 
		long acceptTps = InterlockedExchange(&_acceptCnt, 0);
		InterlockedAdd(&_acceptTotal, acceptTps);
		return acceptTps; 
	}

	long GetAcceptTotal() { return _acceptTotal; }
	long GetRecvMessageTPS() { return (_worker) ? _worker->GetRecvMessageCnt() : 0; }
	long GetSendMessageTPS() { return (_worker) ? _worker->GetSendMessageCnt() : 0; }
	long GetRecvBytes() { return (_worker) ? _worker->GetRecvBytes() : 0; }
	long GetSendBytes() { return (_worker) ? _worker->GetSendBytes() : 0; }
	virtual void RefreshStatistics() { if (_worker) { _worker->RefreshStatistics(); } }

	long GetDisconnectCount() { return _disconnectTotal; }

	void* GetUserData(FSessionId id);

	CSession* GetSession(FSessionId id)
	{
		CSession* session = &_sessions[id.internal.idx];
		if (!session->AddRef())
		{
			session->ReleasePost();
			return nullptr;
		}

		if (session->_id != id)
		{
			session->ReleasePost();
			return nullptr;
		}

		return session;
	}

	bool FreeSession(CSession* session) { return session->ReleasePost(); }

	CWorkerThread* Worker() { return _worker; }

public:
	bool _encoding;

protected:
	char _isRunning;
	CWorkerThread* _worker;
	CLambdaPipe* _serverContext;
	ServerMetrics* _serverMetrics;
	long _sessionCnt;
	int _maxSession = 0;
	unsigned int _nextId;
	CLockFreeStack<int> _sessionIndexStack;
	std::vector<CSession> _sessions;
	std::vector<IWorkerObserver*> _workerObservers;
	unsigned int _prevTime;

private:
	void AcceptThread();

	SOCKET _listenSock;
	long _acceptTotal;
	long _acceptCnt;
	long _disconnectTotal;
	CThread* _acceptor;
};
