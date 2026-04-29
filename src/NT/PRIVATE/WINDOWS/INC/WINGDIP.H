/*++ BUILD Version: 0004    // Increment this if a change has global effects

Copyright (c) 1985-91, Microsoft Corporation

Module Name:

    wingdi.h

Abstract:

    Procedure declarations, constant definitions and macros for the GDI
    component.

--*/
#ifndef _WINGDIP_
#define _WINGDIP_
#ifdef __cplusplus
extern "C" {
#endif
#if(WINVER < 0x0400)
#define ETO_GLYPH_INDEX              0x0010
#define ETO_RTL                      0x0080
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#ifndef NOICM

/* Image Color Matching color definitions */

// The following two structures are used for defining RGB's in terms of
// CIEXYZ. The values are fixed point 16.16.

typedef struct tagCIEXYZ
{
    DWORD ciexyz_X;
    DWORD ciexyz_Y;
    DWORD ciexyz_Z;
} CIEXYZ;

typedef struct tagCIEXYZTRIPLE
{
    CIEXYZ ciexyz_Red;
    CIEXYZ ciexyz_Green;
    CIEXYZ ciexyz_Blue;
} CIEXYZTRIPLE;

// The next structures the logical color space. Unlike pens and brushes,
// but like palettes, there is only one way to create a LogColorSpace.
// A pointer to it must be passed, its elements can't be pushed as
// arguments.

typedef struct tagLOGCOLORSPACE {
    DWORD lcs_version;
    DWORD lcs_size;
    DWORD lcs_ident;
    DWORD lcs_gamut_match;
    CIEXYZTRIPLE lcs_endpoints;
    DWORD lcs_gamma_red;
    DWORD lcs_gamma_green;
    DWORD lcs_gamma_blue;
    char  lcs_filename[MAX_PATH];
} LOGCOLORSPACE, *LPLOGCOLORSPACE;

#endif /* NOICM */
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#ifndef NOFONTSIG
typedef struct tagFONTSIGNATURE
{
    DWORD fsUsb[4];
    DWORD fsCsb[2];
} FONTSIGNATURE, *PFONTSIGNATURE,FAR *LPFONTSIGNATURE;

typedef struct tagLOCALESIGNATURE
{
    DWORD lsUsb[4];
    DWORD lsCsbDefault[2];
    DWORD lsCsbSupported[2];
} LOCALESIGNATURE, *PLOCALESIGNATURE,FAR *LPLOCALESIGNATURE;

#endif
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
typedef struct tagNEWTEXTMETRICEXA
{
    NEWTEXTMETRICA  ntmTm;
    FONTSIGNATURE   ntmFontSig;
}NEWTEXTMETRICEXA;
typedef struct tagNEWTEXTMETRICEXW
{
    NEWTEXTMETRICW  ntmTm;
    FONTSIGNATURE   ntmFontSig;
}NEWTEXTMETRICEXW;
#ifdef UNICODE
typedef NEWTEXTMETRICEXW NEWTEXTMETRICEX;
#else
typedef NEWTEXTMETRICEXA NEWTEXTMETRICEX;
#endif // UNICODE
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
typedef struct tagENUMLOGFONTEXA
{
    LOGFONTA    elfLogFont;
    BYTE        elfFullName[LF_FULLFACESIZE];
    BYTE        elfStyle[LF_FACESIZE];
    BYTE        elfScript[LF_FACESIZE];
} ENUMLOGFONTEXA, FAR *LPENUMLOGFONTEXA;
typedef struct tagENUMLOGFONTEXW
{
    LOGFONTW    elfLogFont;
    BYTE        elfFullName[LF_FULLFACESIZE];
    BYTE        elfStyle[LF_FACESIZE];
    BYTE        elfScript[LF_FACESIZE];
} ENUMLOGFONTEXW, FAR *LPENUMLOGFONTEXW;
#ifdef UNICODE
typedef ENUMLOGFONTEXW ENUMLOGFONTEX;
typedef LPENUMLOGFONTEXW LPENUMLOGFONTEX;
#else
typedef ENUMLOGFONTEXA ENUMLOGFONTEX;
typedef LPENUMLOGFONTEXA LPENUMLOGFONTEX;
#endif // UNICODE
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#define NONANTIALIASED_QUALITY  3
#define ANTIALIASED_QUALITY     4
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#define MAC_CHARSET             77
#define HEBREW_CHARSET          177
#define ARABIC_CHARSET          178
#define GREEK_CHARSET           161
#define TURKISH_CHARSET         162
#define BALTIC_CHARSET          186
#define THAI_CHARSET            222
#define EASTEUROPE_CHARSET      238
#define RUSSIAN_CHARSET         204
#define OEM_CHARSET             255
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#define DEFAULT_GUI_FONT    17
#endif /* WINVER < 0x0400 */
#define CAPS1         94    /* Extra Caps */
/* CAPS1 */
#define C1_TRANSPARENT      0x0001
#define TC_TT_ABLE          0x0002
#define C1_TT_CR_ANY        0x0004
#define C1_EMF_COMPLIANT    0x0008
#define C1_DIBENGINE        0x0010
#define C1_GAMMA_RAMP       0x0020
#define C1_DIC              0x0040
#define C1_REINIT_ABLE      0x0080
#define C1_GLYPH_INDEX      0x0100
#define C1_BIT_PACKED       0x0200
#define C1_BYTE_PACKED      0x0400
#define C1_COLORCURSOR      0x0800
#define CBM_CREATEDIB   0x02L   /* create DIB bitmap */
#if(WINVER < 0x0400)
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#else
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#define DM_UNUSED           0x00020000L
#define DM_BITSPERPEL       0x00040000L
#define DM_PELSWIDTH        0x00080000L
#define DM_PELSHEIGHT       0x00100000L
#define DM_DISPLAYFLAGS     0x00200000L
#define DM_DISPLAYFREQUENCT 0x00400000L
#define DM_RESERVED1        0x00800000L
#define DM_RESERVED2        0x01000000L
#define DM_ICMMETHOD        0x02000000L
#define DM_ICMINTENT        0x04000000L
#define DM_MEDIATYPE        0x08000000L
#define DM_DITHERTYPE       0x10000000L
#endif /* WINVER < 0x0400 */
#define DMDUP_LAST      DMDUP_HORIZONTAL        //
#if(WINVER < 0x0400)
#define DMTT_DOWNLOAD_OUTLINE 4 /* download TT fonts as outline soft fonts */
#define DMTT_LAST             DMTT_DOWNLOAD_OUTLINE //
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
/* ICM methods */
#define DMICMMETHOD_SYSTEM  1   /* ICM handled by system */
#define DMICMMETHOD_NONE    2   /* ICM disabled */
#define DMICMMETHOD_DRIVER  3   /* ICM handled by driver */
#define DMICMMETHOD_DEVICE  4   /* ICM handled by device */
#define DMICMMETHOD_LAST    DMICMMETHOD_DEVICE  //

#define DMICMMETHOD_USER  256   /* Device-specific methods start here */

/* ICM Intents */
#define DMICM_SATURATE      1   /* Maximize color saturation */
#define DMICM_CONTRAST      2   /* Maximize color contrast */
#define DMICM_COLORMETRIC   3   /* Use specific color metric */
#define DMICM_LAST          DMICM_COLORMETRIC //

#define DMICM_USER        256   /* Device-specific intents start here */

/* Media types */
#define DMMEDIA_STANDARD      1   /* Standard paper */
#define DMMEDIA_GLOSSY        2   /* Glossy paper */
#define DMMEDIA_TRANSPARENCY  3   /* Transparency */
#define DMMEDIA_LAST          DMMEDIA_TRANSPARENCY //

#define DMMEDIA_USER        256   /* Device-specific media start here */

/* Dither types */
#define DMDITHER_NONE       1   /* No dithering */
#define DMDITHER_COARSE     2   /* Dither with a coarse brush */
#define DMDITHER_FINE       3   /* Dither with a fine brush */
#define DMDITHER_LINEART    4   /* LineArt dithering */
#define DMDITHER_GRAYSCALE  5   /* Device does grayscaling */
#define DMDITHER_LAST       DMDITHER_GRAYSCALE //

#define DMDITHER_USER     256   /* Device-specific dithers start here */
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#define  GGO_GRAY2_BITMAP   4
#define  GGO_GRAY4_BITMAP   5
#define  GGO_GRAY8_BITMAP   6
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)

#define GCP_DBCS           0x0001
#define GCP_REORDER        0x0002
#define GCP_USEKERNING     0x0008
#define GCP_GLYPHSHAPE     0x0010
#define GCP_LIGATE         0x0020
#define GCP_GLYPHINDEXING  0x0080
#define GCP_DIACRITIC      0x0100
#define GCP_NODIACRITICS   0x1000
#define GCP_MAXEXTENT      0x2000
#define GCP_ERROR          0x8000

typedef struct tagGCP_RESULTSA
    {
    DWORD   lStructSize;
    LPSTR     lpOutString;
    UINT FAR *lpOrder;
    int FAR  *lpDx;
    int FAR  *lpCaretPos;
    UINT FAR *lpGlyphs;
    UINT    nGlyphs;
    int     nMaxFit;
    } GCP_RESULTSA, FAR* LPGCP_RESULTSA;
typedef struct tagGCP_RESULTSW
    {
    DWORD   lStructSize;
    LPWSTR    lpOutString;
    UINT FAR *lpOrder;
    int FAR  *lpDx;
    int FAR  *lpCaretPos;
    UINT FAR *lpGlyphs;
    UINT    nGlyphs;
    int     nMaxFit;
    } GCP_RESULTSW, FAR* LPGCP_RESULTSW;
#ifdef UNICODE
typedef GCP_RESULTSW GCP_RESULTS;
typedef LPGCP_RESULTSW LPGCP_RESULTS;
#else
typedef GCP_RESULTSA GCP_RESULTS;
typedef LPGCP_RESULTSA LPGCP_RESULTS;
#endif // UNICODE
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#define DCTT_DOWNLOAD_OUTLINE   0x0000008L

/* return values for DC_BINADJUST */
#define DCBA_FACEUPNONE       0x0000
#define DCBA_FACEUPCENTER     0x0001
#define DCBA_FACEUPLEFT       0x0002
#define DCBA_FACEUPRIGHT      0x0003
#define DCBA_FACEDOWNNONE     0x0100
#define DCBA_FACEDOWNCENTER   0x0101
#define DCBA_FACEDOWNLEFT     0x0102
#define DCBA_FACEDOWNRIGHT    0x0103
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
WINGDIAPI int  WINAPI EnumFontFamiliesExA(HDC, LPLOGFONTA, FONTENUMPROC, LPARAM,DWORD);
#endif /* WINVER < 0x0400 */
HANDLE WINAPI SetObjectOwner(HGDIOBJ, HANDLE); //
#if(WINVER < 0x0400)
int WINAPI GetTextCharset(HDC hdc);
int WINAPI GetTextCharsetInfo(HDC hdc, DWORD *lpSig, DWORD dwFlags);
DWORD WINAPI GetFontLanguageInfo( HDC );
DWORD WINAPI GetCharacterPlacementA(HDC, LPCSTR, int, int, LPGCP_RESULTSA, DWORD);
DWORD WINAPI GetCharacterPlacementW(HDC, LPCWSTR, int, int, LPGCP_RESULTSW, DWORD);
#ifdef UNICODE
#define GetCharacterPlacement  GetCharacterPlacementW
#else
#define GetCharacterPlacement  GetCharacterPlacementA
#endif // !UNICODE
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#ifndef NOICM

BOOL WINAPI EnableICM(HDC, BOOL);
HANDLE WINAPI LoadImageColorMatcherA(LPSTR);
HANDLE WINAPI LoadImageColorMatcherW(LPWSTR);
#ifdef UNICODE
#define LoadImageColorMatcher  LoadImageColorMatcherW
#else
#define LoadImageColorMatcher  LoadImageColorMatcherA
#endif // !UNICODE
BOOL WINAPI FreeImageColorMatcher(HANDLE);
int WINAPI EnumColorProfiles(HDC,FARPROC,LPARAM);
BOOL WINAPI CheckColorsInGamut(HDC,LPVOID,LPVOID,DWORD);
HANDLE WINAPI GetColorSpace(HDC);
BOOL WINAPI GetLogColorSpace(HCOLORSPACE,LPVOID,DWORD);
HCOLORSPACE WINAPI CreateColorSpace(LPLOGCOLORSPACE);
BOOL WINAPI SetColorSpace(HDC,HCOLORSPACE);
BOOL WINAPI DeleteColorSpace(HCOLORSPACE);
BOOL WINAPI GetColorProfileA(HDC,LPSTR,DWORD);
BOOL WINAPI GetColorProfileW(HDC,LPWSTR,DWORD);
#ifdef UNICODE
#define GetColorProfile  GetColorProfileW
#else
#define GetColorProfile  GetColorProfileA
#endif // !UNICODE
BOOL WINAPI SetColorProfileA(HDC,LPSTR);
BOOL WINAPI SetColorProfileW(HDC,LPWSTR);
#ifdef UNICODE
#define SetColorProfile  SetColorProfileW
#else
#define SetColorProfile  SetColorProfileA
#endif // !UNICODE
BOOL WINAPI GetDeviceGammaRamp(HDC,LPVOID);
BOOL WINAPI SetDeviceGammaRamp(HDC,LPVOID);
BOOL WINAPI ColorMatchToTarget(HDC,HDC,DWORD);
BOOL WINAPI EnumProfiles(HDC,FARPROC,DWORD);

#endif  /* NOICM */
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
#define EMR_ENABLEICM                   98
#define EMR_CREATECOLORSPACE            99
#define EMR_SETCOLORSPACE              100
#define EMR_DELETECOLORSPACE           101
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
typedef struct tagEMRSELECTCOLORSPACE
{
    EMR     emr;
    DWORD   ihCS;               // ColorSpace handle index
} EMRSELECTCOLORSPACE, *PEMRSELECTCOLORSPACE,
  EMRDELETECOLORSPACE, *PEMRDELETECOLORSPACE;
#endif /* WINVER < 0x0400 */
#if(WINVER < 0x0400)
typedef struct tagEMRCREATECOLORSPACE
{
    EMR           emr;
    DWORD         ihCS;               // ColorSpace handle index
    LOGCOLORSPACE lcs;
} EMRCREATECOLORSPACE, *PEMRCREATECOLORSPACE;
#endif /* WINVER < 0x0400 */
#ifdef __cplusplus
}
#endif
// Old fields that Chicago won't support that we can't publically
// support anymore

#define HS_SOLIDCLR         6
#define HS_DITHEREDCLR      7
#define HS_SOLIDTEXTCLR     8
#define HS_DITHEREDTEXTCLR  9
#define HS_SOLIDBKCLR       10
#define HS_DITHEREDBKCLR    11
#define HS_API_MAX          12

#define DIB_PAL_INDICES     2 /* No color table indices into surf palette */

// End of stuff we yanked for Chicago compatability

#define SWAPL(x,y,t)        {t = x; x = y; y = t;}

#define ERROR_BOOL  (BOOL) -1L
#define HSEM_ERROR  ((HSEM) 0)

typedef ULONG   ROP2;
typedef ULONG   ROP3;

#include "winddi.h"

typedef struct _EXTTEXTMETRIC {
    SHORT  etmSize;
    SHORT  etmPointSize;
    SHORT  etmOrientation;
    SHORT  etmMasterHeight;
    SHORT  etmMinScale;
    SHORT  etmMaxScale;
    SHORT  etmMasterUnits;
    SHORT  etmCapHeight;
    SHORT  etmXHeight;
    SHORT  etmLowerCaseAscent;
    SHORT  etmLowerCaseDescent;
    SHORT  etmSlant;
    SHORT  etmSuperScript;
    SHORT  etmSubScript;
    SHORT  etmSuperScriptSize;
    SHORT  etmSubScriptSize;
    SHORT  etmUnderlineOffset;
    SHORT  etmUnderlineWidth;
    SHORT  etmDoubleUpperUnderlineOffset;
    SHORT  etmDoubleLowerUnderlineOffset;
    SHORT  etmDoubleUpperUnderlineWidth;
    SHORT  etmDoubleLowerUnderlineWidth;
    SHORT  etmStrikeOutOffset;
    SHORT  etmStrikeOutWidth;
    WORD   etmNKernPairs;
    WORD   etmNKernTracks;
} EXTTEXTMETRIC;

BOOL      GetETM(HDC hdc, EXTTEXTMETRIC *petm);

/**************************************************************************\
*
*   added tmdiff struc, contains the fields that are possibly different
*   between ansi and unicode versions of TEXTMETRICA and TEXTMETRICW
*
*   ONLY independent quantities are put into the strucure. Dependent ones,
*   such as tmDescent and maybe tmOverhang should be computed on the fly
*
*   tmDesc = tmHt - tmAsc
*   tmOverhang = tt ? 0 : ((tmHt - 1)/2 + (BOLD ? 1 : 0))
*
* History:
*  26-Jan-1993 -by- Bodin Dresevic [BodinD]
* Wrote it.
\**************************************************************************/

// this is a font with nonnegative a and c spaces, good for console

#define TMD_NONNEGATIVE_AC 1

typedef struct _TMDIFF
{
    ULONG       cjotma;     // size of OUTLINETEXTMETRICSA
    FLONG       fl;         // flags, for now only TMD_NONNEGATIVE_AC

    BYTE        chFirst;
    BYTE        chLast;
    BYTE        chDefault;
    BYTE        chBreak;
} TMDIFF; // DIFF between TEXTMETRICA and TEXTMETRICW

typedef struct _TMW_INTERNAL
{
    TEXTMETRICW tmw;
    TMDIFF      tmd;
} TMW_INTERNAL;

typedef struct _NTMW_INTERNAL
{
    NEWTEXTMETRICW ntmw;
    TMDIFF         tmd;
} NTMW_INTERNAL;


/*********************************Struct***********************************\
* struct ENUMFONTDATA
*
* Information for the callback function used by EnumFonts.
*
*   lf      LOGFONT structure corresponding to one of the enumerated fonts.
*
*   tm      The corresponding TEXTMETRIC structure for the LOGFONT above.
*
*   flType  Flags are set as follows:
*
*               DEVICE_FONTTYPE is set if font is device-based (as
*               opposed to IFI-based).
*
*               RASTER_FONTTYPE is set if font is bitmap type.
*
* History:
*  21-May-1991 -by- Gilman Wong [gilmanw]
* Wrote it.
\**************************************************************************/


#if defined(JAPAN)
#define NATIVE_CHARSET        SHIFTJIS_CHARSET
#define NATIVE_CODEPAGE       932
#define NATIVE_LANGUAGE_ID    411
#define DBCS_CHARSET          NATIVE_CHARSET
#elif defined(KOREA)
#define NATIVE_CHARSET        HANGEUL_CHARSET
#define NATIVE_CODEPAGE       944
#define NATIVE_LANGUAGE_ID    412
#define DBCS_CHARSET          NATIVE_CHARSET
#elif defined(TAIWAN)
#define NATIVE_CHARSET        CHINESEBIG5_CHARSET
#define NATIVE_CODEPAGE       950
#define NATIVE_LANGUAGE_ID    404
#define DBCS_CHARSET          NATIVE_CHARSET
#elif defined(PRC)
#define NATIVE_CHARSET        GB2312_CHARSET
#define NATIVE_CODEPAGE       936
#define NATIVE_LANGUAGE_ID    804
#define DBCS_CHARSET          NATIVE_CHARSET
#endif

#if defined(DBCS)
#define IS_DBCS_CHARSET( CharSet )     ( ((CharSet) == DBCS_CHARSET) ? TRUE : FALSE )
#define IS_ANY_DBCS_CHARSET( CharSet ) ( ((CharSet) == SHIFTJIS_CHARSET)    ? TRUE :    \
                                         ((CharSet) == HANGEUL_CHARSET)     ? TRUE :    \
                                         ((CharSet) == CHINESEBIG5_CHARSET) ? TRUE :    \
                                         ((CharSet) == GB2312_CHARSET)      ? TRUE : FALSE )

#define IS_DBCS_CODEPAGE( CodePage )     (((CodePage) == NATIVE_CODEPAGE) ? TRUE : FALSE )
#define IS_ANY_DBCS_CODEPAGE( CodePage ) (((CodePage) == 932) ? TRUE :    \
                                          ((CodePage) == 944) ? TRUE :    \
                                          ((CodePage) == 950) ? TRUE :    \
                                          ((CodePage) == 936) ? TRUE : FALSE )
#endif // DBCS


typedef struct _ENUMFONTDATA {      // efd
    EXTLOGFONTA elf;
    NEWTEXTMETRICA tm;
    FLONG       flType;
} ENUMFONTDATA;

typedef ENUMFONTDATA *PENUMFONTDATA;



/*********************************Struct***********************************\
* struct ENUMFONTDATAW
*
* Information for the callback function used by EnumFontsW
*
*   lfw     LOGFONTW structure corresponding to one of the enumerated fonts.
*
*   tmw     The corresponding TEXTMETRICW structure for the LOGFONTW above.
*
*   flType  Flags are set as follows:
*
*               DEVICE_FONTTYPE is set if font is device-based (as
*               opposed to IFI-based).
*
*               RASTER_FONTTYPE is set if font is bitmap type.
*
* History:
*  Wed 04-Sep-1991 -by- Bodin Dresevic [BodinD]
* Wrote it.
\**************************************************************************/

typedef struct _ENUMFONTDATAW {      // efdw
    EXTLOGFONTW   elfw;
    NTMW_INTERNAL ntmi;
    FLONG         flType;
} ENUMFONTDATAW, *PENUMFONTDATAW;



//
// Function prototypes
//

BOOL bDisableDisplay(HDEV hdev);
VOID vEnableDisplay(HDEV hdev);

BOOL  bDeleteSurface(HSURF hsurf);
BOOL  bSetBitmapOwner(HBITMAP hbm,LONG lPid);
BOOL  bSetBrushOwner(HBRUSH hbr,LONG lPid);
BOOL  bSetPaletteOwner(HPALETTE hpal, LONG lPid);
BOOL  bSetLFONTOwner(HFONT hlfnt, LONG pid);

LBOOL bDeleteRegion(HRGN hrgn);
LBOOL bSetRegionOwner(HRGN hrgn,LONG lPid);
LONG iCombineRectRgn(HRGN hrgnTrg,HRGN hrgnSrc,PRECTL prcl,LONG iMode);

BOOL bGetFontPathName
(
LPWSTR *ppwszPathName,     // place to store the result, full path of the font file
PWCHAR awcPathName,         // ptr to the buffer on the stack, must be MAX_PATH in length
LPWSTR pwszFileName         // file name, possibly  bare name that has to be tacked onto the path
);

HFONT  WINAPI ExtCreateFontIndirectA(LPEXTLOGFONTA);
HFONT  WINAPI ExtCreateFontIndirectW(LPEXTLOGFONTW);

typedef struct _CURSINFO /* ci */
{
    SHORT   xHotspot;
    SHORT   yHotspot;
    HBITMAP hbmMask;      // AND/XOR bits
    HBITMAP hbmColor;
    FLONG   flMode;
} CURSINFO, *PCURSINFO;

ULONG APIENTRY GreGetDriverModes(PWSZ pwszDriver, HANDLE hDriver, ULONG cjSize, DEVMODEW *pdm);

ULONG APIENTRY GreSaveScreenBits(HDEV hdev, ULONG iMode, ULONG iIdent, RECTL *prcl);
VOID APIENTRY GreSetPointer(HDEV hdev,PCURSINFO pci,ULONG fl);
VOID APIENTRY GreMovePointer(HDEV hdev,int x,int y);

HDC  _UserOpenDisplay(LPSTR pszDriver, ULONG iType);
BOOL UserScreenAccessCheck(VOID);
HRGN UserGetClientRgn(HWND hwnd, LPRECT lprc);
BOOL UserGetHwnd(HDC hdc, HWND *phwnd, PVOID *ppwo);
VOID UserAssociateHwnd(HWND hwnd, PVOID pwo);

#define GGB_ENABLE_WINMGR       0x00000001
#define GGB_DISABLE_WINMGR      0x00000002

BOOL APIENTRY GreGetBounds(HDC hdc, LPRECT lprcBounds, DWORD fl);
BOOL APIENTRY GreIntersectVisRect(HDC,int,int,int,int);

#define SVR_DELETEOLD           0x00000001
#define SVR_COPYNEW             0x00000002
#define SVR_ORIGIN              0x00000004


// private flags in low bits of hdc returned from GreCreateDCW

#define GRE_DISPLAYDC   1
#define GRE_PRINTERDC   2
#define GRE_OWNDC 1


enum DCTYPE {DCTYPE_DIRECT,DCTYPE_MEMORY,DCTYPE_INFO};

HDEV hdevOpenDisplayDevice(
PWSZ      pwszDriver,       // The device driver name.
PDEVMODEW pdriv,            // Driver data.
HANDLE    hScreen,          // Handle to the base driver.
BOOL      bDefaultDisplay,  // Is this the default display device.
PRTL_CRITICAL_SECTION *hsem); // Pointer to a variable for the semaphore pointer

LBOOL GreLoadLayeredDisplayDriver(
PWSZ      pwszDriver     // The device driver name.
);

LBOOL bCloseDisplayDevice(HDEV hdev);

HDC hdcOpenDisplayDC(HDEV hdev,ULONG iType);

HDC hdcCloneDC(HDC hdc,ULONG iType);

LBOOL bCloseDC(HDC hdc);

LBOOL bSetDCOwner(HDC hdc,LONG lPid);
DWORD sidGetObjectOwner(HDC hdc, DWORD objType);
LBOOL bSetupDC(HDC hdc,FLONG fl);

#define SETUPDC_CLEANDC         0x00000040
#define SETUPDC_RESERVE         0x00000080

BOOL APIENTRY GreConsoleTextOut
(
  HDC        hdc,
  POLYTEXTW *lpto,
  UINT       nStrings,
  RECTL     *prclBounds
);

#define UTO_NOCLIP 0x0001

BOOL APIENTRY GreUserTextOut
(
  HDC        hdc,
  POLYTEXTW *lpto,
  UINT       nStrings,
  RECTL     *prclBounds,
  FLONG      fl
);

// following values are guaranteed never to be REAL PID's.
// If new values are needed, CSR\SERVER\PROCESS.C
// #define FIRST_SEQUENCE_COUNT reserves these.

#define PID_PUBLIC          0
#define PID_CURRENT         1
#define PID_NOOWNER         2
#define PID_TERMINATION     3

// object ownership flags

#define OBJECTOWNER_ERROR   ((DWORD)-1)
#define OBJECTOWNER_PUBLIC  PID_PUBLIC
#define OBJECTOWNER_CURRENT PID_CURRENT
#define OBJECTOWNER_NONE    PID_NOOWNER

// Server entry point for font enumeration.

ULONG APIENTRY ulEnumFontOpen(
    HDC hdc,                    // device to enumerate on
    BOOL bEnumFonts,            // flag indicates old style EnumFonts()
    FLONG flWin31Compat,        // Win3.1 compatibility flags
    COUNT cwchMax,              // maximum name length (for paranoid CSR code)
    PWSZ pwszName);             // font name to enumerate

LBOOL APIENTRY bEnumFontChunk(
    HDC             hdc,        // device to enumerate on
    ULONG           idEnum,
    COUNT           cefdw,      // (in) capacity of buffer
    COUNT           *pcefdw,    // (out) number of ENUMFONTDATAs returned
    PENUMFONTDATAW  pefdw);     // return buffer

LBOOL APIENTRY bEnumFontClose(
    ULONG   idEnum);            // enumeration id

// Server entry points for adding/removing font resources.

COUNT APIENTRY cLoadFontResData(
    COUNT    cwchPathname,
    PWSZ     pwszPathname,
    ULONG    iResource,
    SIZE_T   cjFontRes,
    PVOID    pvFontRes);

LBOOL APIENTRY bUnloadFont(
    PWSZ     pwszPathname,
    ULONG    iResource);


// Private Control Panel entry point to enumerate fonts by file.

#define GFRI_NUMFONTS       0L
#define GFRI_DESCRIPTION    1L
#define GFRI_LOGFONTS       2L
#define GFRI_ISTRUETYPE     3L
#define GFRI_TTFILENAME     4L
#define GFRI_ISREMOVED      5L
#ifdef DBCS // for GetFontResourceInfo()
#define GFRI_FONTMETRICS    6L
#endif // DBCS


BOOL WINAPI GetFontResourceInfo(           // client side
    LPSTR   lpPathname,
    LPDWORD lpBytes,
    LPVOID  lpBuffer,
    DWORD   iType);

BOOL APIENTRY GreGetFontResourceInfo(        // server side
    LPSTR   lpPathname,
    LPDWORD lpBytes,
    LPVOID  lpBuffer,
    DWORD   iType);

BOOL WINAPI GetFontResourceInfoW(          // client side
    LPWSTR  lpPathname,
    LPDWORD lpBytes,
    LPVOID  lpBuffer,
    DWORD   iType);

BOOL APIENTRY GreGetFontResourceInfoW(       // server side
    LPWSTR  lpPathname,
    LPDWORD lpBytes,
    LPVOID  lpBuffer,
    DWORD   iType);


// Private Control Panel entry point to configure font enumeration.

#define FE_FILTER_NONE      0L
#define FE_FILTER_TRUETYPE  1L

ULONG WINAPI SetFontEnumeration (       // client side
    ULONG   ulType);

ULONG APIENTRY GreSetFontEnumeration (    // server side
    ULONG   ulType);


// Prototypes for GRE calls.
// These are the entry points to be called for maximum performance on the
// server side.

typedef struct
{
// Selected Objects (Server Handles).

    HBRUSH  hbrush;             // Brush.
    HFONT   hfont;              // Logical Font.

// Selected Attributes.

    ULONG   iBkColor;           // Background color.
    ULONG   iTextColor;         // Text color.
    ULONG   iBkMode;            // Background mix mode.
    FLONG   flTextAlign;        // Text allignment options.
} ATTR,*PATTR;


typedef struct _DEVBITMAPINFO  /* dbmi */
{
    ULONG   iFormat;            /* Format (eg. BITMAP_FORMAT_DEVICE)*/
    ULONG   cxBitmap;           /* Bitmap width in pels             */
    ULONG   cyBitmap;           /* Bitmap height in pels            */
    ULONG   cjBits;             /* Size of bitmap in bytes          */
    HPALETTE hpal;              /* handle to palette                */
    FLONG   fl;                 /* How to orient the bitmap         */
} DEVBITMAPINFO, *PDEVBITMAPINFO;

// client/server devcaps structure

typedef struct _DEVCAPS
{
    LONG ulVersion;
    LONG ulTechnology;
    LONG ulHorzSizeM;
    LONG ulVertSizeM;
    LONG ulHorzSize;
    LONG ulVertSize;
    LONG ulHorzRes;
    LONG ulVertRes;
    LONG ulBitsPixel;
    LONG ulPlanes;
    LONG ulNumPens;
    LONG ulNumFonts;
    LONG ulNumColors;
    LONG ulRasterCaps;
    LONG ulAspectX;
    LONG ulAspectY;
    LONG ulAspectXY;
    LONG ulLogPixelsX;
    LONG ulLogPixelsY;
    LONG ulSizePalette;
    LONG ulColorRes;
    LONG ulPhysicalWidth;
    LONG ulPhysicalHeight;
    LONG ulPhysicalOffsetX;
    LONG ulPhysicalOffsetY;
    LONG ulTextCaps;
    LONG ulVRefresh;
    LONG ulDesktopHorzRes;
    LONG ulDesktopVertRes;
    LONG ulBltAlignment;
} DEVCAPS, *PDEVCAPS;

// SAMEHANDLE/DIFFHANDLE macros
//
// These macros should be used to compare engine handles (such as HDCs, etc),
// when insensitivity to the user defined bits are needed.

#define USER_BITS      2
#define SAMEHANDLE(H,K) (((((ULONG) (H)) ^ ((ULONG) (K))) >> USER_BITS) == 0)
#define DIFFHANDLE(H,K) (((((ULONG) (H)) ^ ((ULONG) (K))) >> USER_BITS) != 0)

#define PPB_NOCLIP 0x0001   /* PolyPatBlt flag */

BOOL  APIENTRY GreArc(HDC,int,int,int,int,int,int,int,int);
BOOL  APIENTRY GreArcTo(HDC,int,int,int,int,int,int,int,int);
BOOL  APIENTRY GreBitBlt(HDC,int,int,int,int,HDC,int,int,DWORD,DWORD);
BOOL  APIENTRY GreChord(HDC,int,int,int,int,int,int,int,int);
BOOL  APIENTRY GreEllipse(HDC,int,int,int,int);
COUNT APIENTRY GreEnumObjects(HDC, int, SIZE_T, PVOID);
BOOL  APIENTRY GreExtFloodFill(HDC,int,int,COLORREF,UINT);
BOOL  APIENTRY GreFillRgn(HDC,HRGN,HBRUSH);
BOOL  APIENTRY GreFloodFill(HDC,int,int,COLORREF);
BOOL  APIENTRY GreFrameRgn(HDC,HRGN,HBRUSH,int,int);
BOOL  APIENTRY GreLineTo(HDC,int,int);
BOOL  APIENTRY GreMaskBlt(HDC,int,int,int,int,HDC,int,int,HBITMAP,int,int,DWORD,DWORD);
BOOL  APIENTRY GrePlgBlt(HDC,LPPOINT,HDC,int,int,int,int,HBITMAP,int,int,DWORD);
BOOL  APIENTRY GrePatBlt(HDC,int,int,int,int,DWORD);
BOOL  APIENTRY GrePolyPatBlt(HDC hdc,ULONG cRects,RECTL *prcl,ULONG rop,FLONG fl);
BOOL  APIENTRY GrePie(HDC,int,int,int,int,int,int,int,int);
BOOL  APIENTRY GrePaintRgn(HDC,HRGN);
BOOL  APIENTRY GreRectangle(HDC,int,int,int,int);
BOOL  APIENTRY GreRoundRect(HDC,int,int,int,int,int,int);
BOOL  APIENTRY GreStretchBlt(HDC,int,int,int,int,HDC,int,int,int,int,DWORD,DWORD);
BOOL  APIENTRY GreAngleArc(HDC,int,int,DWORD,FLOAT,FLOAT);
BOOL  APIENTRY GreExtTextOutW(HDC,int,int,UINT,LPRECT,LPWSTR,int,LPINT);
BOOL  APIENTRY GrePlayJournal(HDC,LPWSTR,ULONG,ULONG);
BOOL  APIENTRY GdiPlayJournal(HDC,LPWSTR,DWORD,DWORD,int);
BOOL  APIENTRY GrePolyPolygon(HDC,LPPOINT,LPINT,int);
BOOL  APIENTRY GrePolyPolyline(HDC, CONST POINT *,LPDWORD,DWORD);

BOOL  APIENTRY GrePolyBezierTo(HDC,LPPOINT,DWORD);
BOOL  APIENTRY GrePolylineTo(HDC,LPPOINT,DWORD);
BOOL  APIENTRY GreGetTextExtentW(HDC,LPWSTR,int,LPSIZE,UINT);
BOOL  APIENTRY GreGetTextExtentExW (HDC, LPWSTR, COUNT, ULONG, COUNT *, PULONG, LPSIZE);

BOOL  APIENTRY GreGetTextMetricsW(HDC, TMW_INTERNAL *);

int   APIENTRY GreGetTextFaceW(HDC,int,LPWSTR);

#define ETO_MASKPUBLIC  (ETO_OPAQUE | ETO_CLIPPED )   // public (wingdi.h) flag mask

BOOL  APIENTRY GrePolyTextOutW(HDC, POLYTEXTW *, UINT);

BOOL  APIENTRY GreSetAttrs(HDC hdc);
BOOL  APIENTRY GreSetFontXform(HDC,FLOAT,FLOAT);

BOOL  APIENTRY GreBeginPath(HDC);
BOOL  APIENTRY GreCloseFigure(HDC);
BOOL  APIENTRY GreEndPath(HDC);
BOOL  APIENTRY GreAbortPath(HDC);
BOOL  APIENTRY GreFillPath(HDC);
BOOL  APIENTRY GreFlattenPath(HDC);
int   APIENTRY GreGetPath( HDC,LPPOINT,LPBYTE,int,LPINT);
HRGN  APIENTRY GrePathToRegion(HDC);
BOOL  APIENTRY GrePolyDraw(HDC,LPPOINT,LPBYTE,ULONG);
BOOL  APIENTRY GreSelectClipPath(HDC,int);
int   APIENTRY GreSetArcDirection(HDC,int);
int   APIENTRY GreGetArcDirection(HDC);
BOOL  APIENTRY GreSetMiterLimit(HDC,FLOAT,PFLOAT);
BOOL  APIENTRY GreGetMiterLimit(HDC,PFLOAT);
BOOL  APIENTRY GreStrokeAndFillPath(HDC);
BOOL  APIENTRY GreStrokePath(HDC);
BOOL  APIENTRY GreWidenPath(HDC);

BOOL     APIENTRY GreAnimatePalette(HPALETTE, UINT, UINT, CONST PALETTEENTRY *);
BOOL     APIENTRY GreAspectRatioFilter(HDC, LPSIZE);
BOOL     APIENTRY GreCancelDC(HDC);
int      APIENTRY GreChoosePixelFormat(HDC, UINT, CONST PIXELFORMATDESCRIPTOR *);
int      APIENTRY GreCombineRgn(HRGN, HRGN, HRGN, int);
BOOL     APIENTRY GreCombineTransform(LPXFORM, LPXFORM, LPXFORM);
HBITMAP  APIENTRY GreCreateBitmap(int, int, UINT, UINT, LPBYTE);
HBITMAP  APIENTRY GreCreateCompatibleBitmap(HDC, int, int);
HDC      APIENTRY GreCreateCompatibleDC(HDC);

HDC      APIENTRY GreCreateDCW(LPWSTR, LPWSTR, LPWSTR, LPDEVMODEW, BOOL);
HBRUSH   APIENTRY GreCreateDIBPatternBrush(HGLOBAL, DWORD);
HBRUSH   APIENTRY GreCreateDIBPatternBrushPt(LPVOID, DWORD);
HBITMAP  APIENTRY GreCreateDIBitmap(HDC, LPBITMAPINFOHEADER, DWORD, LPBYTE, LPBITMAPINFO, DWORD);
HRGN     APIENTRY GreCreateEllipticRgn(int, int, int, int);
HRGN     APIENTRY GreCreateEllipticRgnIndirect(LPRECT);

HFONT    APIENTRY GreCreateFontIndirectW(LPLOGFONTW);

HFONT    APIENTRY GreExtCreateFontIndirectW(LPEXTLOGFONTW);
HBRUSH   APIENTRY GreCreateHatchBrush(ULONG, COLORREF);
HPALETTE APIENTRY GreCreatePalette(LPLOGPALETTE);
HBRUSH   APIENTRY GreCreatePatternBrush(HBITMAP);
HPEN     APIENTRY GreCreatePen(int, int, COLORREF,HBRUSH);
HPEN     APIENTRY GreExtCreatePen(ULONG, ULONG, ULONG, ULONG, LONG, ULONG, PULONG, ULONG, BOOL, HBRUSH);
HPEN     APIENTRY GreCreatePenIndirect(LPLOGPEN);
HRGN     APIENTRY GreCreatePolyPolygonRgn(CONST POINT *, CONST INT *, int, int);
HRGN     APIENTRY GreCreatePolygonRgn(CONST POINT *, int, int);
HRGN     APIENTRY GreCreateRectRgn(int, int, int, int);
HRGN     APIENTRY GreCreateRectRgnIndirect(LPRECT);
HRGN     APIENTRY GreCreateRoundRectRgn(int, int, int, int, int, int);
HBRUSH   APIENTRY GreCreateSolidBrush(COLORREF);
BOOL     APIENTRY GreCreateScalableFontResourceW(FLONG, LPWSTR, LPWSTR, LPWSTR);
BOOL     APIENTRY GreDPtoLP(HDC, LPPOINT, int);

BOOL     APIENTRY GreDeleteObject(HANDLE);
BOOL     APIENTRY GreDeleteDCInternal(HDC hdc, BOOL bForce);
BOOL     APIENTRY GreDeleteDC(HDC hdc);
int      APIENTRY GreDescribePixelFormat(HDC hdc,int ipfd,UINT cjpfd,PPIXELFORMATDESCRIPTOR ppfd);

int      APIENTRY GreDeviceCapabilities(LPSTR, LPSTR, LPSTR, int, LPSTR, LPDEVMODE);
int      APIENTRY GreDrawEscape(HDC,int,int,LPSTR);
BOOL     APIENTRY GreEqualRgn(HRGN, HRGN);
int      APIENTRY GreExtEscape(HDC,int,int,LPSTR,int,LPSTR);
int      APIENTRY GreExcludeClipRect(HDC, int, int, int, int);
BOOL     APIENTRY GreGetAspectRatioFilter(HDC, LPSIZE);
BOOL     APIENTRY GreGetBitmapDimension(HBITMAP, LPSIZE);
int      APIENTRY GreGetBkMode(HDC);
DWORD    APIENTRY GreGetBoundsRect(HDC, LPRECT, DWORD);
BOOL     APIENTRY GreGetBrushOrg(HDC, LPPOINT);
BOOL     APIENTRY GreGetCharWidthW(HDC hdc, UINT wcFirstChar, UINT cwc, PWCHAR pwc, UINT fl, PVOID lpBuffer);

BOOL     APIENTRY GreGetCharABCWidthsW(
            HDC,           // hdc
            UINT,          // wcFirst
            COUNT,         // cwc
            PWCHAR,        // pwc to buffer with chars to convert
            BOOL,          // bInt
            PVOID);        // abc or abcf


int      APIENTRY GreGetClipBox(HDC, LPRECT, BOOL);
int      APIENTRY GreGetAppClipBox(HDC, LPRECT);
COLORREF APIENTRY GreGetBkColor(HDC);
COLORREF APIENTRY GreGetBrushColor(HBRUSH);
BOOL     APIENTRY GreGetColorAdjustment(HDC, PCOLORADJUSTMENT);
BOOL     APIENTRY GreGetCurrentPosition(HDC, LPPOINT);
UINT     APIENTRY GreGetDIBColorTable(HDC hdc, UINT iStart, UINT cEntries, RGBQUAD *pRGB);
BOOL     APIENTRY GreGetDCOrg(HDC, LPPOINT);
int      APIENTRY GreGetDeviceCaps(HDC, int);
int      APIENTRY GreGetGraphicsMode(HDC hdc);
HFONT    APIENTRY GreGetHFONT(HDC);
SIZE_T   APIENTRY GreGetFontData(HDC, DWORD, DWORD, PVOID, SIZE_T);
SIZE_T   APIENTRY GreGetGlyphOutline(HDC, WCHAR, UINT, LPGLYPHMETRICS, SIZE_T, PVOID, LPMAT2);
int      APIENTRY GreGetMapMode(HDC);
COLORREF APIENTRY GreGetNearestColor(HDC, COLORREF);
UINT     APIENTRY GreGetNearestPaletteIndex(HPALETTE, COLORREF);

SIZE_T   APIENTRY GreGetOutlineTextMetricsW(HDC, SIZE_T, OUTLINETEXTMETRICW *, TMDIFF *);
int      APIENTRY GreExtGetObjectW(HANDLE, int, LPVOID);

UINT     APIENTRY GreGetPaletteEntries(HPALETTE, UINT, UINT, LPPALETTEENTRY);
DWORD    APIENTRY GreGetPixel(HDC, int, int);
int      APIENTRY GreGetPixelFormat(HDC);
ULONG    APIENTRY GreGetResourceId(HDEV, ULONG, ULONG);
int      APIENTRY GreGetRgnBox(HRGN, LPRECT);
HANDLE   APIENTRY GreGetStockObject(int);
UINT     APIENTRY GreGetSystemPaletteEntries(HDC, UINT, UINT, LPPALETTEENTRY);
UINT     APIENTRY GreGetSystemPaletteUse(HDC);
UINT     APIENTRY GreGetTextAlign(HDC);
int      APIENTRY GreGetTextCharacterExtra(HDC);
COLORREF APIENTRY GreGetTextColor(HDC);
BOOL     APIENTRY GreGetViewportExt(HDC, LPSIZE);
BOOL     APIENTRY GreGetViewportOrg(HDC, LPPOINT);
BOOL     APIENTRY GreGetWindowExt(HDC, LPSIZE);
BOOL     APIENTRY GreGetWindowOrg(HDC, LPPOINT);
BOOL     APIENTRY GreGetWorldTransform(HDC, LPXFORM);
BOOL     APIENTRY GreGetTransform(HDC, DWORD, LPXFORM);
BOOL     APIENTRY GreSetVirtualResolution(HDC, int, int, int, int);
int      APIENTRY GreIntersectClipRect(HDC, int, int, int, int);
HRGN     APIENTRY GreInquireRgn(HDC hdc);
HRGN     APIENTRY GreInquireVisRgn(HDC hdc);
BOOL     APIENTRY GreInvertRgn(HDC, HRGN);
BOOL     APIENTRY GreLPtoDP(HDC, LPPOINT, int);
BOOL     APIENTRY GreModifyWorldTransform(HDC,LPXFORM, DWORD);
BOOL     APIENTRY GreMoveTo(HDC, int, int, LPPOINT);
int      APIENTRY GreOffsetClipRgn(HDC, int, int);
int      APIENTRY GreOffsetRgn(HRGN, int, int);
BOOL     APIENTRY GreOffsetViewportOrg(HDC, int, int, LPPOINT);
BOOL     APIENTRY GreOffsetWindowOrg(HDC, int, int, LPPOINT);
BOOL     APIENTRY GrePolyBezier (HDC, LPPOINT, ULONG);
BOOL     APIENTRY GrePtInRegion(HRGN, int, int);
BOOL     APIENTRY GrePtVisible(HDC, int, int);
int      APIENTRY GreRealizePalette(HDC);
BOOL     APIENTRY GreRectInRegion(HRGN, LPRECT);
BOOL     APIENTRY GreRectVisible(HDC, LPRECT);

BOOL     APIENTRY GreResetDC(HDC, LPDEVMODEW);
BOOL     APIENTRY GreResizePalette(HPALETTE, UINT);
BOOL     APIENTRY GreRestoreDC(HDC, int);
int      APIENTRY GreSaveDC(HDC);
BOOL     APIENTRY GreScaleViewportExt(HDC, int, int, int, int, LPSIZE);
BOOL     APIENTRY GreScaleWindowExt(HDC, int, int, int, int, LPSIZE);
int      APIENTRY GreExtSelectClipRgn(HDC, HRGN, int);
HRGN     APIENTRY GreSelectVisRgn(HDC, HRGN, PRECTL, ULONG);
HPALETTE APIENTRY GreSelectPalette(HDC, HPALETTE, BOOL);

HBRUSH   APIENTRY GreSelectBrush(HDC,HBRUSH);
HPEN     APIENTRY GreSelectPen(HDC,HPEN);
HBITMAP  APIENTRY GreSelectBitmap(HDC,HBITMAP);
HFONT    APIENTRY GreSelectFont(HDC hdc, HFONT hlfntNew);
COLORREF APIENTRY GreSetBkColor(HDC, COLORREF);
int      APIENTRY GreSetBkMode(HDC, int);
LONG     APIENTRY GreSetBitmapBits(HBITMAP, ULONG, PBYTE, PLONG);
BOOL     APIENTRY GreSetBitmapDimension(HBITMAP, int, int, LPSIZE);
DWORD    APIENTRY GreSetBoundsRect(HDC, LPRECT, DWORD);
BOOL     APIENTRY GreSetBrushOrg(HDC, int, int, LPPOINT);
BOOL     APIENTRY GreSetColorAdjustment(HDC, PCOLORADJUSTMENT);
UINT     APIENTRY GreSetDIBColorTable(HDC, UINT, UINT, RGBQUAD *);
int      APIENTRY GreSetDIBits(HDC, HBITMAP, UINT, UINT, LPBYTE, LPBITMAPINFO, UINT);
int      APIENTRY GreSetDIBitsToDevice(HDC, int, int, DWORD, DWORD, int, int, DWORD, DWORD, LPBYTE, LPBITMAPINFO, DWORD);
int      APIENTRY GreGetDIBitsInternal(HDC hdc, HBITMAP hBitmap, UINT iStartScan, UINT cNumScan, LPBYTE pjBits, LPBITMAPINFO pBitsInfo, UINT iUsage, UINT cjMaxBits, UINT cjMaxInfo);
int      APIENTRY GreSetGraphicsMode(HDC hdc, int iMode);
int      APIENTRY GreSetMapMode(HDC, int);
DWORD    APIENTRY GreSetMapperFlags(HDC, DWORD);
UINT     APIENTRY GreSetPaletteEntries(HPALETTE, UINT, UINT, CONST PALETTEENTRY *);
COLORREF APIENTRY GreSetPixel(HDC, int, int, COLORREF);
BOOL     APIENTRY GreSetPixelV(HDC, int, int, COLORREF);
BOOL     APIENTRY GreSetPixelFormat(HDC, int);
BOOL     APIENTRY GreSetRectRgn(HRGN, int, int, int, int);
BOOL     APIENTRY GreSetSolidBrush(HBRUSH hbr, COLORREF clr);
UINT     APIENTRY GreSetSystemPaletteUse(HDC, UINT);
UINT     APIENTRY GreSetTextAlign(HDC, UINT);
int      APIENTRY GreSetStretchBltMode(HDC, int);
HPALETTE APIENTRY GreCreateHalftonePalette(HDC hdc);
int      APIENTRY GreSetTextCharacterExtra(HDC, int);
COLORREF APIENTRY GreSetTextColor(HDC, COLORREF);
BOOL     APIENTRY GreSetTextJustification(HDC, int, int);
BOOL     APIENTRY GreSetViewportExt(HDC, int, int, LPSIZE);
BOOL     APIENTRY GreSetViewportOrg(HDC, int, int, LPPOINT);
BOOL     APIENTRY GreSetWindowOrg(HDC hdc, int x, int y, LPPOINT pPoint);
BOOL     APIENTRY GreSetWindowExt(HDC, int, int, LPSIZE);
BOOL     APIENTRY GreSetWorldTransform(HDC, LPXFORM);
int      APIENTRY GreStretchDIBits(HDC, int, int, int, int, int, int, int, int, LPBYTE, LPBITMAPINFO, DWORD, DWORD);
BOOL     APIENTRY GreSystemFontSelected(HDC, BOOL);
BOOL     APIENTRY GreSwapBuffers(HDC hdc);
BOOL     APIENTRY GreUnrealizeObject(HANDLE);
BOOL     APIENTRY GreUpdateColors(HDC);

// Prototypes for wgl and OpenGL calls

HGLRC    APIENTRY GreCreateRC(HDC);
BOOL     APIENTRY GreMakeCurrent(HDC, HGLRC);
BOOL     APIENTRY GreDeleteRC(HGLRC);
BOOL     APIENTRY GreSwapBuffers(HDC);
BOOL     APIENTRY GreGlAttention(VOID);
BOOL     APIENTRY glsrvDuplicateSection(ULONG, HANDLE);
void     APIENTRY glsrvThreadExit(void);
BOOL     bSetRCOwner(HGLRC hglrc,LONG lPid);

HANDLE   hGetCursorTimer();
DWORD    GDIRealizePalette(HDC hdc);
void     vPingPong();

#ifndef NOMETAFILE
HANDLE  APIENTRY GreCreateServerMetaFile(DWORD iType, ULONG cbData, LPBYTE lpClientData,
            DWORD mm, DWORD xExt, DWORD yExt);
ULONG   APIENTRY GreGetServerMetaFileBits(HANDLE hmo, ULONG cbData, LPBYTE lpClientData,
            PDWORD piType, PDWORD pmm, PDWORD pxExt, PDWORD pyExt);
BOOL    APIENTRY GreDeleteServerMetaFile(HANDLE);
#endif /* NOMETAFILE */

// these should disappear as should all other functions that contain references
// to ansi strings

BOOL  APIENTRY GreGetTextExtent(HDC,LPSTR,int,LPSIZE,UINT);
BOOL  APIENTRY GreExtTextOut(HDC,int,int,UINT,LPRECT,LPSTR,int,LPINT);
BOOL  APIENTRY GreTextOut(HDC,int,int,LPSTR,int);

// Aldus Escape
BOOL   APIENTRY GreGetETM(HDC hdc, EXTTEXTMETRIC *petm);


// these stay

// flags for AddFontResourceW
// AFRW_ADD_LOCAL_FONT : add ONLY if it is a local font
// AFRW_ADD_REMOTE_FONT: add ONLY if it is NOT local font
// if neither one LOCAL or REMOTE bit is set, just add the font

#define AFRW_SEARCH_PATH     0X01
#define AFRW_ADD_LOCAL_FONT  0X02
#define AFRW_ADD_REMOTE_FONT 0X04


int   APIENTRY GreAddFontResourceW(LPWSTR, FLONG);
BOOL  APIENTRY GreRemoveFontResourceW(LPWSTR, BOOL);
VOID vGetFontList(VOID *pvBuffer, COUNT *pNumFonts, UINT *pSize);
BOOL  GreMatchFont(LPWSTR pwszBareName, LPWSTR pwszFontPathName);

// used in clean up at log-off time

BOOL  APIENTRY GreRemoveAllButPermanentFonts();

#ifdef  DBCS
UINT    APIENTRY GreGetCharSet(HDC);
#endif

#ifdef FONTLINK /*EUDC*/
UINT    APIENTRY GreGetEUDCTimeStamp();
UINT    APIENTRY GreEudcQueryLinkW( LPWSTR );
BOOL    APIENTRY GreEudcUnloadLinkW();
BOOL    APIENTRY GreEudcLoadLinkW( LPWSTR, COUNT );
#endif


BOOL     APIENTRY GreStartPage(HDC);
BOOL     APIENTRY GreEndPage(HDC);
int      APIENTRY GreStartDoc(HDC, DOCINFOW *);
BOOL     APIENTRY GreEndDoc(HDC);
BOOL     APIENTRY GreAbortDoc(HDC);

// Prototypes for GDI local helper functions.  These are only available on
// the client side.

HBITMAP     GdiConvertBitmap(HBITMAP hbm);
HBRUSH      GdiConvertBrush(HBRUSH hbrush);
HPALETTE    GdiConvertPalette(HPALETTE hpal);
HFONT       GdiConvertFont(HFONT hfnt);
HRGN        GdiConvertRegion(HRGN hrgn);
HDC         GdiConvertDC(HDC hdc);
HBRUSH      GdiConvertBrush(HBRUSH hbrush);
HANDLE      GdiConvertMetaFilePict(HANDLE hmfp);
HANDLE      GdiConvertEnhMetaFile(HENHMETAFILE hmf);
HDC         GdiGetLocalDC(HDC hdcRemote);
HDC         GdiCreateLocalDC(HDC hdcRemote);
BOOL        GdiReleaseLocalDC(HDC hdcLocal);
HBITMAP     GdiCreateLocalBitmap();
HBRUSH      GdiCreateLocalBrush(HBRUSH hbrushRemote);
HRGN        GdiCreateLocalRegion(HRGN hrgnRemote);
HFONT       GdiCreateLocalFont(HFONT hfntRemote);
HANDLE      GdiCreateLocalMetaFilePict(HANDLE hRemote);
HENHMETAFILE   GdiCreateLocalEnhMetaFile(HANDLE hRemote);
HPALETTE    GdiCreateLocalPalette(HPALETTE hpalRemote);
VOID        GdiAssociateObject(ULONG hLocal,ULONG hRemote);
VOID        GdiDeleteLocalObject(ULONG h);
BOOL        GdiSetAttrs(HDC);
HANDLE      SelectFontLocal(HDC, HANDLE);
HANDLE      SelectBrushLocal(HDC, HANDLE);
HFONT       GdiGetLocalFont(HFONT);
HBRUSH      GdiGetLocalBrush(HBRUSH);
HDC         GdiCloneDC(HDC hdc, UINT iType);
BOOL        GdiPlayScript(PULONG pulScript,ULONG cjScript,PULONG pulEnv,ULONG cjEnv,PULONG pulOutput,ULONG cjOutput,ULONG cLimit);
BOOL        GdiPlayDCScript(HDC hdc,PULONG pulScript,ULONG cjScript,PULONG pulOutput,ULONG cjOutput,ULONG cLimit);
HDC         GdiConvertAndCheckDC(HDC hdc);
BOOL        GdiIsMetaFileDC(HDC hdc);

// Private indicies for GetStockObject over the CS interface.

#define PRIV_STOCK_BITMAP       (STOCK_LAST + 1)
#define PRIV_STOCK_LAST         PRIV_STOCK_BITMAP

// Return codes from server-side ResetDC

#define RESETDC_ERROR   0
#define RESETDC_FAILED  1
#define RESETDC_SUCCESS 2

// GetTransform flags.

#define XFORM_WORLD_TO_PAGE       0x0203
#define XFORM_WORLD_TO_DEVICE     0x0204
#define XFORM_PAGE_TO_DEVICE      0x0304
#define XFORM_PAGE_TO_WORLD       0x0302
#define XFORM_DEVICE_TO_WORLD     0x0402
#define XFORM_DEVICE_TO_PAGE      0x0403

#define PFNNULL ((PFN) NULL)
#define PSZNULL ((PSZ) NULL)

// Object types

#define DEF_TYPE        0
#define DC_TYPE         1
#define LDB_TYPE        2
#define PDB_TYPE        3
#define RGN_TYPE        4
#define SURF_TYPE       5
#define XFORM_TYPE      6
#define PATH_TYPE       7
#define PAL_TYPE        8
#define FD_TYPE         9
#define LFONT_TYPE      10
#define RFONT_TYPE      11
#define PFE_TYPE        12
#define PFT_TYPE        13
#define IDB_TYPE        14
#define XLATE_TYPE      15
#define BRUSH_TYPE      16
#define PFF_TYPE        17
#define CACHE_TYPE      18
#define SPACE_TYPE      19
#define DBRUSH_TYPE     20
#define META_TYPE       21
#define EFSTATE_TYPE    22
#define BMFD_TYPE       23
#define VTFD_TYPE       24
#define TTFD_TYPE       25
#define RC_TYPE         26
#define TEMP_TYPE       27   // temporary memory - should always be at 0
#define DRVOBJ_TYPE     28
#define MAX_TYPE        28

// Object identifiers

#define DEF_IDENTIFIER      0x00000000
#define DC_IDENTIFIER       0x54434445  /* 'EDCT' */
#define SAVED_DC_IDENTIFIER 0x53434445  /* 'EDCS' */
#define LDEV_IDENTIFIER     0x5645444c  /* 'LDEV' */
#define PDEV_IDENTIFIER     0x56454450  /* 'PDEV' */
#define RGN_IDENTIFIER      0x4e474552  /* 'REGN' */
#define SURF_IDENTIFIER     0x46525553  /* 'SURF' */
#define XFORM_IDENTIFIER    0x4d524e58  /* 'XFRM' */
#define PAL_IDENTIFIER      0x4c415048  /* 'HPAL' */
#define XLATE_IDENTIFIER    0x54414C58  /* 'XLAT' */
#define FDEV_IDENTIFIER     0x56454446  /* 'FDEV' */
#define LFONT_IDENTIFIER    0x544E464C  /* 'LFNT' */
#define RFONT_IDENTIFIER    0x544E4652  /* 'RFNT' */
#define PFE_IDENTIFIER      0x5F454650  /* 'PFE_' */
#define PFF_IDENTIFIER      0x5F464650  /* 'PFF_' */
#define PFT_IDENTIFIER      0x5F544650  /* 'PFT_' */
#define IDB_IDENTIFIER      0x5F424449  /* 'IDB_' */
#define BRUSH_IDENTIFIER    0x48535242  /* 'BRSH' */
#define SPACE_IDENTIFIER    0x53504143  /* 'SPAC' */
#define CACHE_IDENTIFIER    0x48534143  /* 'CASH' */
#define DBRUSH_IDENTIFIER   0x53524244  /* 'DBRS' */
#define MFEN_IDENTIFIER     0x5845464D  /* 'MFEN' */
#define MFPICT_IDENTIFIER   0x5F50464D  /* 'MFP_' */
#define EFSTATE_IDENTIFIER  0x5F534645  /* 'EFS_' */

#define DCB_WINDOWMGR   0x00008000L

// Private calls for USER

BOOL APIENTRY GreValidateServerHandle(HANDLE hobj, ULONG ulType);
VOID APIENTRY GreMarkUndeletableDC(HDC hdc);
VOID APIENTRY GreMarkDeletableDC(HDC hdc);
BOOL APIENTRY GreGetAttrs(HDC hdc, PATTR pac);
int  APIENTRY GreGetClipRgn(HDC, HRGN);
BOOL APIENTRY GreSrcBlt(HDC, int, int, int, int, int, int);
int  APIENTRY GreSubtractRgnRectList(HRGN, LPRECT, LPRECT, int);
VOID APIENTRY GreMarkUndeletableFont(HFONT hfnt);
VOID APIENTRY GreMarkDeletableFont(HFONT hfnt);
BOOL APIENTRY bSetDevDragRect(HDEV, RECTL*, RECTL *);
BOOL APIENTRY bSetDevDragWidth(HDEV, ULONG, ULONG);
VOID APIENTRY GreMarkDCUnreadable(HDC);
VOID APIENTRY GreMarkDCReadable(HDC);
BOOL APIENTRY GreCopyBits(HDC,int,int,int,int,HDC,int,int);
VOID APIENTRY GreClientRgnUpdated();
VOID APIENTRY GreSetClientRgn(PVOID, HRGN, LPRECT);
VOID APIENTRY GreDeleteWnd(PVOID pwo);
ULONG APIENTRY GreSetROP2(HDC hdc,int iROP);
VOID APIENTRY GreMarkDeletableBrush(HBRUSH hbr);

// Private calls for metafiling

DWORD   APIENTRY GreGetRegionData(HRGN, DWORD, LPRGNDATA);
HRGN    APIENTRY GreExtCreateRegion(LPXFORM, DWORD, LPRGNDATA);
int     APIENTRY GreExtSelectMetaRgn(HDC, HRGN, int);
BOOL    APIENTRY GreMonoBitmap(HBITMAP);
HBITMAP APIENTRY GreGetObjectBitmapHandle(HBRUSH, UINT *);

int     APIENTRY GreGetRandomRgn(HDC, HRGN, int);
VOID    GreMarkUndeletableBrush(HBRUSH hbr);

#endif

// Win31 compatibility stuff
// GetAppCompatFlags flag values

#define GACF_IGNORENODISCARD    0x0001
#define GACF_FORCETEXTBAND      0x0002
#define GACF_ONELANDGRXBAND     0x0004
#define GACF_IGNORETOPMOST      0x0008
#define GACF_CALLTTDEVICE       0x0010
#define GACF_MULTIPLEBANDS      0x0020
#define GACF_ALWAYSSENDNCPAINT  0x0040
#define GACF_EDITSETTEXTMUNGE   0x0080
#define GACF_MOREEXTRAWNDWORDS  0x0100
#define GACF_TTIGNORERASTERDUPE 0x0200
#define GACF_HACKWINFLAGS       0x0400
#define GACF_DELAYHWHNDSHAKECHK 0x0800
#define GACF_ENUMHELVNTMSRMN    0x1000
#define GACF_ENUMTTNOTDEVICE    0x2000
#define GACF_SUBTRACTCLIPSIBS   0x4000
#define GACF_FORCETTGRAPHICS    0x8000
#define GACF_NOHRGN1            0x00010000
#define GACF_NCCALCSIZEONMOVE   0x00020000
#define GACF_SENDMENUDBLCLK     0x00040000
#define GACF_30AVGWIDTH         0x00080000

// GreGetTextExtentW flags

#define GGTE_WIN3_EXTENT        0x0001

// GreGetCharWidthW flags

#define GGCW_WIN3_WIDTH         0x0001
#define GGCW_INTEGER_WIDTH      0x0002


// TRAPEZOID REMNANTS
//
//  Tue 01-Feb-1994 11:58:50 by Kirk Olynyk [kirko]
//
// Trapeziod support has been officially removed from Daytona.
// However, there may be some drivers written before the
// removal that rely on this stuff. Soooo GDI has to support
// it still. So, I will remove all reference to trapeziods
// from winddi.h and put them here. This means that new
// drivers cannot use trapeziodal features but GDI can
// still support the old drivers that call trapezoidal
// type functions

#define GCAPS_TRAPPAINT         0x00100000

typedef struct _TRAPEZOID
{
    LONG     iScanTop;
    LONG     iScanBottom;
    POINTFIX ptfxLeftLo;
    POINTFIX ptfxLeftHi;
    POINTFIX ptfxRightLo;
    POINTFIX ptfxRightHi;
} TRAPEZOID;

#define TC_TRAPEZOIDS   1
#define CT_TRAPEZOIDS   1L

typedef struct _ENUMTRAPS
{
    ULONG       c;
    TRAPEZOID   atrap[1];
} ENUMTRAPS;

#define JD_ENUM_TRAPEZOID   1L

typedef struct _DDALIST
{
   LONG yTop;
   LONG yBottom;
   LONG axPairs[2];
} DDALIST;

#ifdef FONTLINK

typedef struct
{
    UINT uiWidth;
    UINT uiHeight;
    BYTE ajBits[1];
} STRINGBITMAP, *LPSTRINGBITMAP;


UINT GreGetStringBitmapW(
    HDC hdc,
    LPWSTR pwsz,
    UINT cwc,
    LPSTRINGBITMAP lpSB,
    UINT cj,
    UINT *puiOffset
);

UINT GetStringBitmapW(
    HDC hdc,
    LPWSTR pwsz,
    COUNT cwc,
    UINT cj,
    LPSTRINGBITMAP lpSB
);

UINT GetStringBitmapA(
    HDC hdc,
    LPCSTR psz,
    COUNT cbStr,
    UINT cj,
    LPSTRINGBITMAP lpSB
);

INT GetSystemEUDCRange (
    BYTE *pbEUDCLeadByteTable ,
    INT   cjSize
);


// Defines the key that holds the font link data.

#define     FONT_LINK_KEY       (LPWSTR) L"FontLink"
#endif /* _WINGDIP_ */
