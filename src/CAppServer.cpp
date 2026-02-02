#include "pch.h"
#include <JCore/SLog.h>
#include <JNet/CAppServer.h>
#include <JNet/CTimerManager.h>
#include <JNet/CContentManager.h>
#include "LogTag.h"

CAppServer::CAppServer(int concurrentThreadCnt, int totalThreadCnt, int maxSession)
	: CNetServer(concurrentThreadCnt, totalThreadCnt, maxSession)
{
	_timerMng = new CTimerManager(this, 20000, 3);
	_contentMng = new CContentManager(this, 1000);
}

CAppServer::~CAppServer()
{
	delete _timerMng;
	delete _contentMng;
}

bool CAppServer::Start(const char* ip, const char* port, bool nagle, bool encoding, 
		bool sendBufZero)
{
	do
	{
		_timerMng->Start();

		if (!CNetServer::Start(ip, port, nagle, encoding, sendBufZero))
		{
			return false;
		}

		return true;

	} while (0);

	Stop();

	return false;
}

void CAppServer::Stop()
{
	CNetServer::Stop();

	_timerMng->Stop();
}

Session* CAppServer::CreateSession(SOCKET sock)
{
	if (_sessionCnt >= _maxSession)
	{
		DLOGA(JNetLog::Network, L"max session reached");
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

		session->Start(sock, _worker->_hIOCP, this, _contentMng, sessionId, _encoding);

		InterlockedIncrement(&_sessionCnt);

		return session;
	}

	return nullptr;
}
