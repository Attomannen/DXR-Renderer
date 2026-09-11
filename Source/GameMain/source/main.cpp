#pragma once

#include <cstdio>
#include "Go.h"

int main(const int argc, const char* argv[])
{
	if (argc > 1)
		printf("argv[1] => %s", argv[1]);
	else
		printf("argc => %d", argc);
	Go();
	return 0;
}