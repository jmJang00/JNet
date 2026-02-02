#include "pch.h"
#include <cstdio>
#include <atomic>
#include <JCore/SLog.h>
#include <JCore/Profiler.h>
#include <JCore/CTlsMemoryPool.h>
#include <JNet/CNetServer.h>
#include <JNet/CNetClient.h>
#include <JNet/Session.h>
#include <JNet/Serializer.h>
#include <JNet/CInternalSession.h>
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
{
	_worker = new CWorkerThread(totalThreadCnt, concurrentThreadCnt);
	_worker->Start();

	_maxSession = std::min(655335, maxSession);
	_sessions.resize(_maxSession);
	for (int i = 0; i < _maxSession; ++i)
	{
		_sessions[i] = new Session();
	}

	for (int i = _maxSession - 1; i >= 0; --i)
	{
		_sessionIndexStack.push(i);
	}

	_serverContext = new CInternalSession(_worker, 3000);

	_serverMetrics = new ServerMetrics();
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

	for (int i = 0; i < _maxSession; ++i)
	{
		delete _sessions[i];
	}

	while (_sessionIndexStack.size() > 0)
	{
		int arg;
		_sessionIndexStack.pop(&arg);
	}

	_sessions.clear();
	delete _serverContext;
	delete _serverMetrics;
}

bool CNetServer::Start(const char* ip, const char* port, bool nagle = true, 
		bool encoding = false, bool sendBufZero = true)
{
	do
	{
		SLOGA(JNetLog::Progress, L"Start Server\n");

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
			SLOGA(JNetLog::Progress, L"setsockopt SO_SNDBUG = 0\n");
		}

		if (nagle == false)
		{
			DWORD opt = 1;
			setsockopt(_listenSock, IPPROTO_TCP, TCP_NODELAY, (char*)&opt, sizeof(opt));
			SLOGA(JNetLog::Progress, L"setsockopt TCP_NODELAY\n");
		}

		LINGER linger = { 1, 0 };
		setsockopt(_listenSock, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger));
		SLOGA(JNetLog::Progress, L"setsockopt SO_LINGER = (%d, %d)\n", linger.l_linger, linger.l_onoff);

		retval = listen(_listenSock, SOMAXCONN_HINT(_maxSession));
		if (retval == SOCKET_ERROR)
		{
			ELOGA(JNetLog::Progress, L"listen failed, [ErrorCode]:%d", GetLastError());
			break;
		}

		_encoding = encoding;
		InterlockedExchange8(&_isRunning, 1);

		_serverContext->Resume();
		_worker->Start();
		_acceptor->Resume();
		return true;

	} while (0);

	Stop();

	return false;
}

void CNetServer::Stop()
{
	SLOGA(JNetLog::Progress, L"Stop Server\n");

	InterlockedExchange8(&_isRunning, 0);

	_serverContext->Suspend();

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
		if (_sessions[i]->refCnt & 0x80000000)
		{
			Disconnect(_sessions[i]->id);
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

bool CNetServer::Disconnect(SessionId sessionId)
{
	if (sessionId.internal.idx >= (unsigned int)_maxSession)
	{
		return false;
	}

	Session* session = _sessions[sessionId.internal.idx];
	if (!session->AddRef())
	{
		session->ReleasePost();
		return false;
	}

	if (session->id.total != sessionId.total || InterlockedExchange(&session->disconnect, 1) == 1)
	{
		session->ReleasePost();
		return false;
	}

#ifdef SESSION_DEBUG
	session->debug[(InterlockedIncrement(&session->debugIndex)) % 100] = "Disconnect";
#endif

	InterlockedExchange(&session->invalid, 1);
	long disconnectNum = InterlockedIncrement(&_disconnectTotal);
	DLOGA(JNetLog::Network, L"session disconnected %016llX", session->id.total);
	CancelIoEx((HANDLE)session->sock, (OVERLAPPED*)session->recvOverlapped);
	CancelIoEx((HANDLE)session->sock, (OVERLAPPED*)session->sendOverlapped);

	session->ReleasePost();
	return true;
}

bool CNetServer::SendPacket(SessionId sessionId, Serializer* message)
{
	if (sessionId.internal.idx >= (unsigned int)_maxSession)
	{
		return false;
	}

	Session* session = _sessions[sessionId.internal.idx];

	if (!session->AddRef())
	{
		session->ReleasePost();
		return false;
	}

	if (session->id.total != sessionId.total || session->invalid == 1)
	{
		session->ReleasePost();
		return false;
	}

	Serializer* newMessage = Serializer::Alloc(message);
	if (!newMessage->HasHeader())
	{
		newMessage->MakeHeader(_encoding);
	}

	if (!session->sendBuf.Enqueue(newMessage))
	{
		OnError(NetError::SEND_BUFFER_LIMIT_REACHED, "SendPacket(): Packet count exceeds send buffer limit");
		session->ReleasePost();
		Serializer::Free(newMessage);
		Disconnect(sessionId);
		return false;
	}

	if (InterlockedExchange(&session->sendRequest, 1) == 0)
	{
		PostQueuedCompletionStatus(_worker->_hIOCP, 0, (ULONG_PTR)session, (LPOVERLAPPED)CWorkerThread::SEND_START);
	}
	else
	{
		session->ReleasePost();
	}

	return true;
}

bool CNetServer::SendPacketMultiCast(SessionId* group, int count, Serializer* message)
{
	bool success = true;
	for (int i = 0; i < count; i++)
	{
		success &= SendPacket(group[i], message);
	}
	return success;
}

void CNetServer::OnPrintExternal(wchar_t** wstr, size_t* remaining)
{
	ServerMetrics* metrics = _serverMetrics;
	APPEND_FORMAT(*wstr, *remaining, L"-----------------------------------------------------------------------------------------------\n");
	APPEND_FORMAT(*wstr, *remaining, L" %-21s | %-21s | %-21s | %-21s\n",
		L"[Accpet Total]", L"[Recv Bytes]", L"[Send Bytes]", L"[Session]");
	APPEND_FORMAT(*wstr, *remaining, L" %-21d | %-17.3lfKB/s | %-17.3lfKB/s | %-21d\n",
		metrics->acceptTotal, (double)metrics->recvBytes / 1000, (double)metrics->sendBytes / 1000, metrics->sessionCnt);
	APPEND_FORMAT(*wstr, *remaining, L" %-21s | %-21s | %-21s | %-21s\n",
		L"[Accept TPS]", L"[Recv TPS]", L"[Send TPS]", L"[Disconnected]");
	APPEND_FORMAT(*wstr, *remaining, L" %-19d/s | %-19d/s | %-19d/s | %-21d\n",
		metrics->acceptTPS, metrics->recvMessageTPS, metrics->sendMessageTPS, metrics->disconnectCnt);
}

void CNetServer::OnError(NetError errCode, const char* errMsg)
{
	DLOGA(JNetLog::Network, L"%S\n", errMsg);
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
}

CInternalSession* CNetServer::GetServerContext()
{
	return _serverContext;
}

Session* CNetServer::CreateSession(SOCKET sock)
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

		SessionId sessionId;
		sessionId.internal.id = _nextId++;
		sessionId.internal.idx = idx;

		Session* session = _sessions[sessionId.internal.idx];

		if (!_worker->Register(session, sock))
		{
			_sessionIndexStack.push(idx);
			return nullptr;
		}

		session->Start(sock, _worker->_hIOCP, this, nullptr, sessionId, _encoding);
		InterlockedIncrement(&_sessionCnt);

		return session;
	}

	return nullptr;
}

bool CNetServer::ReleaseSession(Session* session)
{
	SessionId id = session->id;
	void* userData = session->user;
	session->Reset();
	OnRelease(id, userData);
	_sessionIndexStack.push(id.internal.idx);
	InterlockedDecrement(&_sessionCnt);

	return true;
}

void CNetServer::AcceptThread()
{
	SLOGA(JNetLog::Progress, L"Accept Thread Start\n");
	srand(GetCurrentThreadId());

	while (1)
	{
		SOCKET clientSock = accept(_listenSock, NULL, NULL);
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

		Session* session = CreateSession(clientSock);
		if (session == nullptr)
		{
			closesocket(clientSock);
			continue;
		}

		InterlockedIncrement(&_acceptCnt);
		void* userData = nullptr;
		OnAccept(session->id, session->ip, session->port, userData);
		session->user = userData;

		if ((session->refCnt & 0x80000000) == 0)
		{
			CRASH(true);
		}

#ifdef SESSION_DEBUG
		session->debug[(InterlockedIncrement(&session->debugIndex)) % 100] = "First Recv Post";
#endif

		if (session->RecvPost())
		{
			if (session->invalid == 1)
			{
				CancelIoEx((HANDLE)session->sock,
					(OVERLAPPED*)session->recvOverlapped);
			}
		}
		else
		{
			session->ReleasePost();
		}
	}

	SLOGA(JNetLog::Progress, L"Accept Thread Exit\n");
}
