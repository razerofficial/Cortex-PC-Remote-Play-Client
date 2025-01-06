#include "computer.h"

std::vector<NvComputer*> Computer::getComputers()
{
    return ComputerManager::getInstance()->getComputers();
}

std::string Computer::pairStateToString(NvComputer::PairState state)
{
    switch (state)
    {
    case NvComputer::PS_PAIRED:
        return "PS_PAIRED";
    case NvComputer::PS_NOT_PAIRED:
        return "PS_NOT_PAIRED";
    default:
        return "PS_UNKNOWN";
    }
}

std::string Computer::computerStateToString(NvComputer::ComputerState state)
{
    switch (state)
    {
    case NvComputer::CS_ONLINE:
        return "CS_ONLINE";
    case NvComputer::CS_OFFLINE:
        return "CS_OFFLINE";
    default:
        return "CS_UNKNOWN";
    }
}
