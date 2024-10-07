#pragma execution_character_set("utf-8")

#include "MinHook.h"
#include "framework.h"
#include <condition_variable>
#include <mutex>
#include <queue>

#include "spy.h"
#include "log.h"
#include "receive_msg.h"
#include "util.h"

#include <stdlib.h>
#include "nlohmann/json.hpp"
#include "curl/curl.h"
#pragma comment(lib, "libcurl.lib")

// Defined in spy.cpp
extern QWORD g_WeChatWinDllAddr;

// 接收消息call所在地址
#define OS_RECV_MSG_CALL    0x2141E80

// 参数 消息ID 相对地址
#define OS_RECV_MSG_ID      0x30
// 参数 消息类型 相对地址
#define OS_RECV_MSG_TYPE    0x38
// 参数 是否自身 相对地址
#define OS_RECV_MSG_SELF    0x3C
// 参数 时间戳 相对地址
#define OS_RECV_MSG_TS      0x44
// 参数 房间ID 相对地址
#define OS_RECV_MSG_ROOMID  0x48
// 参数 消息内容 相对地址
#define OS_RECV_MSG_CONTENT 0x88
// 参数 wxid 相对地址
#define OS_RECV_MSG_WXID    0x240 // 0x80
// 参数 签名 相对地址
#define OS_RECV_MSG_SIGN    0x260 // 0xA0
// 参数 缩略图 相对地址
#define OS_RECV_MSG_THUMB   0x280 // 0xC0
// 参数 额外信息 相对地址
#define OS_RECV_MSG_EXTRA   0x2A0 // 0xE0
// 参数 XML 相对地址
#define OS_RECV_MSG_XML     0x308 // 0x148

typedef QWORD (*RecvMsg_t)(QWORD, QWORD);

static bool gIsListening = false;
static RecvMsg_t funcRecvMsg = nullptr;
static RecvMsg_t realRecvMsg = nullptr;
static bool isMH_Initialized = false;

const MsgTypes_t msgTypes = {
    { 0x00, "朋友圈消息" },
    { 0x01, "文字" },
    { 0x03, "图片" },
    { 0x22, "语音" },
    { 0x25, "好友确认" },
    { 0x28, "POSSIBLEFRIEND_MSG" },
    { 0x2A, "名片" },
    { 0x2B, "视频" },
    { 0x2F, "石头剪刀布 | 表情图片" },
    { 0x30, "位置" },
    { 0x31, "共享实时位置、文件、转账、链接" },
    { 0x32, "VOIPMSG" },
    { 0x33, "微信初始化" },
    { 0x34, "VOIPNOTIFY" },
    { 0x35, "VOIPINVITE" },
    { 0x3E, "小视频" },
    { 0x42, "微信红包" },
    { 0x270F, "SYSNOTICE" },
    { 0x2710, "红包、系统消息" },
    { 0x2712, "撤回消息" },
    { 0x100031, "搜狗表情" },
    { 0x1000031, "链接" },
    { 0x1A000031, "微信红包" },
    { 0x20010031, "红包封面" },
    { 0x2D000031, "视频号视频" },
    { 0x2E000031, "视频号名片" },
    { 0x31000031, "引用消息" },
    { 0x37000031, "拍一拍" },
    { 0x3A000031, "视频号直播" },
    { 0x3A100031, "商品链接" },
    { 0x3A200031, "视频号直播" },
    { 0x3E000031, "音乐链接" },
    { 0x41000031, "文件" },
};

static string to_string(WxMsg_t wxMsg) {
    nlohmann::json j = {
            { "id", wxMsg.id },
            { "type", wxMsg.type },
            { "is_self", wxMsg.is_self },
            { "ts", wxMsg.ts },
            { "content", wxMsg.content },
            { "sign", wxMsg.sign },
            { "xml", wxMsg.xml },
            { "roomid", wxMsg.roomid },
            { "is_group", wxMsg.is_group },
            { "sender", wxMsg.sender },
            { "thumb", wxMsg.thumb },
            { "extra", wxMsg.extra }
    };
    return j.dump(4);
}

static void notice(string content)
{
    CURL* curl;
    CURLcode res;
    long res_code = 0;
    const char* postContent = content.c_str();
    struct curl_slist* header = NULL;
    char url[MAX_PATH];
    
    res = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (res != CURLE_OK)
        fprintf(stderr, "curl_global_init() failed: %s\n",
            curl_easy_strerror(res));
    curl = curl_easy_init();
    if (curl) {
        LOG_INFO("Message received: {}", content);

        strcpy_s(url, MAX_PATH, baseUrl);
        strcat_s(url, MAX_PATH, "api/receive");

        curl_easy_setopt(curl, CURLOPT_URL, url);
        
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postContent);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(postContent));

        header = curl_slist_append(header, "Accept: */*");
        header = curl_slist_append(header, "Content-Type: application/json; charset=utf-8");
        header = curl_slist_append(header, "Connection: Close");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header);
        curl_easy_setopt(curl, CURLOPT_HEADER, 0L);

        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

        curl_easy_setopt(curl, CURLOPT_USERAGENT, "curl/8.10.1");

        res = curl_easy_perform(curl);
        if (res != CURLE_OK)
            fprintf(stderr, "curl_easy_perform() failed: %s\n",
                curl_easy_strerror(res));
        res = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &res_code);

        curl_slist_free_all(header);
        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();
}

static QWORD DispatchMsg(QWORD arg1, QWORD arg2)
{
    WxMsg_t wxMsg = { 0 };
    string str;
    try {
        wxMsg.id      = GET_QWORD(arg2 + OS_RECV_MSG_ID);
        wxMsg.type    = GET_DWORD(arg2 + OS_RECV_MSG_TYPE);
        wxMsg.is_self = GET_DWORD(arg2 + OS_RECV_MSG_SELF);
        wxMsg.ts      = GET_DWORD(arg2 + OS_RECV_MSG_TS);
        wxMsg.content = GetStringByWstrAddr(arg2 + OS_RECV_MSG_CONTENT);
        wxMsg.sign    = GetStringByWstrAddr(arg2 + OS_RECV_MSG_SIGN);
        wxMsg.xml     = GetStringByWstrAddr(arg2 + OS_RECV_MSG_XML);

        string roomid = GetStringByWstrAddr(arg2 + OS_RECV_MSG_ROOMID);
        wxMsg.roomid  = roomid;
        if (roomid.find("@chatroom") != string::npos) { // 群 ID 的格式为 xxxxxxxxxxx@chatroom
            wxMsg.is_group = true;
            if (wxMsg.is_self) {
                wxMsg.sender = "self";
            } else {
                wxMsg.sender = GetStringByWstrAddr(arg2 + OS_RECV_MSG_WXID);
            }
        } else {
            wxMsg.is_group = false;
            if (wxMsg.is_self) {
                wxMsg.sender = "self";
            } else {
                wxMsg.sender = roomid;
            }
        }
        wxMsg.thumb = GetStringByWstrAddr(arg2 + OS_RECV_MSG_THUMB);
        wxMsg.extra = GetStringByWstrAddr(arg2 + OS_RECV_MSG_EXTRA);
        
        str = to_string(wxMsg);
        notice(str);
    } catch (const std::exception &e) {
        LOG_ERROR(GB2312ToUtf8(e.what()).c_str());
    } catch (...) {
        LOG_ERROR("Unknow exception.");
    }

    return realRecvMsg(arg1, arg2);
}

static MH_STATUS InitializeHook()
{
    if (isMH_Initialized) {
        return MH_OK;
    }
    MH_STATUS status = MH_Initialize();
    if (status == MH_OK) {
        isMH_Initialized = true;
    }
    return status;
}

static MH_STATUS UninitializeHook()
{
    if (!isMH_Initialized) {
        return MH_OK;
    }
    if (gIsListening) {
        return MH_OK;
    }
    MH_STATUS status = MH_Uninitialize();
    if (status == MH_OK) {
        isMH_Initialized = false;
    }
    return status;
}

void ListenMessage()
{
    MH_STATUS status = MH_UNKNOWN;
    if (gIsListening) {
        LOG_WARN("gIsListening");
        return;
    }
    funcRecvMsg = (RecvMsg_t)(g_WeChatWinDllAddr + OS_RECV_MSG_CALL);

    status = InitializeHook();
    if (status != MH_OK) {
        LOG_ERROR("MH_Initialize failed: {}", to_string(status));
        return;
    }

    status = MH_CreateHook(funcRecvMsg, &DispatchMsg, reinterpret_cast<LPVOID *>(&realRecvMsg));
    if (status != MH_OK) {
        LOG_ERROR("MH_CreateHook failed: {}", to_string(status));
        return;
    }

    status = MH_EnableHook(funcRecvMsg);
    if (status != MH_OK) {
        LOG_ERROR("MH_EnableHook failed: {}", to_string(status));
        return;
    }

    gIsListening = true;
}

void UnListenMessage()
{
    MH_STATUS status = MH_UNKNOWN;
    if (!gIsListening) {
        return;
    }

    status = MH_DisableHook(funcRecvMsg);
    if (status != MH_OK) {
        LOG_ERROR("MH_DisableHook failed: {}", to_string(status));
        return;
    }

    gIsListening = false;

    status = UninitializeHook();
    if (status != MH_OK) {
        LOG_ERROR("MH_Uninitialize failed: {}", to_string(status));
        return;
    }
}
