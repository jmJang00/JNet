#include "pch.h"
#include <JNet/CContent.h>
#include <JNet/CContentManager.h>
#include <JNet/CAppServer.h>
#include <JNet/CTimerManager.h>

CContentManager::CContentManager(CAppServer* server)
	: _server(server)
{

}

CContentManager::~CContentManager()
{

}

bool CContentManager::Register(CContent* content)
{
	content->GetContext()->PostLambda([content]()
		{
			content->Start();
		});

	content->WaitStartEvent();

	return true;
}

bool CContentManager::Unregister(CContent* content)
{
	content->GetContext()->PostLambda([content]()
		{
			content->Stop();
		});

	content->WaitStopEvent();

	return true;
}

bool CContentManager::MoveTo(SessionId id, CContent* to)
{
	if (to == nullptr)
		return false;

	Session* session = _server->GetSession(id);
	if (session == nullptr)
	{
		return false;
	}

	CContent* content;
	content = (CContent*)InterlockedExchange((uintptr_t*)&session->content, (uintptr_t)to);
	if (content != nullptr)
	{
		content->_sessionMap.erase(session->id);
		content->OnLeave(session->id, session->user);
	}

	if (to != nullptr)
	{
		FSystemMessage msg(ESystemMessageType::MSG_ENTER, session, 0);
		to->Enqueue(&msg);
	}
	else
	{
		_server->FreeSession(session);
	}

	return true;
}
