#include "pch.h"
#include <JNet/CTimerManager.h>
#include <JNet/CContent.h>

CTimerManager::CTimerManager(CAppServer* server, int maxTimerCnt, int threadCnt)
	: _timerNodeIdxStack(maxTimerCnt)
	, _nextId(0)
{
	for (int i = 0; i < threadCnt; i++)
	{
		_timerThreads.push_back(new CTimerThread(server));
	}

	_timerNodePool.resize(maxTimerCnt);
	for (int i = maxTimerCnt - 1; i >= 0; i--)
	{
		_timerNodeIdxStack.push(i);
	}
	_server = server;
}

CTimerManager::~CTimerManager()
{
	for (int i = 0; i < _timerThreads.size(); i++)
	{
		_timerThreads[i]->Close();
		delete _timerThreads[i];
	}
}

void CTimerManager::Start()
{
	for (int i = 0; i < _timerThreads.size(); i++)
	{
		_timerThreads[i]->Resume();
	}
}

void CTimerManager::Stop()
{
	for (int i = 0; i < _timerThreads.size(); i++)
	{
		_timerThreads[i]->Shutdown();
		_timerThreads[i]->Wait();
	}
}

void CTimerManager::CancelPost(FTimerHandle handle)
{
	if (handle.index < 0 || handle.index > _timerNodePool.size())
	{
		return;
	}

	FTimerNode* node = &_timerNodePool[handle.index];
	if (node->handle.id == handle.id)
	{
		if (InterlockedExchange(&node->active, 0) == 1)
		{
			node->content->Release();
		}
	}
}

bool CTimerManager::IsValid(FTimerHandle handle)
{
	if (handle.index < 0 || handle.index > _timerNodePool.size())
	{
		return false;
	}

	FTimerNode* node = &_timerNodePool[handle.index];
	if (node->handle.id != handle.id || node->active == 0)
	{
		return false;
	}

	return true;
}
