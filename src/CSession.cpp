#include "pch.h"
#include <map>
#include <algorithm>
#include <JCore/SLog.h>
#include <JCore/Profiler.h>
#include <JNet/CSession.h>
#include <JNet/CNetServer.h>
#include <JNet/CPacket.h>
#include <JNet/CContent.h>
#include <JNet/CContentManager.h>
#include "LogTag.h"

thread_local std::vector<FSessionId> gSessionIdVector;

CSession::CSession()
	: _sendOverlapped(nullptr)
	, _recvOverlapped(nullptr)
	, _owner(nullptr)
	, _recvBuf(nullptr)
	, _userData(nullptr)
	, _content(FContentHandle::NONE)
#ifdef SESSION_DEBUG
	, debugIndex(-1)
#endif
{
	_sendOverlapped = new FOverlappedEx();
	_recvOverlapped = new FOverlappedEx();
	_pendingBuffer.resize(PENDING_BUFFER_SIZE);
	_contentQ = new CRingBuffer();
	_address = new FSessionAddress();
	_sendBuf = new CLockFreeQueue<CPacketBuffer*>(1000);
}

CSession::CSession(INetworkEntity* owner)
	: _sendOverlapped(nullptr)
	, _recvOverlapped(nullptr)
	, _owner(owner)
	, _recvBuf(nullptr)
	, _userData(nullptr)
	, _content(FContentHandle::NONE)
#ifdef SESSION_DEBUG
	, debugIndex(-1)
#endif
{
	_sendOverlapped = new FOverlappedEx();
	_recvOverlapped = new FOverlappedEx();
	_pendingBuffer.resize(PENDING_BUFFER_SIZE);
	_contentQ = new CRingBuffer();
	_address = new FSessionAddress();
	_sendBuf = new CLockFreeQueue<CPacketBuffer*>(1000);
}

CSession::~CSession()
{
	delete _recvOverlapped;
	delete _sendOverlapped;
	delete _address;
	delete _sendBuf;
}

void CSession::Start(SOCKET socket, CWorkerThread* iocpWorker, INetworkEntity* sessionOwner, CContentManager* contentMng, FSessionId sessionId, bool encoding)
{
	SOCKADDR_IN clientAddr;
	_sock = socket;
	_worker = iocpWorker;
	int addrLen = sizeof(clientAddr);
	getpeername(_sock, (SOCKADDR*)&clientAddr, &addrLen);
	InetNtopW(AF_INET, &clientAddr.sin_addr, _address->ip, 16);
	_address->port = ntohs(clientAddr.sin_port);
	memset(_sendOverlapped, 0, sizeof(*_sendOverlapped));
	memset(_recvOverlapped, 0, sizeof(*_recvOverlapped));
	_userData = nullptr;
	_assembleCnt = 0;
	InterlockedExchange((uintptr_t*)&_content, (uintptr_t)nullptr);
	_encoding = encoding;
	_contentMng = contentMng;

	_sendBuf->Clear();
	_owner = sessionOwner;
	std::fill(_pendingBuffer.begin(), _pendingBuffer.end(), nullptr);
	_recvBuf = CPacketBuffer::Alloc();
	_recvBuf->AddRef();
	InterlockedExchange8(&_sending, 0);
	InterlockedExchange8(&_invalid, 0);
	InterlockedExchange8(&_disconnect, 0);
	InterlockedExchange(&_id.total, sessionId.total);
	InterlockedAdd(&_refCnt, 0x80000001);
}

void CSession::Reset()
{
	InterlockedExchange(&_id.total, CSession::INVALID_SESSION_ID);
	InterlockedExchange8(&_invalid, 1);

	int bufCnt = _sendOverlapped->bufCnt;
	for (int i = 0; i < bufCnt; ++i)
	{
		_pendingBuffer[i]->Release();
		_pendingBuffer[i] = nullptr;
	}
	_sendOverlapped->bufCnt = 0;

	CPacketBuffer* buffer;
	while (_sendBuf->GetSize() > 0)
	{
		_sendBuf->Dequeue(&buffer);
		buffer->Release();
	}

	_recvBuf->Release();
	_recvBuf = nullptr;

	_contentQ->ClearBuffer();

	_content.handle = FContentHandle::NONE;
	_contentMng = nullptr;
	_userData = nullptr;
	closesocket(_sock);
}

bool CSession::SendPostRaw()
{
	int bufCnt = std::min((int)_sendBuf->GetSize(), PENDING_BUFFER_SIZE);
	int deqCnt = bufCnt;
	std::vector<CPacketBuffer*>& pendingBuffer = _pendingBuffer;
	CPacketBuffer** bufferPtr = pendingBuffer.data();
	while (deqCnt > 0)
	{
		if (!_sendBuf->Dequeue(bufferPtr))
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
#ifdef SESSION_DEBUG
		debug[(InterlockedIncrement(&debugIndex)) % 100] = "SendPost 0";
#endif
		InterlockedExchange8(&_sending, 0);
		Release();
		return true;
	}

	//printf("send wsaBuf bufCnt:%d\n", bufCnt);

	FOverlappedEx* sendOverlapped = _sendOverlapped;
	sendOverlapped->reqLen = sendSize;
	sendOverlapped->bufCnt = bufCnt;
	memset(&sendOverlapped->overlapped, 0, sizeof(OVERLAPPED));
	//DLOG(L"# ioCnt:%d, sending:%d, ip:%ls, port:%d\n", ioCnt, sending, ip, port);

	//PRO_BEGIN(L"Send");
	int retVal = WSASend(_sock, wsaBuf, bufCnt, nullptr, 0, (OVERLAPPED*)sendOverlapped, nullptr);
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

#ifdef SESSION_DEBUG
			debug[(InterlockedIncrement(&debugIndex)) % 100] = "SendPost Error";
#endif

			InterlockedExchange8(&_invalid, 1);
			Release();
			//SLOG(L"# 송신 에러, sessionId:%d, sock:%d, ip:%ls, port:%d\n", id, sock, ip, port);
			return false;
		}
	}

	return true;
}

bool CSession::SendPost()
{
	if (!AddRef())
	{
		CRASH(true);
	}
	// 센드를 하는 건 딱 하나의 스레드로 제한
	if (InterlockedExchange8(&_sending, 1) == 1)
	{
		Release();
		return false;
	}

#pragma region 센드큐에서 대기버퍼로 이동
	int bufCnt = std::min((int)_sendBuf->GetSize(), PENDING_BUFFER_SIZE);
	int deqCnt = bufCnt;
	std::vector<CPacketBuffer*>& pendingBuffer = _pendingBuffer;
	CPacketBuffer** bufferPtr = pendingBuffer.data();
	while (deqCnt > 0)
	{
		if (!_sendBuf->Dequeue(bufferPtr))
		{
			CRASH(true);
		}

		++bufferPtr;
		--deqCnt;
	}
#pragma endregion

	if (bufCnt == 0)
	{
		// 이미 다른 스레드가 사이즈를 체크하고 보낸 경우
		InterlockedExchange8(&_sending, 0);
		Release();
		return true;
	}

#pragma region WSASend 파라미터 설정
	WSABUF wsaBuf[PENDING_BUFFER_SIZE];
	int sendSize = 0;
	for (int i = 0; i < bufCnt; i++)
	{
		wsaBuf[i].buf = pendingBuffer[i]->GetDataPtr();
		int dataSize = pendingBuffer[i]->GetDataSize();
		wsaBuf[i].len = dataSize;
		sendSize += dataSize;
	}

	FOverlappedEx* sendOverlapped = _sendOverlapped;
	sendOverlapped->reqLen = sendSize;
	sendOverlapped->bufCnt = bufCnt;
	memset(&sendOverlapped->overlapped, 0, sizeof(OVERLAPPED));
#pragma endregion

	int retVal = WSASend(_sock, wsaBuf, bufCnt,
		nullptr, 0, (OVERLAPPED*)sendOverlapped, nullptr);
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
			// 더이상 io가 일어나지 않게 방지 플래그를 올림
			InterlockedExchange8(&_invalid, 1);
			Release();
			return false;
		}
	}
	return true;
}

bool CSession::AddRef()
{
	if (InterlockedIncrement(&_refCnt) & 0x80000000)
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool CSession::Release()
{
	if (InterlockedDecrement(&_refCnt) == 0x80000000)
	{
		if (InterlockedCompareExchange(&_refCnt, 0, 0x80000000) == 0x80000000)
		{
#ifdef SESSION_DEBUG
			debug[(InterlockedIncrement(&debugIndex)) % 100] = "Release";
#endif
			FContentHandle con = (FContentHandle)InterlockedOr((uintptr_t*)&_content, (uintptr_t)0);
			if (con.handle == FContentHandle::NONE)
			{
				_owner->ReleaseSession(this);
			}
			else
			{
				FSystemMessage msg;
				msg.type = ESystemMessageType::MSG_RELEASE;
				msg.session = this;
				if (!_contentMng->Execute<CContent>(CContent::Running | CContent::Closing, 
					con, &CContent::Enqueue, &msg))
				{
					_owner->ReleaseSession(this);
				}
			}
			return true;
		}
	}

	return false;
}

bool CSession::ReleasePost()
{
	//SLOG("# Decrease IO cnt, iocnt:%d, sessionId:%d, sock:%d, ip:%ls, port:%d\n", ioCnt, id, sock, ip, port);
	if (InterlockedDecrement(&_refCnt) == 0x80000000)
	{
		if (InterlockedCompareExchange(&_refCnt, 0, 0x80000000) == 0x80000000)
		{
			_worker->PostStatus((ULONG_PTR)this, (LPOVERLAPPED)CWorkerThread::RELEASE_SESSION, 0);
			return true;
		}
	}

	return false;
}

bool CSession::RecvPost() {

#pragma region WSARecv 파라미터 설정
	WSABUF wsaBuf;
	CPacketBuffer* recvBuf = _recvBuf;
	wsaBuf.buf = recvBuf->GetDataPtr() + recvBuf->GetDataSize();

	int reqLen = recvBuf->GetFreeSize();
	wsaBuf.len = reqLen;

	FOverlappedEx* recvOverlapped = _recvOverlapped;
	recvOverlapped->reqLen = reqLen;
	memset(&recvOverlapped->overlapped, 0, sizeof(OVERLAPPED));
	DWORD flags = 0;
#pragma endregion

	int retVal = WSARecv(_sock, &wsaBuf, 1, 
		nullptr, &flags, (OVERLAPPED*)recvOverlapped, nullptr);
	if (retVal == SOCKET_ERROR) {
		int errCode = WSAGetLastError();
		if (errCode != WSA_IO_PENDING)
		{
			if (errCode != WSAECONNRESET &&
				errCode != WSAECONNABORTED)
			{
				SLOGA(JNetLog::Network, L"WSARecv() [%d]", errCode);
			}

			// 더이상 io가 일어나지 않게 방지 플래그를 올림
			InterlockedExchange8(&_invalid, 1);
			return false;
		}
	}

	return true;
}

