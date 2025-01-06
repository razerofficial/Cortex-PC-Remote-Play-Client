#include "mainwindow.h"
#include "streaming/session.h"
#include "backend/computermanager.h"
#include "backend/windowsmessage.h"

MyWindow::MyWindow(LPCWSTR className, LPCWSTR windowTitle)
    : m_hInstance(GetModuleHandle(NULL)), className(className)
    , windowTitle(windowTitle), hWnd(nullptr)
{
    RegisterWndClass();
    CreateWnd();
}

MyWindow::~MyWindow()
{
    DestroyWindow(hWnd);
    UnregisterClass(className.c_str(), GetModuleHandle(NULL));
}

HWND MyWindow::getHandle()
{
    return hWnd;
}

// 注册窗口类
void MyWindow::RegisterWndClass()
{
    WNDCLASSEX wcex = { 0 };
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WindowProc;
    wcex.hInstance = m_hInstance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszClassName = className.c_str();
    RegisterClassEx(&wcex);
}

// 创建窗口
void MyWindow::CreateWnd()
{
    hWnd = CreateWindowEx(
        WS_EX_TOOLWINDOW |
        WS_EX_NOACTIVATE |
        WS_EX_TRANSPARENT |
        WS_EX_LAYERED |
        WS_EX_TOPMOST,
        className.c_str(),
        windowTitle.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        nullptr,
        nullptr,
        m_hInstance,
        this  // 将类实例传递给窗口的创建参数
    );
}

// 窗口消息处理函数
LRESULT CALLBACK MyWindow::WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    MyWindow* pWindow = nullptr;

    if (message == WM_NCCREATE)
    {
        pWindow = static_cast<MyWindow*>(reinterpret_cast<CREATESTRUCT*>(lParam)->lpCreateParams);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pWindow));
    }
    else
    {
        pWindow = reinterpret_cast<MyWindow*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    }

    if (pWindow)
    {
        switch (message)
        {
        case WM_PendingAdd_MESSAGE:
        {
            PendingAddMessage* msg = reinterpret_cast<PendingAddMessage*>(lParam);
            if (nullptr != msg)
            {
                NvComputer* computer = msg->data();
                ComputerManager* computerManager = msg->computerManager();
                computerManager->clientSideAttributeUpdated(computer);
                delete msg;
            }
            return 0;
        }
        case WM_PcMonitor_MESSAGE:
        {
            PcMonitorMessage* msg = reinterpret_cast<PcMonitorMessage*>(lParam);
            if (nullptr != msg)
            {
                NvComputer* computer = msg->data();
                ComputerManager* computerManager = msg->computerManager();
                computerManager->clientSideAttributeUpdated(computer);
                delete msg;
            }
            return 0;
        }
        case WM_Stream_MESSAGE:
        {
            StreamMessage* msg = reinterpret_cast<StreamMessage*>(lParam);
            if (nullptr != msg)
            {
                NvComputer* computer = msg->getNvComputer();
                NvApp app = msg->getNvApp();
                Session session(computer, app);
                session.exec();
                delete msg;
            }
            return 0;
        }
        case WM_Quit_MESSAGE:
        {
            QuitMessage* msg = reinterpret_cast<QuitMessage*>(lParam);
            if (nullptr != msg)
            {
                delete msg;
                PostQuitMessage(0);
            }
            return 0;
        }
        case WM_Setting_MESSAGE:
        {
            SettingMessage* msg = reinterpret_cast<SettingMessage*>(lParam);
            if (nullptr != msg)
            {
                std::string settings = msg->getModifySettings();
                if (!settings.empty())
                    StreamingPreferences::get()->modifySettings(settings);
                else
                    StreamingPreferences::get()->resetSettings();
                delete msg;
            }
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
    }
    else
    {
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
}
