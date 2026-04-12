//+---------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//  File:       objbase.h
//
//  Contents:   Component object model defintions.
//
//  History:    02-7-94   terryru           Created.
//
//----------------------------------------------------------------------------
#if !defined( _OBJBASE_H_ )
#define _OBJBASE_H_

// Set packing to 8 for ISV, and Chicago support (terryru)
#ifndef RC_INVOKED
#pragma pack(8)
#endif //RC_INVOKED

// Component Object Model defines, and macros

#ifdef __cplusplus
    #define EXTERN_C    extern "C"
#else
    #define EXTERN_C    extern
#endif

#ifdef WIN32

// Win32 doesn't support __export

#define STDMETHODCALLTYPE       __stdcall
#define STDMETHODVCALLTYPE      __cdecl

#define STDAPICALLTYPE          __stdcall
#define STDAPIVCALLTYPE         __cdecl

#else

#define STDMETHODCALLTYPE       __export __stdcall
#define STDMETHODVCALLTYPE      __export __cdecl

#define STDAPICALLTYPE          __export __stdcall
#define STDAPIVCALLTYPE         __export __cdecl

#endif

#define STDAPI                  EXTERN_C HRESULT STDAPICALLTYPE
#define STDAPI_(type)           EXTERN_C type STDAPICALLTYPE

#define STDMETHODIMP            HRESULT STDMETHODCALLTYPE
#define STDMETHODIMP_(type)     type STDMETHODCALLTYPE

// The 'V' versions allow Variable Argument lists.

#define STDAPIV                 EXTERN_C HRESULT STDAPIVCALLTYPE
#define STDAPIV_(type)          EXTERN_C type STDAPIVCALLTYPE

#define STDMETHODIMPV           HRESULT STDMETHODVCALLTYPE
#define STDMETHODIMPV_(type)    type STDMETHODVCALLTYPE




/****** Interface Declaration ***********************************************/

/*
 *      These are macros for declaring interfaces.  They exist so that
 *      a single definition of the interface is simulataneously a proper
 *      declaration of the interface structures (C++ abstract classes)
 *      for both C and C++.
 *
 *      DECLARE_INTERFACE(iface) is used to declare an interface that does
 *      not derive from a base interface.
 *      DECLARE_INTERFACE_(iface, baseiface) is used to declare an interface
 *      that does derive from a base interface.
 *
 *      By default if the source file has a .c extension the C version of
 *      the interface declaratations will be expanded; if it has a .cpp
 *      extension the C++ version will be expanded. if you want to force
 *      the C version expansion even though the source file has a .cpp
 *      extension, then define the macro "CINTERFACE".
 *      eg.     cl -DCINTERFACE file.cpp
 *
 *      Example Interface declaration:
 *
 *          #undef  INTERFACE
 *          #define INTERFACE   IClassFactory
 *
 *          DECLARE_INTERFACE_(IClassFactory, IUnknown)
 *          {
 *              // *** IUnknown methods ***
 *              STDMETHOD(QueryInterface) (THIS_
 *                                        REFIID riid,
 *                                        LPVOID FAR* ppvObj) PURE;
 *              STDMETHOD_(ULONG,AddRef) (THIS) PURE;
 *              STDMETHOD_(ULONG,Release) (THIS) PURE;
 *
 *              // *** IClassFactory methods ***
 *              STDMETHOD(CreateInstance) (THIS_
 *                                        LPUNKNOWN pUnkOuter,
 *                                        REFIID riid,
 *                                        LPVOID FAR* ppvObject) PURE;
 *          };
 *
 *      Example C++ expansion:
 *
 *          struct FAR IClassFactory : public IUnknown
 *          {
 *              virtual HRESULT STDMETHODCALLTYPE QueryInterface(
 *                                                  IID FAR& riid,
 *                                                  LPVOID FAR* ppvObj) = 0;
 *              virtual HRESULT STDMETHODCALLTYPE AddRef(void) = 0;
 *              virtual HRESULT STDMETHODCALLTYPE Release(void) = 0;
 *              virtual HRESULT STDMETHODCALLTYPE CreateInstance(
 *                                              LPUNKNOWN pUnkOuter,
 *                                              IID FAR& riid,
 *                                              LPVOID FAR* ppvObject) = 0;
 *          };
 *
 *          NOTE: Our documentation says '#define interface class' but we use
 *          'struct' instead of 'class' to keep a lot of 'public:' lines
 *          out of the interfaces.  The 'FAR' forces the 'this' pointers to
 *          be far, which is what we need.
 *
 *      Example C expansion:
 *
 *          typedef struct IClassFactory
 *          {
 *              const struct IClassFactoryVtbl FAR* lpVtbl;
 *          } IClassFactory;
 *
 *          typedef struct IClassFactoryVtbl IClassFactoryVtbl;
 *
 *          struct IClassFactoryVtbl
 *          {
 *              HRESULT (STDMETHODCALLTYPE * QueryInterface) (
 *                                                  IClassFactory FAR* This,
 *                                                  IID FAR* riid,
 *                                                  LPVOID FAR* ppvObj) ;
 *              HRESULT (STDMETHODCALLTYPE * AddRef) (IClassFactory FAR* This) ;
 *              HRESULT (STDMETHODCALLTYPE * Release) (IClassFactory FAR* This) ;
 *              HRESULT (STDMETHODCALLTYPE * CreateInstance) (
 *                                                  IClassFactory FAR* This,
 *                                                  LPUNKNOWN pUnkOuter,
 *                                                  IID FAR* riid,
 *                                                  LPVOID FAR* ppvObject);
 *              HRESULT (STDMETHODCALLTYPE * LockServer) (
 *                                                  IClassFactory FAR* This,
 *                                                  BOOL fLock);
 *          };
 */


#if defined(__cplusplus) && !defined(CINTERFACE)
//#define interface               struct FAR
#define interface struct
#define STDMETHOD(method)       virtual HRESULT STDMETHODCALLTYPE method
#define STDMETHOD_(type,method) virtual type STDMETHODCALLTYPE method
#define PURE                    = 0
#define THIS_
#define THIS                    void
#define DECLARE_INTERFACE(iface)    interface iface
#define DECLARE_INTERFACE_(iface, baseiface)    interface iface : public baseiface



#else

#define interface               struct

#define STDMETHOD(method)       HRESULT (STDMETHODCALLTYPE * method)
#define STDMETHOD_(type,method) type (STDMETHODCALLTYPE * method)




#define PURE
#define THIS_                   INTERFACE FAR* This,
#define THIS                    INTERFACE FAR* This
#ifdef CONST_VTABLE
#define DECLARE_INTERFACE(iface)    typedef interface iface { \
                                    const struct iface##Vtbl FAR* lpVtbl; \
                                } iface; \
                                typedef const struct iface##Vtbl iface##Vtbl; \
                                const struct iface##Vtbl
#else
#define DECLARE_INTERFACE(iface)    typedef interface iface { \
                                    struct iface##Vtbl FAR* lpVtbl; \
                                } iface; \
                                typedef struct iface##Vtbl iface##Vtbl; \
                                struct iface##Vtbl
#endif
#define DECLARE_INTERFACE_(iface, baseiface)    DECLARE_INTERFACE(iface)

#endif

// macros to define byte pattern for a GUID.
//      Example: DEFINE_GUID(GUID_XXX, a, b, c, ...);
//
// Each dll/exe must initialize the GUIDs once.  This is done in one of
// two ways.  If you are not using precompiled headers for the file(s) which
// initializes the GUIDs, define INITGUID before including objbase.h.  This
// is how OLE builds the initialized versions of the GUIDs which are included
// in ole2.lib.  The GUIDs in ole2.lib are all defined in the same text
// segment GUID_TEXT.
//
// The alternative (which some versions of the compiler don't handle properly;
// they wind up with the initialized GUIDs in a data, not a text segment),
// is to use a precompiled version of objbase.h and then include initguid.h
// after objbase.h followed by one or more of the guid defintion files.


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif

#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif

#ifndef GUID_DEFINED
#define GUID_DEFINED
typedef struct _GUID
{
    unsigned long Data1;
    unsigned short Data2;
    unsigned short Data3;
    unsigned char Data4[8];
} GUID;
#endif /* GUID_DEFINED */

#ifndef INITGUID
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    EXTERN_C const GUID CDECL FAR name
#else

#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
        EXTERN_C const GUID CDECL name \
                = { l, w1, w2, { b1, b2,  b3,  b4,  b5,  b6,  b7,  b8 } }
#endif // INITGUID

#define DEFINE_OLEGUID(name, l, w1, w2) \
    DEFINE_GUID(name, l, w1, w2, 0xC0,0,0,0,0,0,0,0x46)

#define IsEqualIID(riid1, riid2) IsEqualGUID(riid1, riid2)
#define IsEqualCLSID(rclsid1, rclsid2) IsEqualGUID(rclsid1, rclsid2)

#ifdef __cplusplus

// because GUID is defined elsewhere in WIN32 land, the operator == and !=
// are moved outside the class to global scope.

inline BOOL operator==(const GUID& guidOne, const GUID& guidOther)
{
#ifdef WIN32
    return !memcmp(&guidOne,&guidOther,sizeof(GUID));
#else
    return !_fmemcmp(&guidOne,&guidOther,sizeof(GUID)); }
#endif
}

inline BOOL operator!=(const GUID& guidOne, const GUID& guidOther)
{
    return !(guidOne == guidOther);
}

#endif // __cpluscplus




/****** OLE character defines ******************************************/

#if defined(WIN32) && !defined(OLE2ANSI)

#define OLESTR(str)     L##str

#else  //Win32

#define OLESTR(str)     str

#endif //!WIN32



/****** Additional basic types **********************************************/


#ifndef FARSTRUCT
#ifdef __cplusplus
#define FARSTRUCT   FAR
#else
#define FARSTRUCT
#endif  // __cplusplus
#endif  // FARSTRUCT



#ifndef HUGEP
#ifdef WIN32
#define HUGEP
#else
#define HUGEP __huge
#endif // WIN32
#endif // HUGEP


#include <stdlib.h>

#define LISet32(li, v) ((li).HighPart = (v) < 0 ? -1 : 0, (li).LowPart = (v))


#if !defined(_WINNT_) && !defined(_NTDEF_)
typedef struct _ULARGE_INTEGER {
        DWORD LowPart;
        DWORD HighPart;
} ULARGE_INTEGER, *PULARGE_INTEGER;
#endif // _WINNT_

#define ULISet32(li, v) ((li).HighPart = 0, (li).LowPart = (v))






#define CLSCTX_ALL              (CLSCTX_INPROC_SERVER| \
                                 CLSCTX_INPROC_HANDLER| \
                                 CLSCTX_LOCAL_SERVER)

#define CLSCTX_INPROC           (CLSCTX_INPROC_SERVER|CLSCTX_INPROC_HANDLER)

#define CLSCTX_SERVER           (CLSCTX_INPROC_SERVER|CLSCTX_LOCAL_SERVER)


// class registration flags; passed to CoRegisterClassObject
typedef enum tagREGCLS
{
    REGCLS_SINGLEUSE = 0,       // class object only generates one instance
    REGCLS_MULTIPLEUSE = 1,     // same class object genereates multiple inst.
                                // and local automatically goes into inproc tbl.
    REGCLS_MULTI_SEPARATE = 2   // multiple use, but separate control over each
                                // context.
} REGCLS;

// COM initialization flags; passed to CoInitialize.
typedef enum tagCOINIT
{
  COINIT_MULTITHREADED = 0,      // OLE calls objects on any thread.
  COINIT_SINGLETHREADED = 1        // OLE calls objects on single thread.
} COINIT;

// interface marshaling definitions
#define MARSHALINTERFACE_MIN 500 // minimum number of bytes for interface marshl


//
// Common typedefs for paramaters used in Storage API's, gleamed from storage.h
// Also contains Storage error codes, which should be moved into the storage
// idl files.
//


#define CWCSTORAGENAME 32

/* Storage instantiation modes */
#define STGM_DIRECT             0x00000000L
#define STGM_TRANSACTED         0x00010000L

#define STGM_READ               0x00000000L
#define STGM_WRITE              0x00000001L
#define STGM_READWRITE          0x00000002L

#define STGM_SHARE_DENY_NONE    0x00000040L
#define STGM_SHARE_DENY_READ    0x00000030L
#define STGM_SHARE_DENY_WRITE   0x00000020L
#define STGM_SHARE_EXCLUSIVE    0x00000010L

#define STGM_PRIORITY           0x00040000L
#define STGM_DELETEONRELEASE    0x04000000L

#define STGM_CREATE             0x00001000L
#define STGM_CONVERT            0x00020000L
#define STGM_FAILIFTHERE        0x00000000L

//
// TerryRu
// Include rpc.h, and rpcndr.h to define basic midl types, like byte, and boolean.
// Try to rework it so that we don't need to include all of rpc when DCOM is not needed.
//


#include <rpc.h>
#include <rpcndr.h>

#ifndef __WTYPES_H__
#define __WTYPES_H__

#ifdef __cplusplus
extern "C" {
#endif

#define small char




extern RPC_IF_HANDLE IWinTypes_ServerIfHandle;

extern RPC_IF_HANDLE IWinTypes_ClientIfHandle;

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

typedef struct tagRemHGLOBAL
  {
  unsigned long cbData;
  byte data[1];
  }
RemHGLOBAL;

typedef struct tagRemHMETAFILEPICT
  {
  long mm;
  long xExt;
  long yExt;
  unsigned long cbData;
  byte data[1];
  }
RemHMETAFILEPICT;

typedef void *HMETAFILEPICT;
void __RPC_API HMETAFILEPICT_to_xmit (HMETAFILEPICT __RPC_FAR *, RemHMETAFILEPICT __RPC_FAR * __RPC_FAR *);
void __RPC_API HMETAFILEPICT_from_xmit (RemHMETAFILEPICT __RPC_FAR *, HMETAFILEPICT __RPC_FAR *);
void __RPC_API HMETAFILEPICT_free_inst (HMETAFILEPICT __RPC_FAR *);
void __RPC_API HMETAFILEPICT_free_xmit (RemHMETAFILEPICT __RPC_FAR *);

typedef struct tagRemHBITMAP
  {
  unsigned long cbData;
  byte data[1];
  }
RemHBITMAP;

typedef struct tagRemBRUSH
  {
  unsigned long cbData;
  byte data[1];
  }
RemHBRUSH;

#ifndef WINAPI          // If not included with 3.1 headers... 
#ifdef WIN32
#define FAR
#define PASCAL          __stdcall
#define CDECL
#else // !WIN32
#define FAR             _far
#define PASCAL          _pascal
#define CDECL           _cdecl
#endif // !WIN32
#define VOID            void
#define WINAPI      FAR PASCAL
#define CALLBACK    FAR PASCAL
#ifndef FALSE
#define FALSE 0
#define TRUE 1
#endif // !FALSE
#ifndef _BYTE_DEFINED
#define _BYTE_DEFINED
typedef unsigned char BYTE;

#endif // !_BYTE_DEFINED
#ifndef _WORD_DEFINED
#define _WORD_DEFINED
typedef unsigned short WORD;

#endif // !_WORD_DEFINED
typedef unsigned int UINT;
void __RPC_API UINT_to_xmit (UINT __RPC_FAR *, unsigned long __RPC_FAR * __RPC_FAR *);
void __RPC_API UINT_from_xmit (unsigned long __RPC_FAR *, UINT __RPC_FAR *);
void __RPC_API UINT_free_inst (UINT __RPC_FAR *);
void __RPC_API UINT_free_xmit (unsigned long __RPC_FAR *);

typedef int INT;
void __RPC_API INT_to_xmit (INT __RPC_FAR *, long __RPC_FAR * __RPC_FAR *);
void __RPC_API INT_from_xmit (long __RPC_FAR *, INT __RPC_FAR *);
void __RPC_API INT_free_inst (INT __RPC_FAR *);
void __RPC_API INT_free_xmit (long __RPC_FAR *);

typedef long BOOL;

#ifndef _LONG_DEFINED
#define _LONG_DEFINED
typedef long LONG;

#endif // !_LONG_DEFINED
#ifndef _WPARAM_DEFINED
#define _WPARAM_DEFINED
typedef UINT WPARAM;

#endif // _WPARAM_DEFINED
#ifndef _DWORD_DEFINED
#define _DWORD_DEFINED
typedef unsigned long DWORD;

#endif // !_DWORD_DEFINED
#ifndef _LPARAM_DEFINED
#define _LPARAM_DEFINED
typedef LONG LPARAM;

#endif // !_LPARAM_DEFINED
#ifndef _LRESULT_DEFINED
#define _LRESULT_DEFINED
typedef LONG LRESULT;

#endif // !_LRESULT_DEFINED
typedef void *HANDLE;
void __RPC_API HANDLE_to_xmit (HANDLE __RPC_FAR *, long __RPC_FAR * __RPC_FAR *);
void __RPC_API HANDLE_from_xmit (long __RPC_FAR *, HANDLE __RPC_FAR *);
void __RPC_API HANDLE_free_inst (HANDLE __RPC_FAR *);
void __RPC_API HANDLE_free_xmit (long __RPC_FAR *);

typedef void *HMODULE;
void __RPC_API HMODULE_to_xmit (HMODULE __RPC_FAR *, long __RPC_FAR * __RPC_FAR *);
void __RPC_API HMODULE_from_xmit (long __RPC_FAR *, HMODULE __RPC_FAR *);
void __RPC_API HMODULE_free_inst (HMODULE __RPC_FAR *);
void __RPC_API HMODULE_free_xmit (long __RPC_FAR *);

typedef void *HINSTANCE;
void __RPC_API HINSTANCE_to_xmit (HINSTANCE __RPC_FAR *, long __RPC_FAR * __RPC_FAR *);
void __RPC_API HINSTANCE_from_xmit (long __RPC_FAR *, HINSTANCE __RPC_FAR *);
void __RPC_API HINSTANCE_free_inst (HINSTANCE __RPC_FAR *);
void __RPC_API HINSTANCE_free_xmit (long __RPC_FAR *);

typedef void *HICON;
void __RPC_API HICON_to_xmit (HICON __RPC_FAR *, long __RPC_FAR * __RPC_FAR *);
void __RPC_API HICON_from_xmit (long __RPC_FAR *, HICON __RPC_FAR *);
void __RPC_API HICON_free_inst (HICON __RPC_FAR *);
void __RPC_API HICON_free_xmit (long __RPC_FAR *);

typedef void *HFONT;
void __RPC_API HFONT_to_xmit (HFONT __RPC_FAR *, long __RPC_FAR * __RPC_FAR *);
void __RPC_API HFONT_from_xmit (long __RPC_FAR *, HFONT __RPC_FAR *);
void __RPC_API HFONT_free_inst (HFONT __RPC_FAR *);
void __RPC_API HFONT_free_xmit (long __RPC_FAR *);

typedef void *HGLOBAL;
void __RPC_API HGLOBAL_to_xmit (HGLOBAL __RPC_FAR *, RemHGLOBAL __RPC_FAR * __RPC_FAR *);
void __RPC_API HGLOBAL_from_xmit (RemHGLOBAL __RPC_FAR *, HGLOBAL __RPC_FAR *);
void __RPC_API HGLOBAL_free_inst (HGLOBAL __RPC_FAR *);
void __RPC_API HGLOBAL_free_xmit (RemHGLOBAL __RPC_FAR *);

typedef HGLOBAL HLOCAL;

typedef void *HBITMAP;
void __RPC_API HBITMAP_to_xmit (HBITMAP __RPC_FAR *, RemHBITMAP __RPC_FAR * __RPC_FAR *);
void __RPC_API HBITMAP_from_xmit (RemHBITMAP __RPC_FAR *, HBITMAP __RPC_FAR *);
void __RPC_API HBITMAP_free_inst (HBITMAP __RPC_FAR *);
void __RPC_API HBITMAP_free_xmit (RemHBITMAP __RPC_FAR *);

typedef void *HBRUSH;
void __RPC_API HBRUSH_to_xmit (HBRUSH __RPC_FAR *, RemHBRUSH __RPC_FAR * __RPC_FAR *);
void __RPC_API HBRUSH_from_xmit (RemHBRUSH __RPC_FAR *, HBRUSH __RPC_FAR *);
void __RPC_API HBRUSH_free_inst (HBRUSH __RPC_FAR *);
void __RPC_API HBRUSH_free_xmit (RemHBRUSH __RPC_FAR *);

typedef void *HDC;
void __RPC_API HDC_to_xmit (HDC __RPC_FAR *, long __RPC_FAR * __RPC_FAR *);
void __RPC_API HDC_from_xmit (long __RPC_FAR *, HDC __RPC_FAR *);
void __RPC_API HDC_free_inst (HDC __RPC_FAR *);
void __RPC_API HDC_free_xmit (long __RPC_FAR *);

typedef void *HRGN;
void __RPC_API HRGN_to_xmit (HRGN __RPC_FAR *, long __RPC_FAR * __RPC_FAR *);
void __RPC_API HRGN_from_xmit (long __RPC_FAR *, HRGN __RPC_FAR *);
void __RPC_API HRGN_free_inst (HRGN __RPC_FAR *);
void __RPC_API HRGN_free_xmit (long __RPC_FAR *);

typedef void *HWND;
void __RPC_API HWND_to_xmit (HWND __RPC_FAR *, long __RPC_FAR * __RPC_FAR *);
void __RPC_API HWND_from_xmit (long __RPC_FAR *, HWND __RPC_FAR *);
void __RPC_API HWND_free_inst (HWND __RPC_FAR *);
void __RPC_API HWND_free_xmit (long __RPC_FAR *);

typedef void *HMENU;
void __RPC_API HMENU_to_xmit (HMENU __RPC_FAR *, long __RPC_FAR * __RPC_FAR *);
void __RPC_API HMENU_from_xmit (long __RPC_FAR *, HMENU __RPC_FAR *);
void __RPC_API HMENU_free_inst (HMENU __RPC_FAR *);
void __RPC_API HMENU_free_xmit (long __RPC_FAR *);

typedef void *HACCEL;
void __RPC_API HACCEL_to_xmit (HACCEL __RPC_FAR *, long __RPC_FAR * __RPC_FAR *);
void __RPC_API HACCEL_from_xmit (long __RPC_FAR *, HACCEL __RPC_FAR *);
void __RPC_API HACCEL_free_inst (HACCEL __RPC_FAR *);
void __RPC_API HACCEL_free_xmit (long __RPC_FAR *);

typedef void *HTASK;
void __RPC_API HTASK_to_xmit (HTASK __RPC_FAR *, long __RPC_FAR * __RPC_FAR *);
void __RPC_API HTASK_from_xmit (long __RPC_FAR *, HTASK __RPC_FAR *);
void __RPC_API HTASK_free_inst (HTASK __RPC_FAR *);
void __RPC_API HTASK_free_xmit (long __RPC_FAR *);

typedef void *HKEY;
void __RPC_API HKEY_to_xmit (HKEY __RPC_FAR *, long __RPC_FAR * __RPC_FAR *);
void __RPC_API HKEY_from_xmit (long __RPC_FAR *, HKEY __RPC_FAR *);
void __RPC_API HKEY_free_inst (HKEY __RPC_FAR *);
void __RPC_API HKEY_free_xmit (long __RPC_FAR *);

typedef void *HDESK;
void __RPC_API HDESK_to_xmit (HDESK __RPC_FAR *, long __RPC_FAR * __RPC_FAR *);
void __RPC_API HDESK_from_xmit (long __RPC_FAR *, HDESK __RPC_FAR *);
void __RPC_API HDESK_free_inst (HDESK __RPC_FAR *);
void __RPC_API HDESK_free_xmit (long __RPC_FAR *);

typedef void *HENHMETAFILE;
void __RPC_API HENHMETAFILE_to_xmit (HENHMETAFILE __RPC_FAR *, long __RPC_FAR * __RPC_FAR *);
void __RPC_API HENHMETAFILE_from_xmit (long __RPC_FAR *, HENHMETAFILE __RPC_FAR *);
void __RPC_API HENHMETAFILE_free_inst (HENHMETAFILE __RPC_FAR *);
void __RPC_API HENHMETAFILE_free_xmit (long __RPC_FAR *);

typedef void *HMF;
void __RPC_API HMF_to_xmit (HMF __RPC_FAR *, long __RPC_FAR * __RPC_FAR *);
void __RPC_API HMF_from_xmit (long __RPC_FAR *, HMF __RPC_FAR *);
void __RPC_API HMF_free_inst (HMF __RPC_FAR *);
void __RPC_API HMF_free_xmit (long __RPC_FAR *);

typedef void *HEMF;
void __RPC_API HEMF_to_xmit (HEMF __RPC_FAR *, long __RPC_FAR * __RPC_FAR *);
void __RPC_API HEMF_from_xmit (long __RPC_FAR *, HEMF __RPC_FAR *);
void __RPC_API HEMF_free_inst (HEMF __RPC_FAR *);
void __RPC_API HEMF_free_xmit (long __RPC_FAR *);

typedef void *HMETAFILE;
void __RPC_API HMETAFILE_to_xmit (HMETAFILE __RPC_FAR *, long __RPC_FAR * __RPC_FAR *);
void __RPC_API HMETAFILE_from_xmit (long __RPC_FAR *, HMETAFILE __RPC_FAR *);
void __RPC_API HMETAFILE_free_inst (HMETAFILE __RPC_FAR *);
void __RPC_API HMETAFILE_free_xmit (long __RPC_FAR *);

typedef void *HPALETTE;
void __RPC_API HPALETTE_to_xmit (HPALETTE __RPC_FAR *, long __RPC_FAR * __RPC_FAR *);
void __RPC_API HPALETTE_from_xmit (long __RPC_FAR *, HPALETTE __RPC_FAR *);
void __RPC_API HPALETTE_free_inst (HPALETTE __RPC_FAR *);
void __RPC_API HPALETTE_free_xmit (long __RPC_FAR *);

typedef void *HPEN;
void __RPC_API HPEN_to_xmit (HPEN __RPC_FAR *, long __RPC_FAR * __RPC_FAR *);
void __RPC_API HPEN_from_xmit (long __RPC_FAR *, HPEN __RPC_FAR *);
void __RPC_API HPEN_free_inst (HPEN __RPC_FAR *);
void __RPC_API HPEN_free_xmit (long __RPC_FAR *);

typedef void *HRSRC;
void __RPC_API HRSRC_to_xmit (HRSRC __RPC_FAR *, long __RPC_FAR * __RPC_FAR *);
void __RPC_API HRSRC_from_xmit (long __RPC_FAR *, HRSRC __RPC_FAR *);
void __RPC_API HRSRC_free_inst (HRSRC __RPC_FAR *);
void __RPC_API HRSRC_free_xmit (long __RPC_FAR *);

typedef void *HSTR;
void __RPC_API HSTR_to_xmit (HSTR __RPC_FAR *, long __RPC_FAR * __RPC_FAR *);
void __RPC_API HSTR_from_xmit (long __RPC_FAR *, HSTR __RPC_FAR *);
void __RPC_API HSTR_free_inst (HSTR __RPC_FAR *);
void __RPC_API HSTR_free_xmit (long __RPC_FAR *);

typedef void *HWINSTA;
void __RPC_API HWINSTA_to_xmit (HWINSTA __RPC_FAR *, long __RPC_FAR * __RPC_FAR *);
void __RPC_API HWINSTA_from_xmit (long __RPC_FAR *, HWINSTA __RPC_FAR *);
void __RPC_API HWINSTA_free_inst (HWINSTA __RPC_FAR *);
void __RPC_API HWINSTA_free_xmit (long __RPC_FAR *);

typedef void *HKL;
void __RPC_API HKL_to_xmit (HKL __RPC_FAR *, long __RPC_FAR * __RPC_FAR *);
void __RPC_API HKL_from_xmit (long __RPC_FAR *, HKL __RPC_FAR *);
void __RPC_API HKL_free_inst (HKL __RPC_FAR *);
void __RPC_API HKL_free_xmit (long __RPC_FAR *);

typedef void *HGDIOBJ;
void __RPC_API HGDIOBJ_to_xmit (HGDIOBJ __RPC_FAR *, long __RPC_FAR * __RPC_FAR *);
void __RPC_API HGDIOBJ_from_xmit (long __RPC_FAR *, HGDIOBJ __RPC_FAR *);
void __RPC_API HGDIOBJ_free_inst (HGDIOBJ __RPC_FAR *);
void __RPC_API HGDIOBJ_free_xmit (long __RPC_FAR *);

typedef HANDLE HDWP;

#ifndef _HFILE_DEFINED
#define _HFILE_DEFINED
typedef INT HFILE;

#endif // !_HFILE_DEFINED
#ifndef _HCURSOR_DEFINED
#define _HCURSOR_DEFINED
typedef HICON HCURSOR;

#endif // !_HCURSOR_DEFINED
#ifndef _LPWORD_DEFINED
#define _LPWORD_DEFINED
typedef WORD *LPWORD;

#endif // !_LPWORD_DEFINED
#ifndef _LPDWORD_DEFINED
#define _LPDWORD_DEFINED
typedef DWORD *LPDWORD;

#endif // !_LPDWORD_DEFINED
typedef char *LPSTR;

typedef const char *LPCSTR;

#ifndef _WCHAR_DEFINED
#define _WCHAR_DEFINED
typedef wchar_t WCHAR;

typedef WCHAR TCHAR;

#endif // !_WCHAR_DEFINED
typedef WCHAR *LPWSTR;

typedef TCHAR *LPTSTR;

typedef const WCHAR *LPCWSTR;

typedef const TCHAR *LPCTSTR;

typedef struct tagPALETTEENTRY
  {
  BYTE peRed;
  BYTE peGreen;
  BYTE peBlue;
  BYTE peFlags;
  }
PALETTEENTRY;

typedef struct tagPALETTEENTRY *PPALETTEENTRY;

typedef struct tagPALETTEENTRY *LPPALETTEENTRY;

typedef struct tagLOGPALETTE
  {
  WORD palVersion;
  WORD palNumEntries;
  PALETTEENTRY palPalEntry[1];
  }
LOGPALETTE;

typedef struct tagLOGPALETTE *PLOGPALETTE;

typedef struct tagLOGPALETTE *LPLOGPALETTE;

#ifndef _COLORREF_DEFINED
#define _COLORREF_DEFINED
typedef DWORD COLORREF;

#endif // !_COLORREF_DEFINED
#ifndef _LPCOLORREF_DEFINED
#define _LPCOLORREF_DEFINED
typedef DWORD *LPCOLORREF;

#endif // !_LPCOLORREF_DEFINED
typedef HANDLE *LPHANDLE;

typedef struct _RECTL
  {
  LONG left;
  LONG top;
  LONG right;
  LONG bottom;
  }
RECTL;

typedef struct _RECTL *PRECTL;

typedef struct _RECTL *LPRECTL;

typedef struct tagPOINT
  {
  LONG x;
  LONG y;
  }
POINT;

typedef struct tagPOINT *PPOINT;

typedef struct tagPOINT *LPPOINT;

typedef struct _POINTL
  {
  LONG x;
  LONG y;
  }
POINTL;

typedef struct _POINTL *PPOINTL;

#ifndef WIN16
typedef struct tagSIZE
  {
  LONG cx;
  LONG cy;
  }
SIZE;

typedef struct tagSIZE *PSIZE;

typedef struct tagSIZE *LPSIZE;

#else // WIN16
typedef struct tagSIZE
{
    INT cx;
    INT cy;
} SIZE, *PSIZE, *LPSIZE;
#endif // WIN16
typedef struct tagMSG
  {
  HWND hwnd;
  UINT message;
  WPARAM wParam;
  LPARAM lParam;
  DWORD time;
  POINT pt;
  }
MSG;

typedef struct tagMSG *PMSG;

typedef struct tagMSG *NPMSG;

typedef struct tagMSG *LPMSG;

typedef struct tagSIZEL
  {
  LONG cx;
  LONG cy;
  }
SIZEL;

typedef struct tagSIZEL *PSIZEL;

typedef struct tagSIZEL *LPSIZEL;

#endif  //WINAPI
#if defined(WIN32) && !defined(OLE2ANSI)
typedef WCHAR OLECHAR;

typedef OLECHAR *LPOLESTR;

typedef const OLECHAR *LPCOLESTR;

#define OLESTR(str) L##str
#else
typedef char      OLECHAR;
typedef LPSTR     LPOLESTR;
typedef LPCSTR    LPCOLESTR;
#define OLESTR(str) str
#endif
typedef const RECTL *LPCRECTL;

#ifndef _WINDEF_
typedef void *PVOID;

typedef void *LPVOID;

typedef struct tagRECT
  {
  LONG left;
  LONG top;
  LONG right;
  LONG bottom;
  }
RECT;

typedef struct tagRECT *PRECT;

typedef struct tagRECT *LPRECT;

#endif  //_WINDEF_
typedef unsigned char UCHAR;

typedef short SHORT;

typedef unsigned short USHORT;

typedef DWORD ULONG;

#if !defined(_WINNT_) && !defined (_NTDEF_)
typedef struct _LARGE_INTEGER
  {
  DWORD LowPart;
  LONG HighPart;
  }
LARGE_INTEGER;

typedef struct _LARGE_INTEGER *PLARGE_INTEGER;

typedef struct _ULARGE_INTEGER
  {
  DWORD LowPart;
  DWORD HighPart;
  }
ULARGE_INTEGER;

typedef struct _ULARGE_INTEGER *PULARGE_INTEGER;

#endif // !defined(_WINNT_) && !defined (_NTDEF_)
#ifndef _WINBASE_
#ifndef _FILETIME_
#define _FILETIME_
typedef struct _FILETIME
  {
  DWORD dwLowDateTime;
  DWORD dwHighDateTime;
  }
FILETIME;

typedef struct _FILETIME *PFILETIME;

typedef struct _FILETIME *LPFILETIME;

#endif // !_FILETIME
#ifndef _SYSTEMTIME_
#define _SYSTEMTIME_
typedef struct _SYSTEMTIME
  {
  WORD wYear;
  WORD wMonth;
  WORD wDayOfWeek;
  WORD wDay;
  WORD wHour;
  WORD wMinute;
  WORD wSecond;
  WORD wMilliseconds;
  }
SYSTEMTIME;

typedef struct _SYSTEMTIME *PSYSTEMTIME;

typedef struct _SYSTEMTIME *LPSYSTEMTIME;

#endif // !_SYSTEMTIME
#ifndef _SECURITY_ATTRIBUTES_
#define _SECURITY_ATTRIBUTES_
typedef struct _SECURITY_ATTRIBUTES
  {
  DWORD nLength;
  LPVOID lpSecurityDescriptor;
  BOOL bInheritHandle;
  }
SECURITY_ATTRIBUTES;

typedef struct _SECURITY_ATTRIBUTES *PSECURITY_ATTRIBUTES;

typedef struct _SECURITY_ATTRIBUTES *LPSECURITY_ATTRIBUTES;

#endif // !_SECURITY_ATTRIBUTES_
#ifndef SECURITY_DESCRIPTOR_REVISION
typedef USHORT SECURITY_DESCRIPTOR_CONTROL;

typedef USHORT *PSECURITY_DESCRIPTOR_CONTROL;

typedef PVOID PSID;

typedef struct _ACL
  {
  UCHAR AclRevision;
  UCHAR Sbz1;
  USHORT AclSize;
  USHORT AceCount;
  USHORT Sbz2;
  }
ACL;

typedef ACL *PACL;

typedef struct _SECURITY_DESCRIPTOR
  {
  UCHAR Revision;
  UCHAR Sbz1;
  SECURITY_DESCRIPTOR_CONTROL Control;
  PSID Owner;
  PSID Group;
  PACL Sacl;
  PACL Dacl;
  }
SECURITY_DESCRIPTOR;

typedef struct _SECURITY_DESCRIPTOR *PISECURITY_DESCRIPTOR;

#endif // !SECURITY_DESCRIPTOR_REVISION
#endif //_WINBASE_
typedef LONG SCODE;

typedef LONG HRESULT;

typedef SCODE *PSCODE;

#ifndef GUID_DEFINED
#define GUID_DEFINED
typedef struct _GUID
  {
  DWORD Data1;
  WORD Data2;
  WORD Data3;
  BYTE Data4[8];
  }
GUID;

#endif // !GUID_DEFINED
#if !defined( __LPGUID_DEFINED__ )
#define __LPGUID_DEFINED__
typedef GUID *LPGUID;

#endif // !__LPGUID_DEFINED__
#ifndef __OBJECTID_DEFINED
#define __OBJECTID_DEFINED
#define _OBJECTID_DEFINED
typedef struct _OBJECTID
  {
  GUID Lineage;
  unsigned long Uniquifier;
  }
OBJECTID;

#endif // !_OBJECTID_DEFINED
#if !defined( __IID_DEFINED__ )
#define __IID_DEFINED__
typedef GUID IID;

typedef IID *LPIID;

#define IID_NULL            GUID_NULL
#define IsEqualIID(riid1, riid2) IsEqualGUID(riid1, riid2)
typedef GUID CLSID;

typedef CLSID *LPCLSID;

#define CLSID_NULL          GUID_NULL
#define IsEqualCLSID(rclsid1, rclsid2) IsEqualGUID(rclsid1, rclsid2)
#if 0
typedef GUID REFGUID;

typedef IID REFIID;

typedef CLSID REFCLSID;

#endif // 0
#if defined(__cplusplus)
#ifndef _REFGUID_DEFINED
#define _REFGUID_DEFINED
#define REFGUID             const GUID &
#endif // !_REFGUID_DEFINED
#ifndef _REFIID_DEFINED
#define _REFIID_DEFINED
#define REFIID              const IID &
#endif // !_REFIID_DEFINED
#ifndef _REFCLSID_DEFINED
#define _REFCLSID_DEFINED
#define REFCLSID            const CLSID &
#endif // !_REFCLSID_DEFINED
#else // !__cplusplus
#ifndef _REFGUID_DEFINED
#define _REFGUID_DEFINED
#define REFGUID             const GUID * const
#endif // !_REFGUID_DEFINED
#ifndef _REFIID_DEFINED
#define _REFIID_DEFINED
#define REFIID              const IID * const
#endif // !_REFIID_DEFINED
#ifndef _REFCLSID_DEFINED
#define _REFCLSID_DEFINED
#define REFCLSID            const CLSID * const
#endif // !_REFCLSID_DEFINED
#endif // !__cplusplus
#endif // !__IID_DEFINED__
typedef void *HCONTEXT;
void __RPC_USER HCONTEXT_rundown (HCONTEXT);

#ifndef _LCID_DEFINED
#define _LCID_DEFINED
typedef DWORD LCID;

#endif // !_LCID_DEFINED
typedef const RECT *LPCRECT;

#define HANDLE_to_xmit(pHandle, ppLong) { *(ppLong) = (long *) MIDL_user_allocate(sizeof(long)); **ppLong = (long ) *(pHandle); }
#define HANDLE_from_xmit(pLong, pHandle) *(pHandle) = (HANDLE) *(pLong)
#define HANDLE_free_inst(pHandle) 
#define HANDLE_free_xmit(pLong) MIDL_user_free(pLong)
#define HMODULE_to_xmit(pHandle, ppLong) { *(ppLong) = (long *) MIDL_user_allocate(sizeof(long)); **ppLong = (long ) *(pHandle); }
#define HMODULE_from_xmit(pLong, pHandle) *(pHandle) = (HMODULE) *(pLong)
#define HMODULE_free_inst(pHandle) 
#define HMODULE_free_xmit(pLong) MIDL_user_free(pLong)
#define HINSTANCE_to_xmit(pHandle, ppLong) { *(ppLong) = (long *) MIDL_user_allocate(sizeof(long)); **ppLong = (long ) *(pHandle); }
#define HINSTANCE_from_xmit(pLong, pHandle) *(pHandle) = (HINSTANCE) *(pLong)
#define HINSTANCE_free_inst(pHandle) 
#define HINSTANCE_free_xmit(pLong) MIDL_user_free(pLong)
#define HICON_to_xmit(pHandle, ppLong) { *(ppLong) = (long *) MIDL_user_allocate(sizeof(long)); **ppLong = (long ) *(pHandle); }
#define HICON_from_xmit(pLong, pHandle) *(pHandle) = (HICON) *(pLong)
#define HICON_free_inst(pHandle) 
#define HICON_free_xmit(pLong) MIDL_user_free(pLong)
void __RPC_API HGLOBAL_to_xmit (HGLOBAL __RPC_FAR *, RemHGLOBAL __RPC_FAR * __RPC_FAR *);
void __RPC_API HGLOBAL_from_xmit (RemHGLOBAL __RPC_FAR *, HGLOBAL __RPC_FAR *);
void __RPC_API HGLOBAL_free_inst (HGLOBAL __RPC_FAR *);
void __RPC_API HGLOBAL_free_xmit (RemHGLOBAL __RPC_FAR *);
void __RPC_API HBITMAP_to_xmit (HBITMAP __RPC_FAR *, RemHBITMAP __RPC_FAR * __RPC_FAR *);
void __RPC_API HBITMAP_from_xmit (RemHBITMAP __RPC_FAR *, HBITMAP __RPC_FAR *);
void __RPC_API HBITMAP_free_inst (HBITMAP __RPC_FAR *);
void __RPC_API HBITMAP_free_xmit (RemHBITMAP __RPC_FAR *);
void __RPC_API HBRUSH_to_xmit (HBRUSH __RPC_FAR *, RemHBRUSH __RPC_FAR * __RPC_FAR *);
void __RPC_API HBRUSH_from_xmit (RemHBRUSH __RPC_FAR *, HBRUSH __RPC_FAR *);
void __RPC_API HBRUSH_free_inst (HBRUSH __RPC_FAR *);
void __RPC_API HBRUSH_free_xmit (RemHBRUSH __RPC_FAR *);
void __RPC_API HMETAFILEPICT_to_xmit (HMETAFILEPICT __RPC_FAR *, RemHMETAFILEPICT __RPC_FAR * __RPC_FAR *);
void __RPC_API HMETAFILEPICT_from_xmit (RemHMETAFILEPICT __RPC_FAR *, HMETAFILEPICT __RPC_FAR *);
void __RPC_API HMETAFILEPICT_free_inst (HMETAFILEPICT __RPC_FAR *);
void __RPC_API HMETAFILEPICT_free_xmit (RemHMETAFILEPICT __RPC_FAR *);
#define HDC_to_xmit(pHandle, ppLong) { *(ppLong) = (long *) MIDL_user_allocate(sizeof(long)); **ppLong = (long ) *(pHandle); }
#define HDC_from_xmit(pLong, pHandle) *(pHandle) = (HDC) *(pLong)
#define HDC_free_inst(pHandle) 
#define HDC_free_xmit(pLong) MIDL_user_free(pLong)
#define HRGN_to_xmit(pHandle, ppLong) { *(ppLong) = (long *) MIDL_user_allocate(sizeof(long)); **ppLong = (long ) *(pHandle); }
#define HRGN_from_xmit(pLong, pHandle) *(pHandle) = (HRGN) *(pLong)
#define HRGN_free_inst(pHandle) 
#define HRGN_free_xmit(pLong) MIDL_user_free(pLong)
#define HWND_to_xmit(pHandle, ppLong) { *(ppLong) = (long *) MIDL_user_allocate(sizeof(long)); **ppLong = (long ) *(pHandle); }
#define HWND_from_xmit(pLong, pHandle) *(pHandle) = (HWND) *(pLong)
#define HWND_free_inst(pHandle) 
#define HWND_free_xmit(pLong) MIDL_user_free(pLong)
#define HMENU_to_xmit(pHandle, ppLong) { *(ppLong) = (long *) MIDL_user_allocate(sizeof(long)); **ppLong = (long ) *(pHandle); }
#define HMENU_from_xmit(pLong, pHandle) *(pHandle) = (HMENU) *(pLong)
#define HMENU_free_inst(pHandle) 
#define HMENU_free_xmit(pLong) MIDL_user_free(pLong)
#define HACCEL_to_xmit(pHandle, ppLong) { *(ppLong) = (long *) MIDL_user_allocate(sizeof(long)); **ppLong = (long ) *(pHandle); }
#define HACCEL_from_xmit(pLong, pHandle) *(pHandle) = (HACCEL) *(pLong)
#define HACCEL_free_inst(pHandle) 
#define HACCEL_free_xmit(pLong) MIDL_user_free(pLong)
#define HTASK_to_xmit(pHandle, ppLong) { *(ppLong) = (long *) MIDL_user_allocate(sizeof(long)); **ppLong = (long ) *(pHandle); }
#define HTASK_from_xmit(pLong, pHandle) *(pHandle) = (HTASK) *(pLong)
#define HTASK_free_inst(pHandle) 
#define HTASK_free_xmit(pLong) MIDL_user_free(pLong)
#define HKEY_to_xmit(pHandle, ppLong) { *(ppLong) = (long *) MIDL_user_allocate(sizeof(long)); **ppLong = (long ) *(pHandle); }
#define HKEY_from_xmit(pLong, pHandle) *(pHandle) = (HKEY) *(pLong)
#define HKEY_free_inst(pHandle) 
#define HKEY_free_xmit(pLong) MIDL_user_free(pLong)
#define UINT_to_xmit(pHandle, ppLong) { *(ppLong) = (unsigned long *) MIDL_user_allocate(sizeof(unsigned long)); **ppLong = (unsigned long ) *(pHandle); }
#define UINT_from_xmit(pLong, pUINT) *(pUINT) = (UINT) *(pLong)
#define UINT_free_inst(pUINT) 
#define UINT_free_xmit(pLong) MIDL_user_free(pLong)
#define INT_to_xmit(pHandle, ppLong) { *(ppLong) = (long *) MIDL_user_allocate(sizeof(long)); **ppLong = (long ) *(pHandle); }
#define INT_from_xmit(pLong, pINT) *(pINT) = (INT) *(pLong)
#define INT_free_inst(pINT) 
#define INT_free_xmit(pLong) MIDL_user_free(pLong)

#if !defined(IMPORT_USED_MULTIPLE) && !defined(IMPORT_USED_SINGLE)

/* routine that sizes node for struct tagRemHGLOBAL */
void _sns_tagRemHGLOBAL (RemHGLOBAL  *, PRPC_MESSAGE);

/* routine that puts node for struct tagRemHGLOBAL */
void _pns_tagRemHGLOBAL (RemHGLOBAL  *, PRPC_MESSAGE);

/* routine that gets node for struct tagRemHGLOBAL */
void _gns_tagRemHGLOBAL (RemHGLOBAL  *, PRPC_MESSAGE, unsigned long);

/* routine that gets graph for struct tagRemHGLOBAL */
void _ggs_tagRemHGLOBAL (RemHGLOBAL  *, unsigned char **, PRPC_MESSAGE);

/* routine that allocates node for struct tagRemHGLOBAL */
void _ans_tagRemHGLOBAL(PRPC_MESSAGE, unsigned long);

/* routine that allocates graph for struct tagRemHGLOBAL */
void _ags_tagRemHGLOBAL(unsigned char **, PRPC_MESSAGE, unsigned long);

/* routine that sizes node for struct tagRemHMETAFILEPICT */
void _sns_tagRemHMETAFILEPICT (RemHMETAFILEPICT  *, PRPC_MESSAGE);

/* routine that puts node for struct tagRemHMETAFILEPICT */
void _pns_tagRemHMETAFILEPICT (RemHMETAFILEPICT  *, PRPC_MESSAGE);

/* routine that gets node for struct tagRemHMETAFILEPICT */
void _gns_tagRemHMETAFILEPICT (RemHMETAFILEPICT  *, PRPC_MESSAGE, unsigned long);

/* routine that gets graph for struct tagRemHMETAFILEPICT */
void _ggs_tagRemHMETAFILEPICT (RemHMETAFILEPICT  *, unsigned char **, PRPC_MESSAGE);

/* routine that allocates node for struct tagRemHMETAFILEPICT */
void _ans_tagRemHMETAFILEPICT(PRPC_MESSAGE, unsigned long);

/* routine that allocates graph for struct tagRemHMETAFILEPICT */
void _ags_tagRemHMETAFILEPICT(unsigned char **, PRPC_MESSAGE, unsigned long);

/* routine that sizes node for struct tagRemHBITMAP */
void _sns_tagRemHBITMAP (RemHBITMAP  *, PRPC_MESSAGE);

/* routine that puts node for struct tagRemHBITMAP */
void _pns_tagRemHBITMAP (RemHBITMAP  *, PRPC_MESSAGE);

/* routine that gets node for struct tagRemHBITMAP */
void _gns_tagRemHBITMAP (RemHBITMAP  *, PRPC_MESSAGE, unsigned long);

/* routine that gets graph for struct tagRemHBITMAP */
void _ggs_tagRemHBITMAP (RemHBITMAP  *, unsigned char **, PRPC_MESSAGE);

/* routine that allocates node for struct tagRemHBITMAP */
void _ans_tagRemHBITMAP(PRPC_MESSAGE, unsigned long);

/* routine that allocates graph for struct tagRemHBITMAP */
void _ags_tagRemHBITMAP(unsigned char **, PRPC_MESSAGE, unsigned long);

/* routine that sizes node for struct tagRemBRUSH */
void _sns_tagRemBRUSH (RemHBRUSH  *, PRPC_MESSAGE);

/* routine that puts node for struct tagRemBRUSH */
void _pns_tagRemBRUSH (RemHBRUSH  *, PRPC_MESSAGE);

/* routine that gets node for struct tagRemBRUSH */
void _gns_tagRemBRUSH (RemHBRUSH  *, PRPC_MESSAGE, unsigned long);

/* routine that gets graph for struct tagRemBRUSH */
void _ggs_tagRemBRUSH (RemHBRUSH  *, unsigned char **, PRPC_MESSAGE);

/* routine that allocates node for struct tagRemBRUSH */
void _ans_tagRemBRUSH(PRPC_MESSAGE, unsigned long);

/* routine that allocates graph for struct tagRemBRUSH */
void _ags_tagRemBRUSH(unsigned char **, PRPC_MESSAGE, unsigned long);

/* routine that sizes node for struct tagLOGPALETTE */
void _sns_tagLOGPALETTE (LOGPALETTE  *, PRPC_MESSAGE);

/* routine that puts node for struct tagLOGPALETTE */
void _pns_tagLOGPALETTE (LOGPALETTE  *, PRPC_MESSAGE);

/* routine that gets node for struct tagLOGPALETTE */
void _gns_tagLOGPALETTE (LOGPALETTE  *, PRPC_MESSAGE, unsigned long);

/* routine that gets graph for struct tagLOGPALETTE */
void _ggs_tagLOGPALETTE (LOGPALETTE  *, unsigned char **, PRPC_MESSAGE);

/* routine that allocates node for struct tagLOGPALETTE */
void _ans_tagLOGPALETTE(PRPC_MESSAGE, unsigned long);

/* routine that allocates graph for struct tagLOGPALETTE */
void _ags_tagLOGPALETTE(unsigned char **, PRPC_MESSAGE, unsigned long);

/* routine that sizes node for struct tagMSG */
void _sns_tagMSG (MSG  *, PRPC_MESSAGE);

/* routine that puts node for struct tagMSG */
void _pns_tagMSG (MSG  *, PRPC_MESSAGE);

/* routine that gets node for struct tagMSG */
void _gns_tagMSG (MSG  *, PRPC_MESSAGE);

/* routine that frees graph for struct tagMSG */
void _fgs_tagMSG (MSG  *);

/* routine that sizes graph for struct _SECURITY_ATTRIBUTES */
void _sgs__SECURITY_ATTRIBUTES (SECURITY_ATTRIBUTES  *, PRPC_MESSAGE);

/* routine that puts graph for struct _SECURITY_ATTRIBUTES */
void _pgs__SECURITY_ATTRIBUTES (SECURITY_ATTRIBUTES  *, PRPC_MESSAGE);

/* routine that gets graph for struct _SECURITY_ATTRIBUTES */
void _ggs__SECURITY_ATTRIBUTES (SECURITY_ATTRIBUTES  *, unsigned char **, PRPC_MESSAGE);

/* routine that allocates graph for struct _SECURITY_ATTRIBUTES */
void _ags__SECURITY_ATTRIBUTES(unsigned char **, PRPC_MESSAGE);

/* routine that frees graph for struct _SECURITY_ATTRIBUTES */
void _fgs__SECURITY_ATTRIBUTES (SECURITY_ATTRIBUTES  *);

/* routine that gets node for struct _GUID */
void _gns__GUID (GUID  *, PRPC_MESSAGE);

/* routine that gets node for struct _OBJECTID */
void _gns__OBJECTID (OBJECTID  *, PRPC_MESSAGE);

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
typedef interface IUnknown IUnknown;
typedef interface IClassFactory IClassFactory;
typedef interface IEnumFORMATETC IEnumFORMATETC;
typedef interface IEnumString IEnumString;
typedef interface IEnumUnknown IEnumUnknown;
typedef interface IPersist IPersist;
typedef interface IRootStorage IRootStorage;
typedef interface IBindCtx IBindCtx;
typedef interface IRunnableObject IRunnableObject;
typedef interface IStream IStream;
typedef interface IMalloc IMalloc;
typedef interface IStdMarshalInfo IStdMarshalInfo;
typedef interface IMessageFilter IMessageFilter;
typedef interface IEnumSTATSTG IEnumSTATSTG;
typedef interface IMarshal IMarshal;
typedef interface IPersistFile IPersistFile;
typedef interface IRpcChannelBuffer IRpcChannelBuffer;
typedef interface ILockBytes ILockBytes;
typedef interface IRpcProxyBuffer IRpcProxyBuffer;
typedef interface IPersistStream IPersistStream;
typedef interface IStorage IStorage;
typedef interface IRpcStubBuffer IRpcStubBuffer;
typedef interface IPSFactoryBuffer IPSFactoryBuffer;
typedef interface IPersistStorage IPersistStorage;
typedef interface IAdviseSink IAdviseSink;
typedef interface IEnumMoniker IEnumMoniker;
typedef interface IEnumSTATDATA IEnumSTATDATA;
typedef interface IMoniker IMoniker;
typedef interface IRunningObjectTable IRunningObjectTable;
typedef interface IAdviseSink2 IAdviseSink2;
typedef interface IDataObject IDataObject;
typedef interface IDataAdviseHolder IDataAdviseHolder;
typedef interface IExternalConnection IExternalConnection;

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IUnknown
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IUnknown__
#define __IUnknown__

/* Forward declaration */
typedef interface IUnknown IUnknown;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


typedef IUnknown *LPUNKNOWN;


#if defined(__cplusplus) && !defined(CINTERFACE)

extern "C" {

#endif

void STDAPICALLTYPE MIDL_GetMarshalSizeMax(RPC_MESSAGE *pMessage, REFIID riid, IUnknown *punk);

void STDAPICALLTYPE MIDL_MarshalInterface(RPC_MESSAGE *pMessage, REFIID riid, void *pv);

void STDAPICALLTYPE MIDL_UnmarshalInterface(RPC_MESSAGE *pMessage, REFIID riid, void **ppv);

#if defined(__cplusplus) && !defined(CINTERFACE)

}

#endif

EXTERN_C const IID IID_IUnknown;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE QueryInterface
    (
	REFIID riid,
	void **ppvObject
    ) = 0;
    
    virtual ULONG STDMETHODCALLTYPE AddRef
    (
        void
    ) = 0;
    
    virtual ULONG STDMETHODCALLTYPE Release
    (
        void
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IUnknownVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IUnknown FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IUnknown FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IUnknown FAR * This
    );
    
} IUnknownVtbl;

interface IUnknown
{
    IUnknownVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IUnknown__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IClassFactory
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IClassFactory__
#define __IClassFactory__

/* Forward declaration */
typedef interface IClassFactory IClassFactory;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPCLASSFACTORY_DEFINED

#define _LPCLASSFACTORY_DEFINED

typedef IClassFactory *LPCLASSFACTORY;


#endif

EXTERN_C const IID IID_IClassFactory;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IClassFactory : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE CreateInstance
    (
	IUnknown *pUnkOuter,
	REFIID riid,
	void **ppvObject
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE LockServer
    (
	BOOL fLock
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IClassFactoryVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IClassFactory FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IClassFactory FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IClassFactory FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *CreateInstance)
    (
        IClassFactory FAR * This,
	IUnknown *pUnkOuter,
	REFIID riid,
	void **ppvObject
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *LockServer)
    (
        IClassFactory FAR * This,
	BOOL fLock
    );
    
} IClassFactoryVtbl;

interface IClassFactory
{
    IClassFactoryVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IClassFactory__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IEnumFORMATETC
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IEnumFORMATETC__
#define __IEnumFORMATETC__

/* Forward declaration */
typedef interface IEnumFORMATETC IEnumFORMATETC;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPENUMFORMATETC_DEFINED

#define _LPENUMFORMATETC_DEFINED

typedef IEnumFORMATETC *LPENUMFORMATETC;


#endif

#ifndef _tagDVTARGETDEVICE_DEFINED

typedef struct tagDVTARGETDEVICE
  {
  DWORD tdSize;
  WORD tdDeviceNameOffset;
  WORD tdDriverNameOffset;
  WORD tdPortNameOffset;
  WORD tdExtDevmodeOffset;
  BYTE tdData[1];
  }
DVTARGETDEVICE;


#define _tagDVTARGETDEVICE_DEFINED

#define _DVTARGETDEVICE_DEFINED

#endif

#ifndef _CLIPFORMAT_DEFINED

#define _CLIPFORMAT_DEFINED

typedef WORD CLIPFORMAT;


typedef CLIPFORMAT *LPCLIPFORMAT;


#endif

#ifndef _tagFORMATETC_DEFINED

typedef struct tagFORMATETC
  {
  CLIPFORMAT cfFormat;
  DVTARGETDEVICE *ptd;
  DWORD dwAspect;
  LONG lindex;
  DWORD tymed;
  }
FORMATETC;


typedef struct tagFORMATETC *LPFORMATETC;


#define _tagFORMATETC_DEFINED

#define _FORMATETC_FORMATETCDEFINED

#endif

EXTERN_C const IID IID_IEnumFORMATETC;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IEnumFORMATETC : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Next
    (
	ULONG celt,
	FORMATETC *rgelt,
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
	IEnumFORMATETC **ppenum
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IEnumFORMATETCVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IEnumFORMATETC FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IEnumFORMATETC FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IEnumFORMATETC FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Next)
    (
        IEnumFORMATETC FAR * This,
	ULONG celt,
	FORMATETC *rgelt,
	ULONG *pceltFetched
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Skip)
    (
        IEnumFORMATETC FAR * This,
	ULONG celt
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Reset)
    (
        IEnumFORMATETC FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Clone)
    (
        IEnumFORMATETC FAR * This,
	IEnumFORMATETC **ppenum
    );
    
} IEnumFORMATETCVtbl;

interface IEnumFORMATETC
{
    IEnumFORMATETCVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IEnumFORMATETC__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IEnumString
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IEnumString__
#define __IEnumString__

/* Forward declaration */
typedef interface IEnumString IEnumString;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


typedef IEnumString *LPENUMSTRING;


EXTERN_C const IID IID_IEnumString;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IEnumString : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Next
    (
	ULONG celt,
	LPOLESTR *rgelt,
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
	IEnumString **ppenum
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IEnumStringVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IEnumString FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IEnumString FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IEnumString FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Next)
    (
        IEnumString FAR * This,
	ULONG celt,
	LPOLESTR *rgelt,
	ULONG *pceltFetched
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Skip)
    (
        IEnumString FAR * This,
	ULONG celt
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Reset)
    (
        IEnumString FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Clone)
    (
        IEnumString FAR * This,
	IEnumString **ppenum
    );
    
} IEnumStringVtbl;

interface IEnumString
{
    IEnumStringVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IEnumString__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IEnumUnknown
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IEnumUnknown__
#define __IEnumUnknown__

/* Forward declaration */
typedef interface IEnumUnknown IEnumUnknown;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPENUMUNKNOWN_DEFINED

#define _LPENUMUNKNOWN_DEFINED

typedef IEnumUnknown *LPENUMUNKNOWN;


#endif

EXTERN_C const IID IID_IEnumUnknown;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IEnumUnknown : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Next
    (
	ULONG celt,
	IUnknown **rgelt,
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
	IEnumUnknown **ppenum
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IEnumUnknownVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IEnumUnknown FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IEnumUnknown FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IEnumUnknown FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Next)
    (
        IEnumUnknown FAR * This,
	ULONG celt,
	IUnknown **rgelt,
	ULONG *pceltFetched
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Skip)
    (
        IEnumUnknown FAR * This,
	ULONG celt
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Reset)
    (
        IEnumUnknown FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Clone)
    (
        IEnumUnknown FAR * This,
	IEnumUnknown **ppenum
    );
    
} IEnumUnknownVtbl;

interface IEnumUnknown
{
    IEnumUnknownVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IEnumUnknown__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IPersist
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IPersist__
#define __IPersist__

/* Forward declaration */
typedef interface IPersist IPersist;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPPERSIST_DEFINED

#define _LPPERSIST_DEFINED

typedef IPersist *LPPERSIST;


#endif

EXTERN_C const IID IID_IPersist;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IPersist : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE GetClassID
    (
	CLSID *pClassID
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IPersistVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IPersist FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IPersist FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IPersist FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetClassID)
    (
        IPersist FAR * This,
	CLSID *pClassID
    );
    
} IPersistVtbl;

interface IPersist
{
    IPersistVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IPersist__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IRootStorage
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IRootStorage__
#define __IRootStorage__

/* Forward declaration */
typedef interface IRootStorage IRootStorage;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPROOTSTORAGE_DEFINED

#define _LPROOTSTORAGE_DEFINED

typedef IRootStorage *LPROOTSTORAGE;


#endif

EXTERN_C const IID IID_IRootStorage;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IRootStorage : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE SwitchToFile
    (
	LPOLESTR pszFile
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IRootStorageVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IRootStorage FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IRootStorage FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IRootStorage FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SwitchToFile)
    (
        IRootStorage FAR * This,
	LPOLESTR pszFile
    );
    
} IRootStorageVtbl;

interface IRootStorage
{
    IRootStorageVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IRootStorage__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IBindCtx
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IBindCtx__
#define __IBindCtx__

/* Forward declaration */
typedef interface IBindCtx IBindCtx;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPBC_DEFINED

#define _LPBC_DEFINED

typedef IBindCtx *LPBC;


#endif

#ifndef _LPBINDCTX_DEFINED

#define _LPBBINDCTX_DEFINED

typedef IBindCtx *LPBINDCTX;


#endif

#ifndef _tagBIND_OPTS_DEFINED

typedef struct tagBIND_OPTS
  {
  DWORD cbStruct;
  DWORD grfFlags;
  DWORD grfMode;
  DWORD dwTickCountDeadline;
  }
BIND_OPTS;


typedef struct tagBIND_OPTS *LPBIND_OPTS;


#define _tagBIND_OPTS_DEFINED

#define _BIND_OPTS_DEFINED

#define _LPBIND_OPTS_DEFINED

#endif

#ifndef _tagBIND_FLAGS_DEFINED

typedef enum tagBIND_FLAGS
  {
	BIND_MAYBOTHERUSER = 1,
	BIND_JUSTTESTEXISTENCE = 2
  }
BIND_FLAGS;


#define _tagBIND_FLAGS_DEFINED

#define _BIND_FLAGS_DEFINED

#endif

EXTERN_C const IID IID_IBindCtx;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IBindCtx : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE RegisterObjectBound
    (
	IUnknown *punk
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE RevokeObjectBound
    (
	IUnknown *punk
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE ReleaseBoundObjects
    (
        void
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetBindOptions
    (
	BIND_OPTS *pbindopts
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetBindOptions
    (
	BIND_OPTS *pbindopts
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetRunningObjectTable
    (
	IRunningObjectTable **pprot
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE RegisterObjectParam
    (
	LPOLESTR pszKey,
	IUnknown *punk
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetObjectParam
    (
	LPOLESTR pszKey,
	IUnknown **ppunk
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE EnumObjectParam
    (
	IEnumString **ppenum
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE RevokeObjectParam
    (
	LPOLESTR pszKey
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IBindCtxVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IBindCtx FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IBindCtx FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IBindCtx FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *RegisterObjectBound)
    (
        IBindCtx FAR * This,
	IUnknown *punk
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *RevokeObjectBound)
    (
        IBindCtx FAR * This,
	IUnknown *punk
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *ReleaseBoundObjects)
    (
        IBindCtx FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetBindOptions)
    (
        IBindCtx FAR * This,
	BIND_OPTS *pbindopts
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetBindOptions)
    (
        IBindCtx FAR * This,
	BIND_OPTS *pbindopts
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetRunningObjectTable)
    (
        IBindCtx FAR * This,
	IRunningObjectTable **pprot
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *RegisterObjectParam)
    (
        IBindCtx FAR * This,
	LPOLESTR pszKey,
	IUnknown *punk
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetObjectParam)
    (
        IBindCtx FAR * This,
	LPOLESTR pszKey,
	IUnknown **ppunk
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *EnumObjectParam)
    (
        IBindCtx FAR * This,
	IEnumString **ppenum
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *RevokeObjectParam)
    (
        IBindCtx FAR * This,
	LPOLESTR pszKey
    );
    
} IBindCtxVtbl;

interface IBindCtx
{
    IBindCtxVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IBindCtx__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IRunnableObject
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IRunnableObject__
#define __IRunnableObject__

/* Forward declaration */
typedef interface IRunnableObject IRunnableObject;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPRUNNABLEOBJECT_DEFINED

#define _LPRUNNABLEOBJECT_DEFINED

typedef IRunnableObject *LPRUNNABLEOBJECT;


#endif

EXTERN_C const IID IID_IRunnableObject;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IRunnableObject : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE GetRunningClass
    (
	LPCLSID lpClsid
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Run
    (
	LPBINDCTX pbc
    ) = 0;
    
    virtual BOOL STDMETHODCALLTYPE IsRunning
    (
        void
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE LockRunning
    (
	BOOL fLock,
	BOOL fLastUnlockCloses
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetContainedObject
    (
	BOOL fContained
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IRunnableObjectVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IRunnableObject FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IRunnableObject FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IRunnableObject FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetRunningClass)
    (
        IRunnableObject FAR * This,
	LPCLSID lpClsid
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Run)
    (
        IRunnableObject FAR * This,
	LPBINDCTX pbc
    );
    
    BOOL (STDMETHODCALLTYPE FAR *IsRunning)
    (
        IRunnableObject FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *LockRunning)
    (
        IRunnableObject FAR * This,
	BOOL fLock,
	BOOL fLastUnlockCloses
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetContainedObject)
    (
        IRunnableObject FAR * This,
	BOOL fContained
    );
    
} IRunnableObjectVtbl;

interface IRunnableObject
{
    IRunnableObjectVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IRunnableObject__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IStream
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IStream__
#define __IStream__

/* Forward declaration */
typedef interface IStream IStream;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPSTREAM_DEFINED

#define _LPSTREAM_DEFINED

typedef IStream *LPSTREAM;


#endif

#ifndef _tagSTATSTG_DEFINED

typedef struct tagSTATSTG
  {
  LPOLESTR pwcsName;
  DWORD type;
  ULARGE_INTEGER cbSize;
  FILETIME mtime;
  FILETIME ctime;
  FILETIME atime;
  DWORD grfMode;
  DWORD grfLocksSupported;
  CLSID clsid;
  DWORD grfStateBits;
  DWORD reserved;
  }
STATSTG;


#define _tagSTATSTG_DEFINED

#define _STATSTG_DEFINED

#endif

#ifndef _tagSTGTY_DEFINED

typedef enum tagSTGTY
  {
	STGTY_STORAGE = 1,
	STGTY_STREAM = 2,
	STGTY_LOCKBYTES = 3,
	STGTY_PROPERTY = 4
  }
STGTY;


#define _tagSTGTY_DEFINED

#define _STGTY_DEFINED

#endif

#ifndef _tagSTREAM_SEEK_DEFINED

typedef enum tagSTREAM_SEEK
  {
	STREAM_SEEK_SET = 0,
	STREAM_SEEK_CUR = 1,
	STREAM_SEEK_END = 2
  }
STREAM_SEEK;


#define _tagSTREAM_SEEK_DEFINED

#define _STREAM_SEEK_DEFINED

#endif

#ifndef _tagLOCKTYPE_DEFINED

typedef enum tagLOCKTYPE
  {
	LOCK_WRITE = 1,
	LOCK_EXCLUSIVE = 2,
	LOCK_ONLYONCE = 4
  }
LOCKTYPE;


#define _tagLOCKTYPE_DEFINED

#define _LOCKTYPE_DEFINED

#endif

EXTERN_C const IID IID_IStream;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IStream : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Read
    (
	void *pv,
	ULONG cb,
	ULONG *pcbRead
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Write
    (
	const void *pv,
	ULONG cb,
	ULONG *pcbWritten
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Seek
    (
	LARGE_INTEGER dlibMove,
	DWORD dwOrigin,
	ULARGE_INTEGER *plibNewPosition
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetSize
    (
	ULARGE_INTEGER libNewSize
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE CopyTo
    (
	IStream *pstm,
	ULARGE_INTEGER cb,
	ULARGE_INTEGER *pcbRead,
	ULARGE_INTEGER *pcbWritten
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Commit
    (
	DWORD grfCommitFlags
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Revert
    (
        void
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE LockRegion
    (
	ULARGE_INTEGER libOffset,
	ULARGE_INTEGER cb,
	DWORD dwLockType
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE UnlockRegion
    (
	ULARGE_INTEGER libOffset,
	ULARGE_INTEGER cb,
	DWORD dwLockType
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Stat
    (
	STATSTG *pstatstg,
	DWORD grfStatFlag
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Clone
    (
	IStream **ppstm
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IStreamVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IStream FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IStream FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IStream FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Read)
    (
        IStream FAR * This,
	void *pv,
	ULONG cb,
	ULONG *pcbRead
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Write)
    (
        IStream FAR * This,
	const void *pv,
	ULONG cb,
	ULONG *pcbWritten
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Seek)
    (
        IStream FAR * This,
	LARGE_INTEGER dlibMove,
	DWORD dwOrigin,
	ULARGE_INTEGER *plibNewPosition
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetSize)
    (
        IStream FAR * This,
	ULARGE_INTEGER libNewSize
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *CopyTo)
    (
        IStream FAR * This,
	IStream *pstm,
	ULARGE_INTEGER cb,
	ULARGE_INTEGER *pcbRead,
	ULARGE_INTEGER *pcbWritten
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Commit)
    (
        IStream FAR * This,
	DWORD grfCommitFlags
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Revert)
    (
        IStream FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *LockRegion)
    (
        IStream FAR * This,
	ULARGE_INTEGER libOffset,
	ULARGE_INTEGER cb,
	DWORD dwLockType
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *UnlockRegion)
    (
        IStream FAR * This,
	ULARGE_INTEGER libOffset,
	ULARGE_INTEGER cb,
	DWORD dwLockType
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Stat)
    (
        IStream FAR * This,
	STATSTG *pstatstg,
	DWORD grfStatFlag
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Clone)
    (
        IStream FAR * This,
	IStream **ppstm
    );
    
} IStreamVtbl;

interface IStream
{
    IStreamVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IStream__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IMalloc
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IMalloc__
#define __IMalloc__

/* Forward declaration */
typedef interface IMalloc IMalloc;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPMALLOC_DEFINED

#define _LPMALLOC_DEFINED

typedef IMalloc *LPMALLOC;


#endif

EXTERN_C const IID IID_IMalloc;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IMalloc : public IUnknown
{
public:
    virtual void  FAR *STDMETHODCALLTYPE Alloc
    (
	ULONG cb
    ) = 0;
    
    virtual void  FAR *STDMETHODCALLTYPE Realloc
    (
	void *pv,
	ULONG cb
    ) = 0;
    
    virtual void STDMETHODCALLTYPE Free
    (
	void *pv
    ) = 0;
    
    virtual ULONG STDMETHODCALLTYPE GetSize
    (
	void *pv
    ) = 0;
    
    virtual int STDMETHODCALLTYPE DidAlloc
    (
	void *pv
    ) = 0;
    
    virtual void STDMETHODCALLTYPE HeapMinimize
    (
        void
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IMallocVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IMalloc FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IMalloc FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IMalloc FAR * This
    );
    
    void  FAR *(STDMETHODCALLTYPE FAR *Alloc)
    (
        IMalloc FAR * This,
	ULONG cb
    );
    
    void  FAR *(STDMETHODCALLTYPE FAR *Realloc)
    (
        IMalloc FAR * This,
	void *pv,
	ULONG cb
    );
    
    void (STDMETHODCALLTYPE FAR *Free)
    (
        IMalloc FAR * This,
	void *pv
    );
    
    ULONG (STDMETHODCALLTYPE FAR *GetSize)
    (
        IMalloc FAR * This,
	void *pv
    );
    
    int (STDMETHODCALLTYPE FAR *DidAlloc)
    (
        IMalloc FAR * This,
	void *pv
    );
    
    void (STDMETHODCALLTYPE FAR *HeapMinimize)
    (
        IMalloc FAR * This
    );
    
} IMallocVtbl;

interface IMalloc
{
    IMallocVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IMalloc__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IStdMarshalInfo
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IStdMarshalInfo__
#define __IStdMarshalInfo__

/* Forward declaration */
typedef interface IStdMarshalInfo IStdMarshalInfo;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPSTDMARSHALINFO_DEFINED

#define _LPSTDMARSHALINFO_DEFINED

typedef IStdMarshalInfo *LPSTDMARSHALINFO;


#endif

EXTERN_C const IID IID_IStdMarshalInfo;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IStdMarshalInfo : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE GetClassForHandler
    (
	DWORD dwDestContext,
	void *pvDestContext,
	CLSID *pClsid
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IStdMarshalInfoVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IStdMarshalInfo FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IStdMarshalInfo FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IStdMarshalInfo FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetClassForHandler)
    (
        IStdMarshalInfo FAR * This,
	DWORD dwDestContext,
	void *pvDestContext,
	CLSID *pClsid
    );
    
} IStdMarshalInfoVtbl;

interface IStdMarshalInfo
{
    IStdMarshalInfoVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IStdMarshalInfo__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IMessageFilter
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IMessageFilter__
#define __IMessageFilter__

/* Forward declaration */
typedef interface IMessageFilter IMessageFilter;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPMESSAGEFILTER_DEFINED

#define _LPMESSAGEFILTER_DEFINED

typedef IMessageFilter *LPMESSAGEFILTER;


#endif

#ifndef _tagCALLTYPE_DEFINED

typedef enum tagCALLTYPE
  {
	CALLTYPE_TOPLEVEL = 1,
	CALLTYPE_NESTED = 2,
	CALLTYPE_ASYNC = 3,
	CALLTYPE_TOPLEVEL_CALLPENDING = 4,
	CALLTYPE_ASYNC_CALLPENDING = 5
  }
CALLTYPE;


#define _tagCALLTYPE_DEFINED

#define _CALLTYPE_DEFINED

#endif

#ifndef _tagSERVERCALL_DEFINED

typedef enum tagSERVERCALL
  {
	SERVERCALL_ISHANDLED = 0,
	SERVERCALL_REJECTED = 1,
	SERVERCALL_RETRYLATER = 2
  }
SERVERCALL;


#define _tagSERVERCALL_DEFINED

#define _SERVERCALL_DEFINED

#endif

#ifndef _tagPENDINGTYPE_DEFINED

typedef enum tagPENDINGTYPE
  {
	PENDINGTYPE_TOPLEVEL = 1,
	PENDINGTYPE_NESTED = 2
  }
PENDINGTYPE;


#define _tagPENDINGTYPE_DEFINED

#define _PENDINGTYPE_DEFINED

#endif

#ifndef _tagPENDINGMSG_DEFINED

typedef enum tagPENDINGMSG
  {
	PENDINGMSG_CANCELCALL = 0,
	PENDINGMSG_WAITNOPROCESS = 1,
	PENDINGMSG_WAITDEFPROCESS = 2
  }
PENDINGMSG;


#define _tagPENDINGMSG_DEFINED

#define _PENDINGMSG_DEFINED

#endif

#ifndef _tagINTERFACEINFO_DEFINED

typedef struct tagINTERFACEINFO
  {
  IUnknown *pUnk;
  IID iid;
  WORD wMethod;
  }
INTERFACEINFO;


typedef struct tagINTERFACEINFO *LPINTERFACEINFO;


#define _tagINTERFACEINFO_DEFINED

#define _INTERFACEINFO_DEFINED

#define _LPINTERFACEINFO_DEFINED

#endif

EXTERN_C const IID IID_IMessageFilter;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IMessageFilter : public IUnknown
{
public:
    virtual DWORD STDMETHODCALLTYPE HandleInComingCall
    (
	DWORD dwCallType,
	HTASK htaskCaller,
	DWORD dwTickCount,
	LPINTERFACEINFO lpInterfaceInfo
    ) = 0;
    
    virtual DWORD STDMETHODCALLTYPE RetryRejectedCall
    (
	HTASK htaskCallee,
	DWORD dwTickCount,
	DWORD dwRejectType
    ) = 0;
    
    virtual DWORD STDMETHODCALLTYPE MessagePending
    (
	HTASK htaskCallee,
	DWORD dwTickCount,
	DWORD dwPendingType
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IMessageFilterVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IMessageFilter FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IMessageFilter FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IMessageFilter FAR * This
    );
    
    DWORD (STDMETHODCALLTYPE FAR *HandleInComingCall)
    (
        IMessageFilter FAR * This,
	DWORD dwCallType,
	HTASK htaskCaller,
	DWORD dwTickCount,
	LPINTERFACEINFO lpInterfaceInfo
    );
    
    DWORD (STDMETHODCALLTYPE FAR *RetryRejectedCall)
    (
        IMessageFilter FAR * This,
	HTASK htaskCallee,
	DWORD dwTickCount,
	DWORD dwRejectType
    );
    
    DWORD (STDMETHODCALLTYPE FAR *MessagePending)
    (
        IMessageFilter FAR * This,
	HTASK htaskCallee,
	DWORD dwTickCount,
	DWORD dwPendingType
    );
    
} IMessageFilterVtbl;

interface IMessageFilter
{
    IMessageFilterVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IMessageFilter__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IEnumSTATSTG
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IEnumSTATSTG__
#define __IEnumSTATSTG__

/* Forward declaration */
typedef interface IEnumSTATSTG IEnumSTATSTG;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPENUMSTATSTG_DEFINED

#define _LPENUMSTATSTG_DEFINED

typedef IEnumSTATSTG *LPENUMSTATSTG;


#endif

EXTERN_C const IID IID_IEnumSTATSTG;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IEnumSTATSTG : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Next
    (
	ULONG celt,
	STATSTG *rgelt,
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
	IEnumSTATSTG **ppenum
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IEnumSTATSTGVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IEnumSTATSTG FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IEnumSTATSTG FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IEnumSTATSTG FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Next)
    (
        IEnumSTATSTG FAR * This,
	ULONG celt,
	STATSTG *rgelt,
	ULONG *pceltFetched
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Skip)
    (
        IEnumSTATSTG FAR * This,
	ULONG celt
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Reset)
    (
        IEnumSTATSTG FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Clone)
    (
        IEnumSTATSTG FAR * This,
	IEnumSTATSTG **ppenum
    );
    
} IEnumSTATSTGVtbl;

interface IEnumSTATSTG
{
    IEnumSTATSTGVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IEnumSTATSTG__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IMarshal
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IMarshal__
#define __IMarshal__

/* Forward declaration */
typedef interface IMarshal IMarshal;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPMARSHAL_DEFINED

#define _LPMARSHAL_DEFINED

typedef IMarshal *LPMARSHAL;


#endif

EXTERN_C const IID IID_IMarshal;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IMarshal : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE GetUnmarshalClass
    (
	REFIID riid,
	void *pv,
	DWORD dwDestContext,
	void *pvDestContext,
	DWORD mshlflags,
	CLSID *pCid
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetMarshalSizeMax
    (
	REFIID riid,
	void *pv,
	DWORD dwDestContext,
	void *pvDestContext,
	DWORD mshlflags,
	DWORD *pSize
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE MarshalInterface
    (
	IStream *pStm,
	REFIID riid,
	void *pv,
	DWORD dwDestContext,
	void *pvDestContext,
	DWORD mshlflags
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE UnmarshalInterface
    (
	IStream *pStm,
	REFIID riid,
	void **ppv
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE ReleaseMarshalData
    (
	IStream *pStm
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE DisconnectObject
    (
	DWORD dwReserved
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IMarshalVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IMarshal FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IMarshal FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IMarshal FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetUnmarshalClass)
    (
        IMarshal FAR * This,
	REFIID riid,
	void *pv,
	DWORD dwDestContext,
	void *pvDestContext,
	DWORD mshlflags,
	CLSID *pCid
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetMarshalSizeMax)
    (
        IMarshal FAR * This,
	REFIID riid,
	void *pv,
	DWORD dwDestContext,
	void *pvDestContext,
	DWORD mshlflags,
	DWORD *pSize
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *MarshalInterface)
    (
        IMarshal FAR * This,
	IStream *pStm,
	REFIID riid,
	void *pv,
	DWORD dwDestContext,
	void *pvDestContext,
	DWORD mshlflags
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *UnmarshalInterface)
    (
        IMarshal FAR * This,
	IStream *pStm,
	REFIID riid,
	void **ppv
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *ReleaseMarshalData)
    (
        IMarshal FAR * This,
	IStream *pStm
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *DisconnectObject)
    (
        IMarshal FAR * This,
	DWORD dwReserved
    );
    
} IMarshalVtbl;

interface IMarshal
{
    IMarshalVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IMarshal__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IPersistFile
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IPersistFile__
#define __IPersistFile__

/* Forward declaration */
typedef interface IPersistFile IPersistFile;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPPERSISTFILE_DEFINED

#define _LPPERSISTFILE_DEFINED

typedef IPersistFile *LPPERSISTFILE;


#endif

EXTERN_C const IID IID_IPersistFile;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IPersistFile : public IPersist
{
public:
    virtual HRESULT STDMETHODCALLTYPE IsDirty
    (
        void
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Load
    (
	LPCOLESTR pszFileName,
	DWORD dwMode
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Save
    (
	LPCOLESTR pszFileName,
	BOOL fRemember
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SaveCompleted
    (
	LPCOLESTR pszFileName
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetCurFile
    (
	LPOLESTR *ppszFileName
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IPersistFileVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IPersistFile FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IPersistFile FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IPersistFile FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetClassID)
    (
        IPersistFile FAR * This,
	CLSID *pClassID
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *IsDirty)
    (
        IPersistFile FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Load)
    (
        IPersistFile FAR * This,
	LPCOLESTR pszFileName,
	DWORD dwMode
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Save)
    (
        IPersistFile FAR * This,
	LPCOLESTR pszFileName,
	BOOL fRemember
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SaveCompleted)
    (
        IPersistFile FAR * This,
	LPCOLESTR pszFileName
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetCurFile)
    (
        IPersistFile FAR * This,
	LPOLESTR *ppszFileName
    );
    
} IPersistFileVtbl;

interface IPersistFile
{
    IPersistFileVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IPersistFile__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IRpcChannelBuffer
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IRpcChannelBuffer__
#define __IRpcChannelBuffer__

/* Forward declaration */
typedef interface IRpcChannelBuffer IRpcChannelBuffer;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


typedef unsigned long RPCOLEDATAREP;


typedef enum tagRPCFLG
  {
	RPCFLG_ASYNCHRONOUS = 1073741824,
	RPCFLG_INPUT_SYNCHRONOUS = 536870912
  }
RPCFLG;


typedef struct tagRPCOLEMESSAGE
  {
  void *reserved1;
  RPCOLEDATAREP dataRepresentation;
  void *Buffer;
  ULONG cbBuffer;
  ULONG iMethod;
  void *reserved2[5];
  ULONG rpcFlags;
  }
RPCOLEMESSAGE;


typedef RPCOLEMESSAGE *PRPCOLEMESSAGE;


EXTERN_C const IID IID_IRpcChannelBuffer;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IRpcChannelBuffer : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE GetBuffer
    (
	RPCOLEMESSAGE *pMessage,
	REFIID riid
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SendReceive
    (
	RPCOLEMESSAGE *pMessage,
	ULONG *pStatus
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE FreeBuffer
    (
	RPCOLEMESSAGE *pMessage
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetDestCtx
    (
	DWORD *pdwDestContext,
	void **ppvDestContext
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE IsConnected
    (
        void
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IRpcChannelBufferVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IRpcChannelBuffer FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IRpcChannelBuffer FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IRpcChannelBuffer FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetBuffer)
    (
        IRpcChannelBuffer FAR * This,
	RPCOLEMESSAGE *pMessage,
	REFIID riid
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SendReceive)
    (
        IRpcChannelBuffer FAR * This,
	RPCOLEMESSAGE *pMessage,
	ULONG *pStatus
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *FreeBuffer)
    (
        IRpcChannelBuffer FAR * This,
	RPCOLEMESSAGE *pMessage
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetDestCtx)
    (
        IRpcChannelBuffer FAR * This,
	DWORD *pdwDestContext,
	void **ppvDestContext
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *IsConnected)
    (
        IRpcChannelBuffer FAR * This
    );
    
} IRpcChannelBufferVtbl;

interface IRpcChannelBuffer
{
    IRpcChannelBufferVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IRpcChannelBuffer__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for ILockBytes
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __ILockBytes__
#define __ILockBytes__

/* Forward declaration */
typedef interface ILockBytes ILockBytes;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


typedef ILockBytes *LPLOCKBYTES;


EXTERN_C const IID IID_ILockBytes;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface ILockBytes : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE ReadAt
    (
	ULARGE_INTEGER ulOffset,
	void *pv,
	ULONG cb,
	ULONG *pcbRead
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE WriteAt
    (
	ULARGE_INTEGER ulOffset,
	const void *pv,
	ULONG cb,
	ULONG *pcbWritten
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Flush
    (
        void
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetSize
    (
	ULARGE_INTEGER cb
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE LockRegion
    (
	ULARGE_INTEGER libOffset,
	ULARGE_INTEGER cb,
	DWORD dwLockType
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE UnlockRegion
    (
	ULARGE_INTEGER libOffset,
	ULARGE_INTEGER cb,
	DWORD dwLockType
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Stat
    (
	STATSTG *pstatstg,
	DWORD grfStatFlag
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct ILockBytesVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        ILockBytes FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        ILockBytes FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        ILockBytes FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *ReadAt)
    (
        ILockBytes FAR * This,
	ULARGE_INTEGER ulOffset,
	void *pv,
	ULONG cb,
	ULONG *pcbRead
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *WriteAt)
    (
        ILockBytes FAR * This,
	ULARGE_INTEGER ulOffset,
	const void *pv,
	ULONG cb,
	ULONG *pcbWritten
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Flush)
    (
        ILockBytes FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetSize)
    (
        ILockBytes FAR * This,
	ULARGE_INTEGER cb
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *LockRegion)
    (
        ILockBytes FAR * This,
	ULARGE_INTEGER libOffset,
	ULARGE_INTEGER cb,
	DWORD dwLockType
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *UnlockRegion)
    (
        ILockBytes FAR * This,
	ULARGE_INTEGER libOffset,
	ULARGE_INTEGER cb,
	DWORD dwLockType
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Stat)
    (
        ILockBytes FAR * This,
	STATSTG *pstatstg,
	DWORD grfStatFlag
    );
    
} ILockBytesVtbl;

interface ILockBytes
{
    ILockBytesVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__ILockBytes__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IRpcProxyBuffer
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IRpcProxyBuffer__
#define __IRpcProxyBuffer__

/* Forward declaration */
typedef interface IRpcProxyBuffer IRpcProxyBuffer;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


EXTERN_C const IID IID_IRpcProxyBuffer;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IRpcProxyBuffer : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Connect
    (
	IRpcChannelBuffer *pRpcChannelBuffer
    ) = 0;
    
    virtual void STDMETHODCALLTYPE Disconnect
    (
        void
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IRpcProxyBufferVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IRpcProxyBuffer FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IRpcProxyBuffer FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IRpcProxyBuffer FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Connect)
    (
        IRpcProxyBuffer FAR * This,
	IRpcChannelBuffer *pRpcChannelBuffer
    );
    
    void (STDMETHODCALLTYPE FAR *Disconnect)
    (
        IRpcProxyBuffer FAR * This
    );
    
} IRpcProxyBufferVtbl;

interface IRpcProxyBuffer
{
    IRpcProxyBufferVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IRpcProxyBuffer__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IPersistStream
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IPersistStream__
#define __IPersistStream__

/* Forward declaration */
typedef interface IPersistStream IPersistStream;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPPERSISTSTREAM_DEFINED

#define _LPPERSISTSTREAM_DEFINED

typedef IPersistStream *LPPERSISTSTREAM;


#endif

EXTERN_C const IID IID_IPersistStream;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IPersistStream : public IPersist
{
public:
    virtual HRESULT STDMETHODCALLTYPE IsDirty
    (
        void
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Load
    (
	IStream *pStm
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Save
    (
	IStream *pStm,
	BOOL fClearDirty
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetSizeMax
    (
	ULARGE_INTEGER *pcbSize
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IPersistStreamVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IPersistStream FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IPersistStream FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IPersistStream FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetClassID)
    (
        IPersistStream FAR * This,
	CLSID *pClassID
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *IsDirty)
    (
        IPersistStream FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Load)
    (
        IPersistStream FAR * This,
	IStream *pStm
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Save)
    (
        IPersistStream FAR * This,
	IStream *pStm,
	BOOL fClearDirty
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetSizeMax)
    (
        IPersistStream FAR * This,
	ULARGE_INTEGER *pcbSize
    );
    
} IPersistStreamVtbl;

interface IPersistStream
{
    IPersistStreamVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IPersistStream__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IStorage
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IStorage__
#define __IStorage__

/* Forward declaration */
typedef interface IStorage IStorage;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPSTORAGE_DEFINED

#define _LPSTORAGE_DEFINED

typedef IStorage *LPSTORAGE;


#endif

typedef struct tagRemSNB
  {
  unsigned long ulCntStr;
  unsigned long ulCntChar;
  OLECHAR rgString[1];
  }
RemSNB;


#ifndef _SNB_DEFINED

#define _SNB_DEFINED

typedef OLECHAR **SNB;
void __RPC_API SNB_to_xmit (SNB __RPC_FAR *, RemSNB __RPC_FAR * __RPC_FAR *);
void __RPC_API SNB_from_xmit (RemSNB __RPC_FAR *, SNB __RPC_FAR *);
void __RPC_API SNB_free_inst (SNB __RPC_FAR *);
void __RPC_API SNB_free_xmit (RemSNB __RPC_FAR *);


#endif

EXTERN_C const IID IID_IStorage;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IStorage : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE CreateStream
    (
	const OLECHAR *pwcsName,
	DWORD grfMode,
	DWORD reserved1,
	DWORD reserved2,
	IStream **ppstm
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE OpenStream
    (
	const OLECHAR *pwcsName,
	void *reserved1,
	DWORD grfMode,
	DWORD reserved2,
	IStream **ppstm
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE CreateStorage
    (
	const OLECHAR *pwcsName,
	DWORD grfMode,
	DWORD dwStgFmt,
	DWORD reserved2,
	IStorage **ppstg
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE OpenStorage
    (
	const OLECHAR *pwcsName,
	IStorage *pstgPriority,
	DWORD grfMode,
	SNB snbExclude,
	DWORD reserved,
	IStorage **ppstg
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE CopyTo
    (
	DWORD ciidExclude,
	const IID *rgiidExclude,
	SNB snbExclude,
	IStorage *pstgDest
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE MoveElementTo
    (
	const OLECHAR *pwcsName,
	IStorage *pstgDest,
	const OLECHAR *pwcsNewName,
	DWORD grfFlags
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Commit
    (
	DWORD grfCommitFlags
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Revert
    (
        void
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE EnumElements
    (
	DWORD reserved1,
	void *reserved2,
	DWORD reserved3,
	IEnumSTATSTG **ppenum
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE DestroyElement
    (
	const OLECHAR *pwcsName
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE RenameElement
    (
	const OLECHAR *pwcsOldName,
	const OLECHAR *pwcsNewName
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetElementTimes
    (
	const OLECHAR *pwcsName,
	const FILETIME *pctime,
	const FILETIME *patime,
	const FILETIME *pmtime
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetClass
    (
	REFCLSID clsid
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetStateBits
    (
	DWORD grfStateBits,
	DWORD grfMask
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Stat
    (
	STATSTG *pstatstg,
	DWORD grfStatFlag
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IStorageVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IStorage FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IStorage FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IStorage FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *CreateStream)
    (
        IStorage FAR * This,
	const OLECHAR *pwcsName,
	DWORD grfMode,
	DWORD reserved1,
	DWORD reserved2,
	IStream **ppstm
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *OpenStream)
    (
        IStorage FAR * This,
	const OLECHAR *pwcsName,
	void *reserved1,
	DWORD grfMode,
	DWORD reserved2,
	IStream **ppstm
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *CreateStorage)
    (
        IStorage FAR * This,
	const OLECHAR *pwcsName,
	DWORD grfMode,
	DWORD dwStgFmt,
	DWORD reserved2,
	IStorage **ppstg
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *OpenStorage)
    (
        IStorage FAR * This,
	const OLECHAR *pwcsName,
	IStorage *pstgPriority,
	DWORD grfMode,
	SNB snbExclude,
	DWORD reserved,
	IStorage **ppstg
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *CopyTo)
    (
        IStorage FAR * This,
	DWORD ciidExclude,
	const IID *rgiidExclude,
	SNB snbExclude,
	IStorage *pstgDest
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *MoveElementTo)
    (
        IStorage FAR * This,
	const OLECHAR *pwcsName,
	IStorage *pstgDest,
	const OLECHAR *pwcsNewName,
	DWORD grfFlags
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Commit)
    (
        IStorage FAR * This,
	DWORD grfCommitFlags
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Revert)
    (
        IStorage FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *EnumElements)
    (
        IStorage FAR * This,
	DWORD reserved1,
	void *reserved2,
	DWORD reserved3,
	IEnumSTATSTG **ppenum
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *DestroyElement)
    (
        IStorage FAR * This,
	const OLECHAR *pwcsName
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *RenameElement)
    (
        IStorage FAR * This,
	const OLECHAR *pwcsOldName,
	const OLECHAR *pwcsNewName
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetElementTimes)
    (
        IStorage FAR * This,
	const OLECHAR *pwcsName,
	const FILETIME *pctime,
	const FILETIME *patime,
	const FILETIME *pmtime
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetClass)
    (
        IStorage FAR * This,
	REFCLSID clsid
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetStateBits)
    (
        IStorage FAR * This,
	DWORD grfStateBits,
	DWORD grfMask
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Stat)
    (
        IStorage FAR * This,
	STATSTG *pstatstg,
	DWORD grfStatFlag
    );
    
} IStorageVtbl;

interface IStorage
{
    IStorageVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IStorage__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IRpcStubBuffer
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IRpcStubBuffer__
#define __IRpcStubBuffer__

/* Forward declaration */
typedef interface IRpcStubBuffer IRpcStubBuffer;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


EXTERN_C const IID IID_IRpcStubBuffer;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IRpcStubBuffer : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Connect
    (
	IUnknown *pUnkServer
    ) = 0;
    
    virtual void STDMETHODCALLTYPE Disconnect
    (
        void
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Invoke
    (
	RPCOLEMESSAGE *_prpcmsg,
	IRpcChannelBuffer *_pRpcChannelBuffer
    ) = 0;
    
    virtual IRpcStubBuffer  FAR *STDMETHODCALLTYPE IsIIDSupported
    (
	REFIID riid
    ) = 0;
    
    virtual ULONG STDMETHODCALLTYPE CountRefs
    (
        void
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE DebugServerQueryInterface
    (
	void **ppv
    ) = 0;
    
    virtual void STDMETHODCALLTYPE DebugServerRelease
    (
	void *pv
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IRpcStubBufferVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IRpcStubBuffer FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IRpcStubBuffer FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IRpcStubBuffer FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Connect)
    (
        IRpcStubBuffer FAR * This,
	IUnknown *pUnkServer
    );
    
    void (STDMETHODCALLTYPE FAR *Disconnect)
    (
        IRpcStubBuffer FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Invoke)
    (
        IRpcStubBuffer FAR * This,
	RPCOLEMESSAGE *_prpcmsg,
	IRpcChannelBuffer *_pRpcChannelBuffer
    );
    
    IRpcStubBuffer  FAR *(STDMETHODCALLTYPE FAR *IsIIDSupported)
    (
        IRpcStubBuffer FAR * This,
	REFIID riid
    );
    
    ULONG (STDMETHODCALLTYPE FAR *CountRefs)
    (
        IRpcStubBuffer FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *DebugServerQueryInterface)
    (
        IRpcStubBuffer FAR * This,
	void **ppv
    );
    
    void (STDMETHODCALLTYPE FAR *DebugServerRelease)
    (
        IRpcStubBuffer FAR * This,
	void *pv
    );
    
} IRpcStubBufferVtbl;

interface IRpcStubBuffer
{
    IRpcStubBufferVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IRpcStubBuffer__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IPSFactoryBuffer
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IPSFactoryBuffer__
#define __IPSFactoryBuffer__

/* Forward declaration */
typedef interface IPSFactoryBuffer IPSFactoryBuffer;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


EXTERN_C const IID IID_IPSFactoryBuffer;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IPSFactoryBuffer : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE CreateProxy
    (
	IUnknown *pUnkOuter,
	REFIID riid,
	IRpcProxyBuffer **ppProxy,
	void **ppv
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE CreateStub
    (
	REFIID riid,
	IUnknown *pUnkServer,
	IRpcStubBuffer **ppStub
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IPSFactoryBufferVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IPSFactoryBuffer FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IPSFactoryBuffer FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IPSFactoryBuffer FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *CreateProxy)
    (
        IPSFactoryBuffer FAR * This,
	IUnknown *pUnkOuter,
	REFIID riid,
	IRpcProxyBuffer **ppProxy,
	void **ppv
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *CreateStub)
    (
        IPSFactoryBuffer FAR * This,
	REFIID riid,
	IUnknown *pUnkServer,
	IRpcStubBuffer **ppStub
    );
    
} IPSFactoryBufferVtbl;

interface IPSFactoryBuffer
{
    IPSFactoryBufferVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IPSFactoryBuffer__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IPersistStorage
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IPersistStorage__
#define __IPersistStorage__

/* Forward declaration */
typedef interface IPersistStorage IPersistStorage;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPPERSISTSTORAGE_DEFINED

#define _LPPERSISTSTORAGE_DEFINED

typedef IPersistStorage *LPPERSISTSTORAGE;


#endif

EXTERN_C const IID IID_IPersistStorage;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IPersistStorage : public IPersist
{
public:
    virtual HRESULT STDMETHODCALLTYPE IsDirty
    (
        void
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE InitNew
    (
	IStorage *pStg
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Load
    (
	IStorage *pStg
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Save
    (
	IStorage *pStgSave,
	BOOL fSameAsLoad
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SaveCompleted
    (
	IStorage *pStgNew
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE HandsOffStorage
    (
        void
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IPersistStorageVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IPersistStorage FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IPersistStorage FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IPersistStorage FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetClassID)
    (
        IPersistStorage FAR * This,
	CLSID *pClassID
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *IsDirty)
    (
        IPersistStorage FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *InitNew)
    (
        IPersistStorage FAR * This,
	IStorage *pStg
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Load)
    (
        IPersistStorage FAR * This,
	IStorage *pStg
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Save)
    (
        IPersistStorage FAR * This,
	IStorage *pStgSave,
	BOOL fSameAsLoad
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SaveCompleted)
    (
        IPersistStorage FAR * This,
	IStorage *pStgNew
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *HandsOffStorage)
    (
        IPersistStorage FAR * This
    );
    
} IPersistStorageVtbl;

interface IPersistStorage
{
    IPersistStorageVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IPersistStorage__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IAdviseSink
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IAdviseSink__
#define __IAdviseSink__

/* Forward declaration */
typedef interface IAdviseSink IAdviseSink;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


typedef IAdviseSink *LPADVISESINK;


typedef enum tagTYMED
  {
	TYMED_HGLOBAL = 1,
	TYMED_FILE = 2,
	TYMED_ISTREAM = 4,
	TYMED_ISTORAGE = 8,
	TYMED_GDI = 16,
	TYMED_MFPICT = 32,
	TYMED_NULL = 0
  }
TYMED;
void __RPC_API TYMED_to_xmit (TYMED __RPC_FAR *, long __RPC_FAR * __RPC_FAR *);
void __RPC_API TYMED_from_xmit (long __RPC_FAR *, TYMED __RPC_FAR *);
void __RPC_API TYMED_free_inst (TYMED __RPC_FAR *);
void __RPC_API TYMED_free_xmit (long __RPC_FAR *);


#define TYMED_to_xmit(pEnum, ppLong) *(ppLong) = (long *) (pEnum)

#define TYMED_from_xmit(pLong, pEnum) *(pEnum) = (TYMED) *(pLong)

#define TYMED_free_inst(pEnum) 

#define TYMED_free_xmit(pLong) 

typedef struct tagRemSTGMEDIUM
  {
  DWORD tymed;
  unsigned long pData;
  unsigned long pUnkForRelease;
  unsigned long cbData;
  byte data[1];
  }
RemSTGMEDIUM;


typedef struct tagSTGMEDIUM
  {
  DWORD tymed;
union
    {
    HBITMAP hBitmap;
    HMETAFILEPICT hMetaFilePict;
    HGLOBAL hGlobal;
    LPOLESTR lpszFileName;
    IStream *pstm;
    IStorage *pstg;
    }
  ;
  IUnknown *pUnkForRelease;
  }
STGMEDIUM;
void __RPC_API STGMEDIUM_to_xmit (STGMEDIUM __RPC_FAR *, RemSTGMEDIUM __RPC_FAR * __RPC_FAR *);
void __RPC_API STGMEDIUM_from_xmit (RemSTGMEDIUM __RPC_FAR *, STGMEDIUM __RPC_FAR *);
void __RPC_API STGMEDIUM_free_inst (STGMEDIUM __RPC_FAR *);
void __RPC_API STGMEDIUM_free_xmit (RemSTGMEDIUM __RPC_FAR *);

union __MIDL_IAdviseSink_0001
  {
  HBITMAP hBitmap;
  HMETAFILEPICT hMetaFilePict;
  HGLOBAL hGlobal;
  LPOLESTR lpszFileName;
  IStream *pstm;
  IStorage *pstg;
  }
;


typedef STGMEDIUM *LPSTGMEDIUM;


EXTERN_C const IID IID_IAdviseSink;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IAdviseSink : public IUnknown
{
public:
    virtual void STDMETHODCALLTYPE OnDataChange
    (
	FORMATETC *pFormatetc,
	STGMEDIUM *pStgmed
    ) = 0;
    
    virtual void STDMETHODCALLTYPE OnViewChange
    (
	DWORD dwAspect,
	LONG lindex
    ) = 0;
    
    virtual void STDMETHODCALLTYPE OnRename
    (
	IMoniker *pmk
    ) = 0;
    
    virtual void STDMETHODCALLTYPE OnSave
    (
        void
    ) = 0;
    
    virtual void STDMETHODCALLTYPE OnClose
    (
        void
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IAdviseSinkVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IAdviseSink FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IAdviseSink FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IAdviseSink FAR * This
    );
    
    void (STDMETHODCALLTYPE FAR *OnDataChange)
    (
        IAdviseSink FAR * This,
	FORMATETC *pFormatetc,
	STGMEDIUM *pStgmed
    );
    
    void (STDMETHODCALLTYPE FAR *OnViewChange)
    (
        IAdviseSink FAR * This,
	DWORD dwAspect,
	LONG lindex
    );
    
    void (STDMETHODCALLTYPE FAR *OnRename)
    (
        IAdviseSink FAR * This,
	IMoniker *pmk
    );
    
    void (STDMETHODCALLTYPE FAR *OnSave)
    (
        IAdviseSink FAR * This
    );
    
    void (STDMETHODCALLTYPE FAR *OnClose)
    (
        IAdviseSink FAR * This
    );
    
} IAdviseSinkVtbl;

interface IAdviseSink
{
    IAdviseSinkVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IAdviseSink__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IEnumMoniker
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IEnumMoniker__
#define __IEnumMoniker__

/* Forward declaration */
typedef interface IEnumMoniker IEnumMoniker;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPENUMMONIKER_DEFINED

#define _LPENUMMONIKER_DEFINED

typedef IEnumMoniker *LPENUMMONIKER;


#endif

EXTERN_C const IID IID_IEnumMoniker;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IEnumMoniker : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Next
    (
	ULONG celt,
	IMoniker **rgelt,
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
	IEnumMoniker **ppenum
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IEnumMonikerVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IEnumMoniker FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IEnumMoniker FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IEnumMoniker FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Next)
    (
        IEnumMoniker FAR * This,
	ULONG celt,
	IMoniker **rgelt,
	ULONG *pceltFetched
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Skip)
    (
        IEnumMoniker FAR * This,
	ULONG celt
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Reset)
    (
        IEnumMoniker FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Clone)
    (
        IEnumMoniker FAR * This,
	IEnumMoniker **ppenum
    );
    
} IEnumMonikerVtbl;

interface IEnumMoniker
{
    IEnumMonikerVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IEnumMoniker__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IEnumSTATDATA
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IEnumSTATDATA__
#define __IEnumSTATDATA__

/* Forward declaration */
typedef interface IEnumSTATDATA IEnumSTATDATA;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPENUMSTATDATA_DEFINED

#define _LPENUMSTATDATA_DEFINED

typedef IEnumSTATDATA *LPENUMSTATDATA;


#endif

#ifndef _tagADVF_DEFINED

#define _tagADVF_DEFINED

#define _ADVF_DEFINED

typedef enum tagADVF
  {
	ADVF_NODATA = 1,
	ADVF_PRIMEFIRST = 2,
	ADVF_ONLYONCE = 4,
	ADVF_DATAONSTOP = 64,
	ADVFCACHE_NOHANDLER = 8,
	ADVFCACHE_FORCEBUILTIN = 16,
	ADVFCACHE_ONSAVE = 32
  }
ADVF;


#endif

#ifndef _tagSTATDATA_DEFINED

typedef struct tagSTATDATA
  {
  FORMATETC formatetc;
  DWORD advf;
  IAdviseSink *pAdvSink;
  DWORD dwConnection;
  }
STATDATA;


typedef STATDATA *LPSTATDATA;


#define _tagSTATDATA_DEFINED

#define _STATDATA_DEFINED

#endif

EXTERN_C const IID IID_IEnumSTATDATA;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IEnumSTATDATA : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Next
    (
	ULONG celt,
	STATDATA *rgelt,
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
	IEnumSTATDATA **ppenum
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IEnumSTATDATAVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IEnumSTATDATA FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IEnumSTATDATA FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IEnumSTATDATA FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Next)
    (
        IEnumSTATDATA FAR * This,
	ULONG celt,
	STATDATA *rgelt,
	ULONG *pceltFetched
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Skip)
    (
        IEnumSTATDATA FAR * This,
	ULONG celt
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Reset)
    (
        IEnumSTATDATA FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Clone)
    (
        IEnumSTATDATA FAR * This,
	IEnumSTATDATA **ppenum
    );
    
} IEnumSTATDATAVtbl;

interface IEnumSTATDATA
{
    IEnumSTATDATAVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IEnumSTATDATA__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IMoniker
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IMoniker__
#define __IMoniker__

/* Forward declaration */
typedef interface IMoniker IMoniker;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPMONIKER_DEFINED

#define _LPMONIKER_DEFINED

typedef IMoniker *LPMONIKER;


#endif

#ifndef _tagMKSYS_DEFINED

typedef enum tagMKSYS
  {
	MKSYS_NONE = 0,
	MKSYS_GENERICCOMPOSITE = 1,
	MKSYS_FILEMONIKER = 2,
	MKSYS_ANTIMONIKER = 3,
	MKSYS_ITEMMONIKER = 4,
	MKSYS_POINTERMONIKER = 5
  }
MKSYS;


#define _tagMKSYS_DEFINED

#define _MKSYS_DEFINED

#endif

#ifndef _tagMKREDUCE_DEFINED

typedef enum tagMKREDUCE
  {
	MKRREDUCE_ONE = 196608,
	MKRREDUCE_TOUSER = 131072,
	MKRREDUCE_THROUGHUSER = 65536,
	MKRREDUCE_ALL = 0
  }
MKRREDUCE;


#define _tagMKREDUCE_DEFINED

#define _MKRREDUCE_DEFINED

#endif

EXTERN_C const IID IID_IMoniker;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IMoniker : public IPersistStream
{
public:
    virtual HRESULT STDMETHODCALLTYPE BindToObject
    (
	IBindCtx *pbc,
	IMoniker *pmkToLeft,
	REFIID riidResult,
	void **ppvResult
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE BindToStorage
    (
	IBindCtx *pbc,
	IMoniker *pmkToLeft,
	REFIID riid,
	void **ppvObj
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Reduce
    (
	IBindCtx *pbc,
	DWORD dwReduceHowFar,
	IMoniker **ppmkToLeft,
	IMoniker **ppmkReduced
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE ComposeWith
    (
	IMoniker *pmkRight,
	BOOL fOnlyIfNotGeneric,
	IMoniker **ppmkComposite
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Enum
    (
	BOOL fForward,
	IEnumMoniker **ppenumMoniker
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE IsEqual
    (
	IMoniker *pmkOtherMoniker
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Hash
    (
	DWORD *pdwHash
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE IsRunning
    (
	IBindCtx *pbc,
	IMoniker *pmkToLeft,
	IMoniker *pmkNewlyRunning
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetTimeOfLastChange
    (
	IBindCtx *pbc,
	IMoniker *pmkToLeft,
	FILETIME *pFileTime
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Inverse
    (
	IMoniker **ppmk
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE CommonPrefixWith
    (
	IMoniker *pmkOther,
	IMoniker **ppmkPrefix
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE RelativePathTo
    (
	IMoniker *pmkOther,
	IMoniker **ppmkRelPath
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetDisplayName
    (
	IBindCtx *pbc,
	IMoniker *pmkToLeft,
	LPOLESTR *ppszDisplayName
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE ParseDisplayName
    (
	IBindCtx *pbc,
	IMoniker *pmkToLeft,
	LPOLESTR pszDisplayName,
	ULONG *pchEaten,
	IMoniker **ppmkOut
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE IsSystemMoniker
    (
	DWORD *pdwMksys
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IMonikerVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IMoniker FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IMoniker FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IMoniker FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetClassID)
    (
        IMoniker FAR * This,
	CLSID *pClassID
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *IsDirty)
    (
        IMoniker FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Load)
    (
        IMoniker FAR * This,
	IStream *pStm
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Save)
    (
        IMoniker FAR * This,
	IStream *pStm,
	BOOL fClearDirty
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetSizeMax)
    (
        IMoniker FAR * This,
	ULARGE_INTEGER *pcbSize
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *BindToObject)
    (
        IMoniker FAR * This,
	IBindCtx *pbc,
	IMoniker *pmkToLeft,
	REFIID riidResult,
	void **ppvResult
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *BindToStorage)
    (
        IMoniker FAR * This,
	IBindCtx *pbc,
	IMoniker *pmkToLeft,
	REFIID riid,
	void **ppvObj
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Reduce)
    (
        IMoniker FAR * This,
	IBindCtx *pbc,
	DWORD dwReduceHowFar,
	IMoniker **ppmkToLeft,
	IMoniker **ppmkReduced
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *ComposeWith)
    (
        IMoniker FAR * This,
	IMoniker *pmkRight,
	BOOL fOnlyIfNotGeneric,
	IMoniker **ppmkComposite
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Enum)
    (
        IMoniker FAR * This,
	BOOL fForward,
	IEnumMoniker **ppenumMoniker
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *IsEqual)
    (
        IMoniker FAR * This,
	IMoniker *pmkOtherMoniker
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Hash)
    (
        IMoniker FAR * This,
	DWORD *pdwHash
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *IsRunning)
    (
        IMoniker FAR * This,
	IBindCtx *pbc,
	IMoniker *pmkToLeft,
	IMoniker *pmkNewlyRunning
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetTimeOfLastChange)
    (
        IMoniker FAR * This,
	IBindCtx *pbc,
	IMoniker *pmkToLeft,
	FILETIME *pFileTime
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Inverse)
    (
        IMoniker FAR * This,
	IMoniker **ppmk
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *CommonPrefixWith)
    (
        IMoniker FAR * This,
	IMoniker *pmkOther,
	IMoniker **ppmkPrefix
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *RelativePathTo)
    (
        IMoniker FAR * This,
	IMoniker *pmkOther,
	IMoniker **ppmkRelPath
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetDisplayName)
    (
        IMoniker FAR * This,
	IBindCtx *pbc,
	IMoniker *pmkToLeft,
	LPOLESTR *ppszDisplayName
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *ParseDisplayName)
    (
        IMoniker FAR * This,
	IBindCtx *pbc,
	IMoniker *pmkToLeft,
	LPOLESTR pszDisplayName,
	ULONG *pchEaten,
	IMoniker **ppmkOut
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *IsSystemMoniker)
    (
        IMoniker FAR * This,
	DWORD *pdwMksys
    );
    
} IMonikerVtbl;

interface IMoniker
{
    IMonikerVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IMoniker__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IRunningObjectTable
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IRunningObjectTable__
#define __IRunningObjectTable__

/* Forward declaration */
typedef interface IRunningObjectTable IRunningObjectTable;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPRUNNINGOBJECTTABLE_DEFINED

#define _LPRUNNINGOBJECTTABLE_DEFINED

typedef IRunningObjectTable *LPRUNNINGOBJECTTABLE;


#endif

EXTERN_C const IID IID_IRunningObjectTable;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IRunningObjectTable : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Register
    (
	DWORD grfFlags,
	IUnknown *punkObject,
	IMoniker *pmkObjectName,
	DWORD *pdwRegister
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Revoke
    (
	DWORD dwRegister
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE IsRunning
    (
	IMoniker *pmkObjectName
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetObject
    (
	IMoniker *pmkObjectName,
	IUnknown **ppunkObject
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE NoteChangeTime
    (
	DWORD dwRegister,
	FILETIME *pfiletime
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetTimeOfLastChange
    (
	IMoniker *pmkObjectName,
	FILETIME *pfiletime
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE EnumRunning
    (
	IEnumMoniker **ppenumMoniker
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IRunningObjectTableVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IRunningObjectTable FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IRunningObjectTable FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IRunningObjectTable FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Register)
    (
        IRunningObjectTable FAR * This,
	DWORD grfFlags,
	IUnknown *punkObject,
	IMoniker *pmkObjectName,
	DWORD *pdwRegister
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Revoke)
    (
        IRunningObjectTable FAR * This,
	DWORD dwRegister
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *IsRunning)
    (
        IRunningObjectTable FAR * This,
	IMoniker *pmkObjectName
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetObject)
    (
        IRunningObjectTable FAR * This,
	IMoniker *pmkObjectName,
	IUnknown **ppunkObject
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *NoteChangeTime)
    (
        IRunningObjectTable FAR * This,
	DWORD dwRegister,
	FILETIME *pfiletime
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetTimeOfLastChange)
    (
        IRunningObjectTable FAR * This,
	IMoniker *pmkObjectName,
	FILETIME *pfiletime
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *EnumRunning)
    (
        IRunningObjectTable FAR * This,
	IEnumMoniker **ppenumMoniker
    );
    
} IRunningObjectTableVtbl;

interface IRunningObjectTable
{
    IRunningObjectTableVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IRunningObjectTable__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IAdviseSink2
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IAdviseSink2__
#define __IAdviseSink2__

/* Forward declaration */
typedef interface IAdviseSink2 IAdviseSink2;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPADVISESINK2_DEFINED

#define _LPADVISESINK2_DEFINED

typedef IAdviseSink2 *LPADVISESINK2;


#endif

EXTERN_C const IID IID_IAdviseSink2;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IAdviseSink2 : public IAdviseSink
{
public:
    virtual void STDMETHODCALLTYPE OnLinkSrcChange
    (
	IMoniker *pmk
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IAdviseSink2Vtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IAdviseSink2 FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IAdviseSink2 FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IAdviseSink2 FAR * This
    );
    
    void (STDMETHODCALLTYPE FAR *OnDataChange)
    (
        IAdviseSink2 FAR * This,
	FORMATETC *pFormatetc,
	STGMEDIUM *pStgmed
    );
    
    void (STDMETHODCALLTYPE FAR *OnViewChange)
    (
        IAdviseSink2 FAR * This,
	DWORD dwAspect,
	LONG lindex
    );
    
    void (STDMETHODCALLTYPE FAR *OnRename)
    (
        IAdviseSink2 FAR * This,
	IMoniker *pmk
    );
    
    void (STDMETHODCALLTYPE FAR *OnSave)
    (
        IAdviseSink2 FAR * This
    );
    
    void (STDMETHODCALLTYPE FAR *OnClose)
    (
        IAdviseSink2 FAR * This
    );
    
    void (STDMETHODCALLTYPE FAR *OnLinkSrcChange)
    (
        IAdviseSink2 FAR * This,
	IMoniker *pmk
    );
    
} IAdviseSink2Vtbl;

interface IAdviseSink2
{
    IAdviseSink2Vtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IAdviseSink2__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IDataObject
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IDataObject__
#define __IDataObject__

/* Forward declaration */
typedef interface IDataObject IDataObject;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPDATAOBJECT_DEFINED

#define _LPDATAOBJECT_DEFINED

typedef IDataObject *LPDATAOBJECT;


#endif

#ifndef _tagDATADIR_DEFINED

#define _tagDATADIR_DEFINED

#define _DATADIR_DEFINED

typedef enum tagDATADIR
  {
	DATADIR_GET = 1,
	DATADIR_SET = 2
  }
DATADIR;


#endif

EXTERN_C const IID IID_IDataObject;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IDataObject : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE GetData
    (
	FORMATETC *pformatetcIn,
	STGMEDIUM *pmedium
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetDataHere
    (
	FORMATETC *pformatetc,
	STGMEDIUM *pmedium
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE QueryGetData
    (
	FORMATETC *pformatetc
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc
    (
	FORMATETC *pformatectIn,
	FORMATETC *pformatetcOut
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetData
    (
	FORMATETC *pformatetc,
	STGMEDIUM *pmedium,
	BOOL fRelease
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE EnumFormatEtc
    (
	DWORD dwDirection,
	IEnumFORMATETC **ppenumFormatEtc
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE DAdvise
    (
	FORMATETC *pformatetc,
	DWORD advf,
	IAdviseSink *pAdvSink,
	DWORD *pdwConnection
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE DUnadvise
    (
	DWORD dwConnection
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE EnumDAdvise
    (
	IEnumSTATDATA **ppenumAdvise
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IDataObjectVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IDataObject FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IDataObject FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IDataObject FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetData)
    (
        IDataObject FAR * This,
	FORMATETC *pformatetcIn,
	STGMEDIUM *pmedium
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetDataHere)
    (
        IDataObject FAR * This,
	FORMATETC *pformatetc,
	STGMEDIUM *pmedium
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *QueryGetData)
    (
        IDataObject FAR * This,
	FORMATETC *pformatetc
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetCanonicalFormatEtc)
    (
        IDataObject FAR * This,
	FORMATETC *pformatectIn,
	FORMATETC *pformatetcOut
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetData)
    (
        IDataObject FAR * This,
	FORMATETC *pformatetc,
	STGMEDIUM *pmedium,
	BOOL fRelease
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *EnumFormatEtc)
    (
        IDataObject FAR * This,
	DWORD dwDirection,
	IEnumFORMATETC **ppenumFormatEtc
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *DAdvise)
    (
        IDataObject FAR * This,
	FORMATETC *pformatetc,
	DWORD advf,
	IAdviseSink *pAdvSink,
	DWORD *pdwConnection
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *DUnadvise)
    (
        IDataObject FAR * This,
	DWORD dwConnection
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *EnumDAdvise)
    (
        IDataObject FAR * This,
	IEnumSTATDATA **ppenumAdvise
    );
    
} IDataObjectVtbl;

interface IDataObject
{
    IDataObjectVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IDataObject__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IDataAdviseHolder
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IDataAdviseHolder__
#define __IDataAdviseHolder__

/* Forward declaration */
typedef interface IDataAdviseHolder IDataAdviseHolder;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPDATAADVISEHOLDER_DEFINED

#define _LPDATAADVISEHOLDER_DEFINED

typedef IDataAdviseHolder *LPDATAADVISEHOLDER;


#endif

EXTERN_C const IID IID_IDataAdviseHolder;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IDataAdviseHolder : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Advise
    (
	IDataObject *pDataObject,
	FORMATETC *pFetc,
	DWORD advf,
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
    
    virtual HRESULT STDMETHODCALLTYPE SendOnDataChange
    (
	IDataObject *pDataObject,
	DWORD dwReserved,
	DWORD advf
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IDataAdviseHolderVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IDataAdviseHolder FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IDataAdviseHolder FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IDataAdviseHolder FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Advise)
    (
        IDataAdviseHolder FAR * This,
	IDataObject *pDataObject,
	FORMATETC *pFetc,
	DWORD advf,
	IAdviseSink *pAdvise,
	DWORD *pdwConnection
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Unadvise)
    (
        IDataAdviseHolder FAR * This,
	DWORD dwConnection
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *EnumAdvise)
    (
        IDataAdviseHolder FAR * This,
	IEnumSTATDATA **ppenumAdvise
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SendOnDataChange)
    (
        IDataAdviseHolder FAR * This,
	IDataObject *pDataObject,
	DWORD dwReserved,
	DWORD advf
    );
    
} IDataAdviseHolderVtbl;

interface IDataAdviseHolder
{
    IDataAdviseHolderVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IDataAdviseHolder__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IExternalConnection
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IExternalConnection__
#define __IExternalConnection__

/* Forward declaration */
typedef interface IExternalConnection IExternalConnection;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPEXTERNALCONNECTION_DEFINED

#define _LPEXTERNALCONNECTION_DEFINED

typedef IExternalConnection *LPEXTERNALCONNECTION;


#endif

typedef enum tagEXTCONN
  {
	EXTCONN_STRONG = 1
  }
EXTCONN;


EXTERN_C const IID IID_IExternalConnection;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IExternalConnection : public IUnknown
{
public:
    virtual DWORD STDMETHODCALLTYPE AddConnection
    (
	DWORD extconn,
	DWORD reserved
    ) = 0;
    
    virtual DWORD STDMETHODCALLTYPE ReleaseConnection
    (
	DWORD extconn,
	DWORD reserved,
	BOOL fLastReleaseCloses
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IExternalConnectionVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IExternalConnection FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IExternalConnection FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IExternalConnection FAR * This
    );
    
    DWORD (STDMETHODCALLTYPE FAR *AddConnection)
    (
        IExternalConnection FAR * This,
	DWORD extconn,
	DWORD reserved
    );
    
    DWORD (STDMETHODCALLTYPE FAR *ReleaseConnection)
    (
        IExternalConnection FAR * This,
	DWORD extconn,
	DWORD reserved,
	BOOL fLastReleaseCloses
    );
    
} IExternalConnectionVtbl;

interface IExternalConnection
{
    IExternalConnectionVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IExternalConnection__*/

#ifndef INITGUID
#include "cguid.h"
#endif



/****** STD Object API Prototypes *****************************************/

STDAPI_(DWORD) CoBuildVersion( VOID );

/* init/uninit */

STDAPI  CoInitialize(LPMALLOC pMalloc);
STDAPI  CoInitializeEx(LPMALLOC pMalloc, ULONG);
STDAPI_(void)  CoUninitialize(void);
STDAPI  CoGetMalloc(DWORD dwMemContext, LPMALLOC FAR* ppMalloc);
STDAPI_(DWORD) CoGetCurrentProcess(void);
STDAPI  CoCreateStandardMalloc(DWORD memctx, IMalloc FAR* FAR* ppMalloc);

#if DBG == 1
STDAPI_(ULONG) DebugCoGetRpcFault( void );
STDAPI_(void) DebugCoSetRpcFault( ULONG );
#endif

/* register/revoke/get class objects */

STDAPI  CoGetClassObject(REFCLSID rclsid, DWORD dwClsContext, LPVOID pvReserved,
                    REFIID riid, LPVOID FAR* ppv);
STDAPI  CoRegisterClassObject(REFCLSID rclsid, LPUNKNOWN pUnk,
                    DWORD dwClsContext, DWORD flags, LPDWORD lpdwRegister);
STDAPI  CoRevokeClassObject(DWORD dwRegister);


/* marshaling interface pointers */

STDAPI CoGetMarshalSizeMax(ULONG *pulSize, REFIID riid, LPUNKNOWN pUnk,
                    DWORD dwDestContext, LPVOID pvDestContext, DWORD mshlflags);
STDAPI CoMarshalInterface(LPSTREAM pStm, REFIID riid, LPUNKNOWN pUnk,
                    DWORD dwDestContext, LPVOID pvDestContext, DWORD mshlflags);
STDAPI CoUnmarshalInterface(LPSTREAM pStm, REFIID riid, LPVOID FAR* ppv);
STDAPI CoMarshalHresult(LPSTREAM pstm, HRESULT hresult);
STDAPI CoUnmarshalHresult(LPSTREAM pstm, HRESULT FAR * phresult);
STDAPI CoReleaseMarshalData(LPSTREAM pStm);
STDAPI CoDisconnectObject(LPUNKNOWN pUnk, DWORD dwReserved);
STDAPI CoLockObjectExternal(LPUNKNOWN pUnk, BOOL fLock, BOOL fLastUnlockReleases);
STDAPI CoGetStandardMarshal(REFIID riid, LPUNKNOWN pUnk,
                    DWORD dwDestContext, LPVOID pvDestContext, DWORD mshlflags,
                    LPMARSHAL FAR* ppMarshal);

STDAPI_(BOOL) CoIsHandlerConnected(LPUNKNOWN pUnk);
STDAPI_(BOOL) CoHasStrongExternalConnections(LPUNKNOWN pUnk);


/* dll loading helpers; keeps track of ref counts and unloads all on exit */

STDAPI_(HINSTANCE) CoLoadLibrary(LPOLESTR lpszLibName, BOOL bAutoFree);
STDAPI_(void) CoFreeLibrary(HINSTANCE hInst);
STDAPI_(void) CoFreeAllLibraries(void);
STDAPI_(void) CoFreeUnusedLibraries(void);


/* helper for creating instances */

STDAPI CoCreateInstance(REFCLSID rclsid, LPUNKNOWN pUnkOuter,
                    DWORD dwClsContext, REFIID riid, LPVOID FAR* ppv);


/* other helpers */

STDAPI_(BOOL) IsEqualGUID(REFGUID rguid1, REFGUID rguid2);
STDAPI StringFromCLSID(REFCLSID rclsid, LPOLESTR FAR* lplpsz);
STDAPI CLSIDFromString(LPOLESTR lpsz, LPCLSID pclsid);
STDAPI StringFromIID(REFIID rclsid, LPOLESTR FAR* lplpsz);
STDAPI IIDFromString(LPOLESTR lpsz, LPIID lpiid);
STDAPI_(BOOL) CoIsOle1Class(REFCLSID rclsid);
STDAPI ProgIDFromCLSID (REFCLSID clsid, LPOLESTR FAR* lplpszProgID);
STDAPI CLSIDFromProgID (LPCOLESTR lpszProgID, LPCLSID lpclsid);
STDAPI_(int) StringFromGUID2(REFGUID rguid, LPOLESTR lpsz, int cbMax);

STDAPI CoCreateGuid(GUID FAR *pguid);

STDAPI_(BOOL) CoFileTimeToDosDateTime(
                 FILETIME FAR* lpFileTime, LPWORD lpDosDate, LPWORD lpDosTime);
STDAPI_(BOOL) CoDosDateTimeToFileTime(
                       WORD nDosDate, WORD nDosTime, FILETIME FAR* lpFileTime);
STDAPI  CoFileTimeNow( FILETIME FAR* lpFileTime );


STDAPI CoRegisterMessageFilter( LPMESSAGEFILTER lpMessageFilter,
                                LPMESSAGEFILTER FAR* lplpMessageFilter );


/* TreatAs APIS */

STDAPI CoGetTreatAsClass(REFCLSID clsidOld, LPCLSID pClsidNew);
STDAPI CoTreatAsClass(REFCLSID clsidOld, REFCLSID clsidNew);


/* the server dlls must define their DllGetClassObject and DllCanUnloadNow
 * to match these; the typedefs are located here to ensure all are changed at
 * the same time.
 */

#ifdef _MAC
typedef STDAPICALLTYPE HRESULT (* LPFNGETCLASSOBJECT) (REFCLSID, REFIID, LPVOID *);
#else
typedef HRESULT (STDAPICALLTYPE * LPFNGETCLASSOBJECT) (REFCLSID, REFIID, LPVOID *);
#endif

#ifdef _MAC
typedef STDAPICALLTYPE HRESULT (* LPFNCANUNLOADNOW)(void);
#else
typedef HRESULT (STDAPICALLTYPE * LPFNCANUNLOADNOW)(void);
#endif

STDAPI  DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID FAR* ppv);

STDAPI  DllCanUnloadNow(void);


/****** Default Memory Allocation ******************************************/
STDAPI_(LPVOID) CoTaskMemAlloc(ULONG cb);
STDAPI_(LPVOID) CoTaskMemRealloc(LPVOID pv, ULONG cb);
STDAPI_(void)   CoTaskMemFree(LPVOID pv);


/****** Shared Memory Allocation ******************************************/
STDAPI_(void *) SharedMemAlloc(ULONG cNeeded, DWORD dwReserved);
STDAPI_(void *) SharedMemReAlloc(void *pmem, ULONG cNewSize, DWORD dwReserved);
STDAPI_(void) SharedMemFree(void *pmem, DWORD dwReserved);








STDAPI CoNewPersistentInstance(
    REFCLSID rclsid,
    REFIID riid,
    DWORD dwCtrl,
    DWORD grfMode,
    WCHAR *pwszCreateFrom,
    struct IStorage *pstgCreateFrom,
    WCHAR *pwszNewName,
    void **ppunk);

STDAPI CoGetPersistentInstance(
    REFIID riid,
    DWORD dwCtrl,
    DWORD grfMode,
    WCHAR *pwszName,
    struct IStorage *pstg,
    void **ppvUnk);

/****** DV APIs ***********************************************************/


STDAPI CreateDataAdviseHolder(LPDATAADVISEHOLDER FAR* ppDAHolder);

STDAPI CreateDataCache(LPUNKNOWN pUnkOuter, REFCLSID rclsid,
                                        REFIID iid, LPVOID FAR* ppv);




/****** Storage API Prototypes ********************************************/


STDAPI StgCreateDocfile(const OLECHAR FAR* pwcsName,
            DWORD grfMode,
            DWORD reserved,
            IStorage FAR * FAR *ppstgOpen);

STDAPI StgCreateDocfileOnILockBytes(ILockBytes FAR *plkbyt,
                    DWORD grfMode,
                    DWORD reserved,
                    IStorage FAR * FAR *ppstgOpen);
STDAPI StgCreateStorage(const OLECHAR FAR* pwcsName,
            DWORD grfMode,
            DWORD dwStgFmt,
            LPSECURITY_ATTRIBUTES pssSecurity,
            IStorage FAR * FAR *ppstg);

STDAPI StgOpenStorage(const OLECHAR FAR* pwcsName,
              IStorage FAR *pstgPriority,
              DWORD grfMode,
              SNB snbExclude,
              DWORD reserved,
              IStorage FAR * FAR *ppstgOpen);
STDAPI StgOpenStorageOnILockBytes(ILockBytes FAR *plkbyt,
                  IStorage FAR *pstgPriority,
                  DWORD grfMode,
                  SNB snbExclude,
                  DWORD reserved,
                  IStorage FAR * FAR *ppstgOpen);

STDAPI StgIsStorageFile(const OLECHAR FAR* pwcsName);
STDAPI StgIsStorageILockBytes(ILockBytes FAR* plkbyt);
STDAPI StgIsStorage(const OLECHAR FAR *pwcsName,
                    DWORD *pdwStgFmt);

// BUGBUG - These are obsolete APIs and should be removed as
// soon as possible
STDAPI StgCreateStream(const OLECHAR FAR *pwszPath,
            DWORD grfMode,
            DWORD reserved,
            IStream FAR * FAR *ppstm);
STDAPI StgOpenStream(const OLECHAR *pwszPath,
            void *reserved1,
            DWORD grfMode,
            DWORD reserved2,
            IStream FAR * FAR *ppstm);

STDAPI StgSetTimes(OLECHAR const FAR* lpszName,
                   FILETIME const FAR* pctime,
                   FILETIME const FAR* patime,
                   FILETIME const FAR* pmtime);


//
//  Moniker APIs
//

STDAPI  BindMoniker(LPMONIKER pmk, DWORD grfOpt, REFIID iidResult, LPVOID FAR* ppvResult);
STDAPI  MkParseDisplayName(LPBC pbc, LPOLESTR szUserName,
                ULONG FAR * pchEaten, LPMONIKER FAR * ppmk);
STDAPI  MonikerRelativePathTo(LPMONIKER pmkSrc, LPMONIKER pmkDest, LPMONIKER
                FAR* ppmkRelPath, BOOL fCalledFromMethod);
STDAPI  MonikerCommonPrefixWith(LPMONIKER pmkThis, LPMONIKER pmkOther,
                LPMONIKER FAR* ppmkCommon);
STDAPI  CreateBindCtx(DWORD reserved, LPBC FAR* ppbc);
STDAPI  CreateGenericComposite(LPMONIKER pmkFirst, LPMONIKER pmkRest,
    LPMONIKER FAR* ppmkComposite);
STDAPI  GetClassFile (LPCOLESTR szFilename, CLSID FAR* pclsid);

STDAPI  CreateFileMoniker(LPOLESTR lpszPathName, LPMONIKER FAR* ppmk);

#ifdef _CAIRO_
STDAPI  CreateFileMonikerEx(DFS_ROOT dfsRoot,
    LPOLESTR lpszPathName, LPMONIKER FAR* ppmk);
#endif

STDAPI  CreateItemMoniker(LPOLESTR lpszDelim, LPOLESTR lpszItem,
    LPMONIKER FAR* ppmk);
STDAPI  CreateAntiMoniker(LPMONIKER FAR* ppmk);
STDAPI  CreatePointerMoniker(LPUNKNOWN punk, LPMONIKER FAR* ppmk);

STDAPI  GetRunningObjectTable( DWORD reserved, LPRUNNINGOBJECTTABLE FAR* pprot);
#ifndef RC_INVOKED
#pragma pack()
#endif // RC_INVOKED
#endif     // __objbase_H__
