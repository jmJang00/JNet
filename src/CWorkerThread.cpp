#include "pch.h"
#include <JCore/Profiler.h>
#include <JNet/NetworkProfile.h>
#include <JNet/PacketHeader.h>
#include <JNet/CWorkerThread.h>
#include <JNet/CSession.h>
#include <JNet/CPacket.h>
#include <JNet/INetworkEntity.h>
#include <JNet/CLambdaPipe.h>
#include <JNet/CContent.h>
#include <JNet/CContentManager.h>
#include <JNet/CContentQueue.h>
#include <JNet/CPacketView.h>
#include "LogTag.h"

FOverlappedEx* CWorkerThread::sSendStartOverlapped;
FOverlappedEx* CWorkerThread::sReleaseSessionOverlapped;
FOverlappedEx* CWorkerThread::sPostMessageOverlapped;
FOverlappedEx* CWorkerThread::sPostContentOverlapped;
FOverlappedEx* CWorkerThread::sPostJobOverlapped;

CWorkerThread::CWorkerThread(int threadCnt, int concurrentThreadCnt)
	: CThread([this]() { WorkerThread(); }, threadCnt)
	, _hIOCP(INVALID_HANDLE_VALUE)
	, _recvBytes(0)
	, _sendBytes(0)
	, _recvMessageCnt(0)
	, _sendMessageCnt(0)
	, _threadCnt(threadCnt)
	, _chatResCnt(0)
	, _prevTime(0)
{
	_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, concurrentThreadCnt);
	if (_hIOCP == NULL)
	{
		ELOG(JNetLog::Progress, L"CreateIoCompletionPort failed, [ErrorCode]:%d", GetLastError());
		CRASH(true);
	}

	_isRunning.store(false);
	_workerMetrics.resize(threadCnt);
}

CWorkerThread::~CWorkerThread()
{
	CloseHandle(_hIOCP);
	_workerMetrics.clear();
	for (auto obs : _workerObservers)
	{
		delete obs;
	}
	_workerObservers.clear();
}

void CWorkerThread::Start(const std::vector<IWorkerObserver*>& observers)
{ 
	for (int i = 0; i < observers.size(); i++)
	{
		_workerObservers.push_back(observers[i]->Clone());
	}
	if (!Create())
	{
		ELOG(JNetLog::Progress, L"can't create WorkerThread, [ErrorCode]:%d", GetLastError());
		CRASH(true);
	}
	_isRunning.store(true);
}

void CWorkerThread::Start()
{
	_workerObservers.clear();
	if (!Create())
	{
		ELOG(JNetLog::Progress, L"can't create WorkerThread, [ErrorCode]:%d", GetLastError());
		CRASH(true);
	}
	_isRunning.store(true);
}

void CWorkerThread::Stop()
{
	_isRunning.store(false);
	Shutdown();
	Wait();
	Close();
	_workerObservers.clear();
}

void CWorkerThread::Shutdown()
{
	PostQueuedCompletionStatus(_hIOCP, 0, 0, nullptr);
}

bool CWorkerThread::PostStatus(uintptr_t compKey, FOverlappedEx* ov, unsigned int transferred)
{
	return PostQueuedCompletionStatus(_hIOCP, transferred, compKey, (LPOVERLAPPED)ov);
}

bool CWorkerThread::Register(CSession* session, SOCKET sock)
{
	if (CreateIoCompletionPort((HANDLE)sock, _hIOCP, (ULONG_PTR)session, 0) == NULL)
	{
		ELOGA(JNetLog::Progress, L"CreateIoCompletionPort failed, [ErrCode]:%d", GetLastError());
		return false;
	}

	session->_sock = sock;
	return true;
}

void CWorkerThread::RecvProcDecoding(CSession* session)
{
	int dataSize = 0;
	int freeSize = 0;
	int needSize = 0;
	INetworkEntity* owner = session->_owner;
	while (1)
	{
#pragma region 헤더를 읽고 모든 데이터를 수신했는지 확인
		dataSize = session->_recvBuf->GetDataSize();
		if (++session->_assembleCnt > ASSEMBLE_LIMIT)
		{
			owner->OnError(ENetError::INVALID_PACKET_HEADER, "RecvProc(): assemble limit exceeds");
			owner->Disconnect(session->_id);
			return;
		}

		FHeaderEx header;

		if (dataSize < sizeof(header))
		{
			needSize = sizeof(header);
			break;
		}

		memcpy((char*)&header, session->_recvBuf->GetDataPtr(), sizeof(header));

		if (header.code != PacketHeader::SERVER_CODE)
		{
			owner->OnError(ENetError::INVALID_PACKET_HEADER, "RecvProc(): Invalid sever code in packet header");
			owner->Disconnect(session->_id);
			return;
		}

		if (dataSize < header.len + sizeof(header))
		{
			needSize = header.len + sizeof(header);
			break;
		}
#pragma endregion

#pragma region 데이터 디코딩
		int readPos = session->_recvBuf->GetReadPos();
		CPacketView msg(session->_recvBuf, readPos, readPos + sizeof(header) + header.len);
		if (!msg.Decode(header.rkey))
		{
			owner->OnError(ENetError::PAKCET_CHECKSUM_MISMATCH, "RecvProc(): checksum mimatch in packet header");
			owner->Disconnect(session->_id);
			return;
		}
		msg.MoveReadPos(sizeof(header));
#pragma endregion

		InterlockedIncrement(&_recvMessageCnt);
		FContentHandle content(0);
		content = (FContentHandle)InterlockedOr((uintptr_t*)&session->_content, (uintptr_t)0);
		if (content == FContentHandle::NONE)
		{
			if (session->_contentQ->GetUseSize() > 0)
			{
				owner->OnError(ENetError::RECV_UNKNOWN_DEST_PACKET, "RecvProc(): Received a packet whose destination is unknown");
				owner->Disconnect(session->_id);
				return;
			}
			else
			{
				owner->OnRecv(session->_id, &msg);
			}
		}
		else
		{
			unsigned short len = header.len;
			session->_contentQ->Enqueue((char*)&len, sizeof(len));
			session->_contentQ->Enqueue((char*)msg.GetBufferPtr(), header.len);
		}
		session->_assembleCnt = 0;
		session->_recvBuf->MoveReadPos(sizeof(header) + header.len);
	}

	CRASH(needSize < dataSize);

	if (session->_recvBuf->GetFreeSize() < needSize - dataSize)
	{
		if (needSize > session->_recvBuf->GetBufferSize())
		{
			owner->OnError(ENetError::PACKET_SIZE_LIMIT_EXCEEDED, "RecvProc(): Packet size exceeds recv buffer limit");
			owner->Disconnect(session->_id);
			return;
		}
		else
		{
			CPacketBuffer* newBuffer = CPacketBuffer::Alloc();
			newBuffer->AddRef();
			memcpy(newBuffer->GetBufferPtr(), session->_recvBuf->GetDataPtr(), dataSize);
			newBuffer->MoveWritePos(dataSize);
			session->_recvBuf->Release();
			session->_recvBuf = newBuffer;
		}
	}
}

void CWorkerThread::RecvProc(CSession* session)
{
	int dataSize = 0;
	int freeSize = 0;
	int needSize = 0;
	INetworkEntity* owner = session->_owner;

	while (1)
	{
		dataSize = session->_recvBuf->GetDataSize();
		if (++session->_assembleCnt > ASSEMBLE_LIMIT)
		{
			owner->OnError(ENetError::INVALID_PACKET_HEADER, "RecvProc(): assemble limit exceeds");
			owner->Disconnect(session->_id);
			return;
		}

		FHeader header;

		if (dataSize < sizeof(header))
		{
			needSize = sizeof(header);
			break;
		}

		memcpy((char*)&header, session->_recvBuf->GetDataPtr(), sizeof(header));

		if (dataSize < header.size + sizeof(header))
		{
			needSize = header.size + sizeof(header);
			break;
		}

		session->_recvBuf->MoveReadPos(sizeof(header));
		int readPos = session->_recvBuf->GetReadPos();
		CPacketView msg(session->_recvBuf, readPos, readPos + header.size);

		InterlockedIncrement(&_recvMessageCnt);
		CContent* content;
		content = (CContent*)InterlockedOr((uintptr_t*)&session->_content, (uintptr_t)0);
		if (content == nullptr)
		{
			if (session->_contentQ->GetUseSize() > 0)
			{
				owner->OnError(ENetError::RECV_UNKNOWN_DEST_PACKET, "RecvProc(): Received a packet whose destination is unknown");
				owner->Disconnect(session->_id);
				return;
			}
			else
			{
				owner->OnRecv(session->_id, &msg);
			}
		}
		else
		{
			unsigned short len = header.size;
			session->_contentQ->Enqueue((char*)&len, sizeof(len));
			session->_contentQ->Enqueue((char*)msg.GetBufferPtr(), len);
		}
		session->_assembleCnt = 0;
		session->_recvBuf->MoveReadPos(header.size);
	}

	if (session->_recvBuf->GetFreeSize() < needSize - dataSize)
	{
		if (needSize > session->_recvBuf->GetBufferSize())
		{
			owner->OnError(ENetError::PACKET_SIZE_LIMIT_EXCEEDED, "RecvProc(): Packet size exceeds recv buffer limit");
			owner->Disconnect(session->_id);
		}
		else
		{
			CPacketBuffer* newBuffer = CPacketBuffer::Alloc();
			newBuffer->AddRef();
			memcpy(newBuffer->GetBufferPtr(), session->_recvBuf->GetDataPtr(), dataSize);
			newBuffer->MoveWritePos(dataSize);
			session->_recvBuf->Release();
			session->_recvBuf = newBuffer;
		}
	}
}

void CWorkerThread::SendProc(CSession* session)
{
	while (session->_sendBuf->GetSize() > 0 
		&& session->_invalid == 0)
	{
		if (session->SendPost())
		{
			// 다른 스레드가 먼저 보낸 경우
			if (session->_sending == 0)
			{
				continue;
			}

			// Disconnect로 세션이 유효하지 않은 상황
			if (session->_invalid == 1)
			{
				CancelIoEx((HANDLE)session->_sock, 
					(OVERLAPPED*)session->_sendOverlapped);
				break;
			}
		}
		else
		{
			break;
		}
	}
}

void CWorkerThread::OnPrintExternal(CMonitorTable* table)
{
	Context** ctxts = GetContextBufferPtr();
	int size = GetContextBufferSize();
	for (int i = 0; i < size; i++)
	{
		table->PrintColumnFormat(1, L"[%u]", ctxts[i]->_threadId);
		table->PrintColumnFormat(1, L"%.2lf", _workerMetrics[i].cpuUsage);
	}
	table->PrintDivider();
}

void CWorkerThread::OnCollectExternal(MetricsCollector& collector)
{
	Context** ctxts = GetContextBufferPtr();
	int size = GetContextBufferSize();
	unsigned int currTime = timeGetTime();
	unsigned int diff = currTime - _prevTime;
	_prevTime = currTime;
	for (int i = 0; i < size; i++)
	{
		_workerMetrics[i].cpuUsage = (double)ctxts[i]->GetMeasuredTime() / diff * 100;
	}
}

void CWorkerThread::WorkerThread()
{
	SLOGA(JNetLog::Progress, L"Worker Thread Start %ul", GetCurrentThreadId());
	Context* ctxt = GetContextPtr();
	for (int i = 0; i < _workerObservers.size(); i++)
	{
		_workerObservers[i]->OnWorkerEnter();
	}

	while (1)
	{
		CSession* session = nullptr;
		INetworkEntity* owner = nullptr;
		FOverlappedEx* overlapped = nullptr;
		DWORD transferred = 0;
		ULONG_PTR compKey = 0;

		SMPL_PRO_END(ENetworkProfile::NET_PROFILE_IO_COMPLETE);
		for (int i = 0; i < _workerObservers.size(); i++)
		{
			_workerObservers[i]->OnWorkerEnd();
		}
		ctxt->TimeMeasureEnd();
		bool result = GetQueuedCompletionStatus(_hIOCP, &transferred, &compKey, (OVERLAPPED**)&overlapped, 30'000);
		ctxt->TimeMeasureBegin();
		SMPL_PRO_BEGIN(ENetworkProfile::NET_PROFILE_IO_COMPLETE);

		if (result && overlapped == nullptr && compKey == 0 && transferred == 0)
		{
			SLOGA(JNetLog::Progress, L"종료 메시지 수신 %lu", GetCurrentThreadId());
			PostQueuedCompletionStatus(_hIOCP, 0, 0, nullptr);
			break;
		}
		else if (!result && overlapped == nullptr && compKey == 0 && transferred == 0)
		{
			continue;
		}

		switch (overlapped->type)
		{
		case SEND_START:
		{
			SMPL_PROFILER(ENetworkProfile::NET_PROFILE_SEND_START);
			session = (CSession*)compKey;

			if (session->SendPostRaw())
			{
				if (session->_sending == 0)
				{
					SendProc(session);
					continue;
				}

				if (session->_invalid == 1)
				{
					CancelIoEx((HANDLE)session->_sock,
						(OVERLAPPED*)session->_sendOverlapped);
				}
			}
			continue;
		}
		case RELEASE_SESSION:
		{
			SMPL_PROFILER(ENetworkProfile::NET_PROFILE_SEND_START);
			session = (CSession*)compKey;
			owner = session->_owner;
			FContentHandle content = (FContentHandle)InterlockedOr((uintptr_t*)&session->_content, (uintptr_t)0);
			if (content == FContentHandle::NONE)
			{
				owner->ReleaseSession(session);
			}
			else
			{
				FSystemMessage msg;
				msg.type = ESystemMessageType::MSG_RELEASE;
				msg.session = session;
				if (!session->_contentMng->Execute<CContent>(CContent::Running | CContent::Closing, 
					content, &CContent::Enqueue, &msg))
				{
					owner->ReleaseSession(session);
				}
			}
			continue;
		}
		case JOB_POST:
		{
			SMPL_PROFILER(ENetworkProfile::NET_PROFILE_JOB);
			FLambdaTask* task = (FLambdaTask*)compKey;
			task->Invoke();
			FLambdaTask::ReleaseTask(task);
			continue;
		}
		case PIPE_POST:
		{
			SMPL_PROFILER(ENetworkProfile::NET_PROFILE_PIPE);
			CLambdaPipe* internalSession = (CLambdaPipe*)compKey;
			internalSession->Execute();
			continue;
		}
		case CONTENT_POST:
		{
			SMPL_PROFILER(ENetworkProfile::NET_PROFILE_CONTENT);
			CContentQueue* content = (CContentQueue*)compKey;
			content->Execute();
			continue;
		}
		case RECV_POST:
		{
			SMPL_PROFILER(ENetworkProfile::NET_PROFILE_RECV);
			session = (CSession*)compKey;
			if (transferred == 0)
			{
				InterlockedExchange8(&session->_invalid, 1);
				session->Release();
				break;
			}

			InterlockedAdd(&_recvBytes, (long)transferred);
			DISABLE_WARNINGS_BEGIN(WARNING_4244);
			session->_recvBuf->MoveWritePos(transferred);
			DISABLE_WARNINGS_END();

			if (session->_encoding)
			{
				RecvProcDecoding(session);
			}
			else
			{
				RecvProc(session);
			}

			if (session->_invalid == 0)
			{
				if (!session->AddRef())
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
					session->Release();
				}
			}

			session->Release();
			break;
		}
		case SEND_POST:
		{
			SMPL_PROFILER(ENetworkProfile::NET_PROFILE_SEND);
			session = (CSession*)compKey;
			unsigned int requested = overlapped->reqLen;

			if (transferred < requested)
			{
				InterlockedExchange8(&session->_invalid, 1);
				session->Release();
				break;
			}

			if (requested != 0)
			{
				int bufCnt = overlapped->bufCnt;
				for (int i = 0; i < bufCnt; ++i)
				{
					session->_pendingBuffer[i]->Release();
					session->_pendingBuffer[i] = nullptr;
				}

				InterlockedAdd(&_sendMessageCnt, bufCnt);
				InterlockedAdd(&_sendBytes, (long)transferred);
				overlapped->bufCnt = 0;
			}

			InterlockedExchange8(&session->_sending, 0);

			SendProc(session);

			session->Release();
			break;
		}
		}
	}

	for (int i = 0; i < _workerObservers.size(); i++)
	{
		_workerObservers[i]->OnWorkerExit();
	}
	SLOGA(JNetLog::Progress, L"Worker Thread Exit %ul", GetCurrentThreadId());
}
