#pragma once

#include "backend/nvapp.h"
#include "backend/computermanager.h"

class Computer
{
public:
    std::vector<NvComputer*> getComputers();

    std::string pairStateToString(NvComputer::PairState state);
    std::string computerStateToString(NvComputer::ComputerState state);
};
