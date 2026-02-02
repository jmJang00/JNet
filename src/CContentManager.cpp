#include "pch.h"
#include <JNet/CContent.h>
#include <JNet/CContentManager.h>
#include <JNet/CAppServer.h>
#include <JNet/CTimerManager.h>
#include <JNet/Serializer.h>

CContentManager::CContentManager(CAppServer* server, int maxContentCnt)
	: _server(server)
	, _nextId(0)
	, _contentIdxStack(maxContentCnt)
	, _maxContentCnt(maxContentCnt)
{
	_contentPool.resize(maxContentCnt);
	for (int i = maxContentCnt - 1; i >= 0; i--)
	{
		_contentIdxStack.push(i);
	}
	_server = server;
}

CContentManager::~CContentManager()
{

}

FContentHandle CContentManager::Register(CContent* content)
{
	if (InterlockedCompareExchange(&content->_registered, 
		CContent::Initializing, CContent::Cleared) != CContent::Cleared)
	{
		return FContentHandle::INVALID_HANDLE;
	}

	FContentNode* node = Alloc(content);

	content->GetContext()->PostJob(&CContent::Start);

	content->WaitStartEvent();

	InterlockedExchange(&content->_registered, CContent::Running);

	return node->handle;
}

bool CContentManager::Unregister(FContentHandle handle)
{
	FContentNode* node = GetContent(handle);
	if (node == nullptr)
	{
		return false;
	}

	CContent* content = node->content;
	if (InterlockedCompareExchange(&content->_registered, 
		CContent::Closing, CContent::Running) != CContent::Running)
	{
		FreeContent(node);
		return false;
	}

	// 컨텐츠의 registered 플래그를 바꿨으므로 참조를 풀어도 된다
	FreeContent(node);
	FreeContent(node);

	// refCnt가 0으로 떨어졌음이 보장됨
	content->WaitStopEvent();

	Free(node);

	// 여기서부터 다른 스레드가 해당 컨텐츠 포인터로 등록을 하려고 하면 성공
	InterlockedExchange(&content->_registered, CContent::Cleared);

	return true;
}

bool CContentManager::MoveTo(SessionId id, FContentHandle to)
{
	Session* session = _server->GetSession(id);
	if (session == nullptr)
	{
		return false;
	}

	bool success = true;
	FContentNode* nextContentNode = nullptr;

	// 콘텐츠를 바꾸기 전에 먼저 검증을 함
	do
	{
		if (to == FContentHandle::INVALID_HANDLE)
		{
			success = false;
			break;
		}

		if (to.handle != FContentHandle::NONE)
		{
			nextContentNode = GetContent(to);
			if (nextContentNode == nullptr)
			{
				success = false;
				break;
			}

			if (nextContentNode->content->_registered != CContent::Running)
			{
				success = false;
				FreeContent(nextContentNode);
				break;
			}
		}

	} while (0);

	if (!success)
	{
		_server->FreeSession(session);
		_server->Disconnect(session->id);
		return false;
	}

	FContentHandle prevContent(0);
	prevContent.handle = (unsigned long long)InterlockedExchange((uintptr_t*)&session->content.handle, (uintptr_t)to.handle);
	if (prevContent.handle != FContentHandle::NONE)
	{
		// Leave에서 FreeContent를 해주면서 카운트를 내림
		GetContentUnsafe(prevContent)->content->Leave(session);
	}

	if (to.handle != FContentHandle::NONE)
	{
		// Enter에서 FreeSession을 해줌
		FSystemMessage msg(ESystemMessageType::MSG_ENTER, session, 0);
		nextContentNode->content->Enqueue(&msg);
	}
	else
	{
		// 워커스레드로 이동하는 경우
		_server->FreeSession(session);
	}

	return true;
}

FContentNode* CContentManager::GetContent(FContentHandle handle)
{
	if (handle.index >= _maxContentCnt)
	{
		return nullptr;
	}

	FContentNode* node = &_contentPool[handle.index];
	if (!node->AddRef())
	{
		node->Release();
		return nullptr;
	}

	// 컨텐츠의 registered 상태를 보고 결정함
	// 만약 상태가 진행상황이 아니라면 패스
	if (node->handle != handle)
	{
		node->Release();
		return nullptr;
	}

	return node;
}

FContentNode* CContentManager::GetContentUnsafe(FContentHandle handle)
{
	if (handle.index >= _maxContentCnt)
	{
		return nullptr;
	}

	return &_contentPool[handle.index];
}

void CContentManager::FreeContent(FContentNode* node)
{
	node->Release();
}

FContentNode::FContentNode()
	: handle(FContentHandle::INVALID_HANDLE)
	, refCnt(0)
	, content(nullptr)
{
}

void FContentNode::Init(CContent* c)
{
	InterlockedAdd(&refCnt, 0x80000001);
	content = c;
	content->Init(this);
}

void FContentNode::Reset()
{
	InterlockedExchange(&handle.handle, FContentHandle::INVALID_HANDLE);
	content = nullptr;
}

bool FContentNode::AddRef()
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

bool FContentNode::Release()
{
	if (InterlockedDecrement(&refCnt) == 0x80000000)
	{
		if (InterlockedCompareExchange(&refCnt, 0, 0x80000000) == 0x80000000)
		{
			content->GetContext()->PostJob(&CContent::BeginShutdown);
			return true;
		}
	}

	return false;
}

FContentNode* CContentManager::Alloc(CContent* inContent)
{
	int idx;
	if (!_contentIdxStack.pop(&idx))
	{
		return nullptr;
	}

	int id = InterlockedIncrement(&_nextId);
	FContentHandle handle(id, idx);
	FContentNode* node = &_contentPool[idx];
	InterlockedExchange(&node->handle.handle, handle.handle);

	node->Init(inContent);

	return node;
}

void CContentManager::Free(FContentNode* node)
{
	int index = node->handle.index;
	node->Reset();
	_contentIdxStack.push(index);
}
