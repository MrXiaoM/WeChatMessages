#pragma once

#include "framework.h"
#include <map>
#include <string>

typedef uint64_t QWORD;

using namespace std;

typedef map<int, string> MsgTypes_t;

typedef struct {
    bool is_self;
    bool is_group;
    uint32_t type;
    uint32_t ts;
    uint64_t id;
    string sender;
    string roomid;
    string content;
    string sign;
    string thumb;
    string extra;
    string xml;
} WxMsg_t;

struct WxString {
    const wchar_t *wptr;
    DWORD size;
    DWORD capacity;
    const char *ptr;
    DWORD clen;
    WxString()
    {
        wptr     = NULL;
        size     = 0;
        capacity = 0;
        ptr      = NULL;
        clen     = 0;
    }

    WxString(std::wstring &ws)
    {
        wptr     = ws.c_str();
        size     = (DWORD)ws.size();
        capacity = (DWORD)ws.capacity();
        ptr      = NULL;
        clen     = 0;
    }
};

typedef struct RawVector {
#ifdef _DEBUG
    QWORD head;
#endif
    QWORD start;
    QWORD finish;
    QWORD end;
} RawVector_t;
