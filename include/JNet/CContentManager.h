#pragma once
#include <JNet/Session.h>

class CContent;
class CAppServer;

class CContentManager
{
public:
	CContentManager(CAppServer* server);
	~CContentManager();
	bool Register(CContent* content);
	bool Unregister(CContent* content);
	bool MoveTo(SessionId id, CContent* to);
	CAppServer* _server;
};
