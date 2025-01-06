#pragma once

#include <string>

class Quit
{
public:
	Quit();
	~Quit();

	bool startExiting(std::string& errorString);
};
