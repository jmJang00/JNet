#include "pch.h"
#include <JCore/CThread.h>
#include <JCore/SLog.h>
#include <JNet/CNetClient.h>
#include <JNet/Session.h>
#include <JNet/Serializer.h>
#include <JNet/CWorkerThread.h>
#include <JNet/CInternalSession.h>
#include "LogTag.h"

CNetClient::CNetClient(CWorkerThread* worker)
	: _sock(INVALID_SOCKET)
	, _session(nullptr)
	, _worker(worker)
	, _isRunning(0)
	, _encoding(false)
{
	_session = new Session();
	_clientContext = new CInternalSession(worker, this, 1000);
}

CNetClient::~CNetClient()
{
	delete _session;
}

bool CNetClient::Connect(
	const char* bindingIp, const char* serverIp, 
	const char* serverPort, bool nagle, bool encoding)
{
	if (_isRunning)
	{
		return false;
	}

	do
	{
		SLOGA(JNetLog::Progress, L"Start Connect\n");

		_sock = socket(AF_INET, SOCK_STREAM, 0);
		if (_sock == INVALID_SOCKET)
		{
			ELOGA(JNetLog::Progress, L"socket failed, [ErrorCode]:%d", WSAGetLastError());
			break;
		}

		SOCKADDR_IN clientAddr;
		ZeroMemory(&clientAddr, sizeof(clientAddr));
		clientAddr.sin_family = AF_INET;
		if (inet_pton(AF_INET, bindingIp, &clientAddr.sin_addr.s_addr) != 1)
		{
			ELOGA(JNetLog::Progress, L"inet_pton failed, [ErrorCode]:%d", WSAGetLastError());
			break;
		}
		clientAddr.sin_port = 0;

		int retval = ::bind(_sock, (SOCKADDR*)&clientAddr, sizeof(clientAddr));
		if (retval == SOCKET_ERROR)
		{
			ELOGA(JNetLog::Progress, L"bind failed, [ErrorCode]:%d", WSAGetLastError());
			break;
		}

		int size = 0;
		if (setsockopt(_sock, SOL_SOCKET, 
			SO_SNDBUF, (char*)&size, sizeof(size)) != 0)
		{
			ELOGA(JNetLog::Progress, L"setsockopt failed, [ErrorCode]:%d", WSAGetLastError());
			break;
		}

		int actual = 0;
		socklen_t len = sizeof(actual);
		getsockopt(_sock, SOL_SOCKET, SO_SNDBUF, (char*)&actual, &len);
		//SLOGA(L"setsockopt SO_SNDBUF = %d\n", actual);

		if (nagle == false)
		{
			DWORD opt = 1;
			setsockopt(_sock, IPPROTO_TCP, TCP_NODELAY, (char*)&opt, sizeof(opt));
		}

		LINGER linger = { 1, 0 };
		setsockopt(_sock, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger));
		//SLOGA(L"setsockopt SO_LINGER = (%d, %d)\n", linger.l_linger, linger.l_onoff);

		addrinfo hints{};
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;

		if (_worker == nullptr)
		{
			ELOGA(JNetLog::Progress, L"no worker threads");
			break;
		}

		addrinfo* result = nullptr;
		if (getaddrinfo(serverIp, serverPort, &hints, &result) != 0)
		{
			ELOGA(JNetLog::Progress, L"getaddrinfo failed, [ErrorCode]:%d", WSAGetLastError());
			break;
		}

		SOCKADDR_IN* serverAddr = (SOCKADDR_IN*)result->ai_addr;
		if (connect(_sock, (SOCKADDR*)serverAddr, sizeof(*serverAddr)) == SOCKET_ERROR)
		{
			freeaddrinfo(result);
			ELOGA(JNetLog::Progress, L"connect failed, [ErrorCode]:%d", WSAGetLastError());
			break;
		}

		freeaddrinfo(result);

		_encoding = encoding;

		_clientContext->Resume();

		_session = CreateSession(_sock);
		if (_session == nullptr)
		{
			ELOGA(JNetLog::Progress, L"CreateSession(): session is nullptr");
			break;
		}

		OnEnterJoinServer();

		if ((_session->refCnt & 0x80000000) == 0)
		{
			CRASH(true);
		}

		if (!_session->RecvPost())
		{
			_session->Release();
			break;
		}

		return true;

	} while (0);

	SLOGA(JNetLog::Progress, L"Connect Fail\n");

	Clear();

	return false;
}

void CNetClient::Clear()
{
	_clientContext->Suspend();

	while (_isRunning)
	{
		Sleep(100);
	}

	RefreshStatistics();
	_encoding = false;
}

Session* CNetClient::CreateSession(SOCKET sock)
{
	char ip[16] = { 0 };
	SOCKADDR_IN clientAddr;
	int addrLen = sizeof(clientAddr);
	getpeername(sock, (SOCKADDR*)&clientAddr, &addrLen);
	inet_ntop(AF_INET, &clientAddr.sin_addr, ip, 16);
	unsigned short port = ntohs(clientAddr.sin_port);

	SessionId sessionId;
	sessionId.total = 0;

	if (!_worker->Register(_session, sock))
	{
		return nullptr;
	}

	_session->Start(sock, _worker->_hIOCP, this, sessionId, _encoding);

	_isRunning = true;

	return _session;
}

bool CNetClient::ReleaseSession(Session* session)
{
	session->Reset();
	OnLeaveServer();
	_isRunning = false;
	return true;
}

bool CNetClient::IsConnected()
{ 
	return _isRunning; 
}

CInternalSession* CNetClient::GetClientContext()
{
	return _clientContext;
}

void CNetClient::HandleInternalMessage(CInternalSession* session)
{
	session->Execute();
}

unsigned long long CNetClient::GetRecvMessageTPS()
{
	return _worker->GetRecvMessageCnt();
}

unsigned long long CNetClient::GetSendMessageTPS()
{
	return _worker->GetSendMessageCnt();
}

void CNetClient::RefreshStatistics()
{
	if (_worker)
	{
		_worker->RefreshStatistics();
	}
}

bool CNetClient::Disconnect(SessionId sessionId)
{
	if (!_session->AddRef())
	{
		_session->ReleasePost();
		return false;
	}

	if (_session->id.total != sessionId.total || InterlockedExchange(&_session->disconnect, 1) == 1)
	{
		_session->ReleasePost();
		return false;
	}

	InterlockedExchange(&_session->invalid, 1);
	CancelIoEx((HANDLE)_session->sock, (OVERLAPPED*)_session->recvOverlapped);
	CancelIoEx((HANDLE)_session->sock, (OVERLAPPED*)_session->sendOverlapped);

	_session->ReleasePost();
	return true;
}

bool CNetClient::SendPacket(Serializer* packet)
{
	if (!_session->AddRef())
	{
		_session->ReleasePost();
		return false;
	}

	if (_session->invalid == 1)
	{
		_session->ReleasePost();
		return false;
	}

	Serializer* newMessage = Serializer::Alloc(packet);

	if (!newMessage->HasHeader())
	{
		newMessage->MakeHeader(_encoding);
	}

	if (!_session->sendBuf.Enqueue(newMessage))
	{
		OnError(NetError::SEND_BUFFER_LIMIT_REACHED, "SendPacket(): Packet count exceeds send buffer limit");
		_session->ReleasePost();
		Serializer::Free(newMessage);
		Disconnect(_session->id);
		return false;
	}

	if (InterlockedExchange(&_session->sending, 1) == 0)
	{
		PostQueuedCompletionStatus(_worker->_hIOCP, 0, (ULONG_PTR)_session, (LPOVERLAPPED)CWorkerThread::SEND_START);
	}
	else
	{
		_session->ReleasePost();
	}

	return true;
}

bool CNetClient::Disconnect()
{
	if (!_isRunning)
	{
		return false;
	}

	Disconnect(_session->id);
	
	Clear();

	return true;
}

