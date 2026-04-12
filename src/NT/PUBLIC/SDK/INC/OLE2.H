//+---------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//  File:       OLE2.h
//  Contents:   Main OLE2 header; Defines Linking and Emmebbeding interfaces, and API's.
//              Also includes .h files for the compobj, and oleauto  subcomponents.
//
//  History:    04-07-94   terryru   Created
//
//----------------------------------------------------------------------------
#if !defined( _OLE2_H_ )
#define _OLE2_H_

// Set packing to 8 for ISV, and Chicago support (terryru)
#ifndef RC_INVOKED
#pragma pack(8)
#endif // RC_INVOKED

// Make 100% sure WIN32 is defined
#ifndef WIN32
#define WIN32    100  // 100 == NT version 1.0
#endif


// SET to remove _export from interface definitions


#include <objerror.h>

#include <objbase.h>
#include <oleauto.h>





// default inset-width of the hot zone, in pixels
//   typical use: GetProfileInt("windows","DragScrollInset",DD_DEFSCROLLINSET)
#define DD_DEFSCROLLINSET               11

// default delay before scrolling, in milliseconds
//   typical use: GetProfileInt("windows","DragScrollDelay",DD_DEFSCROLLDELAY)
#define DD_DEFSCROLLDELAY               50

// default scroll interval, in milliseconds
//   typical use: GetProfileInt("windows","DragScrollInterval",
//                              DD_DEFSCROLLINTERVAL)
#define DD_DEFSCROLLINTERVAL    50

// default delay before dragging should start, in milliseconds
//   typical use: GetProfileInt("windows", "DragDelay", DD_DEFDRAGDELAY)
#define DD_DEFDRAGDELAY         200

// default minimum distance (radius) before dragging should start, in pixels
//   typical use: GetProfileInt("windows", "DragMinDist", DD_DEFDRAGMINDIST)
#define DD_DEFDRAGMINDIST               2



/* Dragdrop specific error codes */





//
// BUGBUG
// Do these belong here?
//


// View OBJECT Error Codes

#define E_DRAW                  VIEW_E_DRAW

// IDataObject Error Codes
#define DATA_E_FORMATETC        DV_E_FORMATETC





// Common stuff gleamed from OLE.2,

/* verbs */
#define OLEIVERB_PRIMARY            (0L)
#define OLEIVERB_SHOW               (-1L)
#define OLEIVERB_OPEN               (-2L)
#define OLEIVERB_HIDE               (-3L)
#define OLEIVERB_UIACTIVATE         (-4L)
#define OLEIVERB_INPLACEACTIVATE    (-5L)
#define OLEIVERB_DISCARDUNDOSTATE   (-6L)

// for OleCreateEmbeddingHelper flags; roles in low word; options in high word
#define EMBDHLP_INPROC_HANDLER   0x0000L
#define EMBDHLP_INPROC_SERVER    0x0001L
#define EMBDHLP_CREATENOW    0x00000000L
#define EMBDHLP_DELAYCREATE  0x00010000L
#ifndef __OLETYP_H__
#define __OLETYP_H__

#ifdef __cplusplus
extern "C" {
#endif

#define small char





extern RPC_IF_HANDLE IOLETypes_ServerIfHandle;

extern RPC_IF_HANDLE IOLETypes_ClientIfHandle;

#ifndef _SIZE_T_DEFINED
typedef unsigned int size_t;
#define _SIZE_T_DEFINED
#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif

#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif

typedef enum tagMEMCTX
  {
	MEMCTX_TASK = 1,
	MEMCTX_SHARED = 2,
	MEMCTX_MACSYSTEM = 3,
	MEMCTX_UNKNOWN = -1,
	MEMCTX_SAME = -2
  }
MEMCTX;

typedef enum tagCLSCTX
  {
	CLSCTX_INPROC_SERVER = 1,
	CLSCTX_INPROC_HANDLER = 2,
	CLSCTX_LOCAL_SERVER = 4
  }
CLSCTX;

typedef enum tagMSHLFLAGS
  {
	MSHLFLAGS_NORMAL = 0,
	MSHLFLAGS_TABLESTRONG = 1,
	MSHLFLAGS_TABLEWEAK = 2
  }
MSHLFLAGS;

typedef enum tagMSHCTX
  {
	MSHCTX_LOCAL = 0,
	MSHCTX_NOSHAREDMEM = 1,
	MSHCTX_DIFFERENTMACHINE = 2
  }
MSHCTX;

typedef enum tagDVASPECT
  {
	DVASPECT_CONTENT = 1,
	DVASPECT_THUMBNAIL = 2,
	DVASPECT_ICON = 4,
	DVASPECT_DOCPRINT = 8
  }
DVASPECT;

typedef enum tagSTGC
  {
	STGC_DEFAULT = 0,
	STGC_OVERWRITE = 1,
	STGC_ONLYIFCURRENT = 2,
	STGC_DANGEROUSLYCOMMITMERELYTODISKCACHE = 4
  }
STGC;

typedef enum tagSTGMOVE
  {
	STGMOVE_MOVE = 0,
	STGMOVE_COPY = 1
  }
STGMOVE;

typedef enum tagSTATFLAG
  {
	STATFLAG_DEFAULT = 0,
	STATFLAG_NONAME = 1
  }
STATFLAG;

typedef enum tagSTGFMT
  {
	STGFMT_DOCUMENT = 0,
	STGFMT_DIRECTORY = 1,
	STGFMT_CATALOG = 2,
	STGFMT_FILE = 3
  }
STGFMT;

typedef enum tagOLERENDER
  {
	OLERENDER_NONE = 0,
	OLERENDER_DRAW = 1,
	OLERENDER_FORMAT = 2,
	OLERENDER_ASIS = 3
  }
OLERENDER;

typedef OLERENDER *LPOLERENDER;

typedef struct tagOBJECTDESCRIPTOR
  {
  ULONG cbSize;
  CLSID clsid;
  DWORD dwDrawAspect;
  SIZEL sizel;
  POINTL pointl;
  DWORD dwStatus;
  DWORD dwFullUserTypeName;
  DWORD dwSrcOfCopy;
  }
OBJECTDESCRIPTOR;

typedef struct tagOBJECTDESCRIPTOR LINKSRCDESCRIPTOR;

typedef struct tagOBJECTDESCRIPTOR *POBJECTDESCRIPTOR;

typedef struct tagOBJECTDESCRIPTOR *LPOBJECTDESCRIPTOR;

typedef struct tagOBJECTDESCRIPTOR *PLINKSRCDESCRIPTOR;

typedef struct tagOBJECTDESCRIPTOR *LPLINKSRCDESCRIPTOR;

typedef enum tagCONNECTFLAGS
  {
	CONNECTFLAGS_PERSIST = 1
  }
CONNECTFLAGS;

typedef enum tagDISCONNECTFLAGS
  {
	DISCONNECTFLAGS_PASSIVATE = 1,
	DISCONNECTFLAGS_DESTROY = 2
  }
DISCONNECTFLAGS;


#if !defined(IMPORT_USED_MULTIPLE) && !defined(IMPORT_USED_SINGLE)

/* routine that gets node for struct tagOBJECTDESCRIPTOR */
void _gns_tagOBJECTDESCRIPTOR (OBJECTDESCRIPTOR  *, PRPC_MESSAGE);

#endif /*!defined(IMPORT_USED_MULTIPLE) && !defined(IMPORT_USED_SINGLE)*/

void __RPC_FAR * __RPC_API MIDL_user_allocate(size_t);
void __RPC_API MIDL_user_free(void __RPC_FAR *);
#ifndef __MIDL_USER_DEFINED
#define midl_user_allocate MIDL_user_allocate
#define midl_user_free     MIDL_user_free
#define __MIDL_USER_DEFINED
#endif

#ifdef __cplusplus
}
#endif

#endif

// Forward declarations for typedefs in this file
typedef interface IDropSource IDropSource;
typedef interface IEnumOLEVERB IEnumOLEVERB;
typedef interface IOleWindow IOleWindow;
typedef interface IOleInPlaceObject IOleInPlaceObject;
typedef interface IOleInPlaceActiveObject IOleInPlaceActiveObject;
typedef interface IOleInPlaceUIWindow IOleInPlaceUIWindow;
typedef interface IOleInPlaceFrame IOleInPlaceFrame;
typedef interface IOleInPlaceSite IOleInPlaceSite;
typedef interface IOleAdviseHolder IOleAdviseHolder;
typedef interface IOleLink IOleLink;
typedef interface IParseDisplayName IParseDisplayName;
typedef interface IDropTarget IDropTarget;
typedef interface IOleCache IOleCache;
typedef interface IOleCache2 IOleCache2;
typedef interface IOleCacheControl IOleCacheControl;
typedef interface IViewObject IViewObject;
typedef interface IViewObject2 IViewObject2;
typedef interface IOleContainer IOleContainer;
typedef interface IOleItemContainer IOleItemContainer;
typedef interface IOleClientSite IOleClientSite;
typedef interface IOleObject IOleObject;

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IDropSource
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IDropSource__
#define __IDropSource__

/* Forward declaration */
typedef interface IDropSource IDropSource;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPDROPSOURCE_DEFINED

#define _LPDROPSOURCE_DEFINED

typedef IDropSource *LPDROPSOURCE;


#endif

EXTERN_C const IID IID_IDropSource;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IDropSource : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE QueryContinueDrag
    (
	BOOL fEscapePressed,
	DWORD grfKeyState
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GiveFeedback
    (
	DWORD dwEffect
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IDropSourceVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IDropSource FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IDropSource FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IDropSource FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *QueryContinueDrag)
    (
        IDropSource FAR * This,
	BOOL fEscapePressed,
	DWORD grfKeyState
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GiveFeedback)
    (
        IDropSource FAR * This,
	DWORD dwEffect
    );
    
} IDropSourceVtbl;

interface IDropSource
{
    IDropSourceVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IDropSource__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IEnumOLEVERB
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IEnumOLEVERB__
#define __IEnumOLEVERB__

/* Forward declaration */
typedef interface IEnumOLEVERB IEnumOLEVERB;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPENUMOLEVERB_DEFINED

#define _LPENUMOLEVERB_DEFINED

typedef IEnumOLEVERB *LPENUMOLEVERB;


#endif

#ifndef _tagOLEVERB_DEFINED

typedef struct tagOLEVERB
  {
  LONG lVerb;
  LPOLESTR lpszVerbName;
  DWORD fuFlags;
  DWORD grfAttribs;
  }
OLEVERB;


typedef struct tagOLEVERB *LPOLEVERB;


#define _tagOLEVERB_DEFINED

#define _OLEVERB_DEFINED

#define _LPOLEVERB_DEFINED

#endif

#ifndef _tagOLEVERBATTRIB_DEFINED

typedef enum tagOLEVERBATTRIB
  {
	OLEVERBATTRIB_NEVERDIRTIES = 1,
	OLEVERBATTRIB_ONCONTAINERMENU = 2
  }
OLEVERBATTRIB;


#define _tagOLEVERBATTRIB_DEFINED

#define _OLEVERBATTRIB_DEFINED

#endif

EXTERN_C const IID IID_IEnumOLEVERB;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IEnumOLEVERB : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Next
    (
	ULONG celt,
	LPOLEVERB rgelt,
	ULONG *pceltFetched
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Skip
    (
	ULONG celt
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Reset
    (
        void
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Clone
    (
	IEnumOLEVERB **ppenum
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IEnumOLEVERBVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IEnumOLEVERB FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IEnumOLEVERB FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IEnumOLEVERB FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Next)
    (
        IEnumOLEVERB FAR * This,
	ULONG celt,
	LPOLEVERB rgelt,
	ULONG *pceltFetched
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Skip)
    (
        IEnumOLEVERB FAR * This,
	ULONG celt
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Reset)
    (
        IEnumOLEVERB FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Clone)
    (
        IEnumOLEVERB FAR * This,
	IEnumOLEVERB **ppenum
    );
    
} IEnumOLEVERBVtbl;

interface IEnumOLEVERB
{
    IEnumOLEVERBVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IEnumOLEVERB__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IOleWindow
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IOleWindow__
#define __IOleWindow__

/* Forward declaration */
typedef interface IOleWindow IOleWindow;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPOLEWINDOW_DEFINED

#define _LPOLEWINDOW_DEFINED

typedef IOleWindow *LPOLEWINDOW;


#endif

EXTERN_C const IID IID_IOleWindow;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IOleWindow : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE GetWindow
    (
	HWND *phwnd
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE ContextSensitiveHelp
    (
	BOOL fEnterMode
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IOleWindowVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IOleWindow FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IOleWindow FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IOleWindow FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetWindow)
    (
        IOleWindow FAR * This,
	HWND *phwnd
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *ContextSensitiveHelp)
    (
        IOleWindow FAR * This,
	BOOL fEnterMode
    );
    
} IOleWindowVtbl;

interface IOleWindow
{
    IOleWindowVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IOleWindow__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IOleInPlaceObject
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IOleInPlaceObject__
#define __IOleInPlaceObject__

/* Forward declaration */
typedef interface IOleInPlaceObject IOleInPlaceObject;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPOLEINPLACEOBJECT_DEFINED

#define _LPOLEINPLACEOBJECT_DEFINED

typedef IOleInPlaceObject *LPOLEINPLACEOBJECT;


#endif

EXTERN_C const IID IID_IOleInPlaceObject;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IOleInPlaceObject : public IOleWindow
{
public:
    virtual HRESULT STDMETHODCALLTYPE InPlaceDeactivate
    (
        void
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE UIDeactivate
    (
        void
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetObjectRects
    (
	LPCRECT lprcPosRect,
	LPCRECT lprcClipRect
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE ReactivateAndUndo
    (
        void
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IOleInPlaceObjectVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IOleInPlaceObject FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IOleInPlaceObject FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IOleInPlaceObject FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetWindow)
    (
        IOleInPlaceObject FAR * This,
	HWND *phwnd
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *ContextSensitiveHelp)
    (
        IOleInPlaceObject FAR * This,
	BOOL fEnterMode
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *InPlaceDeactivate)
    (
        IOleInPlaceObject FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *UIDeactivate)
    (
        IOleInPlaceObject FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetObjectRects)
    (
        IOleInPlaceObject FAR * This,
	LPCRECT lprcPosRect,
	LPCRECT lprcClipRect
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *ReactivateAndUndo)
    (
        IOleInPlaceObject FAR * This
    );
    
} IOleInPlaceObjectVtbl;

interface IOleInPlaceObject
{
    IOleInPlaceObjectVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IOleInPlaceObject__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IOleInPlaceActiveObject
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IOleInPlaceActiveObject__
#define __IOleInPlaceActiveObject__

/* Forward declaration */
typedef interface IOleInPlaceActiveObject IOleInPlaceActiveObject;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPOLEINPLACEACTIVEOBJECT_DEFINED

#define _LPOLEINPLACEACTIVEOBJECT_DEFINED

typedef IOleInPlaceActiveObject *LPOLEINPLACEACTIVEOBJECT;


#endif

EXTERN_C const IID IID_IOleInPlaceActiveObject;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IOleInPlaceActiveObject : public IOleWindow
{
public:
    virtual HRESULT STDMETHODCALLTYPE TranslateAccelerator
    (
	LPMSG lpmsg
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE OnFrameWindowActivate
    (
	BOOL fActivate
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE OnDocWindowActivate
    (
	BOOL fActivate
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE ResizeBorder
    (
	LPCRECT prcBorder,
	IOleInPlaceUIWindow *pUIWindow,
	BOOL fFrameWindow
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE EnableModeless
    (
	BOOL fEnable
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IOleInPlaceActiveObjectVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IOleInPlaceActiveObject FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IOleInPlaceActiveObject FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IOleInPlaceActiveObject FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetWindow)
    (
        IOleInPlaceActiveObject FAR * This,
	HWND *phwnd
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *ContextSensitiveHelp)
    (
        IOleInPlaceActiveObject FAR * This,
	BOOL fEnterMode
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *TranslateAccelerator)
    (
        IOleInPlaceActiveObject FAR * This,
	LPMSG lpmsg
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *OnFrameWindowActivate)
    (
        IOleInPlaceActiveObject FAR * This,
	BOOL fActivate
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *OnDocWindowActivate)
    (
        IOleInPlaceActiveObject FAR * This,
	BOOL fActivate
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *ResizeBorder)
    (
        IOleInPlaceActiveObject FAR * This,
	LPCRECT prcBorder,
	IOleInPlaceUIWindow *pUIWindow,
	BOOL fFrameWindow
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *EnableModeless)
    (
        IOleInPlaceActiveObject FAR * This,
	BOOL fEnable
    );
    
} IOleInPlaceActiveObjectVtbl;

interface IOleInPlaceActiveObject
{
    IOleInPlaceActiveObjectVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IOleInPlaceActiveObject__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IOleInPlaceUIWindow
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IOleInPlaceUIWindow__
#define __IOleInPlaceUIWindow__

/* Forward declaration */
typedef interface IOleInPlaceUIWindow IOleInPlaceUIWindow;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPOLEINPLACEUIWINDOW_DEFINED

#define _LPOLEINPLACEUIWINDOW_DEFINED

typedef IOleInPlaceUIWindow *LPOLEINPLACEUIWINDOW;


#endif

#ifndef _BORDERWIDTHS_DEFINED

#define _BORDERWIDTHS_DEFINED

typedef RECT BORDERWIDTHS;


#endif

#ifndef _LPBORDERWIDTHS_DEFINED

#define _LPBORDERWIDTHS_DEFINED

typedef LPRECT LPBORDERWIDTHS;


#endif

#ifndef _LPCBORDERWIDTHS_DEFINED

#define _LPCBORDERWIDTHS_DEFINED

typedef LPCRECT LPCBORDERWIDTHS;


#endif

EXTERN_C const IID IID_IOleInPlaceUIWindow;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IOleInPlaceUIWindow : public IOleWindow
{
public:
    virtual HRESULT STDMETHODCALLTYPE GetBorder
    (
	LPRECT lprectBorder
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE RequestBorderSpace
    (
	LPCBORDERWIDTHS pborderwidths
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetBorderSpace
    (
	LPCBORDERWIDTHS pborderwidths
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetActiveObject
    (
	IOleInPlaceActiveObject *pActiveObject,
	LPCOLESTR pszObjName
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IOleInPlaceUIWindowVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IOleInPlaceUIWindow FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IOleInPlaceUIWindow FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IOleInPlaceUIWindow FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetWindow)
    (
        IOleInPlaceUIWindow FAR * This,
	HWND *phwnd
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *ContextSensitiveHelp)
    (
        IOleInPlaceUIWindow FAR * This,
	BOOL fEnterMode
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetBorder)
    (
        IOleInPlaceUIWindow FAR * This,
	LPRECT lprectBorder
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *RequestBorderSpace)
    (
        IOleInPlaceUIWindow FAR * This,
	LPCBORDERWIDTHS pborderwidths
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetBorderSpace)
    (
        IOleInPlaceUIWindow FAR * This,
	LPCBORDERWIDTHS pborderwidths
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetActiveObject)
    (
        IOleInPlaceUIWindow FAR * This,
	IOleInPlaceActiveObject *pActiveObject,
	LPCOLESTR pszObjName
    );
    
} IOleInPlaceUIWindowVtbl;

interface IOleInPlaceUIWindow
{
    IOleInPlaceUIWindowVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IOleInPlaceUIWindow__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IOleInPlaceFrame
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IOleInPlaceFrame__
#define __IOleInPlaceFrame__

/* Forward declaration */
typedef interface IOleInPlaceFrame IOleInPlaceFrame;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPOLEINPLACEFRAME_DEFINED

#define _LPOLEINPLACEFRAME_DEFINED

typedef IOleInPlaceFrame *LPOLEINPLACEFRAME;


#endif

#ifndef _tagOIFI_DEFINED

typedef struct tagOIFI
  {
  UINT cb;
  BOOL fMDIApp;
  HWND hwndFrame;
  HACCEL haccel;
  UINT cAccelEntries;
  }
OLEINPLACEFRAMEINFO;


typedef struct tagOIFI *LPOLEINPLACEFRAMEINFO;


#define _tagOIFI_DEFINED

#define _OLEINPLACEFRAMEINFO_DEFINED

#define _LPOLEINPLACEFRAMEINFO_DEFINED

#endif

#ifndef _tagOleMenuGroupWidths_DEFINED

typedef struct tagOleMenuGroupWidths
  {
  LONG width[6];
  }
OLEMENUGROUPWIDTHS;


typedef struct tagOleMenuGroupWidths *LPOLEMENUGROUPWIDTHS;


#define _tagOleMenuGroupWidths_DEFINED

#define _OLEMENUGROUPWIDTHS__DEFINED

#define _LPOLEMENUGROUPWIDTHS__DEFINED

#endif

#ifndef _HOLEMENU_DEFINED

#define _HOLEMENU_DEFINED

typedef HGLOBAL HOLEMENU;


#endif

EXTERN_C const IID IID_IOleInPlaceFrame;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IOleInPlaceFrame : public IOleInPlaceUIWindow
{
public:
    virtual HRESULT STDMETHODCALLTYPE InsertMenus
    (
	HMENU hmenuShared,
	LPOLEMENUGROUPWIDTHS lpMenuWidths
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetMenu
    (
	HMENU hmenuShared,
	HOLEMENU holemenu,
	HWND hwndActiveObject
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE RemoveMenus
    (
	HMENU hmenuShared
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetStatusText
    (
	LPCOLESTR pszStatusText
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE EnableModeless
    (
	BOOL fEnable
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE TranslateAccelerator
    (
	LPMSG lpmsg,
	WORD wID
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IOleInPlaceFrameVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IOleInPlaceFrame FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IOleInPlaceFrame FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IOleInPlaceFrame FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetWindow)
    (
        IOleInPlaceFrame FAR * This,
	HWND *phwnd
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *ContextSensitiveHelp)
    (
        IOleInPlaceFrame FAR * This,
	BOOL fEnterMode
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetBorder)
    (
        IOleInPlaceFrame FAR * This,
	LPRECT lprectBorder
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *RequestBorderSpace)
    (
        IOleInPlaceFrame FAR * This,
	LPCBORDERWIDTHS pborderwidths
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetBorderSpace)
    (
        IOleInPlaceFrame FAR * This,
	LPCBORDERWIDTHS pborderwidths
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetActiveObject)
    (
        IOleInPlaceFrame FAR * This,
	IOleInPlaceActiveObject *pActiveObject,
	LPCOLESTR pszObjName
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *InsertMenus)
    (
        IOleInPlaceFrame FAR * This,
	HMENU hmenuShared,
	LPOLEMENUGROUPWIDTHS lpMenuWidths
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetMenu)
    (
        IOleInPlaceFrame FAR * This,
	HMENU hmenuShared,
	HOLEMENU holemenu,
	HWND hwndActiveObject
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *RemoveMenus)
    (
        IOleInPlaceFrame FAR * This,
	HMENU hmenuShared
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetStatusText)
    (
        IOleInPlaceFrame FAR * This,
	LPCOLESTR pszStatusText
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *EnableModeless)
    (
        IOleInPlaceFrame FAR * This,
	BOOL fEnable
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *TranslateAccelerator)
    (
        IOleInPlaceFrame FAR * This,
	LPMSG lpmsg,
	WORD wID
    );
    
} IOleInPlaceFrameVtbl;

interface IOleInPlaceFrame
{
    IOleInPlaceFrameVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IOleInPlaceFrame__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IOleInPlaceSite
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IOleInPlaceSite__
#define __IOleInPlaceSite__

/* Forward declaration */
typedef interface IOleInPlaceSite IOleInPlaceSite;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPOLEINPLACESITE_DEFINED

#define _LPOLEINPLACESITE_DEFINED

typedef IOleInPlaceSite *LPOLEINPLACESITE;


#endif

EXTERN_C const IID IID_IOleInPlaceSite;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IOleInPlaceSite : public IOleWindow
{
public:
    virtual HRESULT STDMETHODCALLTYPE CanInPlaceActivate
    (
        void
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE OnInPlaceActivate
    (
        void
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE OnUIActivate
    (
        void
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetWindowContext
    (
	IOleInPlaceFrame **ppFrame,
	IOleInPlaceUIWindow **ppDoc,
	LPRECT lprcPosRect,
	LPRECT lprcClipRect,
	LPOLEINPLACEFRAMEINFO lpFrameInfo
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Scroll
    (
	SIZE scrollExtant
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE OnUIDeactivate
    (
	BOOL fUndoable
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE OnInPlaceDeactivate
    (
        void
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE DiscardUndoState
    (
        void
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE DeactivateAndUndo
    (
        void
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE OnPosRectChange
    (
	LPCRECT lprcPosRect
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IOleInPlaceSiteVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IOleInPlaceSite FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IOleInPlaceSite FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IOleInPlaceSite FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetWindow)
    (
        IOleInPlaceSite FAR * This,
	HWND *phwnd
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *ContextSensitiveHelp)
    (
        IOleInPlaceSite FAR * This,
	BOOL fEnterMode
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *CanInPlaceActivate)
    (
        IOleInPlaceSite FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *OnInPlaceActivate)
    (
        IOleInPlaceSite FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *OnUIActivate)
    (
        IOleInPlaceSite FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetWindowContext)
    (
        IOleInPlaceSite FAR * This,
	IOleInPlaceFrame **ppFrame,
	IOleInPlaceUIWindow **ppDoc,
	LPRECT lprcPosRect,
	LPRECT lprcClipRect,
	LPOLEINPLACEFRAMEINFO lpFrameInfo
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Scroll)
    (
        IOleInPlaceSite FAR * This,
	SIZE scrollExtant
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *OnUIDeactivate)
    (
        IOleInPlaceSite FAR * This,
	BOOL fUndoable
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *OnInPlaceDeactivate)
    (
        IOleInPlaceSite FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *DiscardUndoState)
    (
        IOleInPlaceSite FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *DeactivateAndUndo)
    (
        IOleInPlaceSite FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *OnPosRectChange)
    (
        IOleInPlaceSite FAR * This,
	LPCRECT lprcPosRect
    );
    
} IOleInPlaceSiteVtbl;

interface IOleInPlaceSite
{
    IOleInPlaceSiteVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IOleInPlaceSite__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IOleAdviseHolder
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IOleAdviseHolder__
#define __IOleAdviseHolder__

/* Forward declaration */
typedef interface IOleAdviseHolder IOleAdviseHolder;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPOLEADVISEHOLDER_DEFINED

#define _LPOLEADVISEHOLDER_DEFINED

typedef IOleAdviseHolder *LPOLEADVISEHOLDER;


#endif

EXTERN_C const IID IID_IOleAdviseHolder;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IOleAdviseHolder : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Advise
    (
	IAdviseSink *pAdvise,
	DWORD *pdwConnection
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Unadvise
    (
	DWORD dwConnection
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE EnumAdvise
    (
	IEnumSTATDATA **ppenumAdvise
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SendOnRename
    (
	IMoniker *pmk
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SendOnSave
    (
        void
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SendOnClose
    (
        void
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IOleAdviseHolderVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IOleAdviseHolder FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IOleAdviseHolder FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IOleAdviseHolder FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Advise)
    (
        IOleAdviseHolder FAR * This,
	IAdviseSink *pAdvise,
	DWORD *pdwConnection
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Unadvise)
    (
        IOleAdviseHolder FAR * This,
	DWORD dwConnection
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *EnumAdvise)
    (
        IOleAdviseHolder FAR * This,
	IEnumSTATDATA **ppenumAdvise
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SendOnRename)
    (
        IOleAdviseHolder FAR * This,
	IMoniker *pmk
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SendOnSave)
    (
        IOleAdviseHolder FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SendOnClose)
    (
        IOleAdviseHolder FAR * This
    );
    
} IOleAdviseHolderVtbl;

interface IOleAdviseHolder
{
    IOleAdviseHolderVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IOleAdviseHolder__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IOleLink
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IOleLink__
#define __IOleLink__

/* Forward declaration */
typedef interface IOleLink IOleLink;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPOLELINK_DEFINED

#define _LPOLELINK_DEFINED

typedef IOleLink *LPOLELINK;


#endif

#ifndef _tagOLEUPDATE_DEFINED

typedef enum tagOLEUPDATE
  {
	OLEUPDATE_ALWAYS = 1,
	OLEUPDATE_ONCALL = 3
  }
OLEUPDATE;


typedef OLEUPDATE *LPOLEUPDATE;


#define _tagOLEUPDATE_DEFINED

#define _OLEUPDATE_DEFINED

#endif

#ifndef _POLEUPDATE_DEFINED

#define _POLEUPDATE_DEFINED

typedef OLEUPDATE *POLEUPDATE;


#endif

#ifndef _tagOLELINKBIND_DEFINED

typedef enum tagOLELINKBIND
  {
	OLELINKBIND_EVENIFCLASSDIFF = 1
  }
OLELINKBIND;


#define _tagOLELINKBIND_DEFINED

#define _OLELINKBIND_DEFINED

#endif

EXTERN_C const IID IID_IOleLink;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IOleLink : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE SetUpdateOptions
    (
	DWORD dwUpdateOpt
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetUpdateOptions
    (
	DWORD *pdwUpdateOpt
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetSourceMoniker
    (
	IMoniker *pmk,
	REFCLSID rclsid
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetSourceMoniker
    (
	IMoniker **ppmk
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetSourceDisplayName
    (
	LPCOLESTR pszStatusText
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetSourceDisplayName
    (
	LPOLESTR *ppszDisplayName
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE BindToSource
    (
	DWORD bindflags,
	IBindCtx *pbc
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE BindIfRunning
    (
        void
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetBoundSource
    (
	IUnknown **ppunk
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE UnbindSource
    (
        void
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Update
    (
	IBindCtx *pbc
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IOleLinkVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IOleLink FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IOleLink FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IOleLink FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetUpdateOptions)
    (
        IOleLink FAR * This,
	DWORD dwUpdateOpt
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetUpdateOptions)
    (
        IOleLink FAR * This,
	DWORD *pdwUpdateOpt
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetSourceMoniker)
    (
        IOleLink FAR * This,
	IMoniker *pmk,
	REFCLSID rclsid
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetSourceMoniker)
    (
        IOleLink FAR * This,
	IMoniker **ppmk
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetSourceDisplayName)
    (
        IOleLink FAR * This,
	LPCOLESTR pszStatusText
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetSourceDisplayName)
    (
        IOleLink FAR * This,
	LPOLESTR *ppszDisplayName
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *BindToSource)
    (
        IOleLink FAR * This,
	DWORD bindflags,
	IBindCtx *pbc
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *BindIfRunning)
    (
        IOleLink FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetBoundSource)
    (
        IOleLink FAR * This,
	IUnknown **ppunk
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *UnbindSource)
    (
        IOleLink FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Update)
    (
        IOleLink FAR * This,
	IBindCtx *pbc
    );
    
} IOleLinkVtbl;

interface IOleLink
{
    IOleLinkVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IOleLink__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IParseDisplayName
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IParseDisplayName__
#define __IParseDisplayName__

/* Forward declaration */
typedef interface IParseDisplayName IParseDisplayName;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPPARSEDISPLAYNAME_DEFINED

#define _LPPARSEDISPLAYNAME_DEFINED

typedef IParseDisplayName *LPPARSEDISPLAYNAME;


#endif

EXTERN_C const IID IID_IParseDisplayName;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IParseDisplayName : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE ParseDisplayName
    (
	IBindCtx *pbc,
	LPOLESTR pszDisplayName,
	ULONG *pchEaten,
	IMoniker **ppmkOut
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IParseDisplayNameVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IParseDisplayName FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IParseDisplayName FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IParseDisplayName FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *ParseDisplayName)
    (
        IParseDisplayName FAR * This,
	IBindCtx *pbc,
	LPOLESTR pszDisplayName,
	ULONG *pchEaten,
	IMoniker **ppmkOut
    );
    
} IParseDisplayNameVtbl;

interface IParseDisplayName
{
    IParseDisplayNameVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IParseDisplayName__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IDropTarget
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IDropTarget__
#define __IDropTarget__

/* Forward declaration */
typedef interface IDropTarget IDropTarget;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPDROPTARGET_DEFINED

#define _LPDROPTARGET_DEFINED

typedef IDropTarget *LPDROPTARGET;


#endif

#define MK_ALT	0x20


#define DROPEFFECT_NONE	0


#define DROPEFFECT_COPY	1


#define DROPEFFECT_MOVE	2


#define DROPEFFECT_LINK	4


#define DROPEFFECT_SCROLL	0x80000000L


#define DD_DEFSCROLLINSET	11


#define DD_DEFSCROLLDELAY	50


EXTERN_C const IID IID_IDropTarget;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IDropTarget : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE DragEnter
    (
	IDataObject *pDataObj,
	DWORD grfKeyState,
	POINTL pt,
	DWORD *pdwEffect
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE DragOver
    (
	DWORD grfKeyState,
	POINTL pt,
	DWORD *pdwEffect
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE DragLeave
    (
        void
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Drop
    (
	IDataObject *pDataObj,
	DWORD grfKeyState,
	POINTL pt,
	DWORD *pdwEffect
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IDropTargetVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IDropTarget FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IDropTarget FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IDropTarget FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *DragEnter)
    (
        IDropTarget FAR * This,
	IDataObject *pDataObj,
	DWORD grfKeyState,
	POINTL pt,
	DWORD *pdwEffect
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *DragOver)
    (
        IDropTarget FAR * This,
	DWORD grfKeyState,
	POINTL pt,
	DWORD *pdwEffect
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *DragLeave)
    (
        IDropTarget FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Drop)
    (
        IDropTarget FAR * This,
	IDataObject *pDataObj,
	DWORD grfKeyState,
	POINTL pt,
	DWORD *pdwEffect
    );
    
} IDropTargetVtbl;

interface IDropTarget
{
    IDropTargetVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IDropTarget__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IOleCache
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IOleCache__
#define __IOleCache__

/* Forward declaration */
typedef interface IOleCache IOleCache;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPOLECACHE_DEFINED

#define _LPOLECACHE_DEFINED

typedef IOleCache *LPOLECACHE;


#endif

EXTERN_C const IID IID_IOleCache;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IOleCache : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Cache
    (
	FORMATETC *pformatetc,
	DWORD advf,
	DWORD *pdwConnection
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Uncache
    (
	DWORD dwConnection
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE EnumCache
    (
	IEnumSTATDATA **ppenumSTATDATA
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE InitCache
    (
	IDataObject *pDataObject
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetData
    (
	FORMATETC *pformatetc,
	STGMEDIUM *pmedium,
	BOOL fRelease
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IOleCacheVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IOleCache FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IOleCache FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IOleCache FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Cache)
    (
        IOleCache FAR * This,
	FORMATETC *pformatetc,
	DWORD advf,
	DWORD *pdwConnection
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Uncache)
    (
        IOleCache FAR * This,
	DWORD dwConnection
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *EnumCache)
    (
        IOleCache FAR * This,
	IEnumSTATDATA **ppenumSTATDATA
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *InitCache)
    (
        IOleCache FAR * This,
	IDataObject *pDataObject
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetData)
    (
        IOleCache FAR * This,
	FORMATETC *pformatetc,
	STGMEDIUM *pmedium,
	BOOL fRelease
    );
    
} IOleCacheVtbl;

interface IOleCache
{
    IOleCacheVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IOleCache__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IOleCache2
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IOleCache2__
#define __IOleCache2__

/* Forward declaration */
typedef interface IOleCache2 IOleCache2;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPOLECACHE2_DEFINED

#define _LPOLECACHE2_DEFINED

typedef IOleCache2 *LPOLECACHE2;


#endif

#define UPDFCACHE_NODATACACHE	0x1


#define UPDFCACHE_ONSAVECACHE	0x2


#define UPDFCACHE_ONSTOPCACHE	0x4


#define UPDFCACHE_NORMALCACHE	0x8


#define UPDFCACHE_IFBLANK	0x10


#define UPDFCACHE_ONLYIFBLANK	0x80000000L


#define UPDFCACHE_IFBLANKORONSAVECACHE	(UPDFCACHE_IFBLANK | UPDFCACHE_ONSAVECACHE)


#define UPDFCACHE_ALL	 ~ UPDFCACHE_ONLYIFBLANK


#define UPDFCACHE_ALLBUTNODATACACHE	(UPDFCACHE_ALL &  ~ UPDFCACHE_NODATACACHE)


typedef enum tagDISCARDCACHE
  {
	DISCARDCACHE_SAVEIFDIRTY = 0,
	DISCARDCACHE_NOSAVE = 1
  }
DISCARDCACHE;
void __RPC_API DISCARDCACHE_to_xmit (DISCARDCACHE __RPC_FAR *, long __RPC_FAR * __RPC_FAR *);
void __RPC_API DISCARDCACHE_from_xmit (long __RPC_FAR *, DISCARDCACHE __RPC_FAR *);
void __RPC_API DISCARDCACHE_free_inst (DISCARDCACHE __RPC_FAR *);
void __RPC_API DISCARDCACHE_free_xmit (long __RPC_FAR *);


#define DISCARDCACHE_to_xmit(pEnum, ppLong) *(ppLong) = (long *) (pEnum)

#define DISCARDCACHE_from_xmit(pLong, pEnum) *(pEnum) = (DISCARDCACHE) *(pLong)

#define DISCARDCACHE_free_inst(pEnum) 

#define DISCARDCACHE_free_xmit(pLong) 

EXTERN_C const IID IID_IOleCache2;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IOleCache2 : public IOleCache
{
public:
    virtual HRESULT STDMETHODCALLTYPE UpdateCache
    (
	LPDATAOBJECT pDataObject,
	DWORD grfUpdf,
	LPVOID pReserved
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE DiscardCache
    (
	DWORD dwDiscardOptions
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IOleCache2Vtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IOleCache2 FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IOleCache2 FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IOleCache2 FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Cache)
    (
        IOleCache2 FAR * This,
	FORMATETC *pformatetc,
	DWORD advf,
	DWORD *pdwConnection
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Uncache)
    (
        IOleCache2 FAR * This,
	DWORD dwConnection
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *EnumCache)
    (
        IOleCache2 FAR * This,
	IEnumSTATDATA **ppenumSTATDATA
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *InitCache)
    (
        IOleCache2 FAR * This,
	IDataObject *pDataObject
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetData)
    (
        IOleCache2 FAR * This,
	FORMATETC *pformatetc,
	STGMEDIUM *pmedium,
	BOOL fRelease
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *UpdateCache)
    (
        IOleCache2 FAR * This,
	LPDATAOBJECT pDataObject,
	DWORD grfUpdf,
	LPVOID pReserved
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *DiscardCache)
    (
        IOleCache2 FAR * This,
	DWORD dwDiscardOptions
    );
    
} IOleCache2Vtbl;

interface IOleCache2
{
    IOleCache2Vtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IOleCache2__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IOleCacheControl
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IOleCacheControl__
#define __IOleCacheControl__

/* Forward declaration */
typedef interface IOleCacheControl IOleCacheControl;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPOLECACHECONTROL_DEFINED

#define _LPOLECACHECONTROL_DEFINED

typedef IOleCacheControl *LPOLECACHECONTROL;


#endif

EXTERN_C const IID IID_IOleCacheControl;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IOleCacheControl : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE OnRun
    (
	LPDATAOBJECT pDataObject
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE OnStop
    (
        void
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IOleCacheControlVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IOleCacheControl FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IOleCacheControl FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IOleCacheControl FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *OnRun)
    (
        IOleCacheControl FAR * This,
	LPDATAOBJECT pDataObject
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *OnStop)
    (
        IOleCacheControl FAR * This
    );
    
} IOleCacheControlVtbl;

interface IOleCacheControl
{
    IOleCacheControlVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IOleCacheControl__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IViewObject
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IViewObject__
#define __IViewObject__

/* Forward declaration */
typedef interface IViewObject IViewObject;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPVIEWOBJECT_DEFINED

#define _LPVIEWOBJECT_DEFINED

typedef IViewObject *LPVIEWOBJECT;


#endif

EXTERN_C const IID IID_IViewObject;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IViewObject : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Draw
    (
	DWORD dwDrawAspect,
	LONG lindex,
	void *pvAspect,
	DVTARGETDEVICE *ptd,
	HDC hdcTargetDev,
	HDC hdcDraw,
	LPCRECTL lprcBounds,
	LPCRECTL lprcWBounds,
	        BOOL (__stdcall *pfnContinue)(
	DWORD dwContinue),
	DWORD dwContinue
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetColorSet
    (
	DWORD dwDrawAspect,
	LONG lindex,
	void *pvAspect,
	DVTARGETDEVICE *ptd,
	HDC hicTargetDev,
	LOGPALETTE **ppColorSet
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Freeze
    (
	DWORD dwDrawAspect,
	LONG lindex,
	void *pvAspect,
	DWORD *pdwFreeze
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Unfreeze
    (
	DWORD dwFreeze
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetAdvise
    (
	DWORD aspects,
	DWORD advf,
	IAdviseSink *pAdvSink
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetAdvise
    (
	DWORD *pAspects,
	DWORD *pAdvf,
	IAdviseSink **ppAdvSink
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IViewObjectVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IViewObject FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IViewObject FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IViewObject FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Draw)
    (
        IViewObject FAR * This,
	DWORD dwDrawAspect,
	LONG lindex,
	void *pvAspect,
	DVTARGETDEVICE *ptd,
	HDC hdcTargetDev,
	HDC hdcDraw,
	LPCRECTL lprcBounds,
	LPCRECTL lprcWBounds,
	        BOOL (__stdcall *pfnContinue)(
	DWORD dwContinue),
	DWORD dwContinue
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetColorSet)
    (
        IViewObject FAR * This,
	DWORD dwDrawAspect,
	LONG lindex,
	void *pvAspect,
	DVTARGETDEVICE *ptd,
	HDC hicTargetDev,
	LOGPALETTE **ppColorSet
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Freeze)
    (
        IViewObject FAR * This,
	DWORD dwDrawAspect,
	LONG lindex,
	void *pvAspect,
	DWORD *pdwFreeze
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Unfreeze)
    (
        IViewObject FAR * This,
	DWORD dwFreeze
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetAdvise)
    (
        IViewObject FAR * This,
	DWORD aspects,
	DWORD advf,
	IAdviseSink *pAdvSink
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetAdvise)
    (
        IViewObject FAR * This,
	DWORD *pAspects,
	DWORD *pAdvf,
	IAdviseSink **ppAdvSink
    );
    
} IViewObjectVtbl;

interface IViewObject
{
    IViewObjectVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IViewObject__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IViewObject2
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IViewObject2__
#define __IViewObject2__

/* Forward declaration */
typedef interface IViewObject2 IViewObject2;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPVIEWOBJECT2_DEFINED

#define _LPVIEWOBJECT2_DEFINED

typedef IViewObject2 *LPVIEWOBJECT2;


#endif

EXTERN_C const IID IID_IViewObject2;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IViewObject2 : public IViewObject
{
public:
    virtual HRESULT STDMETHODCALLTYPE GetExtent
    (
	DWORD dwDrawAspect,
	LONG lindex,
	DVTARGETDEVICE *ptd,
	LPSIZEL lpsizel
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IViewObject2Vtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IViewObject2 FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IViewObject2 FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IViewObject2 FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Draw)
    (
        IViewObject2 FAR * This,
	DWORD dwDrawAspect,
	LONG lindex,
	void *pvAspect,
	DVTARGETDEVICE *ptd,
	HDC hdcTargetDev,
	HDC hdcDraw,
	LPCRECTL lprcBounds,
	LPCRECTL lprcWBounds,
	        BOOL (__stdcall *pfnContinue)(
	DWORD dwContinue),
	DWORD dwContinue
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetColorSet)
    (
        IViewObject2 FAR * This,
	DWORD dwDrawAspect,
	LONG lindex,
	void *pvAspect,
	DVTARGETDEVICE *ptd,
	HDC hicTargetDev,
	LOGPALETTE **ppColorSet
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Freeze)
    (
        IViewObject2 FAR * This,
	DWORD dwDrawAspect,
	LONG lindex,
	void *pvAspect,
	DWORD *pdwFreeze
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Unfreeze)
    (
        IViewObject2 FAR * This,
	DWORD dwFreeze
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetAdvise)
    (
        IViewObject2 FAR * This,
	DWORD aspects,
	DWORD advf,
	IAdviseSink *pAdvSink
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetAdvise)
    (
        IViewObject2 FAR * This,
	DWORD *pAspects,
	DWORD *pAdvf,
	IAdviseSink **ppAdvSink
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetExtent)
    (
        IViewObject2 FAR * This,
	DWORD dwDrawAspect,
	LONG lindex,
	DVTARGETDEVICE *ptd,
	LPSIZEL lpsizel
    );
    
} IViewObject2Vtbl;

interface IViewObject2
{
    IViewObject2Vtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IViewObject2__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IOleContainer
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IOleContainer__
#define __IOleContainer__

/* Forward declaration */
typedef interface IOleContainer IOleContainer;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPOLECONTAINER_DEFINED

#define _LPOLECONTAINER_DEFINED

typedef IOleContainer *LPOLECONTAINER;


#endif

EXTERN_C const IID IID_IOleContainer;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IOleContainer : public IParseDisplayName
{
public:
    virtual HRESULT STDMETHODCALLTYPE EnumObjects
    (
	DWORD grfFlags,
	IEnumUnknown **ppenum
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE LockContainer
    (
	BOOL fLock
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IOleContainerVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IOleContainer FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IOleContainer FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IOleContainer FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *ParseDisplayName)
    (
        IOleContainer FAR * This,
	IBindCtx *pbc,
	LPOLESTR pszDisplayName,
	ULONG *pchEaten,
	IMoniker **ppmkOut
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *EnumObjects)
    (
        IOleContainer FAR * This,
	DWORD grfFlags,
	IEnumUnknown **ppenum
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *LockContainer)
    (
        IOleContainer FAR * This,
	BOOL fLock
    );
    
} IOleContainerVtbl;

interface IOleContainer
{
    IOleContainerVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IOleContainer__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IOleItemContainer
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IOleItemContainer__
#define __IOleItemContainer__

/* Forward declaration */
typedef interface IOleItemContainer IOleItemContainer;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPOLEITEMCONTAINER_DEFINED

#define _LPOLEITEMCONTAINER_DEFINED

typedef IOleItemContainer *LPOLEITEMCONTAINER;


#endif

#ifndef _tagBINDSPEED_DEFINED

typedef enum tagBINDSPEED
  {
	BINDSPEED_INDEFINITE = 1,
	BINDSPEED_MODERATE = 2,
	BINDSPEED_IMMEDIATE = 3
  }
BINDSPEED;


#define _tagBINDSPEED_DEFINED

#define _BINDSPEED_DEFINED

#endif

#ifndef _tagOLECONTF_DEFINED

typedef enum tagOLECONTF
  {
	OLECONTF_EMBEDDINGS = 1,
	OLECONTF_LINKS = 2,
	OLECONTF_OTHERS = 4,
	OLECONTF_ONLYUSER = 8,
	OLECONTF_ONLYIFRUNNING = 16
  }
OLECONTF;


#define _tagOLECONTF_DEFINED

#define _OLECONTF_DEFINED

#endif

EXTERN_C const IID IID_IOleItemContainer;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IOleItemContainer : public IOleContainer
{
public:
    virtual HRESULT STDMETHODCALLTYPE GetObject
    (
	LPOLESTR pszItem,
	DWORD dwSpeedNeeded,
	IBindCtx *pbc,
	REFIID riid,
	void **ppvObject
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetObjectStorage
    (
	LPOLESTR pszItem,
	IBindCtx *pbc,
	REFIID riid,
	void **ppvStorage
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE IsRunning
    (
	LPOLESTR pszItem
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IOleItemContainerVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IOleItemContainer FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IOleItemContainer FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IOleItemContainer FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *ParseDisplayName)
    (
        IOleItemContainer FAR * This,
	IBindCtx *pbc,
	LPOLESTR pszDisplayName,
	ULONG *pchEaten,
	IMoniker **ppmkOut
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *EnumObjects)
    (
        IOleItemContainer FAR * This,
	DWORD grfFlags,
	IEnumUnknown **ppenum
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *LockContainer)
    (
        IOleItemContainer FAR * This,
	BOOL fLock
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetObject)
    (
        IOleItemContainer FAR * This,
	LPOLESTR pszItem,
	DWORD dwSpeedNeeded,
	IBindCtx *pbc,
	REFIID riid,
	void **ppvObject
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetObjectStorage)
    (
        IOleItemContainer FAR * This,
	LPOLESTR pszItem,
	IBindCtx *pbc,
	REFIID riid,
	void **ppvStorage
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *IsRunning)
    (
        IOleItemContainer FAR * This,
	LPOLESTR pszItem
    );
    
} IOleItemContainerVtbl;

interface IOleItemContainer
{
    IOleItemContainerVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IOleItemContainer__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IOleClientSite
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IOleClientSite__
#define __IOleClientSite__

/* Forward declaration */
typedef interface IOleClientSite IOleClientSite;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPOLECLIENTSITE_DEFINED

#define _LPOLECLIENTSITE_DEFINED

typedef IOleClientSite *LPOLECLIENTSITE;


#endif

EXTERN_C const IID IID_IOleClientSite;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IOleClientSite : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE SaveObject
    (
        void
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetMoniker
    (
	DWORD dwAssign,
	DWORD dwWhichMoniker,
	IMoniker **ppmk
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetContainer
    (
	IOleContainer **ppContainer
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE ShowObject
    (
        void
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE OnShowWindow
    (
	BOOL fShow
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE RequestNewObjectLayout
    (
        void
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IOleClientSiteVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IOleClientSite FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IOleClientSite FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IOleClientSite FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SaveObject)
    (
        IOleClientSite FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetMoniker)
    (
        IOleClientSite FAR * This,
	DWORD dwAssign,
	DWORD dwWhichMoniker,
	IMoniker **ppmk
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetContainer)
    (
        IOleClientSite FAR * This,
	IOleContainer **ppContainer
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *ShowObject)
    (
        IOleClientSite FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *OnShowWindow)
    (
        IOleClientSite FAR * This,
	BOOL fShow
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *RequestNewObjectLayout)
    (
        IOleClientSite FAR * This
    );
    
} IOleClientSiteVtbl;

interface IOleClientSite
{
    IOleClientSiteVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IOleClientSite__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IOleObject
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IOleObject__
#define __IOleObject__

/* Forward declaration */
typedef interface IOleObject IOleObject;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPOLEOBJECT_DEFINED

#define _LPOLEOBJECT_DEFINED

typedef IOleObject *LPOLEOBJECT;


#endif

#ifndef _tagOLEGETMONIKER_DEFINED

typedef enum tagOLEGETMONIKER
  {
	OLEGETMONIKER_ONLYIFTHERE = 1,
	OLEGETMONIKER_FORCEASSIGN = 2,
	OLEGETMONIKER_UNASSIGN = 3,
	OLEGETMONIKER_TEMPFORUSER = 4
  }
OLEGETMONIKER;


#define _tagOLEGETMONIKER_DEFINED

#define _OLEGETMONIKER_DEFINED

#endif

#ifndef _tagOLEWHICHMK_DEFINED

typedef enum tagOLEWHICHMK
  {
	OLEWHICHMK_CONTAINER = 1,
	OLEWHICHMK_OBJREL = 2,
	OLEWHICHMK_OBJFULL = 3
  }
OLEWHICHMK;


#define _tagOLEWHICHMK_DEFINED

#define _OLEWHICHMK_DEFINED

#endif

#ifndef _tagUSERCLASSTYPE_DEFINED

typedef enum tagUSERCLASSTYPE
  {
	USERCLASSTYPE_FULL = 1,
	USERCLASSTYPE_SHORT = 2,
	USERCLASSTYPE_APPNAME = 3
  }
USERCLASSTYPE;


#define _tagUSERCLASSTYPE_DEFINED

#define _USERCLASSTYPE_DEFINED

#endif

#ifndef _tagOLEMISC_DEFINED

typedef enum tagOLEMISC
  {
	OLEMISC_RECOMPOSEONRESIZE = 1,
	OLEMISC_ONLYICONIC = 2,
	OLEMISC_INSERTNOTREPLACE = 4,
	OLEMISC_STATIC = 8,
	OLEMISC_CANTLINKINSIDE = 16,
	OLEMISC_CANLINKBYOLE1 = 32,
	OLEMISC_ISLINKOBJECT = 64,
	OLEMISC_INSIDEOUT = 128,
	OLEMISC_ACTIVATEWHENVISIBLE = 256,
	OLEMISC_RENDERINGISDEVICEINDEPENDENT = 512
  }
OLEMISC;


#define _tagOLEMISC_DEFINED

#define _OLEMISC_DEFINED

#endif

#ifndef _tagOLECLOSE_DEFINED

typedef enum tagOLECLOSE
  {
	OLECLOSE_SAVEIFDIRTY = 0,
	OLECLOSE_NOSAVE = 1,
	OLECLOSE_PROMPTSAVE = 2
  }
OLECLOSE;


#define _tagOLECLOSE_DEFINED

#define _OLECLOSE_DEFINED

#endif

EXTERN_C const IID IID_IOleObject;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IOleObject : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE SetClientSite
    (
	IOleClientSite *pClientSite
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetClientSite
    (
	IOleClientSite **ppClientSite
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetHostNames
    (
	LPCOLESTR szContainerApp,
	LPCOLESTR szContainerObj
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Close
    (
	DWORD dwSaveOption
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetMoniker
    (
	DWORD dwWhichMoniker,
	IMoniker *pmk
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetMoniker
    (
	DWORD dwAssign,
	DWORD dwWhichMoniker,
	IMoniker **ppmk
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE InitFromData
    (
	IDataObject *pDataObject,
	BOOL fCreation,
	DWORD dwReserved
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetClipboardData
    (
	DWORD dwReserved,
	IDataObject **ppDataObject
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE DoVerb
    (
	LONG iVerb,
	LPMSG lpmsg,
	IOleClientSite *pActiveSite,
	LONG lindex,
	HWND hwndParent,
	LPCRECT lprcPosRect
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE EnumVerbs
    (
	IEnumOLEVERB **ppEnumOleVerb
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Update
    (
        void
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE IsUpToDate
    (
        void
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetUserClassID
    (
	CLSID *pClsid
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetUserType
    (
	DWORD dwFormOfType,
	LPOLESTR *pszUserType
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetExtent
    (
	DWORD dwDrawAspect,
	SIZEL *psizel
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetExtent
    (
	DWORD dwDrawAspect,
	SIZEL *psizel
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Advise
    (
	IAdviseSink *pAdvSink,
	DWORD *pdwConnection
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Unadvise
    (
	DWORD dwConnection
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE EnumAdvise
    (
	IEnumSTATDATA **ppenumAdvise
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetMiscStatus
    (
	DWORD dwAspect,
	DWORD *pdwStatus
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetColorScheme
    (
	LOGPALETTE *pLogpal
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IOleObjectVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IOleObject FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IOleObject FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IOleObject FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetClientSite)
    (
        IOleObject FAR * This,
	IOleClientSite *pClientSite
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetClientSite)
    (
        IOleObject FAR * This,
	IOleClientSite **ppClientSite
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetHostNames)
    (
        IOleObject FAR * This,
	LPCOLESTR szContainerApp,
	LPCOLESTR szContainerObj
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Close)
    (
        IOleObject FAR * This,
	DWORD dwSaveOption
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetMoniker)
    (
        IOleObject FAR * This,
	DWORD dwWhichMoniker,
	IMoniker *pmk
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetMoniker)
    (
        IOleObject FAR * This,
	DWORD dwAssign,
	DWORD dwWhichMoniker,
	IMoniker **ppmk
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *InitFromData)
    (
        IOleObject FAR * This,
	IDataObject *pDataObject,
	BOOL fCreation,
	DWORD dwReserved
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetClipboardData)
    (
        IOleObject FAR * This,
	DWORD dwReserved,
	IDataObject **ppDataObject
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *DoVerb)
    (
        IOleObject FAR * This,
	LONG iVerb,
	LPMSG lpmsg,
	IOleClientSite *pActiveSite,
	LONG lindex,
	HWND hwndParent,
	LPCRECT lprcPosRect
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *EnumVerbs)
    (
        IOleObject FAR * This,
	IEnumOLEVERB **ppEnumOleVerb
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Update)
    (
        IOleObject FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *IsUpToDate)
    (
        IOleObject FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetUserClassID)
    (
        IOleObject FAR * This,
	CLSID *pClsid
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetUserType)
    (
        IOleObject FAR * This,
	DWORD dwFormOfType,
	LPOLESTR *pszUserType
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetExtent)
    (
        IOleObject FAR * This,
	DWORD dwDrawAspect,
	SIZEL *psizel
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetExtent)
    (
        IOleObject FAR * This,
	DWORD dwDrawAspect,
	SIZEL *psizel
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Advise)
    (
        IOleObject FAR * This,
	IAdviseSink *pAdvSink,
	DWORD *pdwConnection
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Unadvise)
    (
        IOleObject FAR * This,
	DWORD dwConnection
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *EnumAdvise)
    (
        IOleObject FAR * This,
	IEnumSTATDATA **ppenumAdvise
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetMiscStatus)
    (
        IOleObject FAR * This,
	DWORD dwAspect,
	DWORD *pdwStatus
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetColorScheme)
    (
        IOleObject FAR * This,
	LOGPALETTE *pLogpal
    );
    
} IOleObjectVtbl;

interface IOleObject
{
    IOleObjectVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IOleObject__*/




/****** DV APIs ***********************************************************/


STDAPI CreateDataAdviseHolder(LPDATAADVISEHOLDER FAR* ppDAHolder);







/****** OLE API Prototypes ************************************************/

STDAPI_(DWORD) OleBuildVersion( VOID );

/* helper functions */
STDAPI ReadClassStg(LPSTORAGE pStg, CLSID FAR* pclsid);
STDAPI WriteClassStg(LPSTORAGE pStg, REFCLSID rclsid);
STDAPI ReadClassStm(LPSTREAM pStm, CLSID FAR* pclsid);
STDAPI WriteClassStm(LPSTREAM pStm, REFCLSID rclsid);
STDAPI WriteFmtUserTypeStg (LPSTORAGE pstg, CLIPFORMAT cf, LPOLESTR lpszUserType);
STDAPI ReadFmtUserTypeStg (LPSTORAGE pstg, CLIPFORMAT FAR* pcf, LPOLESTR FAR* lplpszUserType);


/* init/term */

STDAPI OleInitialize(LPMALLOC pMalloc);
STDAPI OleInitializeEx(LPMALLOC pMalloc, DWORD);
STDAPI_(void) OleUninitialize(void);


/* APIs to query whether (Embedded/Linked) object can be created from
   the data object */

STDAPI  OleQueryLinkFromData(LPDATAOBJECT pSrcDataObject);
STDAPI  OleQueryCreateFromData(LPDATAOBJECT pSrcDataObject);


/* Object creation APIs */

STDAPI  OleCreate(REFCLSID rclsid, REFIID riid, DWORD renderopt,
                LPFORMATETC pFormatEtc, LPOLECLIENTSITE pClientSite,
                LPSTORAGE pStg, LPVOID FAR* ppvObj);

STDAPI  OleCreateFromData(LPDATAOBJECT pSrcDataObj, REFIID riid,
                DWORD renderopt, LPFORMATETC pFormatEtc,
                LPOLECLIENTSITE pClientSite, LPSTORAGE pStg,
                LPVOID FAR* ppvObj);

STDAPI  OleCreateLinkFromData(LPDATAOBJECT pSrcDataObj, REFIID riid,
                DWORD renderopt, LPFORMATETC pFormatEtc,
                LPOLECLIENTSITE pClientSite, LPSTORAGE pStg,
                LPVOID FAR* ppvObj);

STDAPI  OleCreateStaticFromData(LPDATAOBJECT pSrcDataObj, REFIID iid,
                DWORD renderopt, LPFORMATETC pFormatEtc,
                LPOLECLIENTSITE pClientSite, LPSTORAGE pStg,
                LPVOID FAR* ppvObj);


STDAPI  OleCreateLink(LPMONIKER pmkLinkSrc, REFIID riid,
            DWORD renderopt, LPFORMATETC lpFormatEtc,
            LPOLECLIENTSITE pClientSite, LPSTORAGE pStg, LPVOID FAR* ppvObj);

STDAPI  OleCreateLinkToFile(LPCOLESTR lpszFileName, REFIID riid,
            DWORD renderopt, LPFORMATETC lpFormatEtc,
            LPOLECLIENTSITE pClientSite, LPSTORAGE pStg, LPVOID FAR* ppvObj);

STDAPI  OleCreateFromFile(REFCLSID rclsid, LPCOLESTR lpszFileName, REFIID riid,
            DWORD renderopt, LPFORMATETC lpFormatEtc,
            LPOLECLIENTSITE pClientSite, LPSTORAGE pStg, LPVOID FAR* ppvObj);

STDAPI  OleLoad(LPSTORAGE pStg, REFIID riid, LPOLECLIENTSITE pClientSite,
            LPVOID FAR* ppvObj);

STDAPI  OleSave(LPPERSISTSTORAGE pPS, LPSTORAGE pStg, BOOL fSameAsLoad);

STDAPI  OleLoadFromStream( LPSTREAM pStm, REFIID iidInterface, LPVOID FAR* ppvObj);
STDAPI  OleSaveToStream( LPPERSISTSTREAM pPStm, LPSTREAM pStm );


STDAPI  OleSetContainedObject(LPUNKNOWN pUnknown, BOOL fContained);
STDAPI  OleNoteObjectVisible(LPUNKNOWN pUnknown, BOOL fVisible);


/* Drag/Drop APIs */

STDAPI  RegisterDragDrop(HWND hwnd, LPDROPTARGET pDropTarget);
STDAPI  RevokeDragDrop(HWND hwnd);
STDAPI  DoDragDrop(LPDATAOBJECT pDataObj, LPDROPSOURCE pDropSource,
            DWORD dwOKEffects, LPDWORD pdwEffect);

/* Clipboard APIs */

STDAPI  OleSetClipboard(LPDATAOBJECT pDataObj);
STDAPI  OleGetClipboard(LPDATAOBJECT FAR* ppDataObj);
STDAPI  OleFlushClipboard(void);
STDAPI  OleIsCurrentClipboard(LPDATAOBJECT pDataObj);


/* InPlace Editing APIs */

STDAPI_(HOLEMENU)   OleCreateMenuDescriptor (HMENU hmenuCombined,
                                LPOLEMENUGROUPWIDTHS lpMenuWidths);
STDAPI              OleSetMenuDescriptor (HOLEMENU holemenu, HWND hwndFrame,
                                HWND hwndActiveObject,
                                LPOLEINPLACEFRAME lpFrame,
                                LPOLEINPLACEACTIVEOBJECT lpActiveObj);
STDAPI              OleDestroyMenuDescriptor (HOLEMENU holemenu);

STDAPI              OleTranslateAccelerator (LPOLEINPLACEFRAME lpFrame,
                            LPOLEINPLACEFRAMEINFO lpFrameInfo, LPMSG lpmsg);


/* Helper APIs */
STDAPI_(HANDLE) OleDuplicateData (HANDLE hSrc, CLIPFORMAT cfFormat,
                        UINT uiFlags);

STDAPI          OleDraw (LPUNKNOWN pUnknown, DWORD dwAspect, HDC hdcDraw,
                    LPCRECT lprcBounds);

STDAPI          OleRun(LPUNKNOWN pUnknown);
STDAPI_(BOOL)   OleIsRunning(LPOLEOBJECT pObject);
STDAPI          OleLockRunning(LPUNKNOWN pUnknown, BOOL fLock, BOOL fLastUnlockCloses);
STDAPI_(void)   ReleaseStgMedium(LPSTGMEDIUM);
STDAPI          CreateOleAdviseHolder(LPOLEADVISEHOLDER FAR* ppOAHolder);

STDAPI          OleCreateDefaultHandler(REFCLSID clsid, LPUNKNOWN pUnkOuter,
                    REFIID riid, LPVOID FAR* lplpObj);

STDAPI          OleCreateEmbeddingHelper(REFCLSID clsid, LPUNKNOWN pUnkOuter,
                    DWORD flags, LPCLASSFACTORY pCF,
                                        REFIID riid, LPVOID FAR* lplpObj);

STDAPI_(BOOL)   IsAccelerator(HACCEL hAccel, int cAccelEntries, LPMSG lpMsg,
                                        WORD FAR* lpwCmd);
/* Icon extraction Helper APIs */

STDAPI_(HGLOBAL) OleGetIconOfFile(LPOLESTR lpszPath, BOOL fUseFileAsLabel);

STDAPI_(HGLOBAL) OleGetIconOfClass(REFCLSID rclsid,     LPOLESTR lpszLabel,
                                        BOOL fUseTypeAsLabel);

STDAPI_(HGLOBAL) OleMetafilePictFromIconAndLabel(HICON hIcon, LPOLESTR lpszLabel,
                                        LPOLESTR lpszSourceFile, UINT iIconIndex);



/* Registration Database Helper APIs */

STDAPI                  OleRegGetUserType (REFCLSID clsid, DWORD dwFormOfType,
                                        LPOLESTR FAR* pszUserType);

STDAPI                  OleRegGetMiscStatus     (REFCLSID clsid, DWORD dwAspect,
                                        DWORD FAR* pdwStatus);

STDAPI                  OleRegEnumFormatEtc     (REFCLSID clsid, DWORD dwDirection,
                                        LPENUMFORMATETC FAR* ppenum);

STDAPI                  OleRegEnumVerbs (REFCLSID clsid, LPENUMOLEVERB FAR* ppenum);





/* OLE 1.0 conversion APIS */

/***** OLE 1.0 OLESTREAM declarations *************************************/

typedef struct _OLESTREAM FAR*  LPOLESTREAM;

typedef struct _OLESTREAMVTBL
{
    DWORD (CALLBACK* Get)(LPOLESTREAM, void FAR*, DWORD);
    DWORD (CALLBACK* Put)(LPOLESTREAM, const void FAR*, DWORD);
} OLESTREAMVTBL;
typedef  OLESTREAMVTBL FAR*  LPOLESTREAMVTBL;

typedef struct _OLESTREAM
{
    LPOLESTREAMVTBL lpstbl;
} OLESTREAM;


STDAPI OleConvertOLESTREAMToIStorage
    (LPOLESTREAM                lpolestream,
    LPSTORAGE                   pstg,
    const DVTARGETDEVICE FAR*   ptd);

STDAPI OleConvertIStorageToOLESTREAM
    (LPSTORAGE      pstg,
    LPOLESTREAM     lpolestream);


/* Storage Utility APIs */
STDAPI GetHGlobalFromILockBytes (LPLOCKBYTES plkbyt, HGLOBAL FAR* phglobal);
STDAPI CreateILockBytesOnHGlobal (HGLOBAL hGlobal, BOOL fDeleteOnRelease,
                                    LPLOCKBYTES FAR* pplkbyt);

STDAPI GetHGlobalFromStream (LPSTREAM pstm, HGLOBAL FAR* phglobal);
STDAPI CreateStreamOnHGlobal (HGLOBAL hGlobal, BOOL fDeleteOnRelease,
                                LPSTREAM FAR* ppstm);


/* ConvertTo APIS */

STDAPI OleDoAutoConvert(LPSTORAGE pStg, LPCLSID pClsidNew);
STDAPI OleGetAutoConvert(REFCLSID clsidOld, LPCLSID pClsidNew);
STDAPI OleSetAutoConvert(REFCLSID clsidOld, REFCLSID clsidNew);
STDAPI GetConvertStg(LPSTORAGE pStg);
STDAPI SetConvertStg(LPSTORAGE pStg, BOOL fConvert);


STDAPI OleConvertIStorageToOLESTREAMEx
    (LPSTORAGE          pstg,
                                    // Presentation data to OLESTREAM
     CLIPFORMAT         cfFormat,   //      format
     LONG               lWidth,     //      width
     LONG               lHeight,    //      height
     DWORD              dwSize,     //      size in bytes
     LPSTGMEDIUM        pmedium,    //      bits
     LPOLESTREAM        polestm);

STDAPI OleConvertOLESTREAMToIStorageEx
    (LPOLESTREAM        polestm,
     LPSTORAGE          pstg,
                                    // Presentation data from OLESTREAM
     CLIPFORMAT FAR*    pcfFormat,  //      format
     LONG FAR*          plwWidth,   //      width
     LONG FAR*          plHeight,   //      height
     DWORD FAR*         pdwSize,    //      size in bytes
     LPSTGMEDIUM        pmedium);   //      bits
#ifndef RC_INVOKED
#pragma pack()
#endif // RC_INVOKED
#endif     // __ole2_H__
