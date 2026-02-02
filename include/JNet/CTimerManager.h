#pragma once
#include <vector>
#include <unordered_map>
#include <utility>
#include <type_traits>
#include <JCore/CLockFreeStack.h>
#include <JCore/CThread.h>
#include <JNet/CContent.h>
#include <JNet/CContentQueue.h>
#include "CTimerThread.h"

class CContent;
class CTimerThread;
class CAppServer;

class CTimerManager
{
public:
	CTimerManager(CAppServer* server, int maxTimerCnt, int threadCnt);
	~CTimerManager();

	void Start();
	void Stop();

	FTimerHandle PostAfterInternal(int tickAfter, CContent* content, CContentQueue::Func func)
	{
		FTimerNode* node = Alloc(tickAfter, content, func);
		if (node == nullptr)
		{
			return FTimerHandle(FTimerHandle::INVALID_HANDLE);
		}

		int threadIdx = node->handle.id % _timerThreads.size();
		if (!_timerThreads[threadIdx]->Enqueue(node))
		{
			Free(node);
			return FTimerHandle(FTimerHandle::INVALID_HANDLE);
		}

		return node->handle;
	}

	void CancelPost(FTimerHandle handle);

	bool IsValid(FTimerHandle handle);

	FTimerNode* Alloc(int tickAfter, CContent* content, CContentQueue::Func task)
	{
		int idx;
		long id = InterlockedIncrement(&_nextId);
		if (!_timerNodeIdxStack.pop(&idx))
		{
			return nullptr;
		}

		FTimerNode* node = _timerNodePool.data() + idx;
		node->content = content;
		node->lambda = task;
		node->reserveMs = CTimerThread::GetCurrentTick64() + tickAfter;
		node->handle.id = id;
		node->handle.index = idx;
		InterlockedExchange(&node->active, 1);

		return node;
	}

	void Free(FTimerNode* node)
	{
		_timerNodeIdxStack.push(node->handle.index);
	}

private:

	CAppServer* _server;
	long _nextId;
	std::vector<CTimerThread*> _timerThreads;
	std::vector<FTimerNode> _timerNodePool;
	CLockFreeStack<int> _timerNodeIdxStack;
};
