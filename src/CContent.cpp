#include "pch.h"
#include <JNet/CInternalSession.h>
#include <JNet/CContent.h>
#include <JNet/CAppServer.h>
#include <JNet/CTimerManager.h>

CContent::CContent(CAppServer* server, int frameMs)
	: _updateQ(10000)
	, _frameMs(frameMs)
	, _server(server)
	, _timerRefCnt(0)
	, _frameTick(0)
	, _oldTick(0)
	, _shutdown(false)
	, _memoryLog()
{
	_context = new CInternalSession(server->Worker(), server, 1000);
	_startEvent = CreateEvent(nullptr, false, false, nullptr);
	_endEvent = CreateEvent(nullptr, false, false, nullptr);
}

CContent::~CContent()
{
	delete _context;
	CloseHandle(_startEvent);
	CloseHandle(_endEvent);
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

bool CContent::Release()
{
	if (InterlockedDecrement(&_timerRefCnt) == 0x80000000)
	{
		if (InterlockedCompareExchange(&_timerRefCnt, 0, 0x80000000) == 0x80000000)
		{
			EndShutdown();
			return true;
		}
	}
	return false;
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
	InterlockedAdd(&_timerRefCnt, 0x80000001);
	_memoryLog[InterlockedIncrement(&_index) % 100] = "Start AddRef";
	OnRegister();
	_oldTick = timeGetTime();
	_frameTick = _oldTick + _frameMs;
	_memoryLog[InterlockedIncrement(&_index) % 100] = "Reserve Update";
	Reserve(_frameMs, [this]()
		{
			Update();
		});
	SetEvent(_startEvent);
}

void CContent::Stop()
{
	BeginShutdown();
	CTimerManager* mng = _server->Timer();
	for (auto timer : _timers)
	{
		mng->CancelPost(timer);
	}
	_timers.clear();
	Release();
	_memoryLog[InterlockedIncrement(&_index) % 100] = "Stop Release";
}

void CContent::BeginShutdown()
{
	_shutdown = true;
}

void CContent::EndShutdown()
{
	_context->PostLambda([this]()
		{
			OnUnregister();
			_memoryLog[InterlockedIncrement(&_index) % 100] = "Unregister";
			SetEvent(_endEvent);
		});
}

void CContent::ClearInvalidHandles()
{
	int i = 0;
	CTimerManager* mng = _server->Timer();
	while (i < _timers.size())
	{
		if (!mng->IsValid(_timers[i]))
		{
			_timers[i] = _timers.back();
			_timers.pop_back();
		}
		else
		{
			i++;
		}
	}
}

void CContent::Update()
{
	FSystemMessage msg;
	while (_updateQ.Dequeue(&msg))
	{
		switch (msg.type)
		{
		case ESystemMessageType::MSG_ENTER:
		{
			Session* session = msg.session;
			if (session->contentQ.GetSize() > 0)
			{
				_server->Disconnect(session->id);
			}
			else
			{
				OnEnter(session->id, session->user);
			}
			_sessionMap.insert({session->id, session});
			_server->FreeSession(session);
			break;
		}
		case ESystemMessageType::MSG_RELEASE:
		{
			Session* session = msg.session;
			_sessionMap.erase(session->id);
			OnLeave(session->id, session->user);
			_server->ReleaseSession(session);
			break;
		}
		}
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
		_memoryLog[InterlockedIncrement(&_index) % 100] = "Reserve Update In Main Loop";
		Reserve(_frameMs - deltaTick, [this]() 
			{ 
				Update(); 
			});
		_frameTick += _frameMs;
	}
	else
	{
		_memoryLog[InterlockedIncrement(&_index) % 100] = "Execute Update In Main Loop";
		Execute([this]() 
			{ 
				Update(); 
			});
		_frameTick = tick;
	}

	OnTick(tick - _oldTick);
	_oldTick = tick;

	ClearInvalidHandles();
}
