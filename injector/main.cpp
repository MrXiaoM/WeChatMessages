#include "Shlwapi.h"
#include "framework.h"
#include "resource.h"
#include <filesystem>
#include <process.h>
#include <tlhelp32.h>
#include <windows.h>
#include <stdio.h>

#include "injector.h"
#include "main.h"
#include "util.h"
#include "spy.h"
#include "log.h"

static BOOL injected              = false;
static HANDLE wcProcess           = NULL;
static HMODULE spyBase            = NULL;
static WCHAR spyDllPath[MAX_PATH] = { 0 };
static HWND dlg                   = NULL;

static int GetDllPath(bool debug, wchar_t *dllPath)
{
    lstrcpy(dllPath, String2Wstring(std::filesystem::current_path().string()).c_str());
    if (debug) {
        PathAppend(dllPath, WCFSPYDLL_DEBUG);
    } else {
        PathAppend(dllPath, WCFSPYDLL);
    }

    if (!PathFileExists(dllPath)) {
        LOG_WARN("DLL 文件不存在: {}", Wstring2String(dllPath));
        MessageBox(NULL, dllPath, L"文件不存在", 0);
        return ERROR_FILE_NOT_FOUND;
    }

    return 0;
}

int WxInitInject(bool debug)
{
    bool firstOpen = true;
    int status  = 0;
    DWORD wcPid = 0;

    status = GetDllPath(debug, spyDllPath);
    if (status != 0) {
        return status;
    }
    
    LOG_INFO("[WxInitInject] 已找到 spy.dll 路径: {}", Wstring2String(spyDllPath));

    status = OpenWeChat(&wcPid, &firstOpen);
    if (status != 0) {
        LOG_WARN("[WxInitInject] 微信打开失败");
        MessageBox(NULL, L"打开微信失败", L"WxInitInject", 0);
        return status;
    }

    LOG_INFO("微信 PID: {}", to_string(wcPid));

    if (!IsProcessX64(wcPid)) {
        LOG_WARN("[WxInitInject] 只支持 64 位微信");
        MessageBox(NULL, L"只支持 64 位微信", L"WxInitInject", 0);
        return -1;
    }

    if (firstOpen) {
        LOG_INFO("[WxInitInject] 正在等待微信启动");
        if (dlg != NULL) SetDlgItemTextA(dlg, ID_DLL_NAME, "正在等待微信启动");
        Sleep(2000);
    }
    wcProcess = InjectDll(wcPid, spyDllPath, &spyBase);
    if (wcProcess == NULL) {
        LOG_WARN("[WxInitInject] 注入失败");
        MessageBox(NULL, L"注入失败", L"WxInitInject", 0);
        return -1;
    }

    PortPath_t pp = { 0 };
    pp.port = 8081;
    sprintf_s(pp.path, MAX_PATH, "%s", std::filesystem::current_path().string().c_str());

    if (!CallDllFuncEx(wcProcess, spyDllPath, spyBase, "InitSpy", (LPVOID)&pp, sizeof(PortPath_t), NULL)) {
        LOG_WARN("[WxInitInject] 初始化失败");
        MessageBox(NULL, L"初始化失败", L"WxInitInject", 0);
        return -1;
    }

    LOG_INFO("注入完成! 详细信息请见 spy.dll 输出日志");

    injected = true;
    return 0;
}

int WxDestroyInject()
{
    if (!injected) {
        return -1;
    }

    if (!CallDllFunc(wcProcess, spyDllPath, spyBase, "CleanupSpy", NULL)) {
        return -2;
    }

    if (!EjectDll(wcProcess, spyBase)) {
        return -3; // TODO: Unify error codes
    }

    LOG_INFO("[WxDestroyInject] 已取消注入并卸载 DLL");
    injected = false;
    return 0;
}

//所有的消息处理函数
INT_PTR CALLBACK DialogProc(HWND hwndDlg, UINT   uMsg, WPARAM wParam, LPARAM lParam)
{
    wchar_t title[99] = L"WeChat Injector for v";
    int i;
    switch (uMsg)
    {
    case WM_INITDIALOG:
        dlg = hwndDlg;
        wcscat_s(title, SUPPORT_VERSION);
        SetWindowTextW(hwndDlg, title);
        SetDlgItemTextA(hwndDlg, ID_DLL_NAME, "软件已启动");
        break;
        //按钮点击事件 处理
    case WM_COMMAND:
        if (wParam == INJECT_DLL && !injected)
        {
            WxInitInject(false);
            if (injected) {
                SetDlgItemTextA(hwndDlg, ID_DLL_NAME, "已注入到 WeChatWin.dll");
            }
        }
        if (wParam == UN_DLL && injected)
        {
            i = WxDestroyInject();
            if (i == 0) {
                SetDlgItemTextA(hwndDlg, ID_DLL_NAME, "已取消注入");
            }
            if (i == -1) {
                LOG_WARN("[WxDestroyInject] 当前没有被注入，无需取消注入");
            }
        }
        break;
    case WM_CLOSE:
        WxDestroyInject();
        EndDialog(hwndDlg, 0);
        break;
    default:
        break;
    }
    return FALSE;
}

//函数开始
int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    InitLogger("Injector", std::filesystem::current_path().string() + "/logs/injector.log");

    DialogBox(hInstance, MAKEINTRESOURCE(ID_MAIN), NULL, &DialogProc);

    return 0;
}
