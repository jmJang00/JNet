#include "pch.h"
#include <JNet/CContent.h>
#include <JNet/CContentManager.h>
#include <JNet/CAppServer.h>
#include <JNet/CTimerManager.h>
#include <JNet/CPacket.h>

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

	// 플래그를 바꾸면 콘텐츠 내부에서 플래그 확인 후 종료 절차에 들어감
	CContent* content = node->content;
	if (InterlockedCompareExchange(&content->_registered, 
		CContent::Closing, CContent::Running) != CContent::Running)
	{
		FreeContent(node);
		return false;
	}

	// 컨텐츠의 registered 플래그를 바꾸기 위한 Execute를 풀어놓음
	FreeContent(node);

	// 마지막 종료 신호 
	// 카운트가 0으로 떨어지면 컨텐츠 리셋 함수가 호출됨
	FreeContent(node);

	// 리셋 함수가 호출 완료됨
	content->WaitStopEvent();

	Free(node);

	// 여기서부터 다른 스레드가 해당 컨텐츠 포인터로 등록을 하려고 하면 성공
	InterlockedExchange(&content->_registered, CContent::Cleared);

	return true;
}

bool CContentManager::MoveTo(FSessionId id, FContentHandle to)
{
	// MoveTo는 서버의 네트워크 이벤트, 컨텐츠의 네트워크 이벤트에서만 사용 가능
	CSession* session = _server->GetSession(id);
	if (session == nullptr)
	{
		return false;
	}
	else
	{
		if (session->_invalid)
		{
			session->ReleasePost();
			return false;
		}
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
		_server->OnError(ENetError::MOVE_TO_INVALID_CONTENT, "MoveTo(): Invalid content handle");
		_server->Disconnect(session->_id);
		return false;
	}

	if (to.handle == session->_content.handle)
	{
		_server->FreeSession(session);
		FreeContent(nextContentNode);
		_server->OnError(ENetError::MOVE_TO_INVALID_CONTENT, "MoveTo(): Already assigned content handle");
		return false;
	}

	FContentHandle prevContent(0);
	prevContent.handle = (unsigned long long)InterlockedExchange((uintptr_t*)&session->_content.handle, (uintptr_t)to.handle);

	if (prevContent.handle != FContentHandle::NONE)
	{
		// Leave에서 FreeContent를 해주면서 카운트를 내림
		//GetContentUnsafe(prevContent)->content->Leave(session);
		FSystemMessage msg(ESystemMessageType::MSG_LEAVE, session, (uint64_t)nextContentNode);
		GetContentUnsafe(prevContent)->content->Enqueue(&msg);
	}
	else
	{
		if (nextContentNode == nullptr)
		{
			// 워커스레드로 이동하는 경우
			_server->FreeSession(session);
		}
		else
		{
			// Enter에서 FreeSession을 해줌
			FSystemMessage msg(ESystemMessageType::MSG_ENTER, session, 0);
			nextContentNode->content->Enqueue(&msg);
		}
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
