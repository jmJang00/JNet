#pragma once
#include <vector>
#include <algorithm>
#include <mutex>
#include <JCore/CThread.h>
#include <JCore/ScopedLock.h>
#include <JNet/CInternalSession.h>
#include <JNet/Types.h>
#include <queue>

class CAppServer;
class CContent;

struct FTimerHandle
{
	enum : unsigned long long
	{
		INVALID_HANDLE = 0xFFFFFFFFFFFFFFFF
	};
	union
	{
		struct
		{
			int index;
			int id;
		};
		unsigned long long handle;
	};
};

struct FTimerNode
{
	FTimerHandle handle;
	uint64 reserveMs;
	long active;
	CContent* content;
	FInternalTask* lambda;

	bool operator<(const FTimerNode& other)
	{
		return reserveMs < other.reserveMs;
	}
};

struct FCompareTimerNode
{
	bool operator()(const FTimerNode* a, const FTimerNode* b) const
	{
		return a->reserveMs > b->reserveMs;
	}
};

class CTimerThread : public CThread
{
public:
	CTimerThread(CAppServer* server);

	static uint64 GetCurrentTick64();

	void Enqueue(FTimerNode* node);

	void Shutdown() override;

	void TimerThread();

private:
	HANDLE _hShutdownEvent;
	HANDLE _hEvent;
	CAppServer* _server; 
	uint64 _minMs;
	std::priority_queue<FTimerNode*, std::vector<FTimerNode*>, FCompareTimerNode> _timerQueue;
	CCSLock _lock;
};