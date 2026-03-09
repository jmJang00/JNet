#pragma once
#include <JNet/CSession.h>

enum class ESystemMessageType : unsigned char
{
	MSG_ENTER,
	MSG_LEAVE,
	MSG_RELEASE,
};

struct FSystemMessage
{
	FSystemMessage() = default;
	FSystemMessage(ESystemMessageType type, CSession* session, uint64_t payload)
		: type(type)
		, session(session)
		, payload(payload)
	{
	}

	ESystemMessageType type;
	CSession* session;
	uint64_t payload;
};
