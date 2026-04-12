//+---------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//  File:       oleauto.h
//
//  Contents:   Defines the Ole Automation interfaces and APIs.
//
//  Interfaces:
//      IDispatch;
//      ITypeInfo;
//      ITypeLib;
//      ITypeComp;
//      ICreateTypeInfo;
//      ICreateTypeLib;
//
//  Functions:  SysAllocString         BSTR API
//      SysReAllocString
//      SysAllocStringLen
//      SysReAllocStringLen
//      SysFreeString
//      SysStringLen
//      DosDateTimeToVariantTime    Time API
//      VariantTimeToDosDateTime
//      SafeArrayCreate         Safe Array API
//      SafeArrayDestroy
//      SafeArrayGetDim
//      SafeArrayGetElemsize
//      SafeArrayGetUBound
//      SafeArrayGetLBound
//      SafeArrayLock
//      SafeArrayUnlock
//      SafeArrayAccessData
//      SafeArrayUnaccessData
//      SafeArrayGetElement
//      SafeArrayPutElement
//      SafeArrayCopy
//      VariantInit         Variant API
//      VariantClear
//      VariantCopy
//      VariantCopyInd
//      VariantChangeType
//      LHashValOfName          TypeInfo API
//      LoadTypeLib
//      LoadRegTypeLib
//      RegisterTypeLib
//      DeregisterTypeLib
//      CreateTypeLib
//      DispGetParam            Dispatch API
//      DispGetIDsOfNames
//      DispInvoke
//      CreateDispTypeInfo
//      CreateStdDispatch
//      RegisterActiveObject        Active Object Registration API
//      RevokeActiveObject
//      GetActiveObject
//
//
//
//  History:    2-07-94   terryru   Created from dispath.h, variant.h, and basetypes.h
//
//----------------------------------------------------------------------------

#if !defined( _OLEAUTO_H_ )
#define _OLEAUTO_H_

// Set packing to 8 for ISV, and Chicago support (terryru)
#ifndef RC_INVOKED
#pragma pack(8)
#endif // RC_INVOKED

//  Definition of the OLE Automation APIs, and macros.


/* IDispatch related error codes */

#define DISP_ERROR(X) MAKE_SCODE(SEVERITY_ERROR, FACILITY_DISPATCH, X)

#define DISP_E_UNKNOWNINTERFACE         DISP_ERROR(1)
#define DISP_E_MEMBERNOTFOUND           DISP_ERROR(3)
#define DISP_E_PARAMNOTFOUND            DISP_ERROR(4)
#define DISP_E_TYPEMISMATCH             DISP_ERROR(5)
#define DISP_E_UNKNOWNNAME              DISP_ERROR(6)
#define DISP_E_NONAMEDARGS              DISP_ERROR(7)
#define DISP_E_BADVARTYPE               DISP_ERROR(8)
#define DISP_E_EXCEPTION                DISP_ERROR(9)
#define DISP_E_OVERFLOW                 DISP_ERROR(10)
#define DISP_E_BADINDEX                 DISP_ERROR(11)
#define DISP_E_UNKNOWNLCID              DISP_ERROR(12)
#define DISP_E_ARRAYISLOCKED            DISP_ERROR(13)
#define DISP_E_BADPARAMCOUNT            DISP_ERROR(14)
#define DISP_E_PARAMNOTOPTIONAL         DISP_ERROR(15)
#define DISP_E_BADCALLEE                DISP_ERROR(16)
#define DISP_E_NOTACOLLECTION           DISP_ERROR(17)

#define TYPE_ERROR(X) MAKE_SCODE(SEVERITY_ERROR, FACILITY_DISPATCH, X)

#define TYPE_E_BUFFERTOOSMALL           TYPE_ERROR(32790)
#define TYPE_E_INVDATAREAD              TYPE_ERROR(32792)
#define TYPE_E_UNSUPFORMAT              TYPE_ERROR(32793)
#define TYPE_E_REGISTRYACCESS           TYPE_ERROR(32796)
#define TYPE_E_LIBNOTREGISTERED         TYPE_ERROR(32797)
#define TYPE_E_UNDEFINEDTYPE            TYPE_ERROR(32807)
#define TYPE_E_QUALIFIEDNAMEDISALLOWED  TYPE_ERROR(32808)
#define TYPE_E_INVALIDSTATE             TYPE_ERROR(32809)
#define TYPE_E_WRONGTYPEKIND            TYPE_ERROR(32810)
#define TYPE_E_ELEMENTNOTFOUND          TYPE_ERROR(32811)
#define TYPE_E_AMBIGUOUSNAME            TYPE_ERROR(32812)
#define TYPE_E_NAMECONFLICT             TYPE_ERROR(32813)
#define TYPE_E_UNKNOWNLCID              TYPE_ERROR(32814)
#define TYPE_E_DLLFUNCTIONNOTFOUND      TYPE_ERROR(32815)
#define TYPE_E_BADMODULEKIND            TYPE_ERROR(35005)
#define TYPE_E_SIZETOOBIG               TYPE_ERROR(35013)
#define TYPE_E_DUPLICATEID              TYPE_ERROR(35014)
#define TYPE_E_TYPEMISMATCH             TYPE_ERROR(36000)
#define TYPE_E_OUTOFBOUNDS              TYPE_ERROR(36001)
#define TYPE_E_IOERROR                  TYPE_ERROR(36002)
#define TYPE_E_CANTCREATETMPFILE        TYPE_ERROR(36003)
#define TYPE_E_CANTLOADLIBRARY          TYPE_ERROR(40010)
#define TYPE_E_INCONSISTENTPROPFUNCS    TYPE_ERROR(40067)
#define TYPE_E_CIRCULARTYPE             TYPE_ERROR(40068)




/* if not already picked up from olenls.h */
#ifndef _LCID_DEFINED
typedef DWORD LCID;
# define _LCID_DEFINED
#endif
#ifndef __VARNT_H__
#define __VARNT_H__

#ifdef __cplusplus
extern "C" {
#endif

#define small char
















#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif

#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif

/* XBSTR is for internal use only, it is subject to change */
typedef struct tagXBSTR
  {
  ULONG cbSize;
  OLECHAR rgBstrData[1];
  }
XBSTR;

typedef OLECHAR *BSTR;

typedef BSTR *LPBSTR;

#ifndef _tagBLOB_DEFINED
#define _tagBLOB_DEFINED
#define _BLOB_DEFINED
#define _LPBLOB_DEFINED
typedef struct tagBLOB
  {
  ULONG cbSize;
  BYTE *pBlobData;
  }
BLOB;

typedef struct tagBLOB *LPBLOB;

#endif
#ifndef _tagCLIPDATA_DEFINED
#define _tagCLIPDATA_DEFINED
#define _CLIPDATA_DEFINED
typedef struct tagCLIPDATA
  {
  ULONG cbSize;
  long ulClipFmt;
  BYTE *pClipData;
  }
CLIPDATA;

#endif
#ifndef _tagSAFEARRAYBOUND_DEFINED
#define _tagSAFEARRAYBOUND_DEFINED
#define _SAFEARRAYBOUND_DEFINED
#define _LPSAFEARRAYBOUND_DEFINED
typedef struct tagSAFEARRAYBOUND
  {
  ULONG cElements;
  LONG lLbound;
  }
SAFEARRAYBOUND;

typedef struct tagSAFEARRAYBOUND *LPSAFEARRAYBOUND;

#endif
#ifndef _tagSAFEARRAY_DEFINED
#define _tagSAFEARRAY_DEFINED
#define _SAFEARRAY_DEFINED
#define _LPSAFEARRAY_DEFINED
#if 0
/* the following is what RPC knows how to remote */
typedef struct tagSAFEARRAY
  {
  unsigned short cDims;
  unsigned short fFeatures;
  unsigned long cbElements;
  unsigned long cLocks;
  BYTE *pvData;
  SAFEARRAYBOUND rgsabound[1];
  }
SAFEARRAY;

typedef struct tagSAFEARRAY *LPSAFEARRAY;

#else
typedef struct FARSTRUCT tagSAFEARRAY {
    unsigned short cDims;
    unsigned short fFeatures;
#if defined(WIN32)
    unsigned long cbElements;
    unsigned long cLocks;
#else
    unsigned short cbElements;
    unsigned short cLocks;
    unsigned long handle;               // unused but kept for compatiblity
#endif
    void HUGEP* pvData;
    SAFEARRAYBOUND rgsabound[1];
} SAFEARRAY, FAR* LPSAFEARRAY;
#endif
#endif
#define FADF_AUTO	0x1

#define FADF_STATIC	0x2

#define FADF_EMBEDDED	0x4

#define FADF_FIXEDSIZE	0x10

#define FADF_BSTR	0x100

#define FADF_UNKNOWN	0x200

#define FADF_DISPATCH	0x400

#define FADF_VARIANT	0x800

#define FADF_RESERVED	0xf0e8

typedef double DATE;

#ifndef _tagCY_DEFINED
#define _tagCY_DEFINED
#define _CY_DEFINED
typedef struct tagCY
  {
  unsigned long Lo;
  long Hi;
  }
CY;

#endif
typedef CY CURRENCY;

typedef short VARIANT_BOOL;

#ifndef _VARENUM_DEFINED
#define _VARENUM_DEFINED
/*
 * VARENUM usage key,
 *
 * * [V] - may appear in a VARIANT
 * * [T] - may appear in a TYPEDESC
 * * [P] - may appear in an OLE property set
 * * [S] - may appear in a Safe Array
 *
 *
 *  VT_EMPTY            [V]   [P]     nothing                     
 *  VT_NULL             [V]           SQL style Null              
 *  VT_I2               [V][T][P][S]  2 byte signed int           
 *  VT_I4               [V][T][P][S]  4 byte signed int           
 *  VT_R4               [V][T][P][S]  4 byte real                 
 *  VT_R8               [V][T][P][S]  8 byte real                 
 *  VT_CY               [V][T][P][S]  currency                    
 *  VT_DATE             [V][T][P][S]  date                        
 *  VT_BSTR             [V][T][P][S]  binary string               
 *  VT_DISPATCH         [V][T]   [S]  IDispatch FAR*              
 *  VT_ERROR            [V][T]   [S]  SCODE                       
 *  VT_BOOL             [V][T][P][S]  True=-1, False=0            
 *  VT_VARIANT          [V][T][P][S]  VARIANT FAR*                
 *  VT_UNKNOWN          [V][T]   [S]  IUnknown FAR*               
 *  VT_I1                  [T]        signed char                 
 *  VT_UI1                 [T]        unsigned char               
 *  VT_UI2                 [T]        unsigned short              
 *  VT_UI4                 [T]        unsigned short              
 *  VT_I8                  [T][P]     signed 64-bit int           
 *  VT_UI8                 [T]        unsigned 64-bit int         
 *  VT_INT                 [T]        signed machine int          
 *  VT_UINT                [T]        unsigned machine int        
 *  VT_VOID                [T]        C style void                
 *  VT_HRESULT             [T]                                    
 *  VT_PTR                 [T]        pointer type                
 *  VT_SAFEARRAY           [T]        (use VT_ARRAY in VARIANT)   
 *  VT_CARRAY              [T]        C style array               
 *  VT_USERDEFINED         [T]        user defined type           
 *  VT_LPSTR               [T][P]     null terminated string      
 *  VT_LPWSTR              [T][P]     wide null terminated string 
 *  VT_FILETIME               [P]     FILETIME                    
 *  VT_BLOB                   [P]     Length prefixed bytes       
 *  VT_STREAM                 [P]     Name of the stream follows  
 *  VT_STORAGE                [P]     Name of the storage follows 
 *  VT_STREAMED_OBJECT        [P]     Stream contains an object   
 *  VT_STORED_OBJECT          [P]     Storage contains an object  
 *  VT_BLOB_OBJECT            [P]     Blob contains an object     
 *  VT_CF                     [P]     Clipboard format            
 *  VT_CLSID                  [P]     A Class ID                  
 *  VT_VECTOR                 [P]     simple counted array        
 *  VT_ARRAY            [V]           SAFEARRAY*                  
 *  VT_BYREF            [V]                                       
 */
enum VARENUM
  {
	VT_EMPTY = 0,
	VT_NULL = 1,
	VT_I2 = 2,
	VT_I4 = 3,
	VT_R4 = 4,
	VT_R8 = 5,
	VT_CY = 6,
	VT_DATE = 7,
	VT_BSTR = 8,
	VT_DISPATCH = 9,
	VT_ERROR = 10,
	VT_BOOL = 11,
	VT_VARIANT = 12,
	VT_UNKNOWN = 13,
	VT_I1 = 16,
	VT_UI1 = 17,
	VT_UI2 = 18,
	VT_UI4 = 19,
	VT_I8 = 20,
	VT_UI8 = 21,
	VT_INT = 22,
	VT_UINT = 23,
	VT_VOID = 24,
	VT_HRESULT = 25,
	VT_PTR = 26,
	VT_SAFEARRAY = 27,
	VT_CARRAY = 28,
	VT_USERDEFINED = 29,
	VT_LPSTR = 30,
	VT_LPWSTR = 31,
	VT_FILETIME = 64,
	VT_BLOB = 65,
	VT_STREAM = 66,
	VT_STORAGE = 67,
	VT_STREAMED_OBJECT = 68,
	VT_STORED_OBJECT = 69,
	VT_BLOB_OBJECT = 70,
	VT_CF = 71,
	VT_CLSID = 72
  }
;

#endif
#define VT_VECTOR	0x1000

#define VT_ARRAY	0x2000

#define VT_BYREF	0x4000

#define VT_RESERVED	0x8000

#ifndef _VARTYPE_DEFINED
#define _VARTYPE_DEFINED
typedef unsigned short VARTYPE;

#endif
typedef struct tagVARIANT VARIANT;

/* forward declare IDispatch */
typedef interface IDispatch IDispatch;
/* VARIANT STRUCTURE
 *
 *  VARTYPE vt;
 *  unsigned short wReserved1;
 *  unsigned short wReserved2;
 *  unsigned short wReserved3;
 *  union {
 *    short                VT_I2                
 *    long                 VT_I4                
 *    float                VT_R4                
 *    double               VT_R8                
 *    VARIANT_BOOL         VT_BOOL              
 *    SCODE                VT_ERROR             
 *    CY                   VT_CY                
 *    DATE                 VT_DATE              
 *    BSTR                 VT_BSTR              
 *    IUnknown FAR*        VT_UNKNOWN           
 *    IDispatch FAR*       VT_DISPATCH          
 *    SAFEARRAY FAR*       VT_ARRAY|*           
 *    short FAR*           VT_BYREF|VT_I2       
 *    long FAR*            VT_BYREF|VT_I4       
 *    float FAR*           VT_BYREF|VT_R4       
 *    double FAR*          VT_BYREF|VT_R8       
 *    VARIANT_BOOL FAR*    VT_BYREF|VT_BOOL     
 *    SCODE FAR*           VT_BYREF|VT_ERROR    
 *    CY FAR*              VT_BYREF|VT_CY       
 *    DATE FAR*            VT_BYREF|VT_DATE     
 *    BSTR FAR*            VT_BYREF|VT_BSTR     
 *    IUnknown FAR* FAR*   VT_BYREF|VT_UNKNOWN  
 *    IDispatch FAR* FAR*  VT_BYREF|VT_DISPATCH 
 *    SAFEARRAY FAR* FAR*  VT_BYREF|VT_ARRAY|*  
 *    VARIANT FAR*         VT_BYREF|VT_VARIANT  
 *    void FAR*            Generic ByRef        
 */
#ifndef _tagVARIANT_DEFINED
#define _tagVARIANT_DEFINED
#if 0
/* the following is what RPC knows how to remote */
struct tagVARIANT
  {
  VARTYPE vt;
  WORD wReserved1;
  WORD wReserved2;
  WORD wReserved3;
union
    {
    short iVal;
    long lVal;
    float fltVal;
    double dblVal;
    VARIANT_BOOL bool;
    SCODE scode;
    CY cyVal;
    DATE date;
    BSTR bstrVal;
    IUnknown *punkVal;
    SAFEARRAY *parray;
    short *piVal;
    long *plVal;
    float *pfltVal;
    double *pdblVal;
    VARIANT_BOOL *pbool;
    SCODE *pscode;
    CY *pcyVal;
    DATE *pdate;
    BSTR *pbstrVal;
    IUnknown **ppunkVal;
    SAFEARRAY **pparray;
    VARIANT *pvarVal;
    long *byref;
    }
  ;
  }
;

union __MIDL_RemVariant_0001
  {
  short iVal;
  long lVal;
  float fltVal;
  double dblVal;
  VARIANT_BOOL bool;
  SCODE scode;
  CY cyVal;
  DATE date;
  BSTR bstrVal;
  IUnknown *punkVal;
  SAFEARRAY *parray;
  short *piVal;
  long *plVal;
  float *pfltVal;
  double *pdblVal;
  VARIANT_BOOL *pbool;
  SCODE *pscode;
  CY *pcyVal;
  DATE *pdate;
  BSTR *pbstrVal;
  IUnknown **ppunkVal;
  SAFEARRAY **pparray;
  VARIANT *pvarVal;
  long *byref;
  }
;

#endif
struct tagVARIANT{
    VARTYPE vt;
    WORD wReserved1;
    WORD wReserved2;
    WORD wReserved3;
    union
    {
      short         iVal;           /* VT_I2                */
      long          lVal;           /* VT_I4                */
      float         fltVal;         /* VT_R4                */
      double        dblVal;         /* VT_R8                */
      VARIANT_BOOL  bool;           /* VT_BOOL              */
      SCODE         scode;          /* VT_ERROR             */
      CY            cyVal;          /* VT_CY                */
      DATE          date;           /* VT_DATE              */
      BSTR          bstrVal;        /* VT_BSTR              */
      IUnknown      *punkVal;       /* VT_UNKNOWN           */
      IDispatch     *pdispVal;      /* VT_DISPATCH          */
      SAFEARRAY     *parray;        /* VT_ARRAY|*           */
      short         *piVal;         /* VT_BYREF|VT_I2       */
      long          *plVal;         /* VT_BYREF|VT_I4       */
      float         *pfltVal;       /* VT_BYREF|VT_R4       */
      double        *pdblVal;       /* VT_BYREF|VT_R8       */
      VARIANT_BOOL  *pbool;         /* VT_BYREF|VT_BOOL     */
      SCODE         *pscode;        /* VT_BYREF|VT_ERROR    */
      CY            *pcyVal;        /* VT_BYREF|VT_CY       */
      DATE          *pdate;         /* VT_BYREF|VT_DATE     */
      BSTR          *pbstrVal;      /* VT_BYREF|VT_BSTR     */
      IUnknown      **ppunkVal;     /* VT_BYREF|VT_UNKNOWN  */
      IDispatch     **ppdispVal;    /* VT_BYREF|VT_DISPATCH */
      SAFEARRAY     **pparray;      /* VT_BYREF|VT_ARRAY|*  */
      VARIANT       *pvarVal;       /* VT_BYREF|VT_VARIANT  */
      void     * byref;             /* Generic ByRef        */
    }
#if(defined(NONAMELESSUNION))
    u
#endif
     ;
};
#endif
#ifndef _LPVARIANT_DEFINED
#define _LPVARIANT_DEFINED
typedef struct tagVARIANT *LPVARIANT;

#endif
#ifndef _VARIANTARG_DEFINED
#define _VARIANTARG_DEFINED
typedef struct tagVARIANT VARIANTARG;

#endif
#ifndef _LPVARIANTARG_DEFINED
#define _LPVARIANTARG_DEFINED
typedef struct tagVARIANT *LPVARIANTARG;

#endif
#ifndef _DISPID_DEFINED
#define _DISPID_DEFINED
typedef LONG DISPID;

#endif
#ifndef _MEMBERID_DEFINED
#define _MEMBERID_DEFINED
typedef DISPID MEMBERID;

#endif
#ifndef _HREFTYPE_DEFINED
#define _HREFTYPE_DEFINED
typedef DWORD HREFTYPE;

#endif
typedef ULONG PROPID;

typedef enum tagTYPEKIND
  {
	TKIND_ENUM = 0,
	TKIND_RECORD = 1,
	TKIND_MODULE = 2,
	TKIND_INTERFACE = 3,
	TKIND_DISPATCH = 4,
	TKIND_COCLASS = 5,
	TKIND_ALIAS = 6,
	TKIND_UNION = 7,
	TKIND_MAX = 8
  }
TYPEKIND;
void __RPC_API TYPEKIND_to_xmit (TYPEKIND __RPC_FAR *, long __RPC_FAR * __RPC_FAR *);
void __RPC_API TYPEKIND_from_xmit (long __RPC_FAR *, TYPEKIND __RPC_FAR *);
void __RPC_API TYPEKIND_free_inst (TYPEKIND __RPC_FAR *);
void __RPC_API TYPEKIND_free_xmit (long __RPC_FAR *);

#define TYPEKIND_to_xmit(pEnum, ppLong) *(ppLong) = (long *) (pEnum)
#define TYPEKIND_from_xmit(pLong, pEnum) *(pEnum) = (TYPEKIND) *(pLong)
#define TYPEKIND_free_inst(pEnum) 
#define TYPEKIND_free_xmit(pLong) 
#ifndef _tagTYPEDESC_DEFINED
#define _tagTYPEDESC_DEFINED
#define _TYPEDESC_DEFINED
/* VT_PTR - lptdesc, the pointed at type */
/* VT_CARRAY - lpadesc */
/* VT_USERDEFINED - hreftype, used to get the UDT typeinfo */
typedef struct tagTYPEDESC
  {
union
    {
    struct tagTYPEDESC *lptdesc;
    struct tagARRAYDESC *lpadesc;
    HREFTYPE hreftype;
    }
  ;
  VARTYPE vt;
  }
TYPEDESC;

union __MIDL_RemVariant_0002
  {
  struct tagTYPEDESC *lptdesc;
  struct tagARRAYDESC *lpadesc;
  HREFTYPE hreftype;
  }
;

#endif
#ifndef _tagARRAYDESC_DEFINED
#define _tagARRAYDESC_DEFINED
#define _ARRAYDESC_DEFINED
typedef struct tagARRAYDESC
  {
  TYPEDESC tdescElem;
  USHORT cDims;
  SAFEARRAYBOUND rgbounds[1];
  }
ARRAYDESC;

#endif
#ifndef _tagIDLDESC_DEFINED
#define _tagIDLDESC_DEFINED
#define _IDLDESC_DEFINED
#define _LPIDLDESC_DEFINED
#ifdef WIN32
typedef struct tagIDLDESC
  {
  unsigned long dwReserved;
  unsigned short wIDLFlags;
  }
IDLDESC;

typedef struct tagIDLDESC *LPIDLDESC;

#else
typedef struct FARSTRUCT tagIDLDESC {
#if defined(WIN32)
    unsigned long dwReserved;
#else
    BSTR bstrIDLInfo;           /* reserved, but original name retained for
                                   compatibilty */
#endif
    unsigned short wIDLFlags;   /* IN, OUT, etc */
} IDLDESC, FAR* LPIDLDESC;
#endif
#endif
#ifndef _tagELEMDESC_DEFINED
#define _tagELEMDESC_DEFINED
#define _ELEMDESC_DEFINED
#define _LPELEMDESC_DEFINED
typedef struct tagELEMDESC
  {
  TYPEDESC tdesc;
  IDLDESC idldesc;
  }
ELEMDESC;

typedef struct tagELEMDESC *LPELEMDESC;

#endif
#ifndef _tagTYPEATTR_DEFINED
#define _tagTYPEATTR_DEFINED
#define _TYPEATTR_DEFINED
#define _LPTYPEATTR_DEFINED
typedef struct tagTYPEATTR
  {
  GUID guid;
  LCID lcid;
  DWORD dwReserved;
  MEMBERID memidConstructor;
  MEMBERID memidDestructor;
  LPOLESTR lpstrSchema;
  ULONG cbSizeInstance;
  TYPEKIND typekind;
  WORD cFuncs;
  WORD cVars;
  WORD cImplTypes;
  WORD cbSizeVft;
  WORD cbAlignment;
  WORD wTypeFlags;
  WORD wMajorVerNum;
  WORD wMinorVerNum;
  TYPEDESC tdescAlias;
  IDLDESC idldescType;
  }
TYPEATTR;

typedef struct tagTYPEATTR *LPTYPEATTR;

#endif
typedef struct tagDISPPARAMS
  {
  VARIANTARG *rgvarg;
  DISPID *rgdispidNamedArgs;
  UINT cArgs;
  UINT cNamedArgs;
  }
DISPPARAMS;

typedef struct tagRemEXCEPINFO
  {
  WORD wCode;
  WORD wReserved;
  DWORD dwHelpContext;
  DWORD scode;
  DWORD cSource;
  DWORD cDescription;
  DWORD cHelpFile;
  OLECHAR strings[1];
  }
RemEXCEPINFO;

#ifndef _tagEXCEPINFO_DEFINED
#define _tagEXCEPINFO_DEFINED
#define _EXCEPINFO_DEFINED
#define _LPEXCEPINFO_DEFINED
typedef struct tagEXCEPINFO
  {
  WORD wCode;
  WORD wReserved;
  BSTR bstrSource;
  BSTR bstrDescription;
  BSTR bstrHelpFile;
  DWORD dwHelpContext;
  void *pvReserved;
  HRESULT (__stdcall *pfnDeferredFillIn)(
	struct tagEXCEPINFO *__MIDL_0006)  ;
  SCODE scode;
  }
EXCEPINFO;
void __RPC_API EXCEPINFO_to_xmit (EXCEPINFO __RPC_FAR *, RemEXCEPINFO __RPC_FAR * __RPC_FAR *);
void __RPC_API EXCEPINFO_from_xmit (RemEXCEPINFO __RPC_FAR *, EXCEPINFO __RPC_FAR *);
void __RPC_API EXCEPINFO_free_inst (EXCEPINFO __RPC_FAR *);
void __RPC_API EXCEPINFO_free_xmit (RemEXCEPINFO __RPC_FAR *);

typedef struct tagEXCEPINFO *LPEXCEPINFO;
void __RPC_API LPEXCEPINFO_to_xmit (LPEXCEPINFO __RPC_FAR *, RemEXCEPINFO __RPC_FAR * __RPC_FAR *);
void __RPC_API LPEXCEPINFO_from_xmit (RemEXCEPINFO __RPC_FAR *, LPEXCEPINFO __RPC_FAR *);
void __RPC_API LPEXCEPINFO_free_inst (LPEXCEPINFO __RPC_FAR *);
void __RPC_API LPEXCEPINFO_free_xmit (RemEXCEPINFO __RPC_FAR *);

#endif
typedef enum tagCALLCONV
  {
	CC_CDECL = 1,
	CC_MSCPASCAL = 2,
	CC_PASCAL = 2,
	CC_MACPASCAL = 3,
	CC_STDCALL = 4,
	CC_RESERVED = 5,
	CC_SYSCALL = 6,
	CC_MPWCDECL = 7,
	CC_MPWPASCAL = 8,
	CC_MAX = 9
  }
CALLCONV;
void __RPC_API CALLCONV_to_xmit (CALLCONV __RPC_FAR *, long __RPC_FAR * __RPC_FAR *);
void __RPC_API CALLCONV_from_xmit (long __RPC_FAR *, CALLCONV __RPC_FAR *);
void __RPC_API CALLCONV_free_inst (CALLCONV __RPC_FAR *);
void __RPC_API CALLCONV_free_xmit (long __RPC_FAR *);

#define CALLCONV_to_xmit(pEnum, ppLong) *(ppLong) = (long *) (pEnum)
#define CALLCONV_from_xmit(pLong, pEnum) *(pEnum) = (CALLCONV) *(pLong)
#define CALLCONV_free_inst(pEnum) 
#define CALLCONV_free_xmit(pLong) 
typedef enum tagFUNCKIND
  {
	FUNC_VIRTUAL = 0,
	FUNC_PUREVIRTUAL = 1,
	FUNC_NONVIRTUAL = 2,
	FUNC_STATIC = 3,
	FUNC_DISPATCH = 4
  }
FUNCKIND;
void __RPC_API FUNCKIND_to_xmit (FUNCKIND __RPC_FAR *, long __RPC_FAR * __RPC_FAR *);
void __RPC_API FUNCKIND_from_xmit (long __RPC_FAR *, FUNCKIND __RPC_FAR *);
void __RPC_API FUNCKIND_free_inst (FUNCKIND __RPC_FAR *);
void __RPC_API FUNCKIND_free_xmit (long __RPC_FAR *);

#define FUNCKIND_to_xmit(pEnum, ppLong) *(ppLong) = (long *) (pEnum)
#define FUNCKIND_from_xmit(pLong, pEnum) *(pEnum) = (FUNCKIND) *(pLong)
#define FUNCKIND_free_inst(pEnum) 
#define FUNCKIND_free_xmit(pLong) 
typedef enum tagINVOKEKIND
  {
	INVOKE_FUNC = 1,
	INVOKE_PROPERTYGET = 2,
	INVOKE_PROPERTYPUT = 4,
	INVOKE_PROPERTYPUTREF = 8
  }
INVOKEKIND;
void __RPC_API INVOKEKIND_to_xmit (INVOKEKIND __RPC_FAR *, long __RPC_FAR * __RPC_FAR *);
void __RPC_API INVOKEKIND_from_xmit (long __RPC_FAR *, INVOKEKIND __RPC_FAR *);
void __RPC_API INVOKEKIND_free_inst (INVOKEKIND __RPC_FAR *);
void __RPC_API INVOKEKIND_free_xmit (long __RPC_FAR *);

#define INVOKEKIND_to_xmit(pEnum, ppLong) *(ppLong) = (long *) (pEnum)
#define INVOKEKIND_from_xmit(pLong, pEnum) *(pEnum) = (INVOKEKIND) *(pLong)
#define INVOKEKIND_free_inst(pEnum) 
#define INVOKEKIND_free_xmit(pLong) 
typedef struct tagFUNCDESC
  {
  MEMBERID memid;
  SCODE *lprgscode;
  ELEMDESC *lprgelemdescParam;
  FUNCKIND funckind;
  INVOKEKIND invkind;
  CALLCONV callconv;
  SHORT cParams;
  SHORT cParamsOpt;
  SHORT oVft;
  SHORT cScodes;
  ELEMDESC elemdescFunc;
  WORD wFuncFlags;
  }
FUNCDESC;

typedef struct tagFUNCDESC *LPFUNCDESC;

typedef enum tagVARKIND
  {
	VAR_PERINSTANCE = 0,
	VAR_STATIC = 1,
	VAR_CONST = 2,
	VAR_DISPATCH = 3
  }
VARKIND;
void __RPC_API VARKIND_to_xmit (VARKIND __RPC_FAR *, long __RPC_FAR * __RPC_FAR *);
void __RPC_API VARKIND_from_xmit (long __RPC_FAR *, VARKIND __RPC_FAR *);
void __RPC_API VARKIND_free_inst (VARKIND __RPC_FAR *);
void __RPC_API VARKIND_free_xmit (long __RPC_FAR *);

#define VARKIND_to_xmit(pEnum, ppLong) *(ppLong) = (long *) (pEnum)
#define VARKIND_from_xmit(pLong, pEnum) *(pEnum) = (VARKIND) *(pLong)
#define VARKIND_free_inst(pEnum) 
#define VARKIND_free_xmit(pLong) 
#define IMPLTYPEFLAG_FDEFAULT	0x1

#define IMPLTYPEFLAG_FSOURCE	0x2

#define IMPLTYPEFLAG_FRESTRICTED	0x4

typedef struct tagVARDESC
  {
  MEMBERID memid;
  LPOLESTR lpstrSchema;
union
    {
    ULONG oInst;
    VARIANT *lpvarValue;
    }
  ;
  ELEMDESC elemdescVar;
  WORD wVarFlags;
  VARKIND varkind;
  }
VARDESC;

union __MIDL_RemVariant_0003
  {
  ULONG oInst;
  VARIANT *lpvarValue;
  }
;

typedef struct tagVARDESC *LPVARDESC;

#ifndef _tagTYPEFLAGS_DEFINED
#define _tagTYPEFLAGS_DEFINED
#define _TYPEFLAGS_DEFINED
typedef enum tagTYPEFLAGS
  {
	TYPEFLAG_FAPPOBJECT = 1,
	TYPEFLAG_FCANCREATE = 2,
	TYPEFLAG_FLICENSED = 4
  }
TYPEFLAGS;

#endif
#ifndef _tagFUNCFLAGS_DEFINED
#define _tagFUNCFLAGS_DEFINED
#define _FUNCFLAGS_DEFINED
typedef enum tagFUNCFLAGS
  {
	FUNCFLAG_FRESTRICTED = 1,
	FUNCFLAG_FSOURCE = 2,
	FUNCFLAG_FBINDABLE = 4,
	FUNCFLAG_FREQUESTEDIT = 8,
	FUNCFLAG_FDISPLAYBIND = 16,
	FUNCFLAG_FDEFAULTBIND = 32
  }
FUNCFLAGS;

#endif
#ifndef _tagVARFLAGS_DEFINED
#define _tagVARFLAGS_DEFINED
#define _VARFLAGS_DEFINED
typedef enum tagVARFLAGS
  {
	VARFLAG_FREADONLY = 1,
	VARFLAG_FSOURCE = 2,
	VARFLAG_FBINDABLE = 4,
	VARFLAG_FREQUESTEDIT = 8,
	VARFLAG_FDISPLAYBIND = 16,
	VARFLAG_FDEFAULTBIND = 32
  }
VARFLAGS;

#endif

#if !defined(IMPORT_USED_MULTIPLE) && !defined(IMPORT_USED_SINGLE)

/* routine that sizes node for struct tagXBSTR */
void _sns_tagXBSTR (XBSTR  *, PRPC_MESSAGE);

/* routine that puts node for struct tagXBSTR */
void _pns_tagXBSTR (XBSTR  *, PRPC_MESSAGE);

/* routine that gets node for struct tagXBSTR */
void _gns_tagXBSTR (XBSTR  *, PRPC_MESSAGE, unsigned long);

/* routine that gets graph for struct tagXBSTR */
void _ggs_tagXBSTR (XBSTR  *, unsigned char **, PRPC_MESSAGE);

/* routine that allocates node for struct tagXBSTR */
void _ans_tagXBSTR(PRPC_MESSAGE, unsigned long);

/* routine that allocates graph for struct tagXBSTR */
void _ags_tagXBSTR(unsigned char **, PRPC_MESSAGE, unsigned long);

/* routine that sizes graph for struct tagBLOB */
void _sgs_tagBLOB (BLOB  *, PRPC_MESSAGE);

/* routine that puts graph for struct tagBLOB */
void _pgs_tagBLOB (BLOB  *, PRPC_MESSAGE);

/* routine that gets graph for struct tagBLOB */
void _ggs_tagBLOB (BLOB  *, unsigned char **, PRPC_MESSAGE);

/* routine that allocates graph for struct tagBLOB */
void _ags_tagBLOB(unsigned char **, PRPC_MESSAGE);

/* routine that frees graph for struct tagBLOB */
void _fgs_tagBLOB (BLOB  *);

/* routine that sizes graph for struct tagCLIPDATA */
void _sgs_tagCLIPDATA (CLIPDATA  *, PRPC_MESSAGE);

/* routine that puts graph for struct tagCLIPDATA */
void _pgs_tagCLIPDATA (CLIPDATA  *, PRPC_MESSAGE);

/* routine that gets graph for struct tagCLIPDATA */
void _ggs_tagCLIPDATA (CLIPDATA  *, unsigned char **, PRPC_MESSAGE);

/* routine that allocates graph for struct tagCLIPDATA */
void _ags_tagCLIPDATA(unsigned char **, PRPC_MESSAGE);

/* routine that frees graph for struct tagCLIPDATA */
void _fgs_tagCLIPDATA (CLIPDATA  *);

/* routine that sizes node for struct tagSAFEARRAY */
void _sns_tagSAFEARRAY (SAFEARRAY  *, PRPC_MESSAGE);

/* routine that sizes graph for struct tagSAFEARRAY */
void _sgs_tagSAFEARRAY (SAFEARRAY  *, PRPC_MESSAGE);

/* routine that puts node for struct tagSAFEARRAY */
void _pns_tagSAFEARRAY (SAFEARRAY  *, PRPC_MESSAGE);

/* routine that puts graph for struct tagSAFEARRAY */
void _pgs_tagSAFEARRAY (SAFEARRAY  *, PRPC_MESSAGE);

/* routine that gets node for struct tagSAFEARRAY */
void _gns_tagSAFEARRAY (SAFEARRAY  *, PRPC_MESSAGE, unsigned long);

/* routine that gets graph for struct tagSAFEARRAY */
void _ggs_tagSAFEARRAY (SAFEARRAY  *, unsigned char **, PRPC_MESSAGE);

/* routine that allocates node for struct tagSAFEARRAY */
void _ans_tagSAFEARRAY(PRPC_MESSAGE, unsigned long);

/* routine that allocates graph for struct tagSAFEARRAY */
void _ags_tagSAFEARRAY(unsigned char **, PRPC_MESSAGE, unsigned long);

/* routine that frees graph for struct tagSAFEARRAY */
void _fgs_tagSAFEARRAY (SAFEARRAY  *);

/* routine that sizes node for union __MIDL_RemVariant_0001 */
void _snu___MIDL_RemVariant_0001 (union __MIDL_RemVariant_0001 *, PRPC_MESSAGE, unsigned short );

/* routine that sizes graph for union __MIDL_RemVariant_0001 */
void _sgu___MIDL_RemVariant_0001 (union __MIDL_RemVariant_0001 *, PRPC_MESSAGE, unsigned short );

/* routine that puts node for union __MIDL_RemVariant_0001 */
void _pnu___MIDL_RemVariant_0001 (union __MIDL_RemVariant_0001 *, PRPC_MESSAGE, unsigned short );

/* routine that puts graph for union __MIDL_RemVariant_0001 */
void _pgu___MIDL_RemVariant_0001 (union __MIDL_RemVariant_0001 *, PRPC_MESSAGE, unsigned short );

/* routine that gets node for union __MIDL_RemVariant_0001 */
void _gnu___MIDL_RemVariant_0001 (union __MIDL_RemVariant_0001 *, PRPC_MESSAGE, unsigned short );

/* routine that gets graph for union __MIDL_RemVariant_0001 */
void _ggu___MIDL_RemVariant_0001 (union __MIDL_RemVariant_0001 *, unsigned char **, PRPC_MESSAGE, unsigned short );

/* routine that allocates node for union __MIDL_RemVariant_0001 */
void _anu___MIDL_RemVariant_0001(PRPC_MESSAGE, unsigned short );

/* routine that allocates graph for union __MIDL_RemVariant_0001 */
void _agu___MIDL_RemVariant_0001(unsigned char **, PRPC_MESSAGE, unsigned short );

/* routine that frees graph for union __MIDL_RemVariant_0001 */
void _fgu___MIDL_RemVariant_0001 (union __MIDL_RemVariant_0001 *, unsigned short );

/* routine that sizes node for struct tagVARIANT */
void _sns_tagVARIANT (VARIANTARG  *, PRPC_MESSAGE);

/* routine that sizes graph for struct tagVARIANT */
void _sgs_tagVARIANT (VARIANTARG  *, PRPC_MESSAGE);

/* routine that puts node for struct tagVARIANT */
void _pns_tagVARIANT (VARIANTARG  *, PRPC_MESSAGE);

/* routine that puts graph for struct tagVARIANT */
void _pgs_tagVARIANT (VARIANTARG  *, PRPC_MESSAGE);

/* routine that gets node for struct tagVARIANT */
void _gns_tagVARIANT (VARIANTARG  *, PRPC_MESSAGE);

/* routine that gets graph for struct tagVARIANT */
void _ggs_tagVARIANT (VARIANTARG  *, unsigned char **, PRPC_MESSAGE);

/* routine that allocates node for struct tagVARIANT */
void _ans_tagVARIANT(PRPC_MESSAGE);

/* routine that allocates graph for struct tagVARIANT */
void _ags_tagVARIANT(unsigned char **, PRPC_MESSAGE);

/* routine that frees graph for struct tagVARIANT */
void _fgs_tagVARIANT (VARIANTARG  *);

/* routine that sizes node for union __MIDL_RemVariant_0002 */
void _snu___MIDL_RemVariant_0002 (union __MIDL_RemVariant_0002 *, PRPC_MESSAGE, short );

/* routine that sizes graph for union __MIDL_RemVariant_0002 */
void _sgu___MIDL_RemVariant_0002 (union __MIDL_RemVariant_0002 *, PRPC_MESSAGE, short );

/* routine that puts node for union __MIDL_RemVariant_0002 */
void _pnu___MIDL_RemVariant_0002 (union __MIDL_RemVariant_0002 *, PRPC_MESSAGE, short );

/* routine that puts graph for union __MIDL_RemVariant_0002 */
void _pgu___MIDL_RemVariant_0002 (union __MIDL_RemVariant_0002 *, PRPC_MESSAGE, short );

/* routine that gets node for union __MIDL_RemVariant_0002 */
void _gnu___MIDL_RemVariant_0002 (union __MIDL_RemVariant_0002 *, PRPC_MESSAGE, short );

/* routine that gets graph for union __MIDL_RemVariant_0002 */
void _ggu___MIDL_RemVariant_0002 (union __MIDL_RemVariant_0002 *, unsigned char **, PRPC_MESSAGE, short );

/* routine that allocates node for union __MIDL_RemVariant_0002 */
void _anu___MIDL_RemVariant_0002(PRPC_MESSAGE, short );

/* routine that allocates graph for union __MIDL_RemVariant_0002 */
void _agu___MIDL_RemVariant_0002(unsigned char **, PRPC_MESSAGE, short );

/* routine that frees graph for union __MIDL_RemVariant_0002 */
void _fgu___MIDL_RemVariant_0002 (union __MIDL_RemVariant_0002 *, short );

/* routine that sizes node for struct tagTYPEDESC */
void _sns_tagTYPEDESC (TYPEDESC  *, PRPC_MESSAGE);

/* routine that sizes graph for struct tagTYPEDESC */
void _sgs_tagTYPEDESC (TYPEDESC  *, PRPC_MESSAGE);

/* routine that puts node for struct tagTYPEDESC */
void _pns_tagTYPEDESC (TYPEDESC  *, PRPC_MESSAGE);

/* routine that puts graph for struct tagTYPEDESC */
void _pgs_tagTYPEDESC (TYPEDESC  *, PRPC_MESSAGE);

/* routine that gets node for struct tagTYPEDESC */
void _gns_tagTYPEDESC (TYPEDESC  *, PRPC_MESSAGE);

/* routine that gets graph for struct tagTYPEDESC */
void _ggs_tagTYPEDESC (TYPEDESC  *, unsigned char **, PRPC_MESSAGE);

/* routine that allocates node for struct tagTYPEDESC */
void _ans_tagTYPEDESC(PRPC_MESSAGE);

/* routine that allocates graph for struct tagTYPEDESC */
void _ags_tagTYPEDESC(unsigned char **, PRPC_MESSAGE);

/* routine that frees graph for struct tagTYPEDESC */
void _fgs_tagTYPEDESC (TYPEDESC  *);

/* routine that sizes node for struct tagARRAYDESC */
void _sns_tagARRAYDESC (ARRAYDESC  *, PRPC_MESSAGE);

/* routine that sizes graph for struct tagARRAYDESC */
void _sgs_tagARRAYDESC (ARRAYDESC  *, PRPC_MESSAGE);

/* routine that puts node for struct tagARRAYDESC */
void _pns_tagARRAYDESC (ARRAYDESC  *, PRPC_MESSAGE);

/* routine that puts graph for struct tagARRAYDESC */
void _pgs_tagARRAYDESC (ARRAYDESC  *, PRPC_MESSAGE);

/* routine that gets node for struct tagARRAYDESC */
void _gns_tagARRAYDESC (ARRAYDESC  *, PRPC_MESSAGE, unsigned long);

/* routine that gets graph for struct tagARRAYDESC */
void _ggs_tagARRAYDESC (ARRAYDESC  *, unsigned char **, PRPC_MESSAGE);

/* routine that allocates node for struct tagARRAYDESC */
void _ans_tagARRAYDESC(PRPC_MESSAGE, unsigned long);

/* routine that allocates graph for struct tagARRAYDESC */
void _ags_tagARRAYDESC(unsigned char **, PRPC_MESSAGE, unsigned long);

/* routine that frees graph for struct tagARRAYDESC */
void _fgs_tagARRAYDESC (ARRAYDESC  *);

/* routine that puts node for struct tagIDLDESC */
void _pns_tagIDLDESC (IDLDESC  *, PRPC_MESSAGE);

/* routine that sizes node for struct tagELEMDESC */
void _sns_tagELEMDESC (ELEMDESC  *, PRPC_MESSAGE);

/* routine that sizes graph for struct tagELEMDESC */
void _sgs_tagELEMDESC (ELEMDESC  *, PRPC_MESSAGE);

/* routine that puts node for struct tagELEMDESC */
void _pns_tagELEMDESC (ELEMDESC  *, PRPC_MESSAGE);

/* routine that puts graph for struct tagELEMDESC */
void _pgs_tagELEMDESC (ELEMDESC  *, PRPC_MESSAGE);

/* routine that gets node for struct tagELEMDESC */
void _gns_tagELEMDESC (ELEMDESC  *, PRPC_MESSAGE);

/* routine that gets graph for struct tagELEMDESC */
void _ggs_tagELEMDESC (ELEMDESC  *, unsigned char **, PRPC_MESSAGE);

/* routine that allocates node for struct tagELEMDESC */
void _ans_tagELEMDESC(PRPC_MESSAGE);

/* routine that allocates graph for struct tagELEMDESC */
void _ags_tagELEMDESC(unsigned char **, PRPC_MESSAGE);

/* routine that frees graph for struct tagELEMDESC */
void _fgs_tagELEMDESC (ELEMDESC  *);

/* routine that sizes node for struct tagTYPEATTR */
void _sns_tagTYPEATTR (TYPEATTR  *, PRPC_MESSAGE);

/* routine that sizes graph for struct tagTYPEATTR */
void _sgs_tagTYPEATTR (TYPEATTR  *, PRPC_MESSAGE);

/* routine that puts node for struct tagTYPEATTR */
void _pns_tagTYPEATTR (TYPEATTR  *, PRPC_MESSAGE);

/* routine that puts graph for struct tagTYPEATTR */
void _pgs_tagTYPEATTR (TYPEATTR  *, PRPC_MESSAGE);

/* routine that gets node for struct tagTYPEATTR */
void _gns_tagTYPEATTR (TYPEATTR  *, PRPC_MESSAGE);

/* routine that gets graph for struct tagTYPEATTR */
void _ggs_tagTYPEATTR (TYPEATTR  *, unsigned char **, PRPC_MESSAGE);

/* routine that allocates node for struct tagTYPEATTR */
void _ans_tagTYPEATTR(PRPC_MESSAGE);

/* routine that allocates graph for struct tagTYPEATTR */
void _ags_tagTYPEATTR(unsigned char **, PRPC_MESSAGE);

/* routine that frees graph for struct tagTYPEATTR */
void _fgs_tagTYPEATTR (TYPEATTR  *);

/* routine that sizes node for struct tagDISPPARAMS */
void _sns_tagDISPPARAMS (DISPPARAMS  *, PRPC_MESSAGE);

/* routine that sizes graph for struct tagDISPPARAMS */
void _sgs_tagDISPPARAMS (DISPPARAMS  *, PRPC_MESSAGE);

/* routine that puts node for struct tagDISPPARAMS */
void _pns_tagDISPPARAMS (DISPPARAMS  *, PRPC_MESSAGE);

/* routine that puts graph for struct tagDISPPARAMS */
void _pgs_tagDISPPARAMS (DISPPARAMS  *, PRPC_MESSAGE);

/* routine that gets node for struct tagDISPPARAMS */
void _gns_tagDISPPARAMS (DISPPARAMS  *, PRPC_MESSAGE);

/* routine that gets graph for struct tagDISPPARAMS */
void _ggs_tagDISPPARAMS (DISPPARAMS  *, unsigned char **, PRPC_MESSAGE);

/* routine that allocates graph for struct tagDISPPARAMS */
void _ags_tagDISPPARAMS(unsigned char **, PRPC_MESSAGE);

/* routine that frees graph for struct tagDISPPARAMS */
void _fgs_tagDISPPARAMS (DISPPARAMS  *);

/* routine that sizes node for struct tagRemEXCEPINFO */
void _sns_tagRemEXCEPINFO (RemEXCEPINFO  *, PRPC_MESSAGE);

/* routine that puts node for struct tagRemEXCEPINFO */
void _pns_tagRemEXCEPINFO (RemEXCEPINFO  *, PRPC_MESSAGE);

/* routine that gets node for struct tagRemEXCEPINFO */
void _gns_tagRemEXCEPINFO (RemEXCEPINFO  *, PRPC_MESSAGE, unsigned long);

/* routine that gets graph for struct tagRemEXCEPINFO */
void _ggs_tagRemEXCEPINFO (RemEXCEPINFO  *, unsigned char **, PRPC_MESSAGE);

/* routine that allocates node for struct tagRemEXCEPINFO */
void _ans_tagRemEXCEPINFO(PRPC_MESSAGE, unsigned long);

/* routine that allocates graph for struct tagRemEXCEPINFO */
void _ags_tagRemEXCEPINFO(unsigned char **, PRPC_MESSAGE, unsigned long);

/* routine that sizes node for struct tagFUNCDESC */
void _sns_tagFUNCDESC (FUNCDESC  *, PRPC_MESSAGE);

/* routine that sizes graph for struct tagFUNCDESC */
void _sgs_tagFUNCDESC (FUNCDESC  *, PRPC_MESSAGE);

/* routine that puts node for struct tagFUNCDESC */
void _pns_tagFUNCDESC (FUNCDESC  *, PRPC_MESSAGE);

/* routine that puts graph for struct tagFUNCDESC */
void _pgs_tagFUNCDESC (FUNCDESC  *, PRPC_MESSAGE);

/* routine that gets node for struct tagFUNCDESC */
void _gns_tagFUNCDESC (FUNCDESC  *, PRPC_MESSAGE);

/* routine that gets graph for struct tagFUNCDESC */
void _ggs_tagFUNCDESC (FUNCDESC  *, unsigned char **, PRPC_MESSAGE);

/* routine that allocates node for struct tagFUNCDESC */
void _ans_tagFUNCDESC(PRPC_MESSAGE);

/* routine that allocates graph for struct tagFUNCDESC */
void _ags_tagFUNCDESC(unsigned char **, PRPC_MESSAGE);

/* routine that frees graph for struct tagFUNCDESC */
void _fgs_tagFUNCDESC (FUNCDESC  *);

/* routine that sizes node for union __MIDL_RemVariant_0003 */
void _snu___MIDL_RemVariant_0003 (union __MIDL_RemVariant_0003 *, PRPC_MESSAGE, unsigned short );

/* routine that sizes graph for union __MIDL_RemVariant_0003 */
void _sgu___MIDL_RemVariant_0003 (union __MIDL_RemVariant_0003 *, PRPC_MESSAGE, unsigned short );

/* routine that puts node for union __MIDL_RemVariant_0003 */
void _pnu___MIDL_RemVariant_0003 (union __MIDL_RemVariant_0003 *, PRPC_MESSAGE, unsigned short );

/* routine that puts graph for union __MIDL_RemVariant_0003 */
void _pgu___MIDL_RemVariant_0003 (union __MIDL_RemVariant_0003 *, PRPC_MESSAGE, unsigned short );

/* routine that gets node for union __MIDL_RemVariant_0003 */
void _gnu___MIDL_RemVariant_0003 (union __MIDL_RemVariant_0003 *, PRPC_MESSAGE, unsigned short );

/* routine that gets graph for union __MIDL_RemVariant_0003 */
void _ggu___MIDL_RemVariant_0003 (union __MIDL_RemVariant_0003 *, unsigned char **, PRPC_MESSAGE, unsigned short );

/* routine that allocates node for union __MIDL_RemVariant_0003 */
void _anu___MIDL_RemVariant_0003(PRPC_MESSAGE, unsigned short );

/* routine that allocates graph for union __MIDL_RemVariant_0003 */
void _agu___MIDL_RemVariant_0003(unsigned char **, PRPC_MESSAGE, unsigned short );

/* routine that frees graph for union __MIDL_RemVariant_0003 */
void _fgu___MIDL_RemVariant_0003 (union __MIDL_RemVariant_0003 *, unsigned short );

/* routine that sizes node for struct tagVARDESC */
void _sns_tagVARDESC (VARDESC  *, PRPC_MESSAGE);

/* routine that sizes graph for struct tagVARDESC */
void _sgs_tagVARDESC (VARDESC  *, PRPC_MESSAGE);

/* routine that puts node for struct tagVARDESC */
void _pns_tagVARDESC (VARDESC  *, PRPC_MESSAGE);

/* routine that puts graph for struct tagVARDESC */
void _pgs_tagVARDESC (VARDESC  *, PRPC_MESSAGE);

/* routine that gets node for struct tagVARDESC */
void _gns_tagVARDESC (VARDESC  *, PRPC_MESSAGE);

/* routine that gets graph for struct tagVARDESC */
void _ggs_tagVARDESC (VARDESC  *, unsigned char **, PRPC_MESSAGE);

/* routine that allocates node for struct tagVARDESC */
void _ans_tagVARDESC(PRPC_MESSAGE);

/* routine that allocates graph for struct tagVARDESC */
void _ags_tagVARDESC(unsigned char **, PRPC_MESSAGE);

/* routine that frees graph for struct tagVARDESC */
void _fgs_tagVARDESC (VARDESC  *);

#endif /*!defined(IMPORT_USED_MULTIPLE) && !defined(IMPORT_USED_SINGLE)*/


#ifdef __cplusplus
}
#endif

#endif

// Forward declarations for typedefs in this file
typedef interface ITypeLib ITypeLib;
typedef interface IDispatch IDispatch;
typedef interface ITypeComp ITypeComp;
typedef interface ITypeInfo ITypeInfo;
typedef interface ICreateTypeInfo ICreateTypeInfo;
typedef interface ICreateTypeLib ICreateTypeLib;
typedef interface IEnumVARIANT IEnumVARIANT;

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for ITypeLib
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __ITypeLib__
#define __ITypeLib__

/* Forward declaration */
typedef interface ITypeLib ITypeLib;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


typedef enum tagSYSKIND
  {
	SYS_WIN16 = 0,
	SYS_WIN32 = 1,
	SYS_MAC = 2
  }
SYSKIND;
void __RPC_API SYSKIND_to_xmit (SYSKIND __RPC_FAR *, long __RPC_FAR * __RPC_FAR *);
void __RPC_API SYSKIND_from_xmit (long __RPC_FAR *, SYSKIND __RPC_FAR *);
void __RPC_API SYSKIND_free_inst (SYSKIND __RPC_FAR *);
void __RPC_API SYSKIND_free_xmit (long __RPC_FAR *);


typedef enum tagLIBFLAGS
  {
	LIBFLAG_FRESTRICTED = 1
  }
LIBFLAGS;
void __RPC_API LIBFLAGS_to_xmit (LIBFLAGS __RPC_FAR *, long __RPC_FAR * __RPC_FAR *);
void __RPC_API LIBFLAGS_from_xmit (long __RPC_FAR *, LIBFLAGS __RPC_FAR *);
void __RPC_API LIBFLAGS_free_inst (LIBFLAGS __RPC_FAR *);
void __RPC_API LIBFLAGS_free_xmit (long __RPC_FAR *);


#define SYSKIND_to_xmit(pEnum, ppLong) *(ppLong) = (long *) (pEnum)

#define SYSKIND_from_xmit(pLong, pEnum) *(pEnum) = (SYSKIND) *(pLong)

#define SYSKIND_free_inst(pEnum) 

#define SYSKIND_free_xmit(pLong) 

typedef ITypeLib *LPTYPELIB;


typedef struct tagTLIBATTR
  {
  GUID guid;
  LCID lcid;
  SYSKIND syskind;
  WORD wMajorVerNum;
  WORD wMinorVerNum;
  WORD wLibFlags;
  }
TLIBATTR;


typedef TLIBATTR *LPTLIBATTR;


EXTERN_C const IID IID_ITypeLib;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface ITypeLib : public IUnknown
{
public:
    virtual UINT STDMETHODCALLTYPE GetTypeInfoCount
    (
        void
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetTypeInfo
    (
	UINT index,
	ITypeInfo **ppitinfo
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetTypeInfoType
    (
	UINT index,
	TYPEKIND *ptkind
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetTypeInfoOfGuid
    (
	REFGUID guid,
	ITypeInfo **pptinfo
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetLibAttr
    (
	TLIBATTR **pptlibattr
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetTypeComp
    (
	ITypeComp **pptcomp
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetDocumentation
    (
	INT index,
	BSTR *pbstrName,
	BSTR *pbstrDocString,
	DWORD *pdwHelpContext,
	BSTR *pbstrHelpFile
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE IsName
    (
	LPOLESTR szNameBuf,
	ULONG lHashVal,
	BOOL *pfName
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE FindName
    (
	LPOLESTR szNameBuf,
	ULONG lHashVal,
	ITypeInfo **rgptinfo,
	MEMBERID *rgmemid,
	USHORT *pcFound
    ) = 0;
    
    virtual void STDMETHODCALLTYPE ReleaseTLibAttr
    (
	TLIBATTR *ptlibattr
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct ITypeLibVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        ITypeLib FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        ITypeLib FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        ITypeLib FAR * This
    );
    
    UINT (STDMETHODCALLTYPE FAR *GetTypeInfoCount)
    (
        ITypeLib FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetTypeInfo)
    (
        ITypeLib FAR * This,
	UINT index,
	ITypeInfo **ppitinfo
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetTypeInfoType)
    (
        ITypeLib FAR * This,
	UINT index,
	TYPEKIND *ptkind
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetTypeInfoOfGuid)
    (
        ITypeLib FAR * This,
	REFGUID guid,
	ITypeInfo **pptinfo
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetLibAttr)
    (
        ITypeLib FAR * This,
	TLIBATTR **pptlibattr
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetTypeComp)
    (
        ITypeLib FAR * This,
	ITypeComp **pptcomp
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetDocumentation)
    (
        ITypeLib FAR * This,
	INT index,
	BSTR *pbstrName,
	BSTR *pbstrDocString,
	DWORD *pdwHelpContext,
	BSTR *pbstrHelpFile
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *IsName)
    (
        ITypeLib FAR * This,
	LPOLESTR szNameBuf,
	ULONG lHashVal,
	BOOL *pfName
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *FindName)
    (
        ITypeLib FAR * This,
	LPOLESTR szNameBuf,
	ULONG lHashVal,
	ITypeInfo **rgptinfo,
	MEMBERID *rgmemid,
	USHORT *pcFound
    );
    
    void (STDMETHODCALLTYPE FAR *ReleaseTLibAttr)
    (
        ITypeLib FAR * This,
	TLIBATTR *ptlibattr
    );
    
} ITypeLibVtbl;

interface ITypeLib
{
    ITypeLibVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__ITypeLib__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IDispatch
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IDispatch__
#define __IDispatch__

/* Forward declaration */
typedef interface IDispatch IDispatch;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


typedef IDispatch *LPDISPATCH;


/* DISPID reserved to indicate an "unknown" name */

/* only reserved for data members (properties); reused as a method dispid below */

#define DISPID_UNKNOWN	 - 1


/* DISPID reserved for the "value" property */

#define DISPID_VALUE	0


/* The following DISPID is reserved to indicate the param

 * that is the right-hand-side (or "put" value) of a PropertyPut

 */

#define DISPID_PROPERTYPUT	 - 3


/* DISPID reserved for the standard "NewEnum" method */

#define DISPID_NEWENUM	 - 4


/* DISPID reserved for the standard "Evaluate" method */

#define DISPID_EVALUATE	 - 5


EXTERN_C const IID IID_IDispatch;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IDispatch : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE GetTypeInfoCount
    (
	UINT *pctinfo
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetTypeInfo
    (
	UINT itinfo,
	LCID lcid,
	ITypeInfo **pptinfo
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetIDsOfNames
    (
	REFIID riid,
	LPOLESTR *rgszNames,
	UINT cNames,
	LCID lcid,
	DISPID *rgdispid
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Invoke
    (
	DISPID dispidMember,
	REFIID riid,
	LCID lcid,
	WORD wFlags,
	DISPPARAMS *pdispparams,
	VARIANT *pvarResult,
	EXCEPINFO *pexcepinfo,
	UINT *puArgErr
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IDispatchVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IDispatch FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IDispatch FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IDispatch FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetTypeInfoCount)
    (
        IDispatch FAR * This,
	UINT *pctinfo
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetTypeInfo)
    (
        IDispatch FAR * This,
	UINT itinfo,
	LCID lcid,
	ITypeInfo **pptinfo
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetIDsOfNames)
    (
        IDispatch FAR * This,
	REFIID riid,
	LPOLESTR *rgszNames,
	UINT cNames,
	LCID lcid,
	DISPID *rgdispid
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Invoke)
    (
        IDispatch FAR * This,
	DISPID dispidMember,
	REFIID riid,
	LCID lcid,
	WORD wFlags,
	DISPPARAMS *pdispparams,
	VARIANT *pvarResult,
	EXCEPINFO *pexcepinfo,
	UINT *puArgErr
    );
    
} IDispatchVtbl;

interface IDispatch
{
    IDispatchVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IDispatch__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for ITypeComp
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __ITypeComp__
#define __ITypeComp__

/* Forward declaration */
typedef interface ITypeComp ITypeComp;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


typedef ITypeComp *LPTYPECOMP;


typedef enum tagDESCKIND
  {
	DESCKIND_NONE = 0,
	DESCKIND_FUNCDESC = 1,
	DESCKIND_VARDESC = 2,
	DESCKIND_TYPECOMP = 3,
	DESCKIND_IMPLICITAPPOBJ = 4,
	DESCKIND_MAX = 5
  }
DESCKIND;
void __RPC_API DESCKIND_to_xmit (DESCKIND __RPC_FAR *, long __RPC_FAR * __RPC_FAR *);
void __RPC_API DESCKIND_from_xmit (long __RPC_FAR *, DESCKIND __RPC_FAR *);
void __RPC_API DESCKIND_free_inst (DESCKIND __RPC_FAR *);
void __RPC_API DESCKIND_free_xmit (long __RPC_FAR *);


#define DESCKIND_to_xmit(pEnum, ppLong) *(ppLong) = (long *) (pEnum)

#define DESCKIND_from_xmit(pLong, pEnum) *(pEnum) = (DESCKIND) *(pLong)

#define DESCKIND_free_inst(pEnum) 

#define DESCKIND_free_xmit(pLong) 

union tagBINDPTR
  {
  FUNCDESC *lpfuncdesc;
  VARDESC *lpvardesc;
  ITypeComp *lptcomp;
  }
;


typedef union tagBINDPTR BINDPTR;


typedef BINDPTR *LPBINDPTR;


EXTERN_C const IID IID_ITypeComp;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface ITypeComp : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Bind
    (
	LPOLESTR szName,
	ULONG lHashVal,
	WORD fFlags,
	ITypeInfo **pptinfo,
	DESCKIND *pdesckind,
	BINDPTR *pbindptr
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE BindType
    (
	LPOLESTR szName,
	ULONG lHashVal,
	ITypeInfo **pptinfo,
	ITypeComp **pptcomp
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct ITypeCompVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        ITypeComp FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        ITypeComp FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        ITypeComp FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Bind)
    (
        ITypeComp FAR * This,
	LPOLESTR szName,
	ULONG lHashVal,
	WORD fFlags,
	ITypeInfo **pptinfo,
	DESCKIND *pdesckind,
	BINDPTR *pbindptr
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *BindType)
    (
        ITypeComp FAR * This,
	LPOLESTR szName,
	ULONG lHashVal,
	ITypeInfo **pptinfo,
	ITypeComp **pptcomp
    );
    
} ITypeCompVtbl;

interface ITypeComp
{
    ITypeCompVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__ITypeComp__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for ITypeInfo
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __ITypeInfo__
#define __ITypeInfo__

/* Forward declaration */
typedef interface ITypeInfo ITypeInfo;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


typedef ITypeInfo *LPTYPEINFO;


EXTERN_C const IID IID_ITypeInfo;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface ITypeInfo : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE GetTypeAttr
    (
	TYPEATTR **pptypeattr
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetTypeComp
    (
	ITypeComp **pptcomp
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetFuncDesc
    (
	UINT index,
	FUNCDESC **pppfuncdesc
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetVarDesc
    (
	UINT index,
	VARDESC **ppvardesc
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetNames
    (
	MEMBERID memid,
	BSTR *rgbstrNames,
	UINT cMaxNames,
	UINT *pcNames
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetRefTypeOfImplType
    (
	UINT index,
	HREFTYPE *hpreftype
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetImplTypeFlags
    (
	UINT index,
	INT *pimpltypeflags
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetIDsOfNames
    (
	OLECHAR **rglpszNames,
	UINT cNames,
	MEMBERID *rgmemid
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Invoke
    (
	void *pvInstance,
	MEMBERID memid,
	WORD wFlags,
	DISPPARAMS *pdispparams,
	VARIANT *pvarResult,
	EXCEPINFO *pexcepinfo,
	UINT *puArgErr
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetDocumentation
    (
	MEMBERID memid,
	BSTR *pbstrName,
	BSTR *pbstrDocString,
	DWORD *pdwHelpContext,
	BSTR *pbstrHelpFile
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetDllEntry
    (
	MEMBERID memid,
	INVOKEKIND invkind,
	BSTR *pbstrDllName,
	BSTR *pbstrName,
	WORD *pwOrdinal
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetRefTypeInfo
    (
	HREFTYPE hreftype,
	ITypeInfo **pptinfo
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE AddressOfMember
    (
	MEMBERID memid,
	INVOKEKIND invkind,
	void **ppv
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE CreateInstance
    (
	IUnknown *puncOuter,
	REFIID riid,
	void **ppvObj
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetMops
    (
	MEMBERID memid,
	BSTR *pbstrMops
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetContainingTypeLib
    (
	ITypeLib **pptlib,
	UINT *pindex
    ) = 0;
    
    virtual void STDMETHODCALLTYPE ReleaseTypeAttr
    (
	TYPEATTR *ptypeattr
    ) = 0;
    
    virtual void STDMETHODCALLTYPE ReleaseFuncDesc
    (
	FUNCDESC *pfuncdesc
    ) = 0;
    
    virtual void STDMETHODCALLTYPE ReleaseVarDesc
    (
	VARDESC *pvardesc
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct ITypeInfoVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        ITypeInfo FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        ITypeInfo FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        ITypeInfo FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetTypeAttr)
    (
        ITypeInfo FAR * This,
	TYPEATTR **pptypeattr
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetTypeComp)
    (
        ITypeInfo FAR * This,
	ITypeComp **pptcomp
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetFuncDesc)
    (
        ITypeInfo FAR * This,
	UINT index,
	FUNCDESC **pppfuncdesc
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetVarDesc)
    (
        ITypeInfo FAR * This,
	UINT index,
	VARDESC **ppvardesc
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetNames)
    (
        ITypeInfo FAR * This,
	MEMBERID memid,
	BSTR *rgbstrNames,
	UINT cMaxNames,
	UINT *pcNames
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetRefTypeOfImplType)
    (
        ITypeInfo FAR * This,
	UINT index,
	HREFTYPE *hpreftype
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetImplTypeFlags)
    (
        ITypeInfo FAR * This,
	UINT index,
	INT *pimpltypeflags
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetIDsOfNames)
    (
        ITypeInfo FAR * This,
	OLECHAR **rglpszNames,
	UINT cNames,
	MEMBERID *rgmemid
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Invoke)
    (
        ITypeInfo FAR * This,
	void *pvInstance,
	MEMBERID memid,
	WORD wFlags,
	DISPPARAMS *pdispparams,
	VARIANT *pvarResult,
	EXCEPINFO *pexcepinfo,
	UINT *puArgErr
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetDocumentation)
    (
        ITypeInfo FAR * This,
	MEMBERID memid,
	BSTR *pbstrName,
	BSTR *pbstrDocString,
	DWORD *pdwHelpContext,
	BSTR *pbstrHelpFile
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetDllEntry)
    (
        ITypeInfo FAR * This,
	MEMBERID memid,
	INVOKEKIND invkind,
	BSTR *pbstrDllName,
	BSTR *pbstrName,
	WORD *pwOrdinal
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetRefTypeInfo)
    (
        ITypeInfo FAR * This,
	HREFTYPE hreftype,
	ITypeInfo **pptinfo
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *AddressOfMember)
    (
        ITypeInfo FAR * This,
	MEMBERID memid,
	INVOKEKIND invkind,
	void **ppv
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *CreateInstance)
    (
        ITypeInfo FAR * This,
	IUnknown *puncOuter,
	REFIID riid,
	void **ppvObj
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetMops)
    (
        ITypeInfo FAR * This,
	MEMBERID memid,
	BSTR *pbstrMops
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *GetContainingTypeLib)
    (
        ITypeInfo FAR * This,
	ITypeLib **pptlib,
	UINT *pindex
    );
    
    void (STDMETHODCALLTYPE FAR *ReleaseTypeAttr)
    (
        ITypeInfo FAR * This,
	TYPEATTR *ptypeattr
    );
    
    void (STDMETHODCALLTYPE FAR *ReleaseFuncDesc)
    (
        ITypeInfo FAR * This,
	FUNCDESC *pfuncdesc
    );
    
    void (STDMETHODCALLTYPE FAR *ReleaseVarDesc)
    (
        ITypeInfo FAR * This,
	VARDESC *pvardesc
    );
    
} ITypeInfoVtbl;

interface ITypeInfo
{
    ITypeInfoVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__ITypeInfo__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for ICreateTypeInfo
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __ICreateTypeInfo__
#define __ICreateTypeInfo__

/* Forward declaration */
typedef interface ICreateTypeInfo ICreateTypeInfo;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


typedef ICreateTypeInfo *LPCREATETYPEINFO;


EXTERN_C const IID IID_ICreateTypeInfo;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface ICreateTypeInfo : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE SetGuid
    (
	REFGUID guid
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetTypeFlags
    (
	UINT uTypeFlags
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetDocString
    (
	LPOLESTR lpstrDoc
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetHelpContext
    (
	DWORD dwHelpContext
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetVersion
    (
	WORD wMajorVerNum,
	WORD wMinorVerNum
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE AddRefTypeInfo
    (
	ITypeInfo *ptinfo,
	HREFTYPE *phreftype
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE AddFuncDesc
    (
	UINT index,
	FUNCDESC *pfuncdesc
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE AddImplType
    (
	UINT index,
	HREFTYPE hreftype
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetImplTypeFlags
    (
	UINT index,
	INT impltypeflags
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetAlignment
    (
	WORD cbAlignment
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetSchema
    (
	LPOLESTR lpstrSchema
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE AddVarDesc
    (
	UINT index,
	VARDESC *pvardesc
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetFuncAndParamNames
    (
	UINT index,
	LPOLESTR *rgszNames,
	UINT cNames
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetVarName
    (
	UINT index,
	LPOLESTR szName
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetTypeDescAlias
    (
	TYPEDESC *ptdescAlias
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE DefineFuncAsDllEntry
    (
	UINT index,
	LPOLESTR szDllName,
	LPOLESTR szProcName
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetFuncDocString
    (
	UINT index,
	LPOLESTR szDocString
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetVarDocString
    (
	UINT index,
	LPOLESTR szDocString
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetFuncHelpContext
    (
	UINT index,
	DWORD dwHelpContext
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetVarHelpContext
    (
	UINT index,
	DWORD dwHelpContext
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetMops
    (
	UINT index,
	BSTR bstrMops
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetTypeIdldesc
    (
	IDLDESC *pidldesc
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE LayOut
    (
        void
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct ICreateTypeInfoVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        ICreateTypeInfo FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        ICreateTypeInfo FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        ICreateTypeInfo FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetGuid)
    (
        ICreateTypeInfo FAR * This,
	REFGUID guid
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetTypeFlags)
    (
        ICreateTypeInfo FAR * This,
	UINT uTypeFlags
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetDocString)
    (
        ICreateTypeInfo FAR * This,
	LPOLESTR lpstrDoc
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetHelpContext)
    (
        ICreateTypeInfo FAR * This,
	DWORD dwHelpContext
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetVersion)
    (
        ICreateTypeInfo FAR * This,
	WORD wMajorVerNum,
	WORD wMinorVerNum
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *AddRefTypeInfo)
    (
        ICreateTypeInfo FAR * This,
	ITypeInfo *ptinfo,
	HREFTYPE *phreftype
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *AddFuncDesc)
    (
        ICreateTypeInfo FAR * This,
	UINT index,
	FUNCDESC *pfuncdesc
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *AddImplType)
    (
        ICreateTypeInfo FAR * This,
	UINT index,
	HREFTYPE hreftype
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetImplTypeFlags)
    (
        ICreateTypeInfo FAR * This,
	UINT index,
	INT impltypeflags
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetAlignment)
    (
        ICreateTypeInfo FAR * This,
	WORD cbAlignment
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetSchema)
    (
        ICreateTypeInfo FAR * This,
	LPOLESTR lpstrSchema
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *AddVarDesc)
    (
        ICreateTypeInfo FAR * This,
	UINT index,
	VARDESC *pvardesc
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetFuncAndParamNames)
    (
        ICreateTypeInfo FAR * This,
	UINT index,
	LPOLESTR *rgszNames,
	UINT cNames
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetVarName)
    (
        ICreateTypeInfo FAR * This,
	UINT index,
	LPOLESTR szName
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetTypeDescAlias)
    (
        ICreateTypeInfo FAR * This,
	TYPEDESC *ptdescAlias
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *DefineFuncAsDllEntry)
    (
        ICreateTypeInfo FAR * This,
	UINT index,
	LPOLESTR szDllName,
	LPOLESTR szProcName
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetFuncDocString)
    (
        ICreateTypeInfo FAR * This,
	UINT index,
	LPOLESTR szDocString
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetVarDocString)
    (
        ICreateTypeInfo FAR * This,
	UINT index,
	LPOLESTR szDocString
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetFuncHelpContext)
    (
        ICreateTypeInfo FAR * This,
	UINT index,
	DWORD dwHelpContext
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetVarHelpContext)
    (
        ICreateTypeInfo FAR * This,
	UINT index,
	DWORD dwHelpContext
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetMops)
    (
        ICreateTypeInfo FAR * This,
	UINT index,
	BSTR bstrMops
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetTypeIdldesc)
    (
        ICreateTypeInfo FAR * This,
	IDLDESC *pidldesc
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *LayOut)
    (
        ICreateTypeInfo FAR * This
    );
    
} ICreateTypeInfoVtbl;

interface ICreateTypeInfo
{
    ICreateTypeInfoVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__ICreateTypeInfo__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for ICreateTypeLib
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __ICreateTypeLib__
#define __ICreateTypeLib__

/* Forward declaration */
typedef interface ICreateTypeLib ICreateTypeLib;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


typedef ICreateTypeLib *LPCREATETYPELIB;


EXTERN_C const IID IID_ICreateTypeLib;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface ICreateTypeLib : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE CreateTypeInfo
    (
	LPOLESTR szName,
	TYPEKIND tkind,
	ICreateTypeInfo **lplpictinfo
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetName
    (
	LPOLESTR szName
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetVersion
    (
	WORD wMajorVerNum,
	WORD wMinorVerNum
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetGuid
    (
	REFGUID guid
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetDocString
    (
	LPOLESTR szDoc
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetHelpFileName
    (
	LPOLESTR szHelpFileName
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetHelpContext
    (
	DWORD dwHelpContext
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetLcid
    (
	LCID lcid
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SetLibFlags
    (
	UINT uLibFlags
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE SaveAllChanges
    (
        void
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct ICreateTypeLibVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        ICreateTypeLib FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        ICreateTypeLib FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        ICreateTypeLib FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *CreateTypeInfo)
    (
        ICreateTypeLib FAR * This,
	LPOLESTR szName,
	TYPEKIND tkind,
	ICreateTypeInfo **lplpictinfo
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetName)
    (
        ICreateTypeLib FAR * This,
	LPOLESTR szName
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetVersion)
    (
        ICreateTypeLib FAR * This,
	WORD wMajorVerNum,
	WORD wMinorVerNum
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetGuid)
    (
        ICreateTypeLib FAR * This,
	REFGUID guid
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetDocString)
    (
        ICreateTypeLib FAR * This,
	LPOLESTR szDoc
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetHelpFileName)
    (
        ICreateTypeLib FAR * This,
	LPOLESTR szHelpFileName
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetHelpContext)
    (
        ICreateTypeLib FAR * This,
	DWORD dwHelpContext
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetLcid)
    (
        ICreateTypeLib FAR * This,
	LCID lcid
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SetLibFlags)
    (
        ICreateTypeLib FAR * This,
	UINT uLibFlags
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *SaveAllChanges)
    (
        ICreateTypeLib FAR * This
    );
    
} ICreateTypeLibVtbl;

interface ICreateTypeLib
{
    ICreateTypeLibVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__ICreateTypeLib__*/

//+-------------------------------------------------------------------------
//
//  Microsoft Windows
//  Copyright (C) Microsoft Corporation, 1992 - 1993.
//
//
//  Contents: Interface header file for IEnumVARIANT
//
//  History: Created by Microsoft (R) MIDL Compiler Version 1.10.85
//
//--------------------------------------------------------------------------

#ifndef __IEnumVARIANT__
#define __IEnumVARIANT__

/* Forward declaration */
typedef interface IEnumVARIANT IEnumVARIANT;


#ifndef _ERROR_STATUS_T_DEFINED
typedef unsigned long error_status_t;
#define _ERROR_STATUS_T_DEFINED
#endif


#ifndef _WCHAR_T_DEFINED
typedef unsigned short wchar_t;
#define _WCHAR_T_DEFINED
#endif


#ifndef _LPENUMVARIANT_DEFINED

#define _LPENUMVARIANT_DEFINED

typedef IEnumVARIANT *LPENUMVARIANT;


#endif

EXTERN_C const IID IID_IEnumVARIANT;
#if defined(__cplusplus) && !defined(CINTERFACE)

/* C++ Language Binding */

interface IEnumVARIANT : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Next
    (
	unsigned long celt,
	VARIANT *rgvar,
	unsigned long *pceltFetched
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Skip
    (
	unsigned long celt
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Reset
    (
        void
    ) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE Clone
    (
	IEnumVARIANT **ppenum
    ) = 0;
    
};

#else 

/* C Language Binding */

typedef struct IEnumVARIANTVtbl
{

    HRESULT (STDMETHODCALLTYPE FAR *QueryInterface)
    (
        IEnumVARIANT FAR * This,
	REFIID riid,
	void **ppvObject
    );
    
    ULONG (STDMETHODCALLTYPE FAR *AddRef)
    (
        IEnumVARIANT FAR * This
    );
    
    ULONG (STDMETHODCALLTYPE FAR *Release)
    (
        IEnumVARIANT FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Next)
    (
        IEnumVARIANT FAR * This,
	unsigned long celt,
	VARIANT *rgvar,
	unsigned long *pceltFetched
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Skip)
    (
        IEnumVARIANT FAR * This,
	unsigned long celt
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Reset)
    (
        IEnumVARIANT FAR * This
    );
    
    HRESULT (STDMETHODCALLTYPE FAR *Clone)
    (
        IEnumVARIANT FAR * This,
	IEnumVARIANT **ppenum
    );
    
} IEnumVARIANTVtbl;

interface IEnumVARIANT
{
    IEnumVARIANTVtbl FAR *lpVtbl;
} ;


#endif

#endif /*__IEnumVARIANT__*/
/*---------------------------------------------------------------------*/
/*                            BSTR API                                 */
/*---------------------------------------------------------------------*/

STDAPI_(BSTR) SysAllocString(const OLECHAR FAR*);
STDAPI_(int)  SysReAllocString(BSTR FAR*, const OLECHAR FAR*);
STDAPI_(BSTR) SysAllocStringLen(const OLECHAR FAR*, unsigned int);
STDAPI_(int)  SysReAllocStringLen(BSTR FAR*, const OLECHAR FAR*, unsigned int);
STDAPI_(void) SysFreeString(BSTR);
STDAPI_(unsigned int) SysStringLen(BSTR);

#ifdef WIN32
STDAPI_(unsigned int) SysStringByteLen(BSTR bstr);
STDAPI_(BSTR) SysAllocStringByteLen(const char FAR* psz, unsigned int len);
#endif

/*---------------------------------------------------------------------*/
/*                            Time API                                 */
/*---------------------------------------------------------------------*/

STDAPI_(int)
DosDateTimeToVariantTime(
    unsigned short wDosDate,
    unsigned short wDosTime,
    double FAR* pvtime);

STDAPI_(int)
VariantTimeToDosDateTime(
    double vtime,
    unsigned short FAR* pwDosDate,
    unsigned short FAR* pwDosTime);

/*---------------------------------------------------------------------*/
/*                          SafeArray API                              */
/*---------------------------------------------------------------------*/

STDAPI
SafeArrayAllocDescriptor(unsigned int cDims, SAFEARRAY FAR* FAR* ppsaOut);

STDAPI SafeArrayAllocData(SAFEARRAY FAR* psa);

STDAPI_(SAFEARRAY FAR*)
SafeArrayCreate(
    VARTYPE vt,
    unsigned int cDims,
    SAFEARRAYBOUND FAR* rgsabound);

STDAPI SafeArrayDestroyDescriptor(SAFEARRAY FAR* psa);

STDAPI SafeArrayDestroyData(SAFEARRAY FAR* psa);

STDAPI SafeArrayDestroy(SAFEARRAY FAR* psa);

STDAPI SafeArrayRedim(SAFEARRAY FAR* psa, SAFEARRAYBOUND FAR* psaboundNew);

STDAPI_(unsigned int) SafeArrayGetDim(SAFEARRAY FAR* psa);

STDAPI_(unsigned int) SafeArrayGetElemsize(SAFEARRAY FAR* psa);

STDAPI
SafeArrayGetUBound(SAFEARRAY FAR* psa, unsigned int nDim, long FAR* plUbound);

STDAPI
SafeArrayGetLBound(SAFEARRAY FAR* psa, unsigned int nDim, long FAR* plLbound);

STDAPI SafeArrayLock(SAFEARRAY FAR* psa);

STDAPI SafeArrayUnlock(SAFEARRAY FAR* psa);

STDAPI SafeArrayAccessData(SAFEARRAY FAR* psa, void HUGEP* FAR* ppvData);

STDAPI SafeArrayUnaccessData(SAFEARRAY FAR* psa);

STDAPI
SafeArrayGetElement(
    SAFEARRAY FAR* psa,
    long FAR* rgIndices,
    void FAR* pv);

STDAPI
SafeArrayPutElement(
    SAFEARRAY FAR* psa,
    long FAR* rgIndices,
    void FAR* pv);

STDAPI
SafeArrayCopy(
    SAFEARRAY FAR* psa,
    SAFEARRAY FAR* FAR* ppsaOut);

STDAPI
SafeArrayPtrOfIndex(
    SAFEARRAY FAR* psa,
    long FAR* rgIndices,
    void HUGEP* FAR* ppvData);


/*---------------------------------------------------------------------*/
/*                           VARIANT API                               */
/*---------------------------------------------------------------------*/

STDAPI_(void)
VariantInit(VARIANTARG FAR* pvarg);

STDAPI
VariantClear(VARIANTARG FAR* pvarg);

STDAPI
VariantCopy(
    VARIANTARG FAR* pvargDest,
    VARIANTARG FAR* pvargSrc);

STDAPI
VariantCopyInd(
    VARIANT FAR* pvarDest,
    VARIANTARG FAR* pvargSrc);

STDAPI
VariantChangeType(
    VARIANTARG FAR* pvargDest,
    VARIANTARG FAR* pvarSrc,
    unsigned short wFlags,
    VARTYPE vt);

STDAPI
VariantChangeTypeEx(
    VARIANTARG FAR* pvargDest,
    VARIANTARG FAR* pvarSrc,
    LCID lcid,
    unsigned short wFlags,
    VARTYPE vt);

#define VARIANT_NOVALUEPROP 1


/*---------------------------------------------------------------------*/
/*                     VARTYPE Coercion API                            */
/*---------------------------------------------------------------------*/

/* Note: The routines that convert *from* a string are defined
 * to take a OLECHAR* rather than a BSTR because no allocation is
 * required, and this makes the routines a bit more generic.
 * They may of course still be passed a BSTR as the strIn param.
 */


/* Any of the coersion functions that converts either from or to a string
 * takes an additional lcid and dwFlags arguments. The lcid argument allows
 * locale specific parsing to occur.  The dwFlags allow additional function
 * specific condition to occur.  All function that accept the dwFlags argument
 * can include either 0 or LOCALE_NOUSEROVERRIDE flag. In addition, the
 * VarDateFromStr functions also accepts the VAR_TIMEVALUEONLY and
 * VAR_DATEVALUEONLY flags
 */

#define VAR_TIMEVALUEONLY            0x0001    /* return time value */
#define VAR_DATEVALUEONLY            0x0002    /* return date value */


STDAPI VarI2FromI4(long lIn, short FAR* psOut);
STDAPI VarI2FromR4(float fltIn, short FAR* psOut);
STDAPI VarI2FromR8(double dblIn, short FAR* psOut);
STDAPI VarI2FromCy(CY cyIn, short FAR* psOut);
STDAPI VarI2FromDate(DATE dateIn, short FAR* psOut);
STDAPI VarI2FromStr(OLECHAR FAR* strIn, LCID lcid, unsigned long dwFlags, short FAR* psOut);
STDAPI VarI2FromDisp(IDispatch FAR* pdispIn, LCID lcid, short FAR* psOut);
STDAPI VarI2FromBool(VARIANT_BOOL boolIn, short FAR* psOut);

STDAPI VarI4FromI2(short sIn, long FAR* plOut);
STDAPI VarI4FromR4(float fltIn, long FAR* plOut);
STDAPI VarI4FromR8(double dblIn, long FAR* plOut);
STDAPI VarI4FromCy(CY cyIn, long FAR* plOut);
STDAPI VarI4FromDate(DATE dateIn, long FAR* plOut);
STDAPI VarI4FromStr(OLECHAR FAR* strIn, LCID lcid, unsigned long dwFlags, long FAR* plOut);
STDAPI VarI4FromDisp(IDispatch FAR* pdispIn, LCID lcid, long FAR* plOut);
STDAPI VarI4FromBool(VARIANT_BOOL boolIn, long FAR* plOut);

STDAPI VarR4FromI2(short sIn, float FAR* pfltOut);
STDAPI VarR4FromI4(long lIn, float FAR* pfltOut);
STDAPI VarR4FromR8(double dblIn, float FAR* pfltOut);
STDAPI VarR4FromCy(CY cyIn, float FAR* pfltOut);
STDAPI VarR4FromDate(DATE dateIn, float FAR* pfltOut);
STDAPI VarR4FromStr(OLECHAR FAR* strIn, LCID lcid, unsigned long dwFlags, float FAR* pfltOut);
STDAPI VarR4FromDisp(IDispatch FAR* pdispIn, LCID lcid, float FAR* pfltOut);
STDAPI VarR4FromBool(VARIANT_BOOL boolIn, float FAR* pfltOut);

STDAPI VarR8FromI2(short sIn, double FAR* pdblOut);
STDAPI VarR8FromI4(long lIn, double FAR* pdblOut);
STDAPI VarR8FromR4(float fltIn, double FAR* pdblOut);
STDAPI VarR8FromCy(CY cyIn, double FAR* pdblOut);
STDAPI VarR8FromDate(DATE dateIn, double FAR* pdblOut);
STDAPI VarR8FromStr(OLECHAR FAR* strIn, LCID lcid, unsigned long dwFlags, double FAR* pdblOut);
STDAPI VarR8FromDisp(IDispatch FAR* pdispIn, LCID lcid, double FAR* pdblOut);
STDAPI VarR8FromBool(VARIANT_BOOL boolIn, double FAR* pdblOut);

STDAPI VarDateFromI2(short sIn, DATE FAR* pdateOut);
STDAPI VarDateFromI4(long lIn, DATE FAR* pdateOut);
STDAPI VarDateFromR4(float fltIn, DATE FAR* pdateOut);
STDAPI VarDateFromR8(double dblIn, DATE FAR* pdateOut);
STDAPI VarDateFromCy(CY cyIn, DATE FAR* pdateOut);
STDAPI VarDateFromStr(OLECHAR FAR* strIn, LCID lcid, unsigned long dwFlags, DATE FAR* pdateOut);
STDAPI VarDateFromDisp(IDispatch FAR* pdispIn, LCID lcid, DATE FAR* pdateOut);
STDAPI VarDateFromBool(VARIANT_BOOL boolIn, DATE FAR* pdateOut);

STDAPI VarCyFromI2(short sIn, CY FAR* pcyOut);
STDAPI VarCyFromI4(long lIn, CY FAR* pcyOut);
STDAPI VarCyFromR4(float fltIn, CY FAR* pcyOut);
STDAPI VarCyFromR8(double dblIn, CY FAR* pcyOut);
STDAPI VarCyFromDate(DATE dateIn, CY FAR* pcyOut);
STDAPI VarCyFromStr(OLECHAR FAR* strIn, LCID lcid, unsigned long dwFlags, CY FAR* pcyOut);
STDAPI VarCyFromDisp(IDispatch FAR* pdispIn, LCID lcid, CY FAR* pcyOut);
STDAPI VarCyFromBool(VARIANT_BOOL boolIn, CY FAR* pcyOut);

STDAPI VarBstrFromI2(short iVal, LCID lcid, unsigned long dwFlags, BSTR FAR* pbstrOut);
STDAPI VarBstrFromI4(long lIn, LCID lcid, unsigned long dwFlags, BSTR FAR* pbstrOut);
STDAPI VarBstrFromR4(float fltIn, LCID lcid, unsigned long dwFlags, BSTR FAR* pbstrOut);
STDAPI VarBstrFromR8(double dblIn, LCID lcid, unsigned long dwFlags, BSTR FAR* pbstrOut);
STDAPI VarBstrFromCy(CY cyIn, LCID lcid, unsigned long dwFlags, BSTR FAR* pbstrOut);
STDAPI VarBstrFromDate(DATE dateIn, LCID lcid, unsigned long dwFlags, BSTR FAR* pbstrOut);
STDAPI VarBstrFromDisp(IDispatch FAR* pdispIn, LCID lcid, unsigned long dwFlags, BSTR FAR* pbstrOut);
STDAPI VarBstrFromBool(VARIANT_BOOL boolIn, LCID lcid, unsigned long dwFlags, BSTR FAR* pbstrOut);

STDAPI VarBoolFromI2(short sIn, VARIANT_BOOL FAR* pboolOut);
STDAPI VarBoolFromI4(long lIn, VARIANT_BOOL FAR* pboolOut);
STDAPI VarBoolFromR4(float fltIn, VARIANT_BOOL FAR* pboolOut);
STDAPI VarBoolFromR8(double dblIn, VARIANT_BOOL FAR* pboolOut);
STDAPI VarBoolFromDate(DATE dateIn, VARIANT_BOOL FAR* pboolOut);
STDAPI VarBoolFromCy(CY cyIn, VARIANT_BOOL FAR* pboolOut);
STDAPI VarBoolFromStr(OLECHAR FAR* strIn, LCID lcid, unsigned long dwFlags, VARIANT_BOOL FAR* pboolOut);
STDAPI VarBoolFromDisp(IDispatch FAR* pdispIn, LCID lcid, VARIANT_BOOL FAR* pboolOut);


/* Mac Note: On the Mac, the coersion functions support the
 * Symantec C++ calling convention for float/double. To support
 * float/double arguments compiled with the MPW C compiler,
 * use the following APIs to move MPW float/double values into
 * a VARIANT.
 */

/*---------------------------------------------------------------------*/
/*                 ITypeLib                    */
/*---------------------------------------------------------------------*/


typedef ITypeLib FAR* LPTYPELIB;


/*---------------------------------------------------------------------*/
/*                ITypeInfo                    */
/*---------------------------------------------------------------------*/


typedef LONG DISPID;
typedef DISPID MEMBERID;

#define MEMBERID_NIL DISPID_UNKNOWN
#define ID_DEFAULTINST  -2


#define IDLFLAG_NONE    0
#define IDLFLAG_FIN     0x1
#define IDLFLAG_FOUT    0x2


/* Flags for IDispatch::Invoke */
#define DISPATCH_METHOD     0x1
#define DISPATCH_PROPERTYGET    0x2
#define DISPATCH_PROPERTYPUT    0x4
#define DISPATCH_PROPERTYPUTREF 0x8


typedef ITypeInfo FAR* LPTYPEINFO;


/*---------------------------------------------------------------------*/
/*                ITypeComp                    */
/*---------------------------------------------------------------------*/

typedef ITypeComp FAR* LPTYPECOMP;


/*---------------------------------------------------------------------*/
/*             ICreateTypeLib                  */
/*---------------------------------------------------------------------*/

typedef ICreateTypeLib FAR* LPCREATETYPELIB;


typedef ICreateTypeInfo FAR* LPCREATETYPEINFO;

/*---------------------------------------------------------------------*/
/*             TypeInfo API                    */
/*---------------------------------------------------------------------*/

/* compute a 16bit hash value for the given name
 */
STDAPI_(ULONG)
LHashValOfNameSys(SYSKIND syskind, LCID lcid, OLECHAR FAR* szName);

#define LHashValOfName(lcid, szName) \
        LHashValOfNameSys(SYS_WIN32, lcid, szName)

#define WHashValOfLHashVal(lhashval) \
        ((unsigned short) (0x0000ffff & (lhashval)))

#define IsHashValCompatible(lhashval1, lhashval2) \
        ((BOOL) ((0x00ff0000 & (lhashval1)) == (0x00ff0000 & (lhashval2))))

/* load the typelib from the file with the given filename
 */
STDAPI
LoadTypeLib(OLECHAR FAR *szFile, ITypeLib FAR* FAR* pptlib);

/* load registered typelib
 */
STDAPI
LoadRegTypeLib(
    REFGUID rguid,
    WORD wVerMajor,
    WORD wVerMinor,
    LCID lcid,
    ITypeLib FAR* FAR* pptlib);

/* get path to registered typelib
 */
STDAPI
QueryPathOfRegTypeLib(
    REFGUID guid,
    unsigned short wMaj,
    unsigned short wMin,
    LCID lcid,
    LPBSTR lpbstrPathName);

/* add typelib to registry
 */
STDAPI
RegisterTypeLib(ITypeLib FAR* ptlib, OLECHAR FAR *szFullPath,
        OLECHAR FAR *szHelpDir);

/* remove typelib from registry
 */
STDAPI
DeregisterTypeLib(REFGUID rguid, WORD wVerMajor, WORD wVerMinor, LCID lcid);

STDAPI
CreateTypeLib(SYSKIND syskind, OLECHAR FAR *szFile,
        ICreateTypeLib FAR* FAR* ppctlib);

/*---------------------------------------------------------------------*/

/*---------------------------------------------------------------------*/
/*                 IDispatch                   */
/*---------------------------------------------------------------------*/

typedef IDispatch FAR* LPDISPATCH;



/*---------------------------------------------------------------------*/
/*           IDispatch implementation support              */
/*---------------------------------------------------------------------*/

typedef struct FARSTRUCT tagPARAMDATA {
    WCHAR FAR* szName;      /* parameter name */
    VARTYPE vt;         /* parameter type */
} PARAMDATA, FAR* LPPARAMDATA;

typedef struct FARSTRUCT tagMETHODDATA {
    WCHAR FAR* szName;      /* method name */
    PARAMDATA FAR* ppdata;  /* pointer to an array of PARAMDATAs */
    DISPID dispid;      /* method ID */
    UINT iMeth;         /* method index */
    CALLCONV cc;        /* calling convention */
    UINT cArgs;         /* count of arguments */
    WORD wFlags;        /* same wFlags as on IDispatch::Invoke() */
    VARTYPE vtReturn;
} METHODDATA, FAR* LPMETHODDATA;

typedef struct FARSTRUCT tagINTERFACEDATA {
    METHODDATA FAR* pmethdata;  /* pointer to an array of METHODDATAs */
    UINT cMembers;      /* count of members */
} INTERFACEDATA, FAR* LPINTERFACEDATA;



/* Locate the parameter indicated by the given position, and
 * return it coerced to the given target VARTYPE (vtTarg).
 */
STDAPI
DispGetParam(
    DISPPARAMS FAR* pdispparams,
    UINT position,
    VARTYPE vtTarg,
    VARIANT FAR* pvarResult,
    UINT FAR* puArgErr);

/* Automatic TypeInfo driven implementation of IDispatch::GetIDsOfNames()
 */
STDAPI
DispGetIDsOfNames(
    ITypeInfo FAR* ptinfo,
    WCHAR FAR* FAR* rgszNames,
    UINT cNames,
    DISPID FAR* rgdispid);

/* Automatic TypeInfo driven implementation of IDispatch::Invoke()
 */
STDAPI
DispInvoke(
    void FAR* _this,
    ITypeInfo FAR* ptinfo,
    DISPID dispidMember,
    WORD wFlags,
    DISPPARAMS FAR* pparams,
    VARIANT FAR* pvarResult,
    EXCEPINFO FAR* pexcepinfo,
    UINT FAR* puArgErr);

/* Construct a TypeInfo from an interface data description
 */
STDAPI
CreateDispTypeInfo(
    INTERFACEDATA FAR* pidata,
    LCID lcid,
    ITypeInfo FAR* FAR* pptinfo);

/* Create an instance of the standard TypeInfo driven IDispatch
 * implementation.
 */
STDAPI
CreateStdDispatch(
    IUnknown FAR* punkOuter,
    void FAR* pvThis,
    ITypeInfo FAR* ptinfo,
    IUnknown FAR* FAR* ppunkStdDisp);


/*---------------------------------------------------------------------*/
/*            Active Object Registration API               */
/*---------------------------------------------------------------------*/


STDAPI
RegisterActiveObject(
   IUnknown FAR* punk,
   REFCLSID rclsid,
   void FAR* pvReserved,
   DWORD FAR* pdwRegister);

STDAPI
RevokeActiveObject(
    DWORD dwRegister,
    void FAR* pvReserved);

STDAPI
GetActiveObject(
    REFCLSID rclsid,
    void FAR* pvReserved,
    IUnknown FAR* FAR* ppunk);

// Declare variant access functions.



#ifdef NONAMELESSUNION
# define V_UNION(X, Y) ((X)->u.Y)
#else
# define V_UNION(X, Y) ((X)->Y)
#endif

/* Variant access macros */
#define V_VT(X)          ((X)->vt)
#define V_ISBYREF(X)     (V_VT(X)&VT_BYREF)
#define V_ISARRAY(X)     (V_VT(X)&VT_ARRAY)
#define V_ISVECTOR(X)    (V_VT(X)&VT_VECTOR)

#define V_NONE(X)        V_I2(X)

#define V_I2(X)          V_UNION(X, iVal)
#define V_I2REF(X)       V_UNION(X, piVal)

#define V_I4(X)          V_UNION(X, lVal)
#define V_I4REF(X)       V_UNION(X, plVal)

#define V_I8(X)          V_UNION(X, hVal)
#define V_I8REF(X)       V_UNION(X, phVal)

#define V_R4(X)          V_UNION(X, fltVal)
#define V_R4REF(X)       V_UNION(X, pfltVal)

#define V_R8(X)          V_UNION(X, dblVal)
#define V_R8REF(X)       V_UNION(X, pdblVal)

#define V_CY(X)          V_UNION(X, cyVal)
#define V_CYREF(X)       V_UNION(X, pcyVal)

#define V_DATE(X)        V_UNION(X, date)
#define V_DATEREF(X)     V_UNION(X, pdate)

#define V_BSTR(X)        V_UNION(X, bstrVal)
#define V_BSTRREF(X)     V_UNION(X, pbstrVal)

#define V_DISPATCH(X)    V_UNION(X, pdispVal)
#define V_DISPATCHREF(X) V_UNION(X, ppdispVal)

#define V_ERROR(X)       V_UNION(X, scode)
#define V_ERRORREF(X)    V_UNION(X, pscode)

#define V_BOOL(X)        V_UNION(X, bool)
#define V_BOOLREF(X)     V_UNION(X, pbool)

#define V_UNKNOWN(X)     V_UNION(X, punkVal)
#define V_UNKNOWNREF(X)  V_UNION(X, ppunkVal)


#define V_VARIANTREF(X)  V_UNION(X, pvarVal)

#define V_LPSTR(X)        V_UNION(X, pszVal)
#define V_LPSTRREF(X)     V_UNION(X, ppszVal)

#define V_LPWSTR(X)        V_UNION(X, pwszVal)
#define V_LPWSTRREF(X)     V_UNION(X, ppwszVal)

#define V_FILETIME(X)        V_UNION(X, filetime)
#define V_FILETIMEREF(X)        V_UNION(X, pfiletime)

#define V_BLOB(X)        V_UNION(X, blob)

#define V_UUID(X)        V_UNION(X, puuid)
#define V_CLSID(X)       V_UNION(X, puuid)

#define V_ARRAY(X)       V_UNION(X, parray)
#define V_ARRAYREF(X)    V_UNION(X, pparray)

#define V_BYREF(X)       V_UNION(X, byref)

#ifndef RC_INVOKED
#pragma pack()
#endif // RC_INVOKED
#endif     // __oleauto_H__
