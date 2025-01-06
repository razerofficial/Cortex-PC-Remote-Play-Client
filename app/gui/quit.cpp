#include "quit.h"
#include "backend/computermanager.h"

Quit::Quit()
{

}

Quit::~Quit()
{

}

bool Quit::startExiting(std::string& errorString)
{
    ComputerManager::getInstance()->exitMessageLoop();
    return true;
}
