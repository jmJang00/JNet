#include "pch.h"
#include <map>
#include <JCore/SLog.h>
#include <JCore/Profiler.h>
#include <JNet/Session.h>
#include <JNet/CNetServer.h>
#include <JNet/Serializer.h>
#include <JNet/CContent.h>
#include "LogTag.h"

thread_local std::vector<SessionId> gSessionIdVector;

Session::Session()
	: sendOverlapped(nullptr)
	, recvOverlapped(nullptr)
	, owner(nullptr)
	, sendBuf(1000)
	, recvBuf(nullptr)
	, user(nullptr)
	, contentQ(1000)
{
	sendOverlapped = new OverlappedEx();
	recvOverlapped = new OverlappedEx();
	pendingBuffer = new Serializer * [PENDING_BUFFER_SIZE];
}

Session::Session(INetworkEntity* owner)
	: sendOverlapped(nullptr)
	, recvOverlapped(nullptr)
	, owner(owner)
	, sendBuf(1000)
	, recvBuf(nullptr)
	, user(nullptr)
	, contentQ(1000)
{
	sendOverlapped = new OverlappedEx();
	recvOverlapped = new OverlappedEx();
	pendingBuffer = new Serializer * [PENDING_BUFFER_SIZE];
}

Session::~Session()
{
	delete recvOverlapped;
	delete sendOverlapped;
	recvOverlapped = nullptr;
	sendOverlapped = nullptr;
	delete[] pendingBuffer;
	pendingBuffer = nullptr;
}

void Session::Start(SOCKET socket, HANDLE iocp, INetworkEntity* caller, SessionId sessionId, bool crypt)
{
	SOCKADDR_IN clientAddr;
	sock = socket;
	hIOCP = iocp;
	int addrLen = sizeof(clientAddr);
	getpeername(sock, (SOCKADDR*)&clientAddr, &addrLen);
	InetNtopW(AF_INET, &clientAddr.sin_addr, ip, 16);
	port = ntohs(clientAddr.sin_port);
	memset(sendOverlapped, 0, sizeof(*sendOverlapped));
	memset(recvOverlapped, 0, sizeof(*recvOverlapped));
	sendOverlapped->session = this;
	recvOverlapped->session = this;
	user = nullptr;
	assembleCnt = 0;
	InterlockedExchange((uintptr_t*)&content, (uintptr_t)nullptr);
	encoding = crypt;

	sendBuf.Clear();
	owner = caller;
	memset(pendingBuffer, 0, PENDING_BUFFER_SIZE * sizeof(*pendingBuffer));
	recvBuf = Serializer::Alloc(PacketBuffer::Alloc());
	InterlockedExchange(&sending, 0);
	InterlockedAdd(&refCnt, 0x80000001);
	InterlockedExchange(&invalid, 0);
	InterlockedExchange(&disconnect, 0);
	InterlockedExchange(&id.total, sessionId.total);
}

void Session::Reset()
{
	InterlockedExchange(&id.total, Session::INVALID_SESSION_ID);
	InterlockedExchange(&invalid, 1);

	int bufCnt = sendOverlapped->bufCnt;
	for (int i = 0; i < bufCnt; ++i)
	{
		Serializer::Free(pendingBuffer[i]);
		pendingBuffer[i] = nullptr;
	}
	sendOverlapped->bufCnt = 0;

	Serializer* buffer = nullptr;
	while (sendBuf.GetSize() > 0)
	{
		sendBuf.Dequeue(&buffer);
		Serializer::Free(buffer);
	}

	Serializer::Free(recvBuf);
	recvBuf = nullptr;

	while (contentQ.GetSize() > 0)
	{
		contentQ.Dequeue(&buffer);
		Serializer::Free(buffer);
	}
	content = nullptr;
	closesocket(sock);
}

bool Session::SendPost()
{
	int bufCnt = min(sendBuf.GetSize(), PENDING_BUFFER_SIZE);
	int deqCnt = bufCnt;
	Serializer** bufferPtr = &pendingBuffer[0];
	while (deqCnt > 0)
	{
		if (!sendBuf.Dequeue(bufferPtr))
		{
			CRASH(true);
		}

		++bufferPtr;
		--deqCnt;
	}

	WSABUF wsaBuf[PENDING_BUFFER_SIZE];
	int sendSize = 0;
	for (int i = 0; i < bufCnt; i++)
	{
		wsaBuf[i].buf = pendingBuffer[i]->GetDataPtr();
		int dataSize = pendingBuffer[i]->GetDataSize();
		wsaBuf[i].len = dataSize;
		sendSize += dataSize;
	}

	char dummy = 0;
	if (bufCnt == 0)
	{
		//wsaBuf[0].buf = &dummy;
		//wsaBuf[0].len = 0;
		//bufCnt = 1;
		PostQueuedCompletionStatus(hIOCP, 0, (ULONG_PTR)this, (LPOVERLAPPED)CWorkerThread::SEND_START);
		return true;
	}

	//printf("send wsaBuf bufCnt:%d\n", bufCnt);

	sendOverlapped->reqLen = sendSize;
	sendOverlapped->bufCnt = bufCnt;
	memset(&sendOverlapped->overlapped, 0, sizeof(OVERLAPPED));
	//DLOG(L"# ioCnt:%d, sending:%d, ip:%ls, port:%d\n", ioCnt, sending, ip, port);

	//PRO_BEGIN(L"Send");
	int retVal = WSASend(sock, wsaBuf, bufCnt, nullptr, 0, (OVERLAPPED*)sendOverlapped, nullptr);
	//PRO_END(L"Send");

	if (retVal == SOCKET_ERROR)
	{
		int errCode = WSAGetLastError();
		if (errCode != WSA_IO_PENDING)
		{
			if (errCode != WSAECONNRESET && 
				errCode != WSAECONNABORTED)
			{
				SLOGA(JNetLog::Network, L"WSASend() [%d]", errCode);
			}

			InterlockedExchange(&invalid, 1);
			//SLOG(L"# 송신 에러, sessionId:%d, sock:%d, ip:%ls, port:%d\n", id, sock, ip, port);
			return false;
		}
	}

	return true;
}

bool Session::AddRef()
{
	if (InterlockedIncrement(&refCnt) & 0x80000000)
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool Session::Release()
{
	//SLOG("# Decrease IO cnt, iocnt:%d, sessionId:%d, sock:%d, ip:%ls, port:%d\n", ioCnt, id, sock, ip, port);
	if (InterlockedDecrement(&refCnt) == 0x80000000)
	{
		if (InterlockedCompareExchange(&refCnt, 0, 0x80000000) == 0x80000000)
		{
			CContent* con = (CContent*)InterlockedOr((uintptr_t*)&content, (uintptr_t)0);
			if (con == nullptr)
			{
				owner->ReleaseSession(this);
			}
			else
			{
				FSystemMessage msg;
				msg.type = ESystemMessageType::MSG_RELEASE;
				msg.session = this;
				con->Enqueue(&msg);
			}
			return true;
		}
	}

	return false;
}

bool Session::ReleasePost()
{
	//SLOG("# Decrease IO cnt, iocnt:%d, sessionId:%d, sock:%d, ip:%ls, port:%d\n", ioCnt, id, sock, ip, port);
	if (InterlockedDecrement(&refCnt) == 0x80000000)
	{
		if (InterlockedCompareExchange(&refCnt, 0, 0x80000000) == 0x80000000)
		{
			PostQueuedCompletionStatus(hIOCP, 0, (ULONG_PTR)this, (LPOVERLAPPED)CWorkerThread::RELEASE_SESSION);
			return true;
		}
	}

	return false;
}

bool Session::RecvPost()
{
	//PROFILER(L"RecvPost");
	WSABUF wsaBuf;
	wsaBuf.buf = recvBuf->GetDataPtr() + recvBuf->GetDataSize();

	int reqLen = recvBuf->GetFreeSize();
	wsaBuf.len = reqLen;

	//printf("recv wsaBuf %d %d\n", wsaBuf[0].len, wsaBuf[1].len);

	recvOverlapped->reqLen = reqLen;
	memset(&recvOverlapped->overlapped, 0, sizeof(OVERLAPPED));

	DWORD flags = 0;
	int retVal = WSARecv(sock, &wsaBuf, 1, nullptr, &flags, (OVERLAPPED*)recvOverlapped, nullptr);
	if (retVal == SOCKET_ERROR)
	{
		int errCode = WSAGetLastError();
		if (errCode != WSA_IO_PENDING)
		{
			if (errCode != WSAECONNRESET &&
				errCode != WSAECONNABORTED)
			{
				SLOGA(JNetLog::Network, L"WSARecv() [%d]", errCode);
			}

			InterlockedExchange(&invalid, 1);
			//SLOG(L"# 수신 에러, sessionId:%d, sock:%d, ip:%ls, port:%d\n", id, sock, ip, port);
			return false;
		}
	}

	return true;
}

