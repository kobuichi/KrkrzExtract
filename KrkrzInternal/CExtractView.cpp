// CExtractView.cpp: 实现文件
//

#include "stdafx.h"
#include "KrkrzInternal.h"
#include "CExtractView.h"
#include "afxdialogex.h"
#include <my.h>
#include <string>
#include "SectionProtector.h"

// CExtractView 对话框

IMPLEMENT_DYNAMIC(CExtractView, CDialogEx)

static CExtractView* Handle = NULL;
static BOOL BaseInited = FALSE;


void CExtractView::BaseInit()
{
	if (BaseInited)
		return;

	InitializeCriticalSection(&m_DumperCS);

	CreateStreamStub = NULL;
	m_HostAlloc = NULL;
	m_CallTVPCreateStreamCall = NULL;
	m_IStreamAdapterVtable = NULL;
	m_InDumpingStatus = TRUE;

	EnterCriticalSection(&m_DumperCS);
	Handle = this;
	LeaveCriticalSection(&m_DumperCS);

	BaseInited = TRUE;
}

CExtractView::CExtractView(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_KRKRZ_DIALOG, pParent)
{
	BaseInit();
}

CExtractView::~CExtractView()
{
	EnterCriticalSection(&m_DumperCS);
	Handle = NULL;
	LeaveCriticalSection(&m_DumperCS);

	DeleteCriticalSection(&m_DumperCS);
}

void CExtractView::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}


BEGIN_MESSAGE_MAP(CExtractView, CDialogEx)
	ON_BN_CLICKED(IDC_STOP_BUTTON, &CExtractView::OnBnClickedStopButton)
	ON_BN_CLICKED(IDC_EXIT_BUTTON, &CExtractView::OnBnClickedExitButton)
	ON_BN_CLICKED(IDC_BEGIN_BUTTON, &CExtractView::OnBnClickedBeginButton)
	ON_WM_CREATE()
END_MESSAGE_MAP()


// CExtractView 消息处理程序


void CExtractView::OnBnClickedStopButton()
{
	SetDumperStatus(FALSE);
}


void CExtractView::OnBnClickedExitButton()
{
	Ps::ExitProcess(0);
}

static ULONG_PTR FixedBuffer[0x4];
static PULONG_PTR gBuffer = FixedBuffer;

IStream* FASTCALL ConvertBStreamToIStream(tTJSBinaryStream* BStream)
{
	IStream*  Stream;
	PVOID     CallHostAlloc;
	ULONG_PTR IStreamAdapterVTableOffset;

	CallHostAlloc = Handle->m_HostAlloc;
	IStreamAdapterVTableOffset = Handle->m_IStreamAdapterVtable;
	Stream = NULL;

	INLINE_ASM
	{
		;; push 0xC;
		;; call CallHostAlloc;
		;; add  esp, 0x4;
		mov eax, gBuffer;
		test eax, eax;
		jz   NO_CREATE_STREAM;
		mov  esi, IStreamAdapterVTableOffset;
		mov  dword ptr[eax], esi; //Vtable 
		mov  esi, BStream;
		mov  dword ptr[eax + 4], esi; //StreamHolder
		mov  dword ptr[eax + 8], 1;   //ReferCount
		mov  Stream, eax;

	NO_CREATE_STREAM:
	}

	return Stream;
}

std::wstring GetFileName(std::wstring Path)
{
	for (auto& ch : Path)
		if (ch == L'/')
			ch = L'\\';

	auto Index = Path.find_last_of(L'\\');
	if (Index != Path.npos)
		return Path.substr(Index + 1, Path.npos);

	return Path;
}

std::wstring GetExtensionName(std::wstring Name)
{
	for (auto& ch : Name)
		ch = CHAR_UPPER(ch);

	auto Index = Name.find_last_of(L'.');
	if (Index != Name.npos)
		return Name.substr(Index + 1, Name.npos);

	return Name;
}


NTSTATUS ProcessFile(IStream* Stream, LPCWSTR OutFileName)
{
	NTSTATUS         Status;
	STATSTG          Stat;
	NtFileDisk       File;
	LARGE_INTEGER    Tranferred, WriteSize, TempSize, Offset;
	ULONG            ReadSize;

	static BYTE Buffer[1024 * 64];

	Offset.QuadPart = 0;
	Stream->Seek(Offset, FILE_BEGIN, NULL);
	Stream->Stat(&Stat, STATFLAG_DEFAULT);
	Tranferred.QuadPart = 0;

	Status = File.Create(OutFileName);
	if (NT_FAILED(Status))
		return Status;

	while (Tranferred.QuadPart < (LONG64)Stat.cbSize.QuadPart)
	{
		Stream->Read(&Buffer, sizeof(Buffer), &ReadSize);
		Tranferred.QuadPart += ReadSize;
		TempSize.QuadPart = 0;
		while (TempSize.QuadPart < ReadSize)
		{
			File.Write(Buffer, ReadSize, &WriteSize);
			TempSize.QuadPart += WriteSize.QuadPart;
		}
	}
	File.Close();
	return Status;
}

tTJSBinaryStream* FASTCALL HookTVPCreateStream(const ttstr& FilePath, ULONG flag)
{
	tTJSBinaryStream* Stream;
	IStream*          IStream;
	LARGE_INTEGER     Offset;
	ULARGE_INTEGER    Bytes;
	WCHAR             FileNamePath[MAX_PATH];

	///auto locker
	SectionProtector<PRTL_CRITICAL_SECTION> Protector(&Handle->m_DumperCS);

	Stream = Handle->CreateStreamStub(FilePath, flag);
	if (Handle->m_InDumpingStatus && flag == TJS_BS_READ && GetExtensionName(FilePath.c_str()) != L"XP3")
	{
		RtlZeroMemory(FileNamePath, sizeof(FileNamePath));
		GetCurrentDirectoryW(countof(FileNamePath), FileNamePath);
		lstrcatW(FileNamePath, L"\\krkrz_dump");

		CreateDirectoryW(FileNamePath, NULL);
		lstrcatW(FileNamePath, L"\\");
		lstrcatW(FileNamePath, GetFileName(FilePath.c_str()).c_str());

		if (Stream)
		{
			Offset.QuadPart = 0;
			RtlZeroMemory(gBuffer, sizeof(gBuffer));
			IStream = ConvertBStreamToIStream(Stream);
			ProcessFile(IStream, FileNamePath);
			IStream->Seek(Offset, FILE_BEGIN, &Bytes);
			RtlZeroMemory(gBuffer, sizeof(gBuffer));
		}
	}

	return Stream;
}


void CExtractView::Init(PVOID HostAlloc, PVOID CallTVPCreateStreamCall, ULONG_PTR IStreamAdapterVtable, std::wstring Version)
{
	BaseInit();

	m_HostAlloc               = HostAlloc;
	m_CallTVPCreateStreamCall = CallTVPCreateStreamCall;
	m_IStreamAdapterVtable    = IStreamAdapterVtable;
	m_Version                 = Version;

	RtlZeroMemory(FixedBuffer, sizeof(FixedBuffer));

	Mp::PATCH_MEMORY_DATA f[] =
	{
		Mp::FunctionJumpVa(m_CallTVPCreateStreamCall, HookTVPCreateStream, &CreateStreamStub)
	};

	Mp::PatchMemory(f, countof(f));


	//SetDumperStatus(TRUE);
}

void CExtractView::SetDumperStatus(BOOL IsInUse)
{
	SectionProtector<PRTL_CRITICAL_SECTION> Protector(&m_DumperCS);

	CWnd*          CtrlWindow;
	CProgressCtrl* ProgressWindow;

	if (IsInUse == m_InDumpingStatus)
		return;
	
	m_InDumpingStatus = IsInUse;
	if (m_InDumpingStatus)
	{
		CtrlWindow = GetDlgItem(IDC_BEGIN_BUTTON);
		if (CtrlWindow)
			CtrlWindow->EnableWindow(FALSE);
		CtrlWindow = GetDlgItem(IDC_STOP_BUTTON);
		if (CtrlWindow)
			CtrlWindow->EnableWindow(TRUE);

		ProgressWindow = (CProgressCtrl*)GetDlgItem(IDC_PROGRESS1);
		if (ProgressWindow)
			ProgressWindow->SetMarquee(TRUE, 30);
	}
	else
	{
		CtrlWindow = GetDlgItem(IDC_BEGIN_BUTTON);
		if (CtrlWindow)
			CtrlWindow->EnableWindow(TRUE);
		CtrlWindow = GetDlgItem(IDC_STOP_BUTTON);
		if (CtrlWindow)
			CtrlWindow->EnableWindow(FALSE);

		ProgressWindow = (CProgressCtrl*)GetDlgItem(IDC_PROGRESS1);
		if (ProgressWindow)
			ProgressWindow->SetMarquee(FALSE, 30);
	}
}


void CExtractView::OnBnClickedBeginButton()
{
	SetDumperStatus(TRUE);
}


int CExtractView::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CDialogEx::OnCreate(lpCreateStruct) == -1)
		return -1;

	return 0;
}


BOOL CExtractView::OnInitDialog()
{
	WCHAR WindowTitle[400];

	CDialogEx::OnInitDialog();

	SetDumperStatus(TRUE);

	RtlZeroMemory(WindowTitle, sizeof(WindowTitle));
	wsprintfW(WindowTitle, L"[X'moe] KrkrzExtract(Version : %s, Built on : %s %s)", 
		m_Version.c_str(),
		MAKE_WSTRING(__DATE__), 
		MAKE_WSTRING(__TIME__));
	SetWindowTextW(WindowTitle);


	return TRUE;
}
