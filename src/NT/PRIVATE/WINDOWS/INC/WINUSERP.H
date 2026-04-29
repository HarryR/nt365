/*++ BUILD Version: 0003    // Increment this if a change has global effects

Copyright (c) 1985-91, Microsoft Corporation

Module Name:

    winuserp.h

Abstract:

    Private
    Procedure declarations, constant definitions and macros for the User
    component.

--*/
#ifndef _WINUSERP_
#define _WINUSERP_

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
#ifdef STRICT
#if(WINVER < 0x0400)
typedef BOOL (CALLBACK* DRAWSTATEPROC)(HDC hdc, LPARAM lData, WPARAM wData, int cx, int cy);
#endif /* WINVER < 0x0400 */
#else /* !STRICT */
#if(WINVER < 0x0400)
typedef FARPROC DRAWSTATEPROC;
#endif /* WINVER < 0x0400 */
#endif /* !STRICT */

#ifdef STRICT

typedef BOOL (CALLBACK* NAMEENUMPROCA)(LPSTR, LPARAM);
typedef BOOL (CALLBACK* NAMEENUMPROCW)(LPWSTR, LPARAM);

typedef NAMEENUMPROCA   WINSTAENUMPROCA;
typedef NAMEENUMPROCA   DESKTOPENUMPROCA;
typedef NAMEENUMPROCW   WINSTAENUMPROCW;
typedef NAMEENUMPROCW   DESKTOPENUMPROCW;

#else /* !STRICT */

typedef FARPROC NAMEENUMPROCA;
typedef FARPROC NAMEENUMPROCW;
typedef FARPROC WINSTAENUMPROCA;
typedef FARPROC DESKTOPENUMPROCA;
typedef FARPROC WINSTAENUMPROCW;
typedef FARPROC DESKTOPENUMPROCW;

#endif /* !STRICT */

#ifdef UNICODE
typedef WINSTAENUMPROCW     WINSTAENUMPROC;
typedef DESKTOPENUMPROCW    DESKTOPENUMPROC;
#else  /* !UNICODE */
typedef WINSTAENUMPROCA     WINSTAENUMPROC;
typedef DESKTOPENUMPROCA    DESKTOPENUMPROC;
#endif /* UNICODE */

#define RT_MENUEX       MAKEINTRESOURCE(13)     // RT_MENU subtype
#define RT_NAMETABLE    MAKEINTRESOURCE(15)     // removed in 3.1
#define RT_DIALOGEX     MAKEINTRESOURCE(18)     // RT_DIALOG subtype
#if(WINVER < 0x0400)
#define RT_PLUGPLAY     MAKEINTRESOURCE(19)
#define RT_VXD          MAKEINTRESOURCE(20)
#endif /* WINVER < 0x0400 */
#define RT_LAST         MAKEINTRESOURCE(20)
#define RT_AFXFIRST     MAKEINTRESOURCE(0xF0)   // reserved: AFX
#define RT_AFXLAST      MAKEINTRESOURCE(0xFF)   // reserved: AFX
#define SB_MAX              3
#define SB_CMD_MAX          8
/* #define VK_COPY        0x2C not used by keyboards. */
#if(WINVER < 0x0400)
#define WH_CALLWNDPROCRET  12
#define WH_MINHOOK         WH_MIN
#define WH_MAXHOOK         WH_MAX
#define WH_CHOOKS          (WH_MAXHOOK - WH_MINHOOK + 1)
#endif /* WINVER < 0x0400 */
#define MSGF_CBTHOSEBAGSUSEDTHIS  7
#if(WINVER < 0x0400)
#define HSHELL_WINDOWACTIVATED      4
#define HSHELL_GETMINRECT           5
#define HSHELL_REDRAW               6
#define HSHELL_TASKMAN              7
#define HSHELL_LANGUAGE             8
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
/*
 * Message structure used by WH_CALLWNDPROCRET
 */
typedef struct tagCWPRETSTRUCT {
    LRESULT lResult;
    LPARAM  lParam;
    WPARAM  wParam;
    UINT    message;
    HWND    hwnd;
} CWPRETSTRUCT, *PCWPRETSTRUCT, NEAR *NPCWPRETSTRUCT, FAR *LPCWPRETSTRUCT;
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
/*
 * Structure used by WH_HARDWARE
 */
typedef struct tagHARDWAREHOOKSTRUCT {
    HWND    hwnd;
    UINT    message;
    WPARAM  wParam;
    LPARAM  lParam;
} HARDWAREHOOKSTRUCT, FAR *LPHARDWAREHOOKSTRUCT, *PHARDWAREHOOKSTRUCT;
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
typedef struct tagCHARSETINFO {
    UINT    ciCharset;
    UINT    ciACP;
    DWORD   ciSigCP[2];
    DWORD   ciSigU[4];
    } CHARSETINFO, *PCHARSETINFO, NEAR *NPCHARSETINFO, FAR *LPCHARSETINFO;

WINUSERAPI BOOL WINAPI TranslateCharsetInfo( DWORD FAR *lpSrc, LPCHARSETINFO lpCs, DWORD dwFlags);

#define TCI_SRCCHARSET  1
#define TCI_SRCCODEPAGE 2
#define TCI_SRCFONTSIG  3
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#define KLF_REPLACELANG     0x00000010
#define KLF_NOTELLSHELL     0x00000080
#endif /* WINVER < 0x0400 */
#define KLF_INITTIME        0x80000000
#if(WINVER < 0x0400)
WINUSERAPI
HKL
WINAPI
GetKeyboardLayout(
    DWORD dwLayout
);
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
typedef struct tagWNDCLASSEXA {
    UINT        cbSize;
    /* Win 3.x */
    UINT        style;
    WNDPROC     lpfnWndProc;
    int         cbClsExtra;
    int         cbWndExtra;
    HINSTANCE   hInstance;
    HICON       hIcon;
    HCURSOR     hCursor;
    HBRUSH      hbrBackground;
    LPCSTR      lpszMenuName;
    LPCSTR      lpszClassName;
    /* Win 4.0 */
    HICON       hIconSm;
} WNDCLASSEXA, *PWNDCLASSEXA, NEAR *NPWNDCLASSEXA, FAR *LPWNDCLASSEXA;
typedef struct tagWNDCLASSEXW {
    UINT        cbSize;
    /* Win 3.x */
    UINT        style;
    WNDPROC     lpfnWndProc;
    int         cbClsExtra;
    int         cbWndExtra;
    HINSTANCE   hInstance;
    HICON       hIcon;
    HCURSOR     hCursor;
    HBRUSH      hbrBackground;
    LPCWSTR     lpszMenuName;
    LPCWSTR     lpszClassName;
    /* Win 4.0 */
    HICON       hIconSm;
} WNDCLASSEXW, *PWNDCLASSEXW, NEAR *NPWNDCLASSEXW, FAR *LPWNDCLASSEXW;
#ifdef UNICODE
typedef WNDCLASSEXW WNDCLASSEX;
typedef PWNDCLASSEXW PWNDCLASSEX;
typedef NPWNDCLASSEXW NPWNDCLASSEX;
typedef LPWNDCLASSEXW LPWNDCLASSEX;
#else
typedef WNDCLASSEXA WNDCLASSEX;
typedef PWNDCLASSEXA PWNDCLASSEX;
typedef NPWNDCLASSEXA NPWNDCLASSEX;
typedef LPWNDCLASSEXA LPWNDCLASSEX;
#endif // UNICODE
#endif /* WINVER < 0x0400 */
BOOL WowWaitForMsgAndEvent(HANDLE hevent);
#define RST_DONTATTACHQUEUE     0x00000001
#define RST_DONTJOURNALATTACH   0x00000002
WINUSERAPI VOID WINAPI RegisterSystemThread(DWORD flags, DWORD reserved);
#define RST_DONTATTACHQUEUE       0x00000001
#define RST_DONTJOURNALATTACH     0x00000002
#define GWL_WOWWORDS        (-1)
#define GWL_WOWDWORD1       (-30)
#define GWL_WOWDWORD2       (-31)
#define GWL_WOWDWORD3       (-32)
#define GCL_WOWWORDS        (-27)
#define GCL_WOWDWORD1       (-28)
#define GCL_WOWDWORD2       (-29)
#if(WINVER < 0x0400)
#define GCL_HICONSM         (-34)
#endif /* WINVER < 0x0400 */
#define WM_SIZEWAIT                     0x0004
#define WM_SETVISIBLE                   0x0009
#define WM_SYSTEMERROR                  0x0017
/*
 * This is used by DefWindowProc() and DefDlgProc(), it's the 16-bit version
 * of the WM_CTLCOLORBTN, WM_CTLCOLORDLG, ... messages.
 */
#define WM_CTLCOLOR                     0x0019
#define WM_LOGOFF                       0x0025
#define WM_ALTTABACTIVE                 0x0029
#define WM_FILESYSCHANGE                0x0034
#define WM_SHELLNOTIFY                  0x0034
#define WM_ISACTIVEICON                 0x0035
#define WM_QUERYPARKICON                0x0036
#define WM_WINHELP                      0x0038
#define WM_FULLSCREEN                   0x003A
#define WM_CLIENTSHUTDOWN               0x003B
#define WM_DDEMLEVENT                   0x003C
#define MM_CALCSCROLL                   0x003F
#define WM_TESTING                      0x0040
#define WM_OTHERWINDOWCREATED           0x0042
#define WM_OTHERWINDOWDESTROYED         0x0043
#define WM_COPYGLOBALDATA               0x0049
#define WM_LOGONNOTIFY                  0x004C
#if(WINVER < 0x0400)
#define WM_KEYF1                        0x004D
#define WM_NOTIFY                       0x004E
#define WM_ACCESS_WINDOW                0x004F
#define WM_INPUTLANGCHANGEREQUEST       0x0050
#define WM_INPUTLANGCHANGE              0x0051
#define WM_TCARD                        0x0052
#define WM_HELP                         0x0053
#define WM_USERCHANGED                  0x0054
#define WM_CONTEXTMENU                  0x007B
#define WM_STYLECHANGING                0x007C
#define WM_STYLECHANGED                 0x007D
#define WM_DISPLAYCHANGE                0x007E
#define WM_GETICON                      0x007F
#define WM_SETICON                      0x0080
#endif /* WINVER < 0x0400 */
#define WM_FINALDESTROY                 0x0070  /* really destroy (window not locked) */
#define WM_MEASUREITEM_CLIENTDATA       0x0071  /* WM_MEASUREITEM bug clientdata thunked already */
#define WM_SYNCPAINT                    0x0088
#define WM_SYNCTASK                     0x0089
#define WM_YOMICHAR                     0x0108
#define WM_CONVERTREQUEST               0x010A
#define WM_CONVERTRESULT                0x010B
#define WM_INTERIM                      0x010C
#define WM_SYSTIMER                     0x0118
#define WM_LBTRACKPOINT                 0x0131
#define MN_FIRST                        0x01E0
#define MN_SETHMENU                     (MN_FIRST + 0)
#define MN_GETHMENU                     (MN_FIRST + 1)
#define MN_SIZEWINDOW                   (MN_FIRST + 2)
#define MN_OPENHIERARCHY                (MN_FIRST + 3)
#define MN_CLOSEHIERARCHY               (MN_FIRST + 4)
#define MN_SELECTITEM                   (MN_FIRST + 5)
#define MN_CANCELMENUS                  (MN_FIRST + 6)
#define MN_SELECTFIRSTVALIDITEM         (MN_FIRST + 7)
#define MN_GETPPOPUPMENU                (MN_FIRST + 10)
#define MN_FINDMENUWINDOWFROMPOINT      (MN_FIRST + 11)
#define MN_SHOWPOPUPWINDOW              (MN_FIRST + 12)
#define MN_BUTTONDOWN                   (MN_FIRST + 13)
#define MN_MOUSEMOVE                    (MN_FIRST + 14)
#define MN_BUTTONUP                     (MN_FIRST + 15)
#define MN_SETTIMERTOOPENHIERARCHY      (MN_FIRST + 16)
#if(WINVER < 0x0400)
#define WM_NEXTMENU                     0x0213

typedef struct tagMDINEXTMENU
{
    HMENU   hmenuIn;
    HMENU   hmenuNext;
    HWND    hwndNext;
} MDINEXTMENU, * PMDINEXTMENU, FAR * LPMDINEXTMENU;

#define WM_SIZING                       0x0214
#define WM_CAPTURECHANGED               0x0215
#define WM_MOVING                       0x0216
#define WM_POWERBROADCAST               0x0218
#define WM_DEVICECHANGE                 0x0219
#endif /* WINVER < 0x0400 */
#define WM_DROPOBJECT                   0x022A
#define WM_QUERYDROPOBJECT              0x022B
#define WM_BEGINDRAG                    0x022C
#define WM_DRAGLOOP                     0x022D
#define WM_DRAGSELECT                   0x022E
#define WM_DRAGMOVE                     0x022F
#define WM_ENTERSIZEMOVE                0x0231
#define WM_EXITSIZEMOVE                 0x0232
#define WM_KANJIFIRST                   0x0280
#define WM_KANJILAST                    0x029F
#define WM_PALETTEGONNACHANGE           0x0310
#define WM_CHANGEPALETTE                0x0311
#define WM_SYSMENU                      0x0313
#define WM_HOOKMSG                      0x0314
#define WM_EXITPROCESS                  0x0315
#if(WINVER < 0x0400)
#define WM_WAKETHREAD                   0x0316
#define WM_PRINT                        0x0317
#define WM_PRINTCLIENT                  0x0318

#define WM_HANDHELDFIRST                0x0358
#define WM_HANDHELDLAST                 0x035F

#define WM_AFXFIRST                     0x0360
#define WM_AFXLAST                      0x037F
#endif /* WINVER < 0x0400 */
#define WM_COALESCE_FIRST               0x0390
#define WM_COALESCE_LAST                0x039F
#define WM_INTERNAL_DDE_FIRST           0x03E0
#define WM_INTERNAL_DDE_LAST            0x03EF
#if(WINVER < 0x0400)
#define WM_APP                          0x8000
#endif /* WINVER < 0x0400 */
#define WM_COALESCE_FIRST               0x0390
#define WM_COALESCE_LAST                0x039F
#define WM_MM_RESERVED_FIRST            0x03A0
#define WM_MM_RESERVED_LAST             0x03DF
#define WM_CBT_RESERVED_FIRST           0x03F0
#define WM_CBT_RESERVED_LAST            0x03FF
#if(WINVER < 0x0400)
/*  wParam for WM_SIZING message  */
#define WMSZ_LEFT           1
#define WMSZ_RIGHT          2
#define WMSZ_TOP            3
#define WMSZ_TOPLEFT        4
#define WMSZ_TOPRIGHT       5
#define WMSZ_BOTTOM         6
#define WMSZ_BOTTOMLEFT     7
#define WMSZ_BOTTOMRIGHT    8
#define WMSZ_MOVE           9
#define WMSZ_KEYMOVE        10
#define WMSZ_SIZEFIRST      WMSZ_LEFT
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#define HTOBJECT            19
#define HTCLOSE             20
#define HTHELP              21
#endif /* WINVER < 0x0400 */
#define SMTO_BROADCAST      0x0004
#define WVR_MINVALID        WVR_ALIGNTOP
#define WVR_MAXVALID        WVR_VALIDRECTS
#define WS_VALID            (WS_OVERLAPPED     | \
                             WS_POPUP          | \
                             WS_CHILD          | \
                             WS_MINIMIZE       | \
                             WS_VISIBLE        | \
                             WS_DISABLED       | \
                             WS_CLIPSIBLINGS   | \
                             WS_CLIPCHILDREN   | \
                             WS_MAXIMIZE       | \
                             WS_CAPTION        | \
                             WS_BORDER         | \
                             WS_DLGFRAME       | \
                             WS_VSCROLL        | \
                             WS_HSCROLL        | \
                             WS_SYSMENU        | \
                             WS_THICKFRAME     | \
                             WS_GROUP          | \
                             WS_TABSTOP        | \
                             WS_MINIMIZEBOX    | \
                             WS_MAXIMIZEBOX)
#define WS_EX_DRAGOBJECT     0x00000002L
#if(WINVER < 0x0400)
#define WS_EX_MDICHILD          0x00000040L
#define WS_EX_SMCAPTION         0x00000080L

#define WS_EX_WINDOWEDGE        0x00000100L
#define WS_EX_CLIENTEDGE        0x00000200L
#define WS_EX_EDGEMASK          (WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE)
#define WS_EX_CONTEXTHELP       0x00000400L
#define WS_EX_TOOLWINDOW        0x00000800L

#define WS_EX_RIGHT             0x00001000L
#define WS_EX_LEFT              0x00000000L
#define WS_EX_RTLREADING        0x00002000L
#define WS_EX_LTRREADING        0x00000000L
#define WS_EX_LEFTSCROLLBAR     0x00004000L
#define WS_EX_RIGHTSCROLLBAR    0x00000000L

#define WS_EX_CONTROLPARENT     0x00010000L
#define WS_EX_STATICEDGE        0x00020000L

#define WS_EX_ANSICREATOR       0x80000000L

#define WS_EX_OVERLAPPEDWINDOW  (WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE)
#define WS_EX_PALETTEWINDOW     (WS_EX_WINDOWEDGE | WS_EX_SMCAPTION | WS_EX_TOPMOST)

#endif /* WINVER < 0x0400 */
#define WS_EX_ALLEXSTYLES    (WS_EX_TRANSPARENT | WS_EX_DLGMODALFRAME | WS_EX_DRAGOBJECT | WS_EX_NOPARENTNOTIFY | WS_EX_TOPMOST | WS_EX_ACCEPTFILES)

#define WS_EX_VALID          (WS_EX_DLGMODALFRAME  | \
                              WS_EX_DRAGOBJECT     | \
                              WS_EX_NOPARENTNOTIFY | \
                              WS_EX_TOPMOST        | \
                              WS_EX_ACCEPTFILES    | \
                              WS_EX_TRANSPARENT    | \
                              WS_EX_ALLEXSTYLES)

#define WS_EX_VALID40        (WS_EX_VALID          | \
                              WS_EX_MDICHILD       | \
                              WS_EX_SMCAPTION      | \
                              WS_EX_WINDOWEDGE     | \
                              WS_EX_CLIENTEDGE     | \
                              WS_EX_CONTEXTHELP    | \
                              WS_EX_TOOLWINDOW     | \
                              WS_EX_RIGHT          | \
                              WS_EX_LEFT           | \
                              WS_EX_RTLREADING     | \
                              WS_EX_LEFTSCROLLBAR  | \
                              WS_EX_CONTROLPARENT  | \
                              WS_EX_STATICEDGE)
#define CS_OEMCHARS         0x0010  /* reserved (see user\server\usersrv.h) */
#define CS_LVB              0x0400
#define CS_SYSTEM           0x8000
#define CS_VALID            (CS_VREDRAW           | \
                             CS_HREDRAW           | \
                             CS_KEYCVTWINDOW      | \
                             CS_DBLCLKS           | \
                             0x0010               | \
                             CS_OWNDC             | \
                             CS_CLASSDC           | \
                             CS_PARENTDC          | \
                             CS_NOKEYCVT          | \
                             CS_NOCLOSE           | \
                             CS_SAVEBITS          | \
                             CS_BYTEALIGNCLIENT   | \
                             CS_BYTEALIGNWINDOW   | \
                             CS_GLOBALCLASS)
#if(WINVER < 0x0400)
/* WM_PRINT flags */
#define PRF_CHECKVISIBLE    0x00000001L
#define PRF_NONCLIENT       0x00000002L
#define PRF_CLIENT          0x00000004L
#define PRF_ERASEBKGND      0x00000008L
#define PRF_CHILDREN        0x00000010L
#define PRF_OWNED           0x00000020L

/* 3D border styles */
#define BDR_RAISEDOUTER 0x0001
#define BDR_SUNKENOUTER 0x0002
#define BDR_RAISEDINNER 0x0004
#define BDR_SUNKENINNER 0x0008

#define BDR_OUTER       0x0003
#define BDR_INNER       0x000c
#define BDR_RAISED      0x0005
#define BDR_SUNKEN      0x000a

#define BDR_VALID       0x000F

#define EDGE_RAISED     (BDR_RAISEDOUTER | BDR_RAISEDINNER)
#define EDGE_SUNKEN     (BDR_SUNKENOUTER | BDR_SUNKENINNER)
#define EDGE_ETCHED     (BDR_SUNKENOUTER | BDR_RAISEDINNER)
#define EDGE_BUMP       (BDR_RAISEDOUTER | BDR_SUNKENINNER)

/* Border flags */
#define BF_LEFT         0x0001
#define BF_TOP          0x0002
#define BF_RIGHT        0x0004
#define BF_BOTTOM       0x0008

#define BF_TOPLEFT      (BF_TOP | BF_LEFT)
#define BF_TOPRIGHT     (BF_TOP | BF_RIGHT)
#define BF_BOTTOMLEFT   (BF_BOTTOM | BF_LEFT)
#define BF_BOTTOMRIGHT  (BF_BOTTOM | BF_RIGHT)
#define BF_RECT         (BF_LEFT | BF_TOP | BF_RIGHT | BF_BOTTOM)

#define BF_DIAGONAL     0x0010

// For diagonal lines, the BF_RECT flags specify the end point of the
// vector bounded by the rectangle parameter.
#define BF_DIAGONAL_ENDTOPRIGHT     (BF_DIAGONAL | BF_TOP | BF_RIGHT)
#define BF_DIAGONAL_ENDTOPLEFT      (BF_DIAGONAL | BF_TOP | BF_LEFT)
#define BF_DIAGONAL_ENDBOTTOMLEFT   (BF_DIAGONAL | BF_BOTTOM | BF_LEFT)
#define BF_DIAGONAL_ENDBOTTOMRIGHT  (BF_DIAGONAL | BF_BOTTOM | BF_RIGHT)


#define BF_MIDDLE       0x0800  /* Fill in the middle */
#define BF_SOFT         0x1000  /* For softer buttons */
#define BF_ADJUST       0x2000  /* Calculate the space left over */
#define BF_FLAT         0x4000  /* For flat rather than 3D borders */
#define BF_MONO         0x8000  /* For monochrome borders */


WINUSERAPI BOOL WINAPI DrawEdge(HDC hdc, LPRECT qrc, UINT edge, UINT grfFlags);


/* flags for DrawFrameControl */

#define DFC_CAPTION             1
#define DFC_MENU                2
#define DFC_SCROLL              3
#define DFC_BUTTON              4
#define DFC_CACHE               0xFFFF

#define DFCS_CAPTIONCLOSE       0x0000
#define DFCS_CAPTIONMIN         0x0001
#define DFCS_CAPTIONMAX         0x0002
#define DFCS_CAPTIONRESTORE     0x0003
#define DFCS_CAPTIONHELP        0x0004
#define DFCS_INMENU             0x0040
#define DFCS_INSMALL            0x0080

#define DFCS_MENUARROW          0x0000
#define DFCS_MENUCHECK          0x0001
#define DFCS_MENUBULLET         0x0002

#define DFCS_SCROLLMIN          0x0000
#define DFCS_SCROLLVERT         0x0000
#define DFCS_SCROLLMAX          0x0001
#define DFCS_SCROLLHORZ         0x0002
#define DFCS_SCROLLLINE         0x0004

#define DFCS_SCROLLUP           0x0000
#define DFCS_SCROLLDOWN         0x0001
#define DFCS_SCROLLLEFT         0x0002
#define DFCS_SCROLLRIGHT        0x0003
#define DFCS_SCROLLCOMBOBOX     0x0005
#define DFCS_SCROLLSIZEGRIP     0x0008

#define DFCS_BUTTONCHECK        0x0000
#define DFCS_BUTTONRADIOIMAGE   0x0001
#define DFCS_BUTTONRADIOMASK    0x0002
#define DFCS_BUTTONRADIO        0x0004
#define DFCS_BUTTON3STATE       0x0008
#define DFCS_BUTTONPUSH         0x0010

#define DFCS_CACHEICON          0x0000
#define DFCS_CACHEBUTTONS       0x0001

#define DFCS_INACTIVE           0x0100
#define DFCS_PUSHED             0x0200
#define DFCS_CHECKED            0x0400
#define DFCS_ADJUSTRECT         0x2000
#define DFCS_FLAT               0x4000
#define DFCS_MONO               0x8000

WINUSERAPI BOOL    WINAPI DrawFrameControl(HDC, LPRECT, UINT, UINT);


/* flags for DrawCaption */

#define DC_ACTIVE           0x0001
#define DC_SMALLCAP         0x0004
#define DC_NOSENDMSG        0x2000
#define DC_INBUTTON         0x4000

WINUSERAPI BOOL    WINAPI DrawCaption(HWND, HDC, CONST RECT *, UINT);
WINUSERAPI BOOL    WINAPI _DrawCaptionTempA(HWND, HDC, LPRECT, HFONT, HICON, LPSTR, UINT);
WINUSERAPI BOOL    WINAPI _DrawCaptionTempW(HWND, HDC, LPRECT, HFONT, HICON, LPWSTR, UINT);
#ifdef UNICODE
#define _DrawCaptionTemp  _DrawCaptionTempW
#else
#define _DrawCaptionTemp  _DrawCaptionTempA
#endif // !UNICODE

#define IDANI_OPEN          1
#define IDANI_CLOSE         2
WINUSERAPI BOOL    WINAPI DrawAnimatedRects(int idAni, CONST RECT * lprcFrom, CONST RECT * lprcTo, CONST RECT * lprcClip);

WINUSERAPI BOOL    WINAPI PlaySoundEvent(int idSound);

#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#define CF_HDROP            15
#define CF_LOCALE           16
#define CF_MAX              17
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
//***** Control Notification support *********************
// REVIEW: should this be marked internal?              //
typedef struct tagNMHDR                                 //
{                                                       //
    HWND  hwndFrom;                                     //
    UINT  idFrom;                                       //
    UINT  code;                                         //
}   NMHDR;                                              //
typedef NMHDR FAR * LPNMHDR;                            //
                                                        //
typedef struct tagSTYLESTRUCT
{
    DWORD   styleOld;
    DWORD   styleNew;
} STYLESTRUCT, * LPSTYLESTRUCT;
#endif /* WINVER < 0x0400 */
#define WPF_VALID              (WPF_SETMINPOSITION     | \
                                WPF_RESTORETOMAXIMIZED)
#if(WINVER < 0x0400)
#define ODT_STATIC      5
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#define ODS_DEFAULT         0x0020
#define ODS_COMBOBOXEDIT    0x1000
#endif /* WINVER < 0x0400 */
/*
 * MEASUREITEMSTRUCT_EX for ownerdraw
 * used when server initiates a WM_MEASUREITEM and adds the additional info
 * of whether the itemData needs to be thunked when the message is sent to
 * the client (see also WM_MEASUREITEM_CLIENTDATA
 */
typedef struct tagMEASUREITEMSTRUCT_EX {
    UINT       CtlType;
    UINT       CtlID;
    UINT       itemID;
    UINT       itemWidth;
    UINT       itemHeight;
    DWORD      itemData;
    BOOL       bThunkClientData;
} MEASUREITEMSTRUCT_EX, NEAR *PMEASUREITEMSTRUCT_EX, FAR *LPMEASUREITEMSTRUCT_EX;
#define PM_VALID           (PM_NOREMOVE | \
                            PM_REMOVE   | \
                            PM_NOYIELD)
#if(WINVER < 0x0400)
#define EW_RESTARTWINDOWS    0x0042L
#define EW_REBOOTSYSTEM      0x0043L
#define EW_EXITANDEXECAPP    0x0044L
#endif /* WINVER < 0x0400 */
#define EWX_REALLYLOGOFF 0x8000000
#define EWX_SYSTEM_CALLER           0x0100
#define EWX_WINLOGON_CALLER         0x0200
#define EWX_WINLOGON_OLD_SYSTEM     0x0400
#define EWX_WINLOGON_OLD_SHUTDOWN   0x0800
#define EWX_WINLOGON_OLD_REBOOT     0x1000
#define EWX_WINLOGON_API_SHUTDOWN   0x2000
#define EWX_WINLOGON_OLD_POWEROFF   0x4000
#if(WINVER < 0x0400)
WINUSERAPI
LPARAM
WINAPI
SetMessageExtraInfo(
    LPARAM lParam);
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
WINUSERAPI long  WINAPI  BroadcastSystemMessage(DWORD, LPDWORD, UINT, WPARAM, LPARAM);
//Broadcast Special Message Recipient list
#define BSM_ALLCOMPONENTS       0x00000000
#define BSM_VXDS                0x00000001
#define BSM_NETDRIVER           0x00000002
#define BSM_INSTALLABLEDRIVERS  0x00000004
#define BSM_APPLICATIONS        0x00000008

//Broadcast Special Message Flags
#define BSF_QUERY               0x00000001
#define BSF_IGNORECURRENTTASK   0x00000002
#define BSF_FLUSHDISK           0x00000004
#define BSF_NOHANG              0x00000008
#define BSF_POSTMESSAGE         0x00000010
#define BSF_FORCEIFHUNG         0x00000020
#define BSF_SYSTEMSHUTDOWN      0x80000000

typedef struct tagBROADCASTSYSMSG
{
    /* UINT cbSize; */
    UINT    uiMessage;
    WPARAM  wParam;
    LPARAM  lParam;
} BROADCASTSYSMSG;
typedef BROADCASTSYSMSG  FAR *LPBROADCASTSYSMSG;

#define DBWF_LPARAMPOINTER  0x8000
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
WINUSERAPI
ATOM
WINAPI
RegisterClassExA(CONST WNDCLASSEXA *);
WINUSERAPI
ATOM
WINAPI
RegisterClassExW(CONST WNDCLASSEXW *);
#ifdef UNICODE
#define RegisterClassEx  RegisterClassExW
#else
#define RegisterClassEx  RegisterClassExA
#endif // !UNICODE

WINUSERAPI
BOOL
WINAPI
GetClassInfoExA(HINSTANCE, LPSTR, LPWNDCLASSEXA);
WINUSERAPI
BOOL
WINAPI
GetClassInfoExW(HINSTANCE, LPWSTR, LPWNDCLASSEXW);
#ifdef UNICODE
#define GetClassInfoEx  GetClassInfoExW
#else
#define GetClassInfoEx  GetClassInfoExA
#endif // !UNICODE

#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
WINUSERAPI
BOOL
WINAPI
ShowWindowAsync(
    HWND hWnd,
    int nCmdShow);
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#define SWP_DEFERERASE      0x2000
#define SWP_ASYNCWINDOWPOS  0x4000
#define SWP_STATECHANGE     0x8000  /* force size, move messages */
#endif /* WINVER < 0x0400 */
#define SWP_NOCLIENTSIZE    0x0800  /* Client didn't resize */
#define SWP_NOCLIENTMOVE    0x1000  /* Client didn't move   */
#define SWP_NOSENDCHANGING  0x0400  /* Don't send WM_WINDOWPOSCHANGING */

#define SWP_DEFERDRAWING    0x2000

#define SWP_CHANGEMASK      (SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_HIDEWINDOW | SWP_NOCLIENTSIZE | SWP_NOCLIENTMOVE)

#define SWP_NOCHANGE        (SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOCLIENTSIZE | SWP_NOCLIENTMOVE)

#define SWP_VALID1          (SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOREDRAW | SWP_NOACTIVATE | SWP_FRAMECHANGED)
#define SWP_VALID2          (SWP_SHOWWINDOW | SWP_HIDEWINDOW | SWP_NOCOPYBITS | SWP_NOOWNERZORDER | SWP_NOCLIENTSIZE | SWP_NOCLIENTMOVE | SWP_NOSENDCHANGING | SWP_ASYNCWINDOWPOS | SWP_DEFERDRAWING)
#define SWP_VALID           (SWP_VALID1 | SWP_VALID2)
#define HWND_GROUPTOTOP HWND_TOPMOST
/*
 * Chicago dialog template
 */
typedef struct {
    WORD wDlgVer;
    WORD wSignature;
    DWORD dwHelpID;
    DWORD dwExStyle;
    DWORD style;
    WORD cDlgItems;
    short x;
    short y;
    short cx;
    short cy;
} DLGTEMPLATE2;
typedef DLGTEMPLATE2 *LPDLGTEMPLATE2A;
typedef DLGTEMPLATE2 *LPDLGTEMPLATE2W;
#ifdef UNICODE
typedef LPDLGTEMPLATE2W LPDLGTEMPLATE2;
#else
typedef LPDLGTEMPLATE2A LPDLGTEMPLATE2;
#endif // UNICODE
typedef CONST DLGTEMPLATE2 *LPCDLGTEMPLATE2A;
typedef CONST DLGTEMPLATE2 *LPCDLGTEMPLATE2W;
#ifdef UNICODE
typedef LPCDLGTEMPLATE2W LPCDLGTEMPLATE2;
#else
typedef LPCDLGTEMPLATE2A LPCDLGTEMPLATE2;
#endif // UNICODE
/*
 * Dialog item template for NT 1.0a/Chicago (dit2)
 */
typedef struct {
    DWORD dwHelpID;
    DWORD dwExStyle;
    DWORD style;
    short x;
    short y;
    short cx;
    short cy;
    DWORD dwID;
} DLGITEMTEMPLATE2;
typedef DLGITEMTEMPLATE2 *PDLGITEMTEMPLATE2A;
typedef DLGITEMTEMPLATE2 *PDLGITEMTEMPLATE2W;
#ifdef UNICODE
typedef PDLGITEMTEMPLATE2W PDLGITEMTEMPLATE2;
#else
typedef PDLGITEMTEMPLATE2A PDLGITEMTEMPLATE2;
#endif // UNICODE
typedef DLGITEMTEMPLATE2 *LPDLGITEMTEMPLATE2A;
typedef DLGITEMTEMPLATE2 *LPDLGITEMTEMPLATE2W;
#ifdef UNICODE
typedef LPDLGITEMTEMPLATE2W LPDLGITEMTEMPLATE2;
#else
typedef LPDLGITEMTEMPLATE2A LPDLGITEMTEMPLATE2;
#endif // UNICODE

#if(WINVER < 0x0400)
WINUSERAPI
int
WINAPI
ToAsciiEx(
    UINT uVirtKey,
    UINT uScanCode,
    PBYTE lpKeyState,
    LPWORD lpChar,
    UINT uFlags,
    HKL dwhkl);
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
WINUSERAPI
WINAPI VkKeyScanExA(
    CHAR  ch,
    DWORD   dwhkl);
WINUSERAPI
WINAPI VkKeyScanExW(
    WCHAR  ch,
    DWORD   dwhkl);
#ifdef UNICODE
#define VkKeyScanEx  VkKeyScanExW
#else
#define VkKeyScanEx  VkKeyScanExA
#endif // !UNICODE
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
WINUSERAPI
UINT
WINAPI
MapVirtualKeyExA(
    UINT uCode,
    UINT uMapType,
    DWORD dwhkl);
WINUSERAPI
UINT
WINAPI
MapVirtualKeyExW(
    UINT uCode,
    UINT uMapType,
    DWORD dwhkl);
#ifdef UNICODE
#define MapVirtualKeyEx  MapVirtualKeyExW
#else
#define MapVirtualKeyEx  MapVirtualKeyExA
#endif // !UNICODE
#endif /* WINVER < 0x0400 */
#define QS_TRANSFER     0x4000      // Input was transfered from another thread
#define QS_VALID        (QS_KEY           | \
                         QS_MOUSEMOVE     | \
                         QS_MOUSEBUTTON   | \
                         QS_POSTMESSAGE   | \
                         QS_TIMER         | \
                         QS_PAINT         | \
                         QS_SENDMESSAGE   | \
                         QS_TRANSFER      | \
                         QS_HOTKEY)

#if(WINVER < 0x0400)
#define SM_SECURE               44
#define SM_CXEDGE               45
#define SM_CYEDGE               46
#define SM_CXMINSPACING         47
#define SM_CYMINSPACING         48
#define SM_CXSMICON             49
#define SM_CYSMICON             50
#define SM_CYSMCAPTION          51
#define SM_CXSMSIZE             52
#define SM_CYSMSIZE             53
#define SM_CXMENUSIZE           54
#define SM_CYMENUSIZE           55
#define SM_ARRANGE              56
#define SM_USERTYPE             57
#define SM_XWORKAREA            58
#define SM_YWORKAREA            59
#define SM_CXWORKAREA           60
#define SM_CYWORKAREA           61
#define SM_CYCAPTIONICON        62
#define SM_CYSMCAPTIONICON      63
#define SM_CXMINIMIZED          64
#define SM_CYMINIMIZED          65
#define SM_CXMAXTRACK           66
#define SM_CYMAXTRACK           67
#define SM_CXMAXIMIZED          68
#define SM_CYMAXIMIZED          69
#define SM_KEYBOARDPREF         71
#define SM_HIGHCONTRAST         72
#define SM_SCREENREADER         73
#define SM_CURSORSIZE           74   /* Obsolete Going Away */
#define SM_CLEANBOOT            75
#define SM_CXDRAG               76
#define SM_CYDRAG               77
#define SM_NETWORK              78
#define SM_CXMENUCHECK          79   /* Use instead of GetMenuCheckMarkDimensions()! */
#define SM_CYMENUCHECK          80
#define SM_SLOWMACHINE          81


#endif /* WINVER < 0x0400 */
#define SM_MAX                  70
#if(WINVER < 0x0400)
/* return codes for WM_MENUCHAR */
#define MNC_IGNORE  0
#define MNC_CLOSE   1
#define MNC_EXECUTE 2
#define MNC_SELECT  3

typedef struct tagTPMPARAMS
{
    UINT    cbSize;     /* Size of structure */
    RECT    rcExclude;  /* Screen coordinates of rectangle to exclude when positioning */
}   TPMPARAMS;
typedef TPMPARAMS FAR *LPTPMPARAMS;

WINUSERAPI BOOL    WINAPI TrackPopupMenuEx(HMENU, UINT, int, int, HWND, LPTPMPARAMS);

#define MIIM_STATE       0x00000001
#define MIIM_ID          0x00000002
#define MIIM_SUBMENU     0x00000004
#define MIIM_CHECKMARKS  0x00000008
#define MIIM_TYPE        0x00000010
#define MIIM_DATA        0x00000020

typedef struct tagMENUITEMINFOA
{
    UINT    cbSize;
    UINT    fMask;
    UINT    fType;          // used if MIIM_TYPE
    UINT    fState;         // used if MIIM_STATE
    UINT    wID;            // used if MIIM_ID
    HMENU   hSubMenu;       // used if MIIM_SUBMENU
    HBITMAP hbmpChecked;    // used if MIIM_CHECKMARKS
    HBITMAP hbmpUnchecked;  // used if MIIM_CHECKMARKS
    DWORD   dwItemData;     // used if MIIM_DATA
    LPSTR   dwTypeData;     // used if MIIM_TYPE
    UINT    cch;            // used if MIIM_TYPE
}   MENUITEMINFOA, FAR *LPMENUITEMINFOA, CONST FAR *LPCMENUITEMINFOA;
typedef struct tagMENUITEMINFOW
{
    UINT    cbSize;
    UINT    fMask;
    UINT    fType;          // used if MIIM_TYPE
    UINT    fState;         // used if MIIM_STATE
    UINT    wID;            // used if MIIM_ID
    HMENU   hSubMenu;       // used if MIIM_SUBMENU
    HBITMAP hbmpChecked;    // used if MIIM_CHECKMARKS
    HBITMAP hbmpUnchecked;  // used if MIIM_CHECKMARKS
    DWORD   dwItemData;     // used if MIIM_DATA
    LPWSTR  dwTypeData;     // used if MIIM_TYPE
    UINT    cch;            // used if MIIM_TYPE
}   MENUITEMINFOW, FAR *LPMENUITEMINFOW, CONST FAR *LPCMENUITEMINFOW;
#ifdef UNICODE
typedef MENUITEMINFOW MENUITEMINFO;
typedef LPMENUITEMINFOW LPMENUITEMINFO;
typedef LPCMENUITEMINFOW LPCMENUITEMINFO;
#else
typedef MENUITEMINFOA MENUITEMINFO;
typedef LPMENUITEMINFOA LPMENUITEMINFO;
typedef LPCMENUITEMINFOA LPCMENUITEMINFO;
#endif // UNICODE


WINUSERAPI
BOOL
WINAPI
InsertMenuItemA(
    HMENU,
    UINT,
    BOOL,
    LPCMENUITEMINFOA
    );
WINUSERAPI
BOOL
WINAPI
InsertMenuItemW(
    HMENU,
    UINT,
    BOOL,
    LPCMENUITEMINFOW
    );
#ifdef UNICODE
#define InsertMenuItem  InsertMenuItemW
#else
#define InsertMenuItem  InsertMenuItemA
#endif // !UNICODE

WINUSERAPI
BOOL
WINAPI
GetMenuItemInfoA(
    HMENU,
    UINT,
    BOOL,
    LPMENUITEMINFOA
    );
WINUSERAPI
BOOL
WINAPI
GetMenuItemInfoW(
    HMENU,
    UINT,
    BOOL,
    LPMENUITEMINFOW
    );
#ifdef UNICODE
#define GetMenuItemInfo  GetMenuItemInfoW
#else
#define GetMenuItemInfo  GetMenuItemInfoA
#endif // !UNICODE

WINUSERAPI
BOOL
WINAPI
SetMenuItemInfoA(
    HMENU,
    UINT,
    BOOL,
    LPCMENUITEMINFOA
    );
WINUSERAPI
BOOL
WINAPI
SetMenuItemInfoW(
    HMENU,
    UINT,
    BOOL,
    LPCMENUITEMINFOW
    );
#ifdef UNICODE
#define SetMenuItemInfo  SetMenuItemInfoW
#else
#define SetMenuItemInfo  SetMenuItemInfoA
#endif // !UNICODE

#define GMDI_USEDISABLED    0x0001L
#define GMDI_GOINTOPOPUPS   0x0002L

WINUSERAPI UINT    WINAPI GetMenuDefaultItem(HMENU hMenu, UINT fByPos, UINT gmdiFlags);
WINUSERAPI BOOL    WINAPI SetMenuDefaultItem(HMENU hMenu, UINT uItem, UINT fByPos);

WINUSERAPI BOOL    WINAPI GetMenuItemRect(HWND hWnd, HMENU hMenu, UINT uItem, LPRECT lprcItem);
WINUSERAPI int     WINAPI MenuItemFromPoint(HWND hWnd, HMENU hMenu, POINT ptScreen);

#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#define TPM_TOPALIGN        0x0000L
#define TPM_VCENTERALIGN    0x0010L
#define TPM_BOTTOMALIGN     0x0020L

#define TPM_HORIZONTAL      0x0000L     /* Horz alignment matters more */
#define TPM_VERTICAL        0x0040L     /* Vert alignment matters more */
#define TPM_NONOTIFY        0x0080L     /* Don't send any notification msgs */
#define TPM_RETURNCMD       0x0100L

#endif /* WINVER < 0x0400 */
#define TPM_VALID      (TPM_LEFTBUTTON  | \
                        TPM_RIGHTBUTTON | \
                        TPM_LEFTALIGN   | \
                        TPM_CENTERALIGN | \
                        TPM_RIGHTALIGN)
typedef struct _dropfilestruct {
   DWORD pFiles;                       // offset of file list
   POINT pt;                           // drop point
   BOOL fNC;                           // is it on NonClient area
   BOOL fWide;                         // WIDE character switch
} DROPFILESTRUCT, FAR * LPDROPFILESTRUCT;
#if(WINVER < 0x0400)
//
// Drag-and-drop support
//

typedef struct tagDROPSTRUCT
{
    HWND    hwndSource;
    HWND    hwndSink;
    DWORD   wFmt;
    DWORD   dwData;
    POINT   ptDrop;
    DWORD   dwControlData;
} DROPSTRUCT, *PDROPSTRUCT, *LPDROPSTRUCT;

#define DOF_EXECUTABLE      0x8001
#define DOF_DOCUMENT        0x8002
#define DOF_DIRECTORY       0x8003
#define DOF_MULTIPLE        0x8004
#define DOF_PROGMAN         0x0001
#define DOF_SHELLDATA       0x0002

#define DO_DROPFILE         0x454C4946L
#define DO_PRINTFILE        0x544E5250L

WINUSERAPI
DWORD
WINAPI
DragObject(HWND, HWND, UINT, DWORD, HCURSOR);

WINUSERAPI
BOOL
WINAPI
DragDetect(HWND, POINT);
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#define DT_EDITCONTROL      0x00002000
#define DT_PATH_ELLIPSIS    0x00004000
#define DT_END_ELLIPSIS     0x00008000
#define DT_MODIFYSTRING     0x00010000
#define DT_RTLREADING       0x00020000

typedef struct tagDRAWTEXTPARAMS
{
    UINT    cbSize;
    int     iTabLength;
    int     iLeftMargin;
    int     iRightMargin;
    UINT    uiLengthDrawn;
} DRAWTEXTPARAMS, FAR *LPDRAWTEXTPARAMS;
#endif /* WINVER < 0x0400 */
#define DT_CTABS            0xff00
#define DT_VALID           (DT_TOP             | \
                            DT_LEFT            | \
                            DT_CENTER          | \
                            DT_RIGHT           | \
                            DT_VCENTER         | \
                            DT_BOTTOM          | \
                            DT_WORDBREAK       | \
                            DT_SINGLELINE      | \
                            DT_EXPANDTABS      | \
                            DT_TABSTOP         | \
                            DT_NOCLIP          | \
                            DT_EXTERNALLEADING | \
                            DT_CALCRECT        | \
                            DT_NOPREFIX        | \
                            DT_INTERNAL        | \
                            DT_CTABS)
#if(WINVER < 0x0400)
WINUSERAPI
int
WINAPI
DrawTextExA(HDC, LPCSTR, int, LPRECT, UINT, LPDRAWTEXTPARAMS);
WINUSERAPI
int
WINAPI
DrawTextExW(HDC, LPCWSTR, int, LPRECT, UINT, LPDRAWTEXTPARAMS);
#ifdef UNICODE
#define DrawTextEx  DrawTextExW
#else
#define DrawTextEx  DrawTextExA
#endif // !UNICODE
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
/* Monolithic state-drawing routine */
/* Image type */
#define DST_COMPLEX     0x0000
#define DST_TEXT        0x0001
#define DST_PREFIXTEXT  0x0002
#define DST_TEXTMAX     0x0002
#define DST_ICON        0x0003
#define DST_BITMAP      0x0004
#define DST_GLYPH       0x0005
#define DST_TYPEMASK    0x0007
#define DST_GRAYSTRING  0x0008

/* State type */
#define DSS_NORMAL      0x0000
#define DSS_UNION       0x0010  /* Gray string appearance */
#define DSS_DISABLED    0x0020
#define DSS_DEFAULT     0x0040
#define DSS_MONO        0x0080

WINUSERAPI BOOL WINAPI DrawStateA(HDC, HBRUSH, DRAWSTATEPROC, LPARAM, WPARAM, int, int, int, int, UINT);
WINUSERAPI BOOL WINAPI DrawStateW(HDC, HBRUSH, DRAWSTATEPROC, LPARAM, WPARAM, int, int, int, int, UINT);
#ifdef UNICODE
#define DrawState  DrawStateW
#else
#define DrawState  DrawStateA
#endif // !UNICODE
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
WINUSERAPI BOOL WINAPI PaintDesktop(HDC hdc);

WINUSERAPI VOID WINAPI SwitchToThisWindow(HWND hwnd, BOOL fUnknown);
#endif /* WINVER < 0x0400 */
#define DCX_USESTYLE         0x00010000L

#define DCX_INVALID          0x00000800L
#define DCX_INUSE            0x00001000L
#define DCX_SAVEDRGNINVALID  0x00002000L

#define DCX_NEEDFONT         0x00020000L
#define DCX_NODELETERGN      0x00040000L
#define DCX_NOCLIPCHILDREN   0x00080000L

#define DCX_NORECOMPUTE      0x00100000L
#define DCX_OWNDC            0x00008000L
#define DCX_DESTROYTHIS      0x00400000L

#define DCX_PWNDORGINVISIBLE    0x10000000L

#define DCX_DONTRIPONDESTROY    0x80000000L

#define DCX_MATCHMASK       (DCX_WINDOW       | \
                             DCX_CACHE        | \
                             DCX_CLIPCHILDREN | \
                             DCX_CLIPSIBLINGS | \
                             DCX_NORESETATTRS | \
                             DCX_LOCKWINDOWUPDATE)

#define DCX_VALID           (DCX_WINDOW           | \
                             DCX_CACHE            | \
                             DCX_NORESETATTRS     | \
                             DCX_CLIPCHILDREN     | \
                             DCX_CLIPSIBLINGS     | \
                             DCX_PARENTCLIP       | \
                             DCX_EXCLUDERGN       | \
                             DCX_INTERSECTRGN     | \
                             DCX_EXCLUDEUPDATE    | \
                             DCX_INTERSECTUPDATE  | \
                             DCX_LOCKWINDOWUPDATE | \
                             DCX_INVALID          | \
                             DCX_INUSE            | \
                             DCX_SAVEDRGNINVALID  | \
                             DCX_OWNDC            | \
                             DCX_USESTYLE         | \
                             DCX_NEEDFONT         | \
                             DCX_NODELETERGN      | \
                             DCX_NOCLIPCHILDREN   | \
                             DCX_NORECOMPUTE      | \
                             DCX_VALIDATE         | \
                             DCX_DESTROYTHIS)
#define RDW_REDRAWWINDOW        0x1000  /* Called from RedrawWindow()*/
#define RDW_SUBTRACTSELF        0x2000  /* Subtract self from hrgn   */

#define RDW_COPYRGN             0x4000  /* Copy the passed-in region */
#define RDW_VALIDMASK          (RDW_INVALIDATE      | \
                                RDW_INTERNALPAINT   | \
                                RDW_ERASE           | \
                                RDW_VALIDATE        | \
                                RDW_NOINTERNALPAINT | \
                                RDW_NOERASE         | \
                                RDW_NOCHILDREN      | \
                                RDW_ALLCHILDREN     | \
                                RDW_UPDATENOW       | \
                                RDW_ERASENOW        | \
                                RDW_FRAME           | \
                                RDW_NOFRAME)
#define SW_SCROLLWINDOW     0x8000  /* Called from ScrollWindow() */
#define SW_VALIDFLAGS      (SW_SCROLLWINDOW     | \
                            SW_SCROLLCHILDREN   | \
                            SW_INVALIDATE       | \
                            SW_ERASE)
#if(WINVER < 0x0400)
WINUSERAPI
int
WINAPI
SetScrollPage(HWND, int, int, BOOL);

WINUSERAPI
int
WINAPI
GetScrollPage(HWND, int);

#endif /* WINVER < 0x0400 */
#define ESB_MAX             0x0003
#define SB_DISABLE_MASK     ESB_DISABLE_BOTH
#if(WINVER < 0x0400)
#define HELPINFO_WINDOW    0x0001
#define HELPINFO_MENUITEM  0x0002
typedef struct tagHELPINFO      /* Structure pointed to by lParam of WM_HELP */
{
    UINT    cbSize;             /* Size in bytes of this struct  */
    int     iContextType;       /* Either HELPINFO_WINDOW or HELPINFO_MENUITEM */
    int     iCtrlId;            /* Control Id or a Menu item Id. */
    HANDLE  hItemHandle;        /* hWnd of control or hMenu.     */
    DWORD   dwContextId;        /* Context Id associated with this item */
    POINT   MousePos;           /* Mouse Position in screen co-ordinates */
}  HELPINFO, FAR *LPHELPINFO;

WINUSERAPI BOOL  WINAPI  SetWindowContextHelpId(HWND, DWORD);
WINUSERAPI DWORD WINAPI  GetWindowContextHelpId(HWND);
WINUSERAPI BOOL  WINAPI  SetMenuContextHelpId(HMENU, DWORD);
WINUSERAPI DWORD WINAPI  GetMenuContextHelpId(HMENU);

#endif /* WINVER < 0x0400 */

/*
 * Help Engine stuff
 *
 * Note: for Chicago this is in winhelp.h and called WINHLP
 */
typedef struct {
    WORD cbData;              /* Size of data                     */
    WORD usCommand;           /* Command to execute               */
    DWORD ulTopic;            /* Topic/context number (if needed) */
    DWORD ulReserved;         /* Reserved (internal use)          */
    WORD offszHelpFile;       /* Offset to help file in block     */
    WORD offabData;           /* Offset to other data in block    */
} HLP, *LPHLP;

#if(WINVER < 0x0400)
#define MB_DEFBUTTON4               0x00000300L
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#define MB_HELP                     0x00004000L // Help Button
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#define MB_USERICON                 0x00000080L
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#define MB_TOPMOST          0x00040000L

#define MBEX_VALIDL         0xf3f7
#define MBEX_VALIDH         1

typedef void (CALLBACK *MSGBOXCALLBACK)(LPHELPINFO lpHelpInfo);

typedef struct tagMSGBOXPARAMSA
{
    UINT        cbSize;
    HWND        hwndOwner;
    HINSTANCE   hInstance;
    LPCSTR      lpszText;
    LPCSTR      lpszCaption;
    DWORD       dwStyle;
    LPCSTR      lpszIcon;
    DWORD       dwContextHelpId;
    MSGBOXCALLBACK      lpfnMsgBoxCallback;
    DWORD   dwLanguageId;
} MSGBOXPARAMSA, *PMSGBOXPARAMSA, *LPMSGBOXPARAMSA;
typedef struct tagMSGBOXPARAMSW
{
    UINT        cbSize;
    HWND        hwndOwner;
    HINSTANCE   hInstance;
    LPCWSTR     lpszText;
    LPCWSTR     lpszCaption;
    DWORD       dwStyle;
    LPCWSTR     lpszIcon;
    DWORD       dwContextHelpId;
    MSGBOXCALLBACK      lpfnMsgBoxCallback;
    DWORD   dwLanguageId;
} MSGBOXPARAMSW, *PMSGBOXPARAMSW, *LPMSGBOXPARAMSW;
#ifdef UNICODE
typedef MSGBOXPARAMSW MSGBOXPARAMS;
typedef PMSGBOXPARAMSW PMSGBOXPARAMS;
typedef LPMSGBOXPARAMSW LPMSGBOXPARAMS;
#else
typedef MSGBOXPARAMSA MSGBOXPARAMS;
typedef PMSGBOXPARAMSA PMSGBOXPARAMS;
typedef LPMSGBOXPARAMSA LPMSGBOXPARAMS;
#endif // UNICODE


WINUSERAPI int     WINAPI MessageBoxIndirectA(LPMSGBOXPARAMSA);
WINUSERAPI int     WINAPI MessageBoxIndirectW(LPMSGBOXPARAMSW);
#ifdef UNICODE
#define MessageBoxIndirect  MessageBoxIndirectW
#else
#define MessageBoxIndirect  MessageBoxIndirectA
#endif // !UNICODE
#endif /* WINVER < 0x0400 */
#define MB_VALID                   (MB_OK                   | \
                                    MB_OKCANCEL             | \
                                    MB_ABORTRETRYIGNORE     | \
                                    MB_YESNOCANCEL          | \
                                    MB_YESNO                | \
                                    MB_RETRYCANCEL          | \
                                    MB_ICONHAND             | \
                                    MB_ICONQUESTION         | \
                                    MB_ICONEXCLAMATION      | \
                                    MB_ICONASTERISK         | \
                                    MB_DEFBUTTON1           | \
                                    MB_DEFBUTTON2           | \
                                    MB_DEFBUTTON3           | \
                                    MB_APPLMODAL            | \
                                    MB_SYSTEMMODAL          | \
                                    MB_TASKMODAL            | \
                                    MB_NOFOCUS              | \
                                    MB_SETFOREGROUND        | \
                                    MB_DEFAULT_DESKTOP_ONLY | \
                                    MB_SERVICE_NOTIFICATION | \
                                    MB_TYPEMASK             | \
                                    MB_ICONMASK             | \
                                    MB_DEFMASK              | \
                                    MB_MODEMASK             | \
                                    MB_MISCMASK)
#if(WINVER < 0x0400)
#define CWP_ALL             0x0000
#define CWP_SKIPINVISIBLE   0x0001
#define CWP_SKIPDISABLED    0x0002
#define CWP_SKIPTRANSPARENT 0x0004
#define CWP_VALID           0x0007

WINUSERAPI HWND    WINAPI ChildWindowFromPointEx(HWND, POINT, UINT);
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#define COLOR_3DDKSHADOW        21
#define COLOR_3DLIGHT           22
#define COLOR_MSGBOX            23
#define COLOR_MSGBOXTEXT        24
#endif /* WINVER < 0x0400 */
#define COLOR_ENDCOLORS         COLOR_BTNHIGHLIGHT
#define COLOR_MAX               (COLOR_ENDCOLORS+1)
#if(WINVER < 0x0400)
WINUSERAPI
HBRUSH
WINAPI
GetSysColorBrush(
    int nIndex);

WINUSERAPI HANDLE WINAPI SetSysColorsTemp(COLORREF FAR *, HBRUSH FAR *, UINT wCnt);

#endif /* WINVER < 0x0400 */

WINUSERAPI
BOOL
WINAPI
SetDeskWallpaper(
    LPCSTR lpString);

#if(WINVER < 0x0400)
WINUSERAPI HWND    WINAPI FindWindowExA(HWND, HWND, LPCSTR, LPCSTR);
WINUSERAPI HWND    WINAPI FindWindowExW(HWND, HWND, LPCWSTR, LPCWSTR);
#ifdef UNICODE
#define FindWindowEx  FindWindowExW
#else
#define FindWindowEx  FindWindowExA
#endif // !UNICODE

WINUSERAPI HWND    WINAPI  GetShellWindow(void);
WINUSERAPI BOOL    WINAPI  SetShellWindow(HWND);
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#define MF_END              0x00000080L  /* Obsolete -- only used by old RES files */
#endif /* WINVER < 0x0400 */
#define MF_CHANGE_VALID   (MF_INSERT          | \
                           MF_CHANGE          | \
                           MF_APPEND          | \
                           MF_DELETE          | \
                           MF_REMOVE          | \
                           MF_BYCOMMAND       | \
                           MF_BYPOSITION      | \
                           MF_SEPARATOR       | \
                           MF_ENABLED         | \
                           MF_GRAYED          | \
                           MF_DISABLED        | \
                           MF_UNCHECKED       | \
                           MF_CHECKED         | \
                           MF_USECHECKBITMAPS | \
                           MF_STRING          | \
                           MF_BITMAP          | \
                           MF_OWNERDRAW       | \
                           MF_POPUP           | \
                           MF_MENUBARBREAK    | \
                           MF_MENUBREAK       | \
                           MF_UNHILITE        | \
                           MF_HILITE          | \
                           MF_SYSMENU)

#define MF_VALID          (MF_CHANGE_VALID    | \
                           MF_HELP            | \
                           MF_MOUSESELECT)


/* fType field */

#define MFT_STRING          0x00000000L
#define MFT_BITMAP          0x00000004L
#define MFT_MENUBARBREAK    0x00000020L
#define MFT_MENUBREAK       0x00000040L
#define MFT_OWNERDRAW       0x00000100L
#define MFT_RADIOCHECK      0x00000200L // new
#define MFT_SEPARATOR       0x00000800L
#define MFT_RIGHTJUSTIFY    0x00004000L // new
#define MFT_MASK            0x00004B64L

/* fState field */

#define MFS_GRAYED          0x00000003L // MF_GRAYED | MF_DISABLED
#define MFS_DISABLED        0x00000002L
#define MFS_CHECKED         0x00000008L
#define MFS_HILITE          0x00000080L
#define MFS_ENABLED         0x00000000L
#define MFS_UNCHECKED       0x00000000L
#define MFS_UNHILITE        0x00000000L
#define MFS_DEFAULT         0x00001000L
#define MFS_MASK            0x0000108BL

/* bResInfo field */

#define MFR_POPUP           0x01   // = MF_POPUP >> 1
#define MFR_END             0x80   // = MF_END
#define MFS_MASK            0x0000108BL
#define MFR_POPUP           0x01
#define MFR_END             0x80
#define MFT_OLDAPI_MASK     0x00004B64L
#define MFS_OLDAPI_MASK     0x0000108BL
//#define MFT_MASK            0x00000904L
//#define MFS_MASK            0x000040EBL
#define MFT_NONSTRING       0x00000904L  // MF_BITMAP | MF_OWNERDRAW | MF_SEPARATOR
#define MFT_BREAK           0x00000060L  // MF_MENUBREAK | MF_MENUBARBREAK
#define MFS_GRAYEDOUT       0x00000003L  // MF_DISABLED | MF_GRAYED
#if(WINVER < 0x0400)
WINUSERAPI
BOOL
WINAPI
CheckMenuRadioItem(HMENU, UINT, UINT, UINT, UINT);
#endif /* WINVER < 0x0400 */
typedef struct {        // version 1
    DWORD dwHelpID;
    DWORD fType;
    DWORD fState;
    DWORD menuId;
    WORD  wResInfo;
    WCHAR mtString[1];
} MENUITEMTEMPLATE2, *PMENUITEMTEMPLATE2;
#if(WINVER < 0x0400)
#define SC_DEFAULT      0xF160
#define SC_MONITORPOWER 0xF170
#define SC_CONTEXTHELP  0xF180
#define SC_SEPARATOR    0xF00F
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
WINUSERAPI
HCURSOR
WINAPI
LoadCursorFromFileA(
    LPCSTR    lpFileName);
WINUSERAPI
HCURSOR
WINAPI
LoadCursorFromFileW(
    LPCWSTR    lpFileName);
#ifdef UNICODE
#define LoadCursorFromFile  LoadCursorFromFileW
#else
#define LoadCursorFromFile  LoadCursorFromFileA
#endif // !UNICODE
#endif /* WINVER < 0x0400 */
#define IDC_NWPEN           MAKEINTRESOURCE(32531)
#define IDC_HUNG            MAKEINTRESOURCE(32632)
#if(WINVER < 0x0400)
#define IDC_HELP            MAKEINTRESOURCE(32651)
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
WINUSERAPI
int
WINAPI
LookupIconIdFromDirectoryEx(
    PBYTE presbits,
    BOOL  fIcon,
    int   cxDesired,
    int   cyDesired,
    UINT  Flags);
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
WINUSERAPI
HICON
WINAPI
CreateIconFromResourceEx(
    PBYTE presbits,
    DWORD dwResSize,
    BOOL  fIcon,
    DWORD dwVer,
    int   cxDesired,
    int   cyDesired,
    UINT  Flags);

/* Icon/Cursor header */
typedef struct tagCURSORSHAPE
{
    int     xHotSpot;
    int     yHotSpot;
    int     cx;
    int     cy;
    int     cbWidth;
    BYTE    Planes;
    BYTE    BitsPixel;
} CURSORSHAPE, FAR *LPCURSORSHAPE;

#define IMAGE_BITMAP        0
#define IMAGE_ICON          1
#define IMAGE_CURSOR        2
#define IMAGE_ENHMETAFILE   3

#define LR_DEFAULTCOLOR     0x0000
#define LR_MONOCHROME       0x0001
#define LR_COLOR            0x0002
#define LR_COPYRETURNORG    0x0004
#define LR_COPYDELETEORG    0x0008
#define LR_LOADFROMFILE     0x0010
#define LR_LOADREALSIZE     0x0020
#define LR_DEFAULTSIZE      0x0040
#define LR_LOADMAP3DCOLORS  0x1000
#define LR_CREATEDIBSECTION 0x2000
#define LR_COPYFROMRESOURCE 0x4000
#define LR_SHARED           0x8000
#define LR_VALID            0xB03F

WINUSERAPI
HANDLE
WINAPI
LoadImageA(
    HINSTANCE,
    LPCSTR,
    UINT,
    int,
    int,
    UINT);
WINUSERAPI
HANDLE
WINAPI
LoadImageW(
    HINSTANCE,
    LPCWSTR,
    UINT,
    int,
    int,
    UINT);
#ifdef UNICODE
#define LoadImage  LoadImageW
#else
#define LoadImage  LoadImageA
#endif // !UNICODE

WINUSERAPI
HICON
WINAPI
CopyImage(
    HANDLE,
    UINT,
    int,
    int,
    UINT);

#define DI_MASK     0x0001
#define DI_IMAGE    0x0002
#define DI_NORMAL   0x0003
#define DI_COMPAT   0x0004
#define DI_DEFAULTSIZE  0x0008

WINUSERAPI BOOL WINAPI DrawIconEx(HDC hdc, int xLeft, int yTop,
              HICON hIcon, int cxWidth, int cyWidth,
              UINT istepIfAniCur, HBRUSH hbrFlickerFreeDraw, UINT diFlags);
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#define RES_ICON    1
#define RES_CURSOR  2
#endif /* WINVER < 0x0400 */
#define OBM_STARTUP         32733
#define OBM_TRUETYPE        32732
#define OCR_NWPEN           32631
#if(WINVER < 0x0400)
#define OCR_APPSTARTING     32650
#define OCR_HUNG            32651
#define OCR_HELP            32652
#endif /* WINVER < 0x0400 */
/*
 * Default Cursor IDs to get original image from User
 */
#define OCR_ARROW_DEFAULT       100
#define OCR_IBEAM_DEFAULT       101
#define OCR_WAIT_DEFAULT        102
#define OCR_CROSS_DEFAULT       103
#define OCR_UPARROW_DEFAULT     104
#define OCR_SIZENWSE_DEFAULT    105
#define OCR_SIZENESW_DEFAULT    106
#define OCR_SIZEWE_DEFAULT      107
#define OCR_SIZENS_DEFAULT      108
#define OCR_SIZEALL_DEFAULT     109
#define OCR_NO_DEFAULT          110
#define OCR_APPSTARTING_DEFAULT 111
#define OCR_HELP_DEFAULT        112
#define OCR_HUNG_DEFAULT        113
#define OCR_NWPEN_DEFAULT       114
#if(WINVER < 0x0400)
#define OIC_WINLOGO         32517
#endif /* WINVER < 0x0400 */
/* Default IDs for original User images */
#define OIC_APPLICATION_DEFAULT     100
#define OIC_HAND_DEFAULT            101
#define OIC_QUESTION_DEFAULT        102
#define OIC_EXCLAMATION_DEFAULT     103
#define OIC_ASTERISK_DEFAULT        104
#define OIC_WINLOGO_DEFAULT         105
#ifdef RC_INVOKED
#if(WINVER < 0x0400)
#define IDI_WINLOGO         32517
#endif /* WINVER < 0x0400 */
#else
#if(WINVER < 0x0400)
#define IDI_WINLOGO       MAKEINTERESOURCE(32517)
#endif /* WINVER < 0x0400 */
#endif /* RC_INVOKED */
#if(WINVER < 0x0400)
#define IDCLOSE         8
#define IDHELP          9
#define IDUSERICON      10
#endif /* WINVER < 0x0400 */
#define ES_FMTMASK          0x0003L
#define ES_COMBOBOX         0x0200L
#if(WINVER < 0x0400)
/* Edit control EM_SETMARGIN parameters */
#define EC_LEFTMARGIN       0x0001
#define EC_RIGHTMARGIN      0x0002
#define EC_USEFONTINFO      0xffff
#endif /* WINVER < 0x0400 */
#define EM_SETFONT              0x00C3 /* no longer suported */
#define EM_SETWORDBREAK         0x00CA /* no longer suported */
#if(WINVER < 0x0400)
#define EM_SETMARGINS           0x00D3
#define EM_GETMARGINS           0x00D4
#define EM_GETLIMITTEXT         0x00D5
#define EM_POSFROMCHAR          0x00D6
#define EM_CHARFROMPOS          0x00D7
#endif /* WINVER < 0x0400 */
#define EM_MSGMAX               0x00D3
#define BS_PUSHBOX          0x0000000AL
#define BS_TYPEMASK         0x0000000FL
#if(WINVER < 0x0400)
#define BS_TEXT             0x00000000L
#define BS_ICON             0x00000040L
#define BS_BITMAP           0x00000080L
#define BS_IMAGEMASK        0x000000C0L
#define BS_LEFT             0x00000100L
#define BS_RIGHT            0x00000200L
#define BS_CENTER           0x00000300L
#define BS_HORZMASK         0x00000300L
#define BS_TOP              0x00000400L
#define BS_BOTTOM           0x00000800L
#define BS_VCENTER          0x00000C00L
#define BS_VERTMASK         0x00000C00L
#define BS_ALIGNMASK        0x00000F00L
#define BS_PUSHLIKE         0x00001000L
#define BS_MULTILINE        0x00002000L
#define BS_NOTIFY           0x00004000L
#define BS_FLAT             0x00008000L
#define BS_RIGHTBUTTON      BS_LEFTTEXT
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#define BN_SETFOCUS         6
#define BN_KILLFOCUS        7
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#define BM_CLICK           0x00F5
#define BM_GETIMAGE        0x00F6
#define BM_SETIMAGE        0x00F7
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#define SS_OWNERDRAW        0x0000000DL
#define SS_BITMAP           0x0000000EL
#define SS_ENHMETAFILE      0x0000000FL
#define SS_ETCHEDHORZ       0x00000010L
#define SS_ETCHEDVERT       0x00000011L
#define SS_ETCHEDFRAME      0x00000012L
#define SS_TYPEMASK         0x0000001FL
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#define SS_NOTIFY           0x00000100L
#define SS_CENTERIMAGE      0x00000200L
#define SS_RIGHTIMAGE       0x00000400L
#define SS_REALSIZEIMAGE    0x00000800L
#define SS_SUNKEN           0x00001000L
#define SS_RAISED           0x00002000L
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#define STM_SETIMAGE        0x0172
#define STM_GETIMAGE        0x0173

#define STN_CLICKED         0
#define STN_DBLCLK          1
#define STN_ENABLE          2
#define STN_DISABLE         3
#endif /* WINVER < 0x0400 */
#define DDL_VALID          (DDL_READWRITE  | \
                            DDL_READONLY   | \
                            DDL_HIDDEN     | \
                            DDL_SYSTEM     | \
                            DDL_DIRECTORY  | \
                            DDL_ARCHIVE    | \
                            DDL_POSTMSGS   | \
                            DDL_DRIVES     | \
                            DDL_EXCLUSIVE)
/*
 * Valid dialog style bits for Chicago compatibility.
 */
//#define DS_VALID_FLAGS (DS_ABSALIGN|DS_SYSMODAL|DS_LOCALEDIT|DS_SETFONT|DS_MODALFRAME|DS_NOIDLEMSG | DS_SETFOREGROUND)
#define DS_VALID_FLAGS   0x1FFF

#define DS_VALID31          0x01e3L
#define DS_VALID40          0x3FFFL
#if(WINVER < 0x0400)
#define DS_3DLOOK           0x0004L
#define DS_FIXEDSYS         0x0008L
#define DS_NOFAILCREATE     0x0010L
#define DS_CONTROL          0x0400L
#define DS_RECURSE      DS_CONTROL  /* BOGUS GOING AWAY */
#define DS_CENTER           0x0800L
#define DS_CENTERMOUSE      0x1000L

#define DS_NONBOLD  DS_3DLOOK   /* BOGUS GOING AWAY */
#define DS_CONTEXTHELP  0x2000L

#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#define DM_REPOSITION       (WM_USER+2)

#define PSM_PAGEINFO        (WM_USER+100)
#define PSM_SHEETINFO       (WM_USER+101)

#define PSI_SETACTIVE       0x0001L
#define PSI_KILLACTIVE      0x0002L
#define PSI_APPLY           0x0003L
#define PSI_RESET           0x0004L
#define PSI_HASHELP         0x0005L
#define PSI_HELP            0x0006L

#define PSI_CHANGED         0x0001L
#define PSI_GUISTART        0x0002L
#define PSI_REBOOT          0x0003L
#define PSI_GETSIBLINGS     0x0004L
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#define DLGC_RECURSE        0x8000      /* Dialog that acts like a control  */
#endif /* WINVER < 0x0400 */
#define LBCB_CARETON            0x01A3
#define LBCB_CARETOFF           0x01A4
#if(WINVER < 0x0400)
#define LB_INITSTORAGE          0x01A8
#define LB_ITEMFROMPOINT        0x01A9
#define LB_INSERTSTRINGUPPER    0x01AA
#define LB_INSERTSTRINGLOWER    0x01AB
#define LB_ADDSTRINGUPPER       0x01AC
#define LB_ADDSTRINGLOWER       0x01AD
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#define LBS_NOSEL             0x4000L
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#define CBS_UPPERCASE           0x2000L
#define CBS_LOWERCASE           0x4000L
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#define CB_GETTOPINDEX              0x015b
#define CB_SETTOPINDEX              0x015c
#define CB_GETHORIZONTALEXTENT      0x015d
#define CB_SETHORIZONTALEXTENT      0x015e
#define CB_GETDROPPEDWIDTH          0x015f
#define CB_SETDROPPEDWIDTH          0x0160
#define CB_INITSTORAGE              0x0161
#endif /* WINVER < 0x0400 */
#define CBEC_SETCOMBOFOCUS          (CB_MSGMAX+1)
#define CBEC_KILLCOMBOFOCUS         (CB_MSGMAX+2)
#if(WINVER < 0x0400)
#define SBS_SIZEGRIP                0x0010L
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#define SBM_SETPAGE                 0x00E7
#define SBM_GETPAGE                 0x00E8
#define SBM_SETSCROLLINFO           0x00E9
#define SBM_GETSCROLLINFO           0x00EA

#define SIF_RANGE           0x0001
#define SIF_PAGE            0x0002
#define SIF_POS             0x0004
#define SIF_ALL             (SIF_RANGE | SIF_PAGE | SIF_POS)
#define SIF_DISABLENOSCROLL 0x0008
#define SIF_RETURNOLDVALUE  0x1000
#define SIF_NOSCROLL        0x2000
#define SIF_RETURNOLDPOS    0x4000

typedef struct tagSCROLLINFO
{
    UINT    cbSize;
    UINT    fMask;
    int     nMin;
    int     nMax;
    UINT    nPage;
    int     nPos;
}   SCROLLINFO, FAR *LPSCROLLINFO, CONST FAR *LPCSCROLLINFO;

WINUSERAPI int     WINAPI SetScrollInfo(HWND, int, LPCSCROLLINFO, BOOL);
WINUSERAPI BOOL    WINAPI GetScrollInfo(HWND, int, LPSCROLLINFO);
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#define MDITILE_SKIPNOCAPTION   0x0004
#define MDITILE_SKIPTOPMOST     0x0008
#define MDITILE_SKIPOFFSCREEN   0x0010
#define MDITILE_REALWINDOWS     0x001E
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
WINUSERAPI WORD    WINAPI TileWindows(HWND hwndParent, UINT wHow, CONST RECT * lpRect, UINT cKids, const HWND FAR * lpKids);
WINUSERAPI WORD    WINAPI CascadeWindows(HWND hwndParent, UINT wHow, CONST RECT * lpRect, UINT cKids,  const HWND FAR * lpKids);
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#define HELP_CONTEXTMENU  0x000a
#define HELP_FINDER       0x000b
#define HELP_WM_HELP      0x000c

#define HELP_TCARD              0x8000
#define HELP_TCARD_DATA         0x0010
#define HELP_TCARD_NEXT         0x0011
#define HELP_TCARD_OTHER_CALLER 0x0011
#endif /* WINVER < 0x0400 */
#define HELP_HB_NORMAL    0x0000L
#define HELP_HB_STRING    0x0100L
#define HELP_HB_STRUCT    0x0200L
#if(WINVER < 0x0400)
WINUSERAPI BOOL  WINAPI  ResetDisplay(void);
#endif /* WINVER < 0x0400 */
#define SPI_TIMEOUTS                7
#define SPI_KANJIMENU               8
#if(WINVER < 0x0400)
#define SPI_SETDRAGFULLWINDOWS     37
#define SPI_GETDRAGFULLWINDOWS     38
#define SPI_GETKEYBOARDLAYOUT      39
#define SPI_SETKEYBOARDLAYOUT      40
#define SPI_GETNONCLIENTMETRICS    41
#define SPI_SETNONCLIENTMETRICS    42
#define SPI_GETMINIMIZEDMETRICS    43
#define SPI_SETMINIMIZEDMETRICS    44
#define SPI_GETICONMETRICS         45
#define SPI_SETICONMETRICS         46
#define SPI_SETWORKAREA            47      /* FOR NOW */
#define SPI_SETUSERTYPE            48
#define SPI_SETPENWINDOWS          49

#define SPI_GETHIGHCONTRAST        66
#define SPI_SETHIGHCONTRAST        67
#define SPI_GETKEYBOARDPREF        68
#define SPI_SETKEYBOARDPREF        69
#define SPI_GETSCREENREADER        70
#define SPI_SETSCREENREADER        71
#define SPI_GETANIMATION           72
#define SPI_SETANIMATION           73
// #define SPI_GETCURSORSIZE          74 /* Obsolete */
// #define SPI_SETCURSORSIZE          75 /* Obsolete */
#define SPI_SETDRAGWIDTH           76
#define SPI_SETDRAGHEIGHT          77
#define SPI_SETHANDHELD            78
#define SPI_GETLOWPOWERTIMEOUT     79
#define SPI_GETPOWEROFFTIMEOUT     80
#define SPI_SETLOWPOWERTIMEOUT     81
#define SPI_SETPOWEROFFTIMEOUT     82
#define SPI_GETLOWPOWERACTIVE      83
#define SPI_GETPOWEROFFACTIVE      84
#define SPI_SETLOWPOWERACTIVE      85
#define SPI_SETPOWEROFFACTIVE      86
#define SPI_SETCURSORS             87
#define SPI_SETICONS               88
#endif /* WINVER < 0x0400 */
#define SPI_MAX                    71
#define SPIF_VALID            (SPIF_UPDATEINIFILE | SPIF_SENDWININICHANGE)
#if(WINVER < 0x0400)
#define METRICS_USEDEFAULT -1
typedef struct tagNONCLIENTMETRICSA
{
    UINT    cbSize;
    int     iBorderWidth;
    int     iScrollWidth;
    int     iScrollHeight;
    int     iCaptionWidth;
    int     iCaptionHeight;
    LOGFONTA lfCaptionFont;
    int     iSmCaptionWidth;
    int     iSmCaptionHeight;
    LOGFONTA lfSmCaptionFont;
    int     iMenuWidth;
    int     iMenuHeight;
    LOGFONTA lfMenuFont;
    LOGFONTA lfStatusFont;
    LOGFONTA lfMessageFont;
}   NONCLIENTMETRICSA, *PNONCLIENTMETRICSA, FAR* LPNONCLIENTMETRICSA;
// #define SPI_GETCURSORSIZE          74 /* Obsolete */
// #define SPI_SETCURSORSIZE          75 /* Obsolete */
#define SPI_SETDRAGWIDTH           76
#define SPI_SETDRAGHEIGHT          77
#define SPI_SETHANDHELD            78
#define SPI_GETLOWPOWERTIMEOUT     79
#define SPI_GETPOWEROFFTIMEOUT     80
#define SPI_SETLOWPOWERTIMEOUT     81
#define SPI_SETPOWEROFFTIMEOUT     82
#define SPI_GETLOWPOWERACTIVE      83
#define SPI_GETPOWEROFFACTIVE      84
#define SPI_SETLOWPOWERACTIVE      85
#define SPI_SETPOWEROFFACTIVE      86
#define SPI_SETCURSORS             87
#define SPI_SETICONS               88
#endif /* WINVER < 0x0400 */
#define SPI_MAX                    71
#define SPIF_VALID            (SPIF_UPDATEINIFILE | SPIF_SENDWININICHANGE)
#if(WINVER < 0x0400)
#define METRICS_USEDEFAULT -1
typedef struct tagNONCLIENTMETRICSW
{
    UINT    cbSize;
    int     iBorderWidth;
    int     iScrollWidth;
    int     iScrollHeight;
    int     iCaptionWidth;
    int     iCaptionHeight;
    LOGFONTW lfCaptionFont;
    int     iSmCaptionWidth;
    int     iSmCaptionHeight;
    LOGFONTW lfSmCaptionFont;
    int     iMenuWidth;
    int     iMenuHeight;
    LOGFONTW lfMenuFont;
    LOGFONTW lfStatusFont;
    LOGFONTW lfMessageFont;
}   NONCLIENTMETRICSW, *PNONCLIENTMETRICSW, FAR* LPNONCLIENTMETRICSW;
#ifdef UNICODE
#define tagNONCLIENTMETRICS  tagNONCLIENTMETRICSW
#else
#define tagNONCLIENTMETRICS  tagNONCLIENTMETRICSA
#endif // !UNICODE

#define ARW_BOTTOMLEFT              0x0000L
#define ARW_BOTTOMRIGHT             0x0001L
#define ARW_TOPLEFT                 0x0002L
#define ARW_TOPRIGHT                0x0003L
#define ARW_STARTMASK               0x0003L
#define ARW_STARTRIGHT              0x0001L
#define ARW_STARTTOP                0x0002L

#define ARW_LEFT                    0x0000L
#define ARW_RIGHT                   0x0000L
#define ARW_UP                      0x0004L
#define ARW_DOWN                    0x0004L
#define ARW_HIDE                    0x0008L
#define ARW_VALID                   0x000FL

typedef struct tagMINIMIZEDMETRICSA
{
    UINT    cbSize;
    int     iWidth;
    int     iHorzGap;
    int     iVertGap;
    int     iArrange;
}   MINIMIZEDMETRICSA, *PMINIMIZEDMETRICSA, *LPMINIMIZEDMETRICSA;
typedef struct tagMINIMIZEDMETRICSW
{
    UINT    cbSize;
    int     iWidth;
    int     iHorzGap;
    int     iVertGap;
    int     iArrange;
}   MINIMIZEDMETRICSW, *PMINIMIZEDMETRICSW, *LPMINIMIZEDMETRICSW;
#ifdef UNICODE
typedef MINIMIZEDMETRICSW MINIMIZEDMETRICS;
typedef PMINIMIZEDMETRICSW PMINIMIZEDMETRICS;
typedef LPMINIMIZEDMETRICSW LPMINIMIZEDMETRICS;
#else
typedef MINIMIZEDMETRICSA MINIMIZEDMETRICS;
typedef PMINIMIZEDMETRICSA PMINIMIZEDMETRICS;
typedef LPMINIMIZEDMETRICSA LPMINIMIZEDMETRICS;
#endif // UNICODE

typedef struct tagICONMETRICSA
{
    UINT    cbSize;
    int     iHorzSpacing;
    int     iVertSpacing;
    int     iTitleWrap;
    LOGFONTA lfFont;
}   ICONMETRICSA, *PICONMETRICSA, *LPICONMETRICSA;
typedef struct tagICONMETRICSW
{
    UINT    cbSize;
    int     iHorzSpacing;
    int     iVertSpacing;
    int     iTitleWrap;
    LOGFONTW lfFont;
}   ICONMETRICSW, *PICONMETRICSW, *LPICONMETRICSW;
#ifdef UNICODE
typedef ICONMETRICSW ICONMETRICS;
typedef PICONMETRICSW PICONMETRICS;
typedef LPICONMETRICSW LPICONMETRICS;
#else
typedef ICONMETRICSA ICONMETRICS;
typedef PICONMETRICSA PICONMETRICS;
typedef LPICONMETRICSA LPICONMETRICS;
#endif // UNICODE

typedef struct tagANIMATIONINFO
{
    UINT    cbSize;
    int     iMinAnimate;
}   ANIMATIONINFO, *LPANIMATIONINFO;

typedef struct tagSERIALKEYSA
{
    UINT    cbSize;
    BOOL    fSerialKeysOn;
    BOOL    fAvailable;
    LPSTR     lpszActivePort;
    LPSTR     lpszPort;
    UINT    iBaudRate;
    UINT    iPortState;
    UINT    iActive;
}   SERIALKEYSA, *LPSERIALKEYSA;
typedef struct tagSERIALKEYSW
{
    UINT    cbSize;
    BOOL    fSerialKeysOn;
    BOOL    fAvailable;
    LPWSTR    lpszActivePort;
    LPWSTR    lpszPort;
    UINT    iBaudRate;
    UINT    iPortState;
    UINT    iActive;
}   SERIALKEYSW, *LPSERIALKEYSW;
#ifdef UNICODE
typedef SERIALKEYSW SERIALKEYS;
typedef LPSERIALKEYSW LPSERIALKEYS;
#else
typedef SERIALKEYSA SERIALKEYS;
typedef LPSERIALKEYSA LPSERIALKEYS;
#endif // UNICODE

typedef struct tagHIGHCONTRAST
{
    UINT    cbSize;
    BOOL    fHighContrastOn;
    BOOL    fHokeyActive;
    BOOL    fAvailable;
    BOOL    fConfirmHotkey;
}   HIGHCONTRAST, *LPHIGHCONTRAST;

#endif /* WINVER < 0x0400 */
void LoadRemoteFonts(void);

#define LOGON_LOGOFF        0
#define LOGON_INPUT_TIMEOUT 1
#define LOGON_FLG_MASK      0xF0000000
#define LOGON_FLG_SHIFT     28

#define STARTF_DESKTOPINHERIT   0x40000000
#define STARTF_SCREENSAVER      0x80000000

#define WSS_ERROR       0
#define WSS_BUSY        1
#define WSS_IDLE        2

#define DTF_CENTER    0x00      /* Center the bitmap (default) */
#define DTF_TILE      0x01      /* Tile the bitmap */
#define DTF_NOPALETTE 0x04      /* Realize palette, otherwise match to default. */
#define DTF_RETAIN    0x08      /* Retain bitmap, ignore win.ini changes */

#ifdef _INC_DDEMLH
BOOL DdeIsDataHandleReadOnly(
    HDDEDATA hData);

int DdeGetDataHandleFormat(
    HDDEDATA hData);

DWORD DdeGetCallbackInstance(VOID);
#endif /* defined _INC_DDEMLH */


WINUSERAPI
HWND
WINAPI
WOWFindWindow(
    LPCSTR lpClassName,
    LPCSTR lpWindowName);

int
InternalDoEndTaskDlg(
    TCHAR* pszTitle);

DWORD
InternalWaitCancel(
    HANDLE handle,
    DWORD dwMilliseconds);

HANDLE
InternalCreateCallbackThread(
    HANDLE hProcess,
    DWORD lpfn,
    DWORD dwData);

WINUSERAPI
UINT
WINAPI
GetInternalWindowPos(
    HWND hWnd,
    LPRECT lpRect,
    LPPOINT lpPoint);

WINUSERAPI
BOOL
WINAPI
SetInternalWindowPos(
    HWND hWnd,
    UINT cmdShow,
    LPRECT lpRect,
    LPPOINT lpPoint);

WINUSERAPI
BOOL
WINAPI
CalcChildScroll(
    HWND hWnd,
    UINT sb);

WINUSERAPI
BOOL
WINAPI
RegisterTasklist(
    HWND hWndTasklist);

WINUSERAPI
BOOL
WINAPI
CascadeChildWindows(
    HWND hWndParent,
    UINT flags);

WINUSERAPI
BOOL
WINAPI
TileChildWindows(
    HWND hWndParent,
    UINT flags);

WINUSERAPI
int
WINAPI
InternalGetWindowText(
    HWND hWnd,
    LPWSTR lpString,
    int nMaxCount);

BOOL
InternalBoostHardError(
    DWORD dwProcessId,
    BOOL fForce);

/*
 * Logon support routines
 */
WINUSERAPI
BOOL
WINAPI
RegisterLogonProcess(
    DWORD dwProcessId,
    BOOL fSecure);

WINUSERAPI
UINT
WINAPI
LockWindowStation(
    HWINSTA hWindowStation);

WINUSERAPI
BOOL
WINAPI
UnlockWindowStation(
    HWINSTA hWindowStation);

WINUSERAPI
BOOL
WINAPI
SetWindowStationUser(
    HWINSTA hWindowStation,
    PLUID pLuidUser);

WINUSERAPI
BOOL
WINAPI
SetDesktopBitmap(
    HDESK hdesk,
    HBITMAP hbmWallpaper,
    DWORD dwStyle);

WINUSERAPI
BOOL
WINAPI
SetLogonNotifyWindow(
    HWINSTA hWindowStation,
    HWND hWndNotify);

WINUSERAPI
UINT
WINAPI
GetIconId(
    HANDLE hRes,
    LPSTR lpszType);

int
CriticalNullCall(
    VOID);

int
NullCall(
    VOID);

VOID
UserNotifyConsoleApplication(
    DWORD dwProcessId);

HBRUSH
GetConsoleWindowBrush(
    PVOID pWnd);


#ifndef NOMSG

#define TM_POSTCHARBREAKS 0x0002

WINUSERAPI
BOOL
WINAPI
TranslateMessageEx(
    CONST MSG *lpMsg,
    UINT flags);

#endif /* !NOMSG */

int
WCSToMBEx(
    WORD wCodePage,
    LPCWSTR pUnicodeString,
    int cbUnicodeChar,
    LPSTR *ppAnsiString,
    int nAnsiChar,
    BOOL bAllocateMem);

int
MBToWCSEx(
    WORD wCodePage,
    LPCSTR pAnsiString,
    int nAnsiChar,
    LPWSTR *ppUnicodeString,
    int cbUnicodeChar,
    BOOL bAllocateMem);

WINUSERAPI
BOOL
WINAPI
EndTask(
    HWND hWnd,
    BOOL fShutDown,
    BOOL fForce);

WINUSERAPI
BOOL
WINAPI
UpdatePerUserSystemParameters(
    BOOL bUserLoggedOn);

typedef VOID  (APIENTRY *PFNW32ET)(VOID);

BOOL
RegisterUserHungAppHandlers(
    PFNW32ET pfnW32EndTask,
    HANDLE   hEventWowExec);

ATOM
RegisterClassWOWA(
    PVOID   lpWndClass,
    LPDWORD pdwWOWstuff);

LONG
GetClassWOWWords(
    HINSTANCE hInstance,
    LPCTSTR pString);

DWORD
CurrentTaskLock(
    DWORD hlck);


/*
 * hack because GDI includes winuserp.h without including winuser.h !
 * This should go with the other types at top.
 */
#ifdef STRICT
typedef BOOL (CALLBACK* DEVICEENUMPROC)(LPVOID, DWORD);
#else
typedef FARPROC DEVICEENUMPROC;
#endif

WINUSERAPI
BOOL
WINAPI
EnumDisplayDevicesA(
    DEVICEENUMPROC lpfnDeviceCallback,
    DWORD dwData);
WINUSERAPI
BOOL
WINAPI
EnumDisplayDevicesW(
    DEVICEENUMPROC lpfnDeviceCallback,
    DWORD dwData);
#ifdef UNICODE
#define EnumDisplayDevices  EnumDisplayDevicesW
#else
#define EnumDisplayDevices  EnumDisplayDevicesA
#endif // !UNICODE

typedef struct _DISPLAY_DEVICEA {
    DWORD cb;
    LPCSTR   lpszDeviceName;
    LPSTR   lpszDeviceString;
} DISPLAY_DEVICEA, PDISPLAY_DEVICEA, LPDISPLAY_DEVICEA;
typedef struct _DISPLAY_DEVICEW {
    DWORD cb;
    LPCWSTR  lpszDeviceName;
    LPWSTR  lpszDeviceString;
} DISPLAY_DEVICEW, PDISPLAY_DEVICEW, LPDISPLAY_DEVICEW;
#ifdef UNICODE
typedef DISPLAY_DEVICEW DISPLAY_DEVICE;
typedef PDISPLAY_DEVICEW PDISPLAY_DEVICE;
typedef LPDISPLAY_DEVICEW LPDISPLAY_DEVICE;
#else
typedef DISPLAY_DEVICEA DISPLAY_DEVICE;
typedef PDISPLAY_DEVICEA PDISPLAY_DEVICE;
typedef LPDISPLAY_DEVICEA LPDISPLAY_DEVICE;
#endif // UNICODE

WINUSERAPI
BOOL
WINAPI
EnumDisplayDeviceModesA(
    LPCSTR lpszDeviceName,
    DEVICEENUMPROC lpfnModeCallback,
    DWORD dwData);
WINUSERAPI
BOOL
WINAPI
EnumDisplayDeviceModesW(
    LPCWSTR lpszDeviceName,
    DEVICEENUMPROC lpfnModeCallback,
    DWORD dwData);
#ifdef UNICODE
#define EnumDisplayDeviceModes  EnumDisplayDeviceModesW
#else
#define EnumDisplayDeviceModes  EnumDisplayDeviceModesA
#endif // !UNICODE

WINUSERAPI
HDESK
WINAPI
GetInputDesktop(
    VOID);

WINUSERAPI
BOOL
WINAPI
EnumWindowStationsA(
    WINSTAENUMPROCA lpEnumFunc,
    LPARAM lParam);
WINUSERAPI
BOOL
WINAPI
EnumWindowStationsW(
    WINSTAENUMPROCW lpEnumFunc,
    LPARAM lParam);
#ifdef UNICODE
#define EnumWindowStations  EnumWindowStationsW
#else
#define EnumWindowStations  EnumWindowStationsA
#endif // !UNICODE

WINUSERAPI
BOOL
WINAPI
EnumDesktopsA(
    HWINSTA hwinsta,
    DESKTOPENUMPROCA lpEnumFunc,
    LPARAM lParam);
WINUSERAPI
BOOL
WINAPI
EnumDesktopsW(
    HWINSTA hwinsta,
    DESKTOPENUMPROCW lpEnumFunc,
    LPARAM lParam);
#ifdef UNICODE
#define EnumDesktops  EnumDesktopsW
#else
#define EnumDesktops  EnumDesktopsA
#endif // !UNICODE

#define WINDOWED                    0
#define FULLSCREEN                  1
#define GDIFULLSCREEN               2

WINUSERAPI
BOOL
WINAPI
SetWindowFullScreenState(
    HWND hWnd,
    UINT uiNewState);

WINUSERAPI
UINT
WINAPI
GetWindowFullScreenState(
    HWND hWnd);


#define WCSToMB(pUnicodeString, cbUnicodeChar, ppAnsiString, nAnsiChar,\
bAllocateMem)\
WCSToMBEx(0, pUnicodeString, cbUnicodeChar, ppAnsiString, nAnsiChar, bAllocateMem)

#define MBToWCS(pAnsiString, nAnsiChar, ppUnicodeString, cbUnicodeChar,\
bAllocateMem)\
MBToWCSEx(0, pAnsiString, nAnsiChar, ppUnicodeString, cbUnicodeChar, bAllocateMem)

#define ID(string) (((DWORD)string & 0xffff0000) == 0)

/*
 * For setting RIT timers and such.  GDI uses this for the cursor-restore
 * timer.
 */
#define TMRF_READY      0x0001
#define TMRF_SYSTEM     0x0002
#define TMRF_RIT        0x0004
#define TMRF_INIT       0x0008
#define TMRF_ONESHOT    0x0010
#define TMRF_WAITING    0x0020


/*
 * For GDI SetAbortProc support.
 */

int
CsDrawTextA(
    HDC hDC,
    LPCSTR lpString,
    int nCount,
    LPRECT lpRect,
    UINT uFormat);
int
CsDrawTextW(
    HDC hDC,
    LPCWSTR lpString,
    int nCount,
    LPRECT lpRect,
    UINT uFormat);
#ifdef UNICODE
#define CsDrawText  CsDrawTextW
#else
#define CsDrawText  CsDrawTextA
#endif // !UNICODE

LONG
CsTabbedTextOutA(
    HDC hDC,
    int X,
    int Y,
    LPCSTR lpString,
    int nCount,
    int nTabPositions,
    LPINT lpnTabStopPositions,
    int nTabOrigin);
LONG
CsTabbedTextOutW(
    HDC hDC,
    int X,
    int Y,
    LPCWSTR lpString,
    int nCount,
    int nTabPositions,
    LPINT lpnTabStopPositions,
    int nTabOrigin);
#ifdef UNICODE
#define CsTabbedTextOut  CsTabbedTextOutW
#else
#define CsTabbedTextOut  CsTabbedTextOutA
#endif // !UNICODE

int
CsFrameRect(
    HDC hDC,
    CONST RECT *lprc,
    HBRUSH hbr);

#ifdef UNICODE
#define CsDrawText      CsDrawTextW
#define CsTabbedTextOut CsTabbedTextOutW
#else /* !UNICODE */
#define CsDrawText      CsDrawTextA
#define CsTabbedTextOut CsTabbedTextOutA
#endif /* !UNICODE */

/*
 * Custom Cursor action.
 */
WINUSERAPI
BOOL
WINAPI
SetSystemCursor(
    HCURSOR hcur,
    DWORD id);

HCURSOR
GetCursorInfo(
    HCURSOR hcur,
    LPWSTR id,
    int iFrame,
    LPDWORD pjifRate,
    LPINT pccur);


/*
 * WOW: replace cursor/icon handle
 */

WINUSERAPI
BOOL
WINAPI
SetCursorContents(HCURSOR hCursor, HCURSOR hCursorNew);

typedef struct _TAG {
    DWORD type;
    DWORD style;
    DWORD len;
} TAG, *PTAG;

#define MAKETAG(a, b, c, d) (DWORD)(a | (b<<8) | ((DWORD)c<<16) | ((DWORD)d<<24))


/* Valid TAG types. */

/* 'ASDF' (CONT) - Advanced Systems Data Format */

#define TAGT_ASDF MAKETAG('A', 'S', 'D', 'F')


/* 'RAD ' (CONT) - ?R Animation ?Definition (an aggregate type) */

#define TAGT_RAD  MAKETAG('R', 'A', 'D', ' ')


/* 'ANIH' (DATA) - ANImation Header */
/* Contains an ANIHEADER structure. */

#define TAGT_ANIH MAKETAG('A', 'N', 'I', 'H')


/*
 * 'RATE' (DATA) - RATE table (array of jiffies)
 * Contains an array of JIFs.  Each JIF specifies how long the corresponding
 * animation frame is to be displayed before advancing to the next frame.
 * If the AF_SEQUENCE flag is set then the count of JIFs == anih.cSteps,
 * otherwise the count == anih.cFrames.
 */
#define TAGT_RATE MAKETAG('R', 'A', 'T', 'E')

/*
 * 'SEQ ' (DATA) - SEQuence table (array of frame index values)
 * Countains an array of DWORD frame indices.  anih.cSteps specifies how
 * many.
 */
#define TAGT_SEQ  MAKETAG('S', 'E', 'Q', ' ')


/* 'ICON' (DATA) - Windows ICON format image (replaces MPTR) */

#define TAGT_ICON MAKETAG('I', 'C', 'O', 'N')


/* 'TITL' (DATA) - TITLe string (can be inside or outside aggregates) */
/* Contains a single ASCIIZ string that titles the file. */

#define TAGT_TITL MAKETAG('T', 'I', 'T', 'L')


/* 'AUTH' (DATA) - AUTHor string (can be inside or outside aggregates) */
/* Contains a single ASCIIZ string that indicates the author of the file. */

#define TAGT_AUTH MAKETAG('A', 'U', 'T', 'H')



#define TAGT_AXOR MAKETAG('A', 'X', 'O', 'R')


/* Valid TAG styles. */

/* 'CONT' - CONTainer chunk (contains other DATA and CONT chunks) */

#define TAGS_CONT MAKETAG('C', 'O', 'N', 'T')


/* 'DATA' - DATA chunk */

#define TAGS_DATA MAKETAG('D', 'A', 'T', 'A')

typedef DWORD JIF, *PJIF;

typedef struct _ANIHEADER {     /* anih */
    DWORD cbSizeof;
    DWORD cFrames;
    DWORD cSteps;
    DWORD cx, cy;
    DWORD cBitCount, cPlanes;
    JIF   jifRate;
    DWORD fl;
} ANIHEADER, *PANIHEADER;

/* If the AF_ICON flag is specified the fields cx, cy, cBitCount, and */
/* cPlanes are all unused.  Each frame will be of type ICON and will */
/* contain its own dimensional information. */

#define AF_ICON     0x0001L     /* Windows format icon/cursor animation */
#define AF_SEQUENCE 0x0002L     /* Animation is sequenced */
#ifdef __cplusplus
}
#endif  /* __cplusplus */
#endif  /* !_WINUSERP_ */
