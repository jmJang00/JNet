#include "pch.h"
#include <algorithm>
#include <JNet/CInternalSession.h>
#include <JNet/CContent.h>
#include <JNet/CAppServer.h>
#include <JNet/CTimerManager.h>
#include <JNet/CContentManager.h>

CContent::CContent(CAppServer* server, int frameMs)
	: _updateQ(10000)
	, _frameMs(frameMs)
	, _server(server)
	, _timerRefCnt(0)
	, _frameTick(0)
	, _oldTick(0)
	, _shutdown(false)
	, _timers(10000)
	, _node(nullptr)
	, _registered(0)
	, _requestQ(10000)
{
	_context = new CContentQueue(server->Worker(), this, 1000);
	_startEvent = CreateEvent(nullptr, false, false, nullptr);
	_endEvent = CreateEvent(nullptr, false, false, nullptr);
}

CContent::~CContent()
{
	delete _context;
	CloseHandle(_startEvent);
	CloseHandle(_endEvent);
}

void CContent::Init(FContentNode* node)
{
	InterlockedAdd(&_timerRefCnt, 0x80000001);
	InterlockedExchange((uintptr_t*)&_node, (uintptr_t)node);
}

void CContent::Reset()
{
	CRASH(_timerRefCnt != 0);
	_shutdown = false;
	_context->Clear();
	InterlockedExchange((uintptr_t*)&_node, (uintptr_t)nullptr);
}

void CContent::Enter(Session* session)
{
	// 이건 유저가 결정하도록 하자
	//if (session->contentQ.GetSize() > 0)
	//{
	//	_server->OnError(NetError::RECV_UNKNOWN_DEST_PACKET, "RecvProc(): Received a packet whose destination is unknown");
	//	_server->Disconnect(session->id);
	//}
	OnEnter(session->id, session->user);
	_sessionMap.insert({ session->id, session });
}

void CContent::Leave(Session* session)
{
	_sessionMap.erase(session->id);
	OnLeave(session->id, session->user);
	_server->ContentMng()->FreeContent(_node);
}

bool CContent::AddRef()
{
	if (InterlockedIncrement(&_timerRefCnt) & 0x80000000)
	{
		return true;
	}
	else
	{
		return false;
	}
}

void CContent::Release()
{
	if (InterlockedDecrement(&_timerRefCnt) == 0x80000000)
	{
		if (InterlockedCompareExchange(&_timerRefCnt, 0, 0x80000000) == 0x80000000)
		{
			GetContext()->PostJob(&CContent::EndShutdown);
		}
	}
}

void CContent::WaitStartEvent()
{
	WaitForSingleObject(_startEvent, INFINITE);
}

void CContent::WaitStopEvent()
{
	WaitForSingleObject(_endEvent, INFINITE);
}

void CContent::Start()
{
	OnRegister();
	_oldTick = timeGetTime();
	_frameTick = _oldTick + _frameMs;
	Reserve(_frameMs, &CContent::Update);
	SetEvent(_startEvent);
}

void CContent::ClearSession()
{
	for (auto& it : _sessionMap)
	{
		Session* session = it.second;
		_server->Disconnect(session->id);
	}
}

void CContent::BeginShutdown()
{
	// 이 시점에서 이미 세션은 더 이상 진입하지 못하는 상태
	// 이제부터 잡 요청이 막힌다
	_shutdown = true;

	CTimerManager* mng = _server->TimerMng();

	FTimerHandle handle;
	while (_timers.Dequeue(&handle))
	{
		mng->CancelPost(handle);
	}

	ClearRequest();
	// 잡 카운트를 0으로 낮춤
	Release();
}

void CContent::EndShutdown()
{
	Reset();
	OnUnregister();

	// 이벤트가 전달되면 컨텐츠 매니저의 노드는 재사용 가능한 상태로 돌아간다
	SetEvent(_endEvent);
}

bool CContent::Reserve(int ms, CContentQueue::Func func)
{
	if (_shutdown)
	{
		return false;
	}

	if (!AddRef())
	{
		Release();
		return false;
	}

	FTimerHandle handle = _server->TimerMng()->PostAfterInternal(ms, this, func);
	if (handle.handle == FTimerHandle::INVALID_HANDLE)
	{
		Release();
		return false;
	}

	_timers.ForceEnqueue(handle);

	return true;
}

bool CContent::Reserve(CContentQueue::Func func)
{
	if (_shutdown)
	{
		return false;
	}

	if (!AddRef())
	{
		Release();
		return false;
	}

	_context->PostJob(func);
	_context->PostJob(&CContent::Release);

	return true;
}

void CContent::ProcessUpdateQueue()
{
	FSystemMessage msg;
	while (_updateQ.Dequeue(&msg))
	{
		switch (msg.type)
		{
		case ESystemMessageType::MSG_ENTER:
		{
			Session* session = msg.session;
			Enter(session);
			_server->FreeSession(session);
			break;
		}
		case ESystemMessageType::MSG_RELEASE:
		{
			Session* session = msg.session;
			Leave(session);
			_server->ReleaseSession(session);
			break;
		}
		default:
		{
			break;
		}
		}
	}
}

FContentHandle CContent::GetHandle()
{
	if (_node == nullptr)
	{
		return FContentHandle::INVALID_HANDLE;
	}

	return _node->handle;
}

void CContent::ClearInvalidHandles()
{
	CTimerManager* mng = _server->TimerMng();
	int size = std::max((int)_timers.GetSize(), 0);
	FTimerHandle handle;
	while (size > 0)
	{
		if (!_timers.Dequeue(&handle))
		{
			break;
		}

		if (mng->IsValid(handle))
		{
			_timers.Enqueue(handle);
		}
		size--;
	}
}

void CContent::ClearRequest()
{
	FInternalTask* task;
	while (_requestQ.Dequeue(&task))
	{
		FInternalTask::ReleaseTask(task);
	}
}

void CContent::ProcessRequest()
{
	FInternalTask* task;
	while (_requestQ.Dequeue(&task))
	{
		task->Invoke();
		FInternalTask::ReleaseTask(task);
	}
}

void CContent::Update()
{
	ProcessUpdateQueue();

	if (_registered == CContent::Closing)
	{
		ClearSession();
	}

	for (auto it = _sessionMap.begin(); it != _sessionMap.end(); ++it)
	{
		SessionId id = it->first;
		Session* session = _server->GetSession(id);
		if (session == nullptr)
		{
			continue;
		}

		Serializer* packet;
		while (session->contentQ.Dequeue(&packet))
		{
			OnRecv(session->id, packet);
		}

		_server->FreeSession(session);
	}

	DWORD tick = timeGetTime();
	int deltaTick = tick - _frameTick;
	if (deltaTick < _frameMs)
	{
		Reserve(_frameMs - deltaTick, &CContent::Update);
		_frameTick += _frameMs;
	}
	else
	{
		Reserve(&CContent::Update);
		_frameTick = tick;
	}

	OnTick(tick - _oldTick);
	_oldTick = tick;

	ProcessRequest();

	ClearInvalidHandles();
}

