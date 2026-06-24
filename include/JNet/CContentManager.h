#pragma once
#include <vector>
#include <JNet/CSession.h>
#include <JNet/FContentHandle.h>
#include <JCore/CLockFreeStack.h>
#include <JNet/CContent.h>

class CAppServer;
class CContentManager;

struct FContentNode
{
	FContentHandle handle;
	long refCnt;
	CContent* content;

	FContentNode();
	void Init(CContent* c);
	void Reset();
	bool AddRef();
	bool Release();
};

class CContentManager
{
public:
	friend class CContent;

	CContentManager(CAppServer* server, int maxContentCnt);
	~CContentManager();
	FContentHandle Register(CContent* content);
	bool Unregister(FContentHandle handle);
	FContentNode* Alloc(CContent* inContent);
	void Free(FContentNode* node);

	template <typename Lambda>
	bool Execute(FContentHandle handle, Lambda func)
	{
		FContentNode* node = GetContent(handle);
		if (node == nullptr)
		{
			return false;
		}

		func();

		FreeContent(node);
		return true;
	}

	template <typename ContentType, typename MemFunc, typename... Args>
	bool Execute(long statusMask, FContentHandle handle, MemFunc func, Args&&... args)
	{
		FContentNode* node = GetContent(handle);
		if (node == nullptr)
		{
			return false;
		}

		if (!(node->content->_registered & statusMask))
		{
			return false;
		}

		ContentType* content = static_cast<ContentType*>(node->content);
		(content->*func)(std::forward<Args>(args)...);
		FreeContent(node);
		return true;
	}

	template <typename ContentType, typename MemFunc, typename... Args>
	bool Execute(FContentHandle handle, MemFunc func, Args&&... args)
	{
		FContentNode* node = GetContent(handle);
		if (node == nullptr)
		{
			return false;
		}

		ContentType* content = static_cast<ContentType*>(node->content);
		(content->*func)(std::forward<Args>(args)...);
		FreeContent(node);
		return true;
	}

	bool MoveTo(FSessionId id, FContentHandle to);
	CAppServer* _server;

public:
	FContentNode* GetContent(FContentHandle handle);
	void FreeContent(FContentNode* node);

private:
	FContentNode* GetContentUnsafe(FContentHandle handle);

private:
	long _nextId;
	long _maxContentCnt;
	std::vector<FContentNode> _contentPool;
	CLockFreeStack<int> _contentIdxStack;
};
