// KrkrzInternal.h: KrkrzInternal DLL 的主标头文件
//

#pragma once

#ifndef __AFXWIN_H__
	#error "在包含此文件之前包含“stdafx.h”以生成 PCH 文件"
#endif

#include "resource.h"		// 主符号
#include "CExtractView.h"
#include "tp_stub.h"

class CKrkrzInternalApp : public CWinApp
{
public:
	CKrkrzInternalApp();

// 重写
public:
	virtual BOOL InitInstance();
	static CKrkrzInternalApp* GetApp();

	BOOL InitKrkrExtract(HMODULE hModule);
	NTSTATUS InitHook(LPCWSTR ModuleName, PVOID ImageBase);
	BOOL GetTVPCreateStreamCall();

	DECLARE_MESSAGE_MAP()

public:
	PVOID                     m_HostAlloc;
	PVOID                     m_CallTVPCreateStreamCall;
	ULONG_PTR                 m_IStreamAdapterVtable;
	CExtractView              m_Viewer;

	iTVPFunctionExporter *    m_TVPFunctionExporter;
	BOOL                      m_Inited;
	PVOID                     m_Module;
	WCHAR                     m_Path[MAX_PATH];

	using V2LinkFunc = HRESULT(NTAPI*)(iTVPFunctionExporter *);
	V2LinkFunc                m_V2LinkStub;
	CRITICAL_SECTION          m_LoadCS;

};
