#include "pch.h"
#include <cstdio>
#include <atomic>
#include <JCore/SLog.h>
#include <JCore/CTlsMemoryPool.h>
#include <JCore/Profiler.h>
#include <JNet/NetworkProfile.h>
#include <JNet/CNetServer.h>
#include <JNet/CNetClient.h>
#include <JNet/CSession.h>
#include <JNet/CPacket.h>
#include <JNet/CLambdaPipe.h>
#include <JNet/CMonitorThread.h>
#include <JNet/CContent.h>
#include "LogTag.h"

CNetServer::CNetServer(int concurrentThreadCnt, int totalThreadCnt, int maxSession = 10000)
	: _worker(nullptr)
	, _acceptor(nullptr)
	, _listenSock(INVALID_SOCKET)
	, _acceptCnt(0)
	, _disconnectTotal(0)
	, _acceptTotal(0)
	, _isRunning(false)
	, _encoding(false)
	, _nextId(1)
	, _sessionCnt(0)
	, _sessionIndexStack(maxSession)
	, _prevTime(0)
{
	_worker = new CWorkerThread(totalThreadCnt, concurrentThreadCnt);

	_maxSession = std::min(655335, maxSession);
	_sessions.resize(_maxSession);

	for (int i = _maxSession - 1; i >= 0; --i)
	{
		_sessionIndexStack.push(i);
	}

	_serverContext = new CLambdaPipe(_worker, 1000);

	_serverMetrics = new ServerMetrics();

	class CNetWorkerObserver : public IWorkerObserver
	{
	public:
		CNetWorkerObserver()
			: _lastFlushTime(GetTickCount64())
		{
		}

		~CNetWorkerObserver() override
		{

		}

		IWorkerObserver* Clone() const
		{
			return new CNetWorkerObserver();
		}

		void OnWorkerEnter()
		{
			CProfiler::Register(NET_PROFILE_ACCEPT, L"Network", L"Accept", 1);
			CProfiler::Register(NET_PROFILE_SEND, L"Network", L"Send", 0.2);
			CProfiler::Register(NET_PROFILE_SEND_START, L"Network", L"SendStart", 0.2);
			CProfiler::Register(NET_PROFILE_RELEASE, L"Network", L"Release", 1);
			CProfiler::Register(NET_PROFILE_RECV, L"Network", L"Recv", 0.2);
			CProfiler::Register(NET_PROFILE_IO_COMPLETE, L"Network", L"IOComplete", 0.2);
			CProfiler::Register(NET_PROFILE_JOB, L"Network", L"Job", 0.2);
			CProfiler::Register(NET_PROFILE_PIPE, L"Network", L"Pipe", 0.2);
			CProfiler::Register(NET_PROFILE_CONTENT, L"Network", L"Content", 0.2);
			CProfiler::Register(NET_PROFILE_SEND_PACKET, L"Network", L"SendPacket", 0.2);
		}

		void OnWorkerEnd()
		{
			uint64_t now = GetTickCount64();

			if (now - _lastFlushTime >= 60000)
			{
				_lastFlushTime = now;
				CProfiler::Flush();
			}
		}

		uint64_t _lastFlushTime;
	};

	AddWorkerObserver(new CNetWorkerObserver());
}

CNetServer::~CNetServer()
{
	if (_isRunning)
		Stop();

	if (_worker != nullptr)
	{
		_worker->Stop();
		delete _worker;
		_worker = nullptr;
	}

	_sessions.clear();

	while (_sessionIndexStack.size() > 0)
	{
		int arg;
		_sessionIndexStack.pop(&arg);
	}

	for (auto obs : _workerObservers)
	{
		delete obs;
	}

	_workerObservers.clear();

	_sessions.clear();
	delete _serverContext;
	delete _serverMetrics;
}

bool CNetServer::Start(const char* ip, const char* port, bool nagle = true, 
		bool encoding = false, bool sendBufZero = true)
{
	do
	{
		SLOGA(JNetLog::Progress, L"Start Server");

		_listenSock = socket(AF_INET, SOCK_STREAM, 0);
		if (_listenSock == INVALID_SOCKET)
		{
			ELOGA(JNetLog::Progress, L"socket failed, [ErrorCode]:%d", GetLastError());
			break;
		}

		addrinfo hints{};
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;

		addrinfo* result = nullptr;
		if (getaddrinfo(ip, port, &hints, &result) != 0)
		{
			ELOGA(JNetLog::Progress, L"getaddrinfo failed, [ErrorCode]:%d", GetLastError());
			break;
		}

		SOCKADDR_IN* serverAddr = (SOCKADDR_IN*)result->ai_addr;
		freeaddrinfo(result);

		int retval = ::bind(_listenSock, (SOCKADDR*)serverAddr, sizeof(*serverAddr));
		if (retval == SOCKET_ERROR)
		{
			ELOGA(JNetLog::Progress, L"bind failed, [ErrorCode]:%d", GetLastError());
			break;
		}

		_acceptor = new CThread([this]() { this->AcceptThread(); });
		if (!_acceptor->Create(true))
		{
			ELOGA(JNetLog::Progress, L"can't create AcceptThread, [ErrorCode]:%d", GetLastError());
			break;
		}

		if (sendBufZero)
		{
			int size = 0;
			setsockopt(_listenSock, SOL_SOCKET, SO_SNDBUF, (char*)&size, sizeof(size));
			SLOGA(JNetLog::Progress, L"setsockopt SO_SNDBUG = 0");
		}

		if (nagle == false)
		{
			DWORD opt = 1;
			setsockopt(_listenSock, IPPROTO_TCP, TCP_NODELAY, (char*)&opt, sizeof(opt));
			SLOGA(JNetLog::Progress, L"setsockopt TCP_NODELAY");
		}

		LINGER linger = { 1, 0 };
		setsockopt(_listenSock, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger));
		SLOGA(JNetLog::Progress, L"setsockopt SO_LINGER = (%d, %d)", linger.l_linger, linger.l_onoff);

		retval = listen(_listenSock, SOMAXCONN_HINT(_maxSession));
		if (retval == SOCKET_ERROR)
		{
			ELOGA(JNetLog::Progress, L"listen failed, [ErrorCode]:%d", GetLastError());
			break;
		}

		_encoding = encoding;
		InterlockedExchange8(&_isRunning, 1);

		_worker->Start(_workerObservers);
		_acceptor->Resume();
		return true;

	} while (0);

	Stop();

	return false;
}

void CNetServer::Stop()
{
	SLOGA(JNetLog::Progress, L"Stop Server");

	InterlockedExchange8(&_isRunning, 0);

	_serverContext->ClearPendingTasks();

	if (_listenSock != INVALID_SOCKET)
	{
		closesocket(_listenSock);
		_listenSock = INVALID_SOCKET;
	}

	if (_acceptor != nullptr)
	{
		_acceptor->Wait();
		_acceptor->Close();
	}

	for (int i = 0; i < _maxSession; ++i)
	{
		if (_sessions[i]._refCnt & 0x80000000)
		{
			Disconnect(_sessions[i]._id);
		}
	}

	while (_sessionIndexStack.size() != _maxSession)
	{
		Sleep(10);
	}

	if (_acceptor != nullptr)
	{
		delete _acceptor;
		_acceptor = nullptr;
	}

	_encoding = false;
}

bool CNetServer::Disconnect(FSessionId sessionId)
{
	if (sessionId.internal.idx >= (unsigned int)_maxSession)
	{
		return false;
	}

	CSession* session = &_sessions[sessionId.internal.idx];
	if (!session->AddRef())
	{
		session->ReleasePost();
		return false;
	}

	if (session->_id.total != sessionId.total || InterlockedExchange8(&session->_disconnect, 1) == 1)
	{
		session->ReleasePost();
		return false;
	}

#ifdef SESSION_DEBUG
	session->debug[(InterlockedIncrement(&session->debugIndex)) % 100] = "Disconnect";
#endif

	InterlockedExchange8(&session->_invalid, 1);
	long disconnectNum = InterlockedIncrement(&_disconnectTotal);
	DLOGA(JNetLog::Network, L"session disconnected %016llX", session->_id.total);
	CancelIoEx((HANDLE)session->_sock, (OVERLAPPED*)session->_recvOverlapped);
	CancelIoEx((HANDLE)session->_sock, (OVERLAPPED*)session->_sendOverlapped);

	session->ReleasePost();
	return true;
}

bool CNetServer::SendPacket(FSessionId sessionId, CPacketBuffer* message)
{
	SMPL_PROFILER(NET_PROFILE_SEND_PACKET);
	if (sessionId.internal.idx >= (unsigned int)_maxSession)
	{
		return false;
	}

	CSession* session = &_sessions[sessionId.internal.idx];

	if (!session->AddRef())
	{
		session->ReleasePost();
		return false;
	}

	if (session->_id.total != sessionId.total || session->_invalid == 1)
	{
		session->ReleasePost();
		return false;
	}

	message->AddRef();

	if (!session->_sendBuf->Enqueue(message))
	{
		OnError(ENetError::SEND_BUFFER_LIMIT_REACHED, "SendPacket(): Packet count exceeds send buffer limit");
		session->ReleasePost();
		message->Release();
		Disconnect(sessionId);
		return false;
	}

	if (InterlockedExchange8(&session->_sending, 1) == 0)
	{
		_worker->PostStatus((ULONG_PTR)session, CWorkerThread::sSendStartOverlapped, 0);
	}
	else
	{
		session->ReleasePost();
	}

	return true;
}

bool CNetServer::SendPacketUnsafe(FSessionId sessionId, CPacketBuffer* message)
{
	CSession* session = &_sessions[sessionId.internal.idx];

	message->AddRef();

	if (!session->_sendBuf->Enqueue(message))
	{
		OnError(ENetError::SEND_BUFFER_LIMIT_REACHED, "SendPacket(): Packet count exceeds send buffer limit");
		session->ReleasePost();
		message->Release();
		Disconnect(sessionId);
		return false;
	}

	if (InterlockedExchange8(&session->_sending, 1) == 0)
	{
		session->AddRef();
		_worker->PostStatus((ULONG_PTR)session, CWorkerThread::sSendStartOverlapped, 0);
	}

	return true;
}

bool CNetServer::SendPacketMultiCast(FSessionId* group, int count, CPacketBuffer* message)
{
	bool success = true;
	for (int i = 0; i < count; i++)
	{
		success &= SendPacket(group[i], message);
	}
	return success;
}

void CNetServer::OnError(ENetError errCode, const char* errMsg)
{
	DLOGA(JNetLog::Network, L"%S", errMsg);
}

void CNetServer::OnCollectExternal(MetricsCollector& collector)
{
	_serverMetrics->acceptTotal = GetAcceptTotal();
	_serverMetrics->acceptTPS = GetAcceptTPS();
	_serverMetrics->disconnectCnt = GetDisconnectCount();
	_serverMetrics->recvBytes = GetRecvBytes();
	_serverMetrics->sendBytes = GetSendBytes();
	_serverMetrics->recvMessageTPS = GetRecvMessageTPS();
	_serverMetrics->sendMessageTPS = GetSendMessageTPS();
	_serverMetrics->sessionCnt = GetSessionCount();

	unsigned int currTime = timeGetTime();
	unsigned int diff = currTime - _prevTime;
	_prevTime = currTime;
	Context* ctxt = (*_acceptor->GetContextBufferPtr());
	_serverMetrics->acceptUsage = (double)ctxt->GetMeasuredTime() / diff * 100;
	_worker->OnCollectExternal(collector);
}

void CNetServer::OnPrintExternal(CMonitorTable* table)
{
	ServerMetrics* metrics = _serverMetrics;
	table->PrintColumnStr(2, L"[Accpet Total]");
	table->PrintColumnStr(2, L"[Recv Bytes]");
	table->PrintColumnStr(2, L"[Send Bytes]");
	table->PrintColumnStr(2, L"[Session]");

	table->PrintColumnFormat(2, L" %d", metrics->acceptTotal);
	table->PrintColumnFormat(2, L"%.3lfKB/s", (double)metrics->recvBytes / 1000);
	table->PrintColumnFormat(2, L"%.3lfKB/s", (double)metrics->sendBytes / 1000);
	table->PrintColumnFormat(2, L"%d", metrics->sessionCnt);

	table->PrintColumnStr(2, L"[Accpet Tps]");
	table->PrintColumnStr(2, L"[Recv Tps]");
	table->PrintColumnStr(2, L"[Send Tps]");
	table->PrintColumnStr(2, L"[Disconnected]");

	table->PrintColumnFormat(2, L"%d/s", metrics->acceptTPS);
	table->PrintColumnFormat(2, L"%d/s", metrics->recvMessageTPS);
	table->PrintColumnFormat(2, L"%d/s", metrics->sendMessageTPS);
	table->PrintColumnFormat(2, L"%d", metrics->disconnectCnt);

	table->PrintDivider();
	table->PrintColumnFormat(1, L"[%u]", (*_acceptor->GetContextBufferPtr())->_threadId);
	table->PrintColumnFormat(1, L"%.2lf", _serverMetrics->acceptUsage);
	table->PrintDivider();
	_worker->OnPrintExternal(table);
}

void CNetServer::AddWorkerObserver(IWorkerObserver* obs)
{
	_workerObservers.push_back(obs);
}

CLambdaPipe* CNetServer::GetServerContext()
{
	return _serverContext;
}

CSession* CNetServer::CreateSession(SOCKET sock)
{
	if (_sessionCnt >= _maxSession)
	{
		DLOGA(JNetLog::Network ,L"max session reached");
		return nullptr;
	}

	wchar_t ip[16] = { 0 };
	SOCKADDR_IN clientAddr;
	int addrLen = sizeof(clientAddr);
	getpeername(sock, (SOCKADDR*)&clientAddr, &addrLen);
	InetNtopW(AF_INET, &clientAddr.sin_addr, ip, 16);
	unsigned short port = ntohs(clientAddr.sin_port);
	if (OnConnectionRequest(ip, port))
	{
		int idx;
		if (!_sessionIndexStack.pop(&idx))
		{
			DLOGA(JNetLog::Network ,L"no session index in the stack");
			return nullptr;
		}

		FSessionId sessionId;
		sessionId.internal.id = _nextId++;
		sessionId.internal.idx = idx;

		CSession* session = &_sessions[sessionId.internal.idx];

		if (!_worker->Register(session, sock))
		{
			_sessionIndexStack.push(idx);
			return nullptr;
		}

		session->Start(sock, _worker, this, nullptr, sessionId, _encoding);
		InterlockedIncrement(&_sessionCnt);

		return session;
	}

	return nullptr;
}

bool CNetServer::ReleaseSession(CSession* session)
{
	FSessionId id = session->_id;
	void* userData = session->_userData;
	session->Reset();
	OnRelease(id, userData);
	_sessionIndexStack.push(id.internal.idx);
	InterlockedDecrement(&_sessionCnt);

	return true;
}

void* CNetServer::GetUserData(FSessionId id)
{
	CSession* session = GetSession(id);
	void* userData = session->_userData;
	FreeSession(session);
	return userData;
}

void CNetServer::AcceptThread()
{
	SLOGA(JNetLog::Progress, L"Accept Thread Start %lu", GetCurrentThreadId());
	for (auto obs : _workerObservers)
	{
		obs->OnWorkerEnter();
	}

	while (1)
	{
		SMPL_PRO_END(ENetworkProfile::NET_PROFILE_ACCEPT);
		for (auto obs : _workerObservers)
		{
			obs->OnWorkerEnd();
		}
		SOCKET clientSock = accept(_listenSock, NULL, NULL);
		SMPL_PRO_BEGIN(ENetworkProfile::NET_PROFILE_ACCEPT);
		if (clientSock == INVALID_SOCKET)
		{
			int errCode = WSAGetLastError();
			if (errCode == WSAEINTR)
			{
				break;
			}

			ELOGA(JNetLog::Network, L"accept() [%d]", errCode);
			break;
		}

		CSession* session = CreateSession(clientSock);
		if (session == nullptr)
		{
			closesocket(clientSock);
			continue;
		}

		InterlockedIncrement(&_acceptCnt);
		OnAccept(session->_id, session->_address->ip, session->_address->port, session->_userData);

		if ((session->_refCnt & 0x80000000) == 0)
		{
			CRASH(true);
		}

		if (session->RecvPost())
		{
			if (session->_invalid == 1)
			{
				CancelIoEx((HANDLE)session->_sock,
					(OVERLAPPED*)session->_recvOverlapped);
			}
		}
		else
		{
			session->ReleasePost();
		}
	}

	for (auto obs : _workerObservers)
	{
		obs->OnWorkerExit();
	}
	SLOGA(JNetLog::Progress, L"Accept Thread Exit %lu", GetCurrentThreadId());
}
