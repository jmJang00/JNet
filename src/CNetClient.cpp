#include "pch.h"
#include <JCore/CThread.h>
#include <JCore/SLog.h>
#include <JCore/Profiler.h>
#include <JNet/NetworkProfile.h>
#include <JNet/CNetClient.h>
#include <JNet/CSession.h>
#include <JNet/CPacket.h>
#include <JNet/CWorkerThread.h>
#include <JNet/CLambdaPipe.h>
#include "LogTag.h"

CNetClient::CNetClient(CWorkerThread* worker)
	: _session(nullptr)
	, _worker(worker)
	, _isRunning(0)
	, _encoding(false)
{
	_session = new CSession();
	_clientContext = new CLambdaPipe(worker, 1000);
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
		SLOGA(JNetLog::Progress, L"Start Connect");

		SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
		if (sock == INVALID_SOCKET)
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

		int retval = ::bind(sock, (SOCKADDR*)&clientAddr, sizeof(clientAddr));
		if (retval == SOCKET_ERROR)
		{
			ELOGA(JNetLog::Progress, L"bind failed, [ErrorCode]:%d", WSAGetLastError());
			break;
		}

		int size = 0;
		if (setsockopt(sock, SOL_SOCKET, 
			SO_SNDBUF, (char*)&size, sizeof(size)) != 0)
		{
			ELOGA(JNetLog::Progress, L"setsockopt failed, [ErrorCode]:%d", WSAGetLastError());
			break;
		}

		int actual = 0;
		socklen_t len = sizeof(actual);
		getsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char*)&actual, &len);

		if (nagle == false)
		{
			DWORD opt = 1;
			setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char*)&opt, sizeof(opt));
		}

		LINGER linger = { 1, 0 };
		setsockopt(sock, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger));

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
		if (connect(sock, (SOCKADDR*)serverAddr, sizeof(*serverAddr)) == SOCKET_ERROR)
		{
			freeaddrinfo(result);
			ELOGA(JNetLog::Progress, L"connect failed, [ErrorCode]:%d", WSAGetLastError());
			break;
		}

		freeaddrinfo(result);

		_encoding = encoding;

		_session = CreateSession(sock);
		if (_session == nullptr)
		{
			ELOGA(JNetLog::Progress, L"CreateSession(): session is nullptr");
			break;
		}

		OnEnterJoinServer();

		if ((_session->_refCnt & 0x80000000) == 0)
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
	while (_isRunning)
	{
		Sleep(100);
	}

	_clientContext->ClearPendingTasks();

	_encoding = false;
}

CSession* CNetClient::CreateSession(SOCKET sock)
{
	char ip[16] = { 0 };
	SOCKADDR_IN clientAddr;
	int addrLen = sizeof(clientAddr);
	getpeername(sock, (SOCKADDR*)&clientAddr, &addrLen);
	inet_ntop(AF_INET, &clientAddr.sin_addr, ip, 16);
	unsigned short port = ntohs(clientAddr.sin_port);

	FSessionId sessionId;
	sessionId.total = 0;

	if (!_worker->Register(_session, sock))
	{
		return nullptr;
	}

	_session->Start(sock, _worker, this, nullptr, sessionId, _encoding);

	_isRunning = true;

	return _session;
}

bool CNetClient::ReleaseSession(CSession* session)
{
	OnLeaveServer();
	session->Reset();
	_isRunning = false;
	return true;
}

bool CNetClient::IsConnected()
{ 
	return _isRunning; 
}

CLambdaPipe* CNetClient::GetClientContext()
{
	return _clientContext;
}

bool CNetClient::Disconnect(FSessionId sessionId)
{
	if (!_session->AddRef())
	{
		_session->ReleasePost();
		return false;
	}

	if (_session->_id.total != sessionId.total || InterlockedExchange8(&_session->_disconnect, 1) == 1)
	{
		_session->ReleasePost();
		return false;
	}

	InterlockedExchange8(&_session->_invalid, 1);
	CancelIoEx((HANDLE)_session->_sock, (OVERLAPPED*)_session->_recvOverlapped);
	CancelIoEx((HANDLE)_session->_sock, (OVERLAPPED*)_session->_sendOverlapped);

	_session->ReleasePost();
	return true;
}

bool CNetClient::SendPacket(CPacketBuffer* packet)
{
	SMPL_PROFILER(NET_PROFILE_SEND_PACKET);
	if (!_session->AddRef())
	{
		_session->ReleasePost();
		return false;
	}

	if (_session->_invalid == 1)
	{
		_session->ReleasePost();
		return false;
	}

	packet->AddRef();

	if (!_session->_sendBuf->Enqueue(packet))
	{
		OnError(ENetError::SEND_BUFFER_LIMIT_REACHED, "SendPacket(): Packet count exceeds send buffer limit");
		_session->ReleasePost();
		packet->Release();
		Disconnect(_session->_id);
		return false;
	}

	if (InterlockedExchange8(&_session->_sending, 1) == 0)
	{
		_worker->PostStatus((ULONG_PTR)_session, CWorkerThread::sSendStartOverlapped);
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

	Disconnect(_session->_id);
	
	Clear();

	return true;
}

