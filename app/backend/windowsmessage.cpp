#include "windowsmessage.h"

PendingAddMessage::PendingAddMessage(NvComputer* computer, ComputerManager* computerManager)
    : m_message(WM_PendingAdd_MESSAGE)
    , m_computer(computer)
    , m_computerManager(computerManager)
{

}

PcMonitorMessage::PcMonitorMessage(NvComputer* computer, ComputerManager* computerManager)
    : m_message(WM_PcMonitor_MESSAGE)
    , m_computer(computer)
    , m_computerManager(computerManager)
{

}

StreamMessage::StreamMessage(NvComputer* computer, NvApp app)
    : m_message(WM_Stream_MESSAGE)
    , m_computer(computer)
    , m_app(app)
{

}

QuitMessage::QuitMessage()
    : m_message(WM_Quit_MESSAGE)
{

}

SettingMessage::SettingMessage(const std::string& settings)
    : m_settings(settings)
    , m_message(WM_Setting_MESSAGE)
{

}

SettingMessage::SettingMessage()
    : m_settings("")
    , m_message(WM_Setting_MESSAGE)
{

}
