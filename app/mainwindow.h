#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

class MyWindow {
public:
    MyWindow(LPCWSTR className, LPCWSTR windowTitle);

    ~MyWindow();

    HWND getHandle();

private:
    // 注册窗口类
    void RegisterWndClass();

    // 创建窗口
    void CreateWnd();

    // 窗口消息处理函数
    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
    HINSTANCE m_hInstance;
    std::wstring className;
    std::wstring windowTitle;
    HWND hWnd;
};
