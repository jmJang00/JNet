#include "pch.h"
#include <JCore/SLog.h>
#include <JNet/PacketHeader.h>
#include <JNet/CWorkerThread.h>
#include <JNet/Session.h>
#include <JNet/Serializer.h>
#include <JNet/INetworkEntity.h>
#include <JNet/CInternalSession.h>
#include <JNet/CContent.h>
#include "LogTag.h"

CWorkerThread::CWorkerThread(int threadCnt, int concurrentThreadCnt)
	: CThread([this]() { WorkerThread(); }, threadCnt)
	, _hIOCP(INVALID_HANDLE_VALUE)
	, _recvBytes(0)
	, _sendBytes(0)
	, _recvMessageCnt(0)
	, _sendMessageCnt(0)
	, _threadCnt(threadCnt)
{
	_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, concurrentThreadCnt);
	if (_hIOCP == NULL)
	{
		ELOG(JNetLog::Network, L"CreateIoCompletionPort failed, [ErrorCode]:%d", GetLastError());
		CRASH(true);
	}

	if (!Create(true))
	{
		ELOG(JNetLog::Network, L"can't create WorkerThread, [ErrorCode]:%d", GetLastError());
		CRASH(true);
	}

	_isRunning.store(false);
}

CWorkerThread::~CWorkerThread()
{
	CloseHandle(_hIOCP);
	_hIOCP = NULL;
}

void CWorkerThread::Start()
{ 
	Resume();
	_isRunning.store(true);
}

void CWorkerThread::Stop()
{
	_isRunning.store(false);
	Shutdown();
	Wait();
	Close();
}

void CWorkerThread::Shutdown()
{
	PostQueuedCompletionStatus(_hIOCP, 0, 0, nullptr);
}

bool CWorkerThread::PostStatus(uintptr_t compKey, OVERLAPPED* ov, unsigned int transferred)
{
	return PostQueuedCompletionStatus(_hIOCP, transferred, compKey, ov);
}

bool CWorkerThread::Register(Session* session, SOCKET sock)
{
	if (CreateIoCompletionPort((HANDLE)sock, _hIOCP, (ULONG_PTR)session, 0) == NULL)
	{
		ELOGA(JNetLog::Network, L"CreateIoCompletionPort failed, [ErrCode]:%d", GetLastError());
		return false;
	}

	session->sock = sock;
	return true;
}

void CWorkerThread::RecvProcDecoding(Session* session)
{
	int dataSize = 0;
	int freeSize = 0;
	int needSize = 0;
	INetworkEntity* owner = session->owner;
	while (1)
	{
		dataSize = session->recvBuf->GetDataSize();
		if (++session->assembleCnt > 5)
		{
			owner->OnError(NetError::INVALID_PACKET_HEADER, "RecvProc(): assemble limit exceeds");
			owner->Disconnect(session->id);
			return;
		}

		HeaderEx header;

		if (dataSize < sizeof(header))
		{
			needSize = sizeof(header);
			break;
		}

		memcpy((char*)&header, session->recvBuf->GetDataPtr(), sizeof(header));

		if (header.code != PacketHeader::SERVER_CODE)
		{
			owner->OnError(NetError::INVALID_PACKET_HEADER, "RecvProc(): Invalid sever code in packet header");
			owner->Disconnect(session->id);
			return;
		}

		if (dataSize < header.len + sizeof(header))
		{
			needSize = header.len + sizeof(header);
			break;
		}

		int readPos = session->recvBuf->GetReadPos();
		Serializer* msg = Serializer::Alloc(session->recvBuf->GetPacketBuffer(), readPos, readPos + sizeof(header) + header.len);
		if (!msg->Decode(header.rkey))
		{
			owner->OnError(NetError::PAKCET_CHECKSUM_MISMATCH, "RecvProc(): checksum mimatch in packet header");
			owner->Disconnect(session->id);
			return;
		}

		InterlockedIncrement(&_recvMessageCnt);
		msg->MoveReadPos(sizeof(header));
		CContent* content;
		content = (CContent*)InterlockedOr((uintptr_t*)&session->content, (uintptr_t)0);
		if (content == nullptr)
		{
			owner->OnRecv(session->id, msg);
		}
		else
		{
			session->contentQ.Enqueue(msg);
		}
		session->assembleCnt = 0;
		session->recvBuf->MoveReadPos(sizeof(header) + header.len);
	}

	CRASH(needSize < dataSize);

	if (session->recvBuf->GetFreeSize() < needSize - dataSize)
	{
		if (needSize > session->recvBuf->GetBufferSize())
		{
			owner->OnError(NetError::PACKET_SIZE_LIMIT_EXCEEDED, "RecvProc(): Packet size exceeds recv buffer limit");
			owner->Disconnect(session->id);
		}
		else
		{
			Serializer* newBuffer = Serializer::Alloc(PacketBuffer::Alloc());
			newBuffer->PutData(session->recvBuf->GetDataPtr(), dataSize);
			Serializer::Free(session->recvBuf);
			session->recvBuf = newBuffer;
		}
	}
}

void CWorkerThread::RecvProc(Session* session)
{
	int dataSize = 0;
	int freeSize = 0;
	int needSize = 0;
	INetworkEntity* owner = session->owner;

	while (1)
	{
		dataSize = session->recvBuf->GetDataSize();
		if (++session->assembleCnt > 5)
		{
			owner->OnError(NetError::INVALID_PACKET_HEADER, "RecvProc(): assemble limit exceeds");
			owner->Disconnect(session->id);
			return;
		}

		Header header;

		if (dataSize < sizeof(header))
		{
			needSize = sizeof(header);
			break;
		}

		memcpy((char*)&header, session->recvBuf->GetDataPtr(), sizeof(header));

		if (dataSize < header.size + sizeof(header))
		{
			needSize = header.size + sizeof(header);
			break;
		}

		session->recvBuf->MoveReadPos(sizeof(header));
		int readPos = session->recvBuf->GetReadPos();
		Serializer* msg = Serializer::Alloc(session->recvBuf->GetPacketBuffer(), readPos, readPos + header.size);

		InterlockedIncrement(&_recvMessageCnt);
		CContent* content;
		content = (CContent*)InterlockedOr((uintptr_t*)&session->content, (uintptr_t)0);
		if (content == nullptr)
		{
			owner->OnRecv(session->id, msg);
		}
		else
		{
			session->contentQ.Enqueue(msg);
		}
		session->assembleCnt = 0;
		session->recvBuf->MoveReadPos(header.size);
	}

	if (session->recvBuf->GetFreeSize() < needSize - dataSize)
	{
		if (needSize > session->recvBuf->GetBufferSize())
		{
			owner->OnError(NetError::PACKET_SIZE_LIMIT_EXCEEDED, "RecvProc(): Packet size exceeds recv buffer limit");
			owner->Disconnect(session->id);
		}
		else
		{
			Serializer* newBuffer = Serializer::Alloc(PacketBuffer::Alloc());
			newBuffer->PutData(session->recvBuf->GetDataPtr(), dataSize);
			Serializer::Free(session->recvBuf);
			session->recvBuf = newBuffer;
		}
	}
}

void CWorkerThread::WorkerThread()
{
	SLOGA(JNetLog::Network, L"Worker Thread Start\n");

	while (1)
	{
		Session* session = nullptr;
		INetworkEntity* owner = nullptr;
		OverlappedEx* overlapped = nullptr;
		DWORD transferred = 0;
		ULONG_PTR compKey = 0;
		unsigned int requested = 0;
		GetQueuedCompletionStatus(_hIOCP, &transferred, &compKey, (OVERLAPPED**)&overlapped, INFINITE);

		if (overlapped == nullptr && compKey == 0 && transferred == 0)
		{
			SLOGA(JNetLog::Network, L"# 종료 메시지 수신\n");
			PostQueuedCompletionStatus(_hIOCP, 0, 0, nullptr);
			break;
		}

		switch ((ULONG_PTR)overlapped)
		{
		case SEND_START:
		{
			session = (Session*)compKey;

			InterlockedExchange(&session->sending, 0);

			if (session->sendBuf.GetSize() > 0 && session->invalid == 0)
			{
				if (!session->AddRef())
				{
					session->Release();
					session->Release();
					continue;
				}

				if (InterlockedExchange(&session->sending, 1) == 0)
				{
					if (session->SendPost())
					{
						if (session->invalid == 1)
						{
							CancelIoEx((HANDLE)session->sock,
								(OVERLAPPED*)session->sendOverlapped);
						}
					}
					else
					{
						if (session->Release())
						{
							CRASH(true);
						}
					}
				}
				else
				{
					if (session->Release())
					{
						CRASH(true);
					}
				}
			}
			session->Release();
			continue;
		}
		case RELEASE_SESSION:
		{
			session = (Session*)compKey;
			owner = session->owner;
			CContent* content = (CContent*)InterlockedOr((uintptr_t*)&session->content, (uintptr_t)0);
			if (content == nullptr)
			{
				owner->ReleaseSession(session);
			}
			else
			{
				FSystemMessage msg;
				msg.type = ESystemMessageType::MSG_RELEASE;
				msg.session = session;
				content->Enqueue(&msg);
			}
			continue;
		}
		case POST_MESSAGE:
		{
			CInternalSession* internalSession = (CInternalSession*)compKey;
			owner = internalSession->GetOwner();
			owner->HandleInternalMessage(internalSession);
			continue;
		}
		default:
		{
			session = (Session*)compKey;
			break;
		}
		}

		if (session->recvOverlapped == overlapped)
		{
			if (transferred == 0)
			{
				InterlockedExchange(&session->invalid, 1);
				session->Release();
				continue;
			}

			InterlockedAdd(&_recvBytes, (long)transferred);
			session->recvBuf->MoveWritePos(transferred);
			if (session->encoding)
			{
				RecvProcDecoding(session);
			}
			else
			{
				RecvProc(session);
			}

			if (session->invalid == 0)
			{
				if (!session->AddRef())
				{
					session->Release();
					session->Release();
					continue;
				}

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
					if (session->Release())
					{
						CRASH(true);
					}
				}
			}
		}
		else if (session->sendOverlapped == overlapped)
		{
			requested = overlapped->reqLen;

			if (transferred < requested)
			{
				InterlockedExchange(&session->invalid, 1);
				session->Release();
				continue;
			}

			if (requested != 0)
			{
				int bufCnt = overlapped->bufCnt;
				for (int i = 0; i < bufCnt; ++i)
				{
					Serializer::Free(session->pendingBuffer[i]);
					session->pendingBuffer[i] = nullptr;
				}

				InterlockedAdd(&_sendMessageCnt, bufCnt);
				InterlockedAdd(&_sendBytes, (long)transferred);
				overlapped->bufCnt = 0;
			}

			InterlockedExchange(&session->sending, 0);

			if (session->sendBuf.GetSize() > 0 && session->invalid == 0)
			{
				if (!session->AddRef())
				{
					session->Release();
					session->Release();
					continue;
				}

				if (InterlockedExchange(&session->sending, 1) == 0)
				{
					if (session->SendPost())
					{
						if (session->invalid == 1)
						{
							CancelIoEx((HANDLE)session->sock,
								(OVERLAPPED*)session->sendOverlapped);
						}
					}
					else
					{
						if (session->Release())
						{
							CRASH(true);
						}
					}
				}
				else
				{
					if (session->Release())
					{
						CRASH(true);
					}
				}
			}
		}
		session->Release();
	}

	SLOGA(JNetLog::Network, L"Worker Thread Exit\n");
}
