#pragma once
#include <JNet/Session.h>

enum class ESystemMessageType : unsigned char
{
	MSG_ENTER,
	MSG_LEAVE,
	MSG_RELEASE,
};

struct FSystemMessage
{
	FSystemMessage() = default;
	FSystemMessage(ESystemMessageType type, Session* session, uint64_t payload)
		: type(type)
		, session(session)
		, payload(payload)
	{
	}

	ESystemMessageType type;
	Session* session;
	uint64_t payload;
};
