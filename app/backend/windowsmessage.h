#ifndef WINDOWSMESSAGE_H
#define WINDOWSMESSAGE_H
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "backend/nvapp.h"

const unsigned int WM_PendingAdd_MESSAGE = WM_USER + 1;
const unsigned int WM_PcMonitor_MESSAGE  = WM_USER + 2;
const unsigned int WM_Stream_MESSAGE     = WM_USER + 3;
const unsigned int WM_Quit_MESSAGE       = WM_USER + 4;
const unsigned int WM_Setting_MESSAGE    = WM_USER + 5;

class NvComputer;
class ComputerManager;

class PendingAddMessage
{
public:
    explicit PendingAddMessage(NvComputer* computer, ComputerManager* computerManager);
    NvComputer* data() { return m_computer; }
    ComputerManager* computerManager() { return m_computerManager; }
    unsigned int messageType() { return m_message; }

private:
    unsigned int m_message;
    NvComputer* m_computer;
    ComputerManager* m_computerManager;
};

class PcMonitorMessage
{
public:
    explicit PcMonitorMessage(NvComputer* computer, ComputerManager* computerManager);
    NvComputer* data() { return m_computer; }
    ComputerManager* computerManager() { return m_computerManager; }
    unsigned int messageType() { return m_message; }

private:
    unsigned int m_message;
    NvComputer* m_computer;
    ComputerManager* m_computerManager;
};

class StreamMessage
{
public:
    explicit StreamMessage(NvComputer* computer, NvApp app);
    unsigned int messageType() { return m_message; }
    NvComputer* getNvComputer() { return m_computer; }
    NvApp getNvApp() { return m_app; }

private:
    unsigned int m_message;
    NvComputer* m_computer;
    NvApp m_app;
};

class QuitMessage
{
public:
    explicit QuitMessage();
    unsigned int messageType() { return m_message; }

private:
    unsigned int m_message;
};

class SettingMessage
{
public:
    explicit SettingMessage(const std::string& settings);
    explicit SettingMessage();
    unsigned int messageType() { return m_message; }
    std::string getModifySettings() { return m_settings; };

private:
    std::string m_settings;
    unsigned int m_message;
};

#endif // WINDOWSMESSAGE_H
