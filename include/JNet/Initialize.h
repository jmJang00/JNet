#pragma once

class JNetInit
{
public:
	static void Initialize();
	static void Release();

	static long _initLock;
	static long _init;
};
