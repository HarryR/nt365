/********************************************************************/
/**                     Microsoft LAN Manager                      **/
/**               Copyright(c) Microsoft Corp., 1990-1992          **/
/********************************************************************/
/* :ts=4 */

//***   Init.c - IP VxD init routines.
//
//  All C init routines are located in this file. We get
//  config. information, allocate structures, and generally get things going.

#include    "oscfg.h"
#include    "cxport.h"
#include    "ndis.h"
#include    "ip.h"
#include    "ipdef.h"
#include    "ipinit.h"
#include    "llipif.h"
#include	"arp.h"
#include    "info.h"
#include	"iproute.h"
#include	"iprtdef.h"
#include	"ipxmit.h"
#include	"icmp.h"


#define NUM_IP_NONHDR_BUFFERS  50

/* DEFAULT_RA_TIMEOUT — stripped with IP reassembly (was the in-flight
   reassembly TTL in seconds; no longer meaningful). */

#define DEFAULT_ICMP_BUFFERS 5

extern  IPConfigInfo    *IPGetConfig(void);
extern  void            IPFreeConfig(IPConfigInfo *);
extern  int             IsIPBCast(IPAddr, uchar);

// The IPRcv routine.
extern void IPRcv(void *, void *, uint , uint , NDIS_HANDLE , uint , uint );
// The transmit complete routine.
extern void IPSendComplete(void *, PNDIS_PACKET , NDIS_STATUS );
// Status indication routine.
extern void IPStatus(void *, NDIS_STATUS, void *, uint);
// Transfer data complete routine.
extern void IPTDComplete(void *, PNDIS_PACKET , NDIS_STATUS , uint );

extern void     IPRcvComplete(void);

extern IPAddr OpenRCE(IPAddr Address, IPAddr Src, RouteCacheEntry **RCE,
    uchar *Type, ushort *MSS, IPOptInfo *OptInfo);
extern void CloseRCE(RouteCacheEntry *);
extern void ICMPInit(uint);
extern void ICMPTimer(NetTableEntry *);
extern IP_STATUS SendICMPErr(IPAddr, IPHeader UNALIGNED *, uchar, uchar, ulong);
extern void TDUserRcv(void *, PNDIS_PACKET, NDIS_STATUS, uint);
/* extern void FreeRH(ReassemblyHeader *); — stripped with IP reassembly */
extern PNDIS_PACKET	GrowIPPacketList(void);
extern PNDIS_BUFFER	FreeIPPacket(PNDIS_PACKET Packet);

extern ulong GetGMTDelta(void);
extern ulong GetTime(void);

extern  NDIS_HANDLE BufferPool;
EXTERNAL_LOCK(HeaderLock)
EXTERNAL_LOCK(IPIDLock)
extern	NetTableEntry	*LoopNTE;

//NetTableEntry   *NetTable;          // Pointer to the net table.

NetTableEntry	*NetTableList;		// List of NTEs.
int     NumNTE;                     // Number of NTEs.
/* uchar RATimeout — stripped with IP reassembly */
ushort	NextNTEContext;				// Next NTE context to use.

#if 0
DEFINE_LOCK_STRUCTURE(PILock)
#endif

ProtInfo        IPProtInfo[MAX_IP_PROT];    // Protocol information table.
ProtInfo        *LastPI;                    // Last protinfo structure looked at.
int             NextPI;                     // Next PI field to be used.

ulong   TimeStamp;
ulong   TSFlag;

uint	DefaultTTL;
uint	DefaultTOS;

// Interface       *IFTable[MAX_IP_NETS];
Interface		*IFList;			// List of interfaces active.
Interface		*FirstIF;			// First 'real' IF.
ulong           NumIF;
IPSNMPInfo      IPSInfo;
uint			DHCPActivityCount = 0;

#ifdef NT

extern  NameMapping			*AdptNameTable;
extern	DriverRegMapping	*DriverNameTable;

#else // NT

extern  NameMapping			AdptNameTable[];
extern	DriverRegMapping	DriverNameTable[];

#endif

extern	uint				NumRegDrivers;

uint            MaxIPNets = 0;
uint            InterfaceSize;  // Size of a net interface.


#ifdef NT
#ifdef ALLOC_PRAGMA
//
// Make init code disposable.
//
void InitTimestamp();
int InitNTE(NetTableEntry *NTE);
int InitInterface(NetTableEntry *NTE, LLIPBindInfo *ARPInfo);
LLIPRegRtn GetLLRegPtr(PNDIS_STRING Name);
LLIPRegRtn FindRegPtr(PNDIS_STRING Name);
uint IPRegisterDriver(PNDIS_STRING Name, LLIPRegRtn Ptr);
void CleanAdaptTable();
void OpenAdapters();
int IPInit();

#if 0    // BUGBUG: These can eventually be made init time only.

#pragma alloc_text(INIT, IPGetInfo)
#pragma alloc_text(INIT, IPTimeout)

#endif // 0

#pragma alloc_text(INIT, InitTimestamp)
#pragma alloc_text(INIT, InitNTE)
#pragma alloc_text(INIT, InitInterface)
#pragma alloc_text(INIT, CleanAdaptTable)
#pragma alloc_text(INIT, OpenAdapters)
#pragma alloc_text(INIT, IPRegisterDriver)
#pragma alloc_text(INIT, GetLLRegPtr)
#pragma alloc_text(INIT, FindRegPtr)
#pragma alloc_text(INIT, IPInit)

#endif // ALLOC_PRAGMA

extern PDRIVER_OBJECT  IPDriverObject;

NTSTATUS
SetRegDWORDValue(
    HANDLE           KeyHandle,
    PWCHAR           ValueName,
    PULONG           ValueData
    );

//
// Debugging macros
//
#if DBG

#define TCPTRACE(many_args) DbgPrint many_args

#else // DBG

#define TCPTRACE(many_args) DbgPrint many_args

#endif // DBG

#endif // NT

#ifdef CHICAGO
extern void	NotifyAddrChange(IPAddr Addr, IPMask Mask, void *Context,
	uint Added);
#endif

//**    CloseNets - Close active nets.
//
//  Called when we need to close some lower layer interfaces.
//
//  Entry:  Nothing
//
//  Returns: Nothing
//
void
CloseNets(void)
{
    NetTableEntry   *nt;

    for (nt = NetTableList; nt != NULL; nt = nt->nte_next)
        (*nt->nte_if->if_close)(nt->nte_if->if_lcontext);   // Call close routine for this net.
}

//**    IPRegisterProtocol - Register a protocol with IP.
//
//  Called by upper layer software to register a protocol. The UL supplies
//  pointers to receive routines and a protocol value to be used on xmits/receives.
//
//  Entry:
//      Protocol - Protocol value to be returned.
//      RcvHandler - Receive handler to be called when frames for Protocol are received.
//      XmitHandler - Xmit. complete handler to be called when frames from Protocol are completed.
//      StatusHandler - Handler to be called when status indication is to be delivered.
//
//  Returns:
//      Pointer to ProtInfo,
//
void *
IPRegisterProtocol(uchar Protocol, void *RcvHandler, void *XmitHandler,
    void *StatusHandler, void *RcvCmpltHandler)
{
    ProtInfo        *PI = (ProtInfo *)NULL;
    int             i;
	int				Incr;
#if 0
    CTELockHandle   Handle;

    CTEGetLock(&PILock, &Handle);
#endif

    // First check to see if it's already registered. If it is just replace it.
    for (i = 0; i < NextPI; i++)
        if (IPProtInfo[i].pi_protocol == Protocol) {
            PI = &IPProtInfo[i];
			Incr = 0;
            break;
        }

    if (PI == (ProtInfo *)NULL)
        if (NextPI >= MAX_IP_PROT) {
#if 0
            CTEFreeLock(&PILock, Handle);
#endif
            return PI;
        }
        else {
            PI = &IPProtInfo[NextPI];
			Incr = 1;
		}

        PI->pi_protocol = Protocol;
        PI->pi_rcv = RcvHandler;
        PI->pi_xmitdone = XmitHandler;
        PI->pi_status = StatusHandler;
        PI->pi_rcvcmplt = RcvCmpltHandler;
		NextPI += Incr;

#if 0
    CTEFreeLock(&PILock, Handle);
#endif
    return PI;
}

//** IPGetAddrType - Return the type of a address.
//
//  Called by the upper layer to determine the type of a remote address.
//
//  Input:  Address         - The address in question.
//
//  Returns: The DEST type of the address.
//
uchar
IPGetAddrType(IPAddr Address)
{
    return GetAddrType(Address);
}

//** IPGetLocalMTU - Return the MTU for a local address
//
//  Called by the upper layer to get the local MTU for a local address.
//
//  Input:  LocalAddr		- Local address in question.
//          MTU				- Where to return the local MTU.
//
//  Returns: TRUE if we found the MTU, FALSE otherwise.
//
uchar
IPGetLocalMTU(IPAddr LocalAddr, ushort *MTU)
{
	NetTableEntry	*NTE;
	
	for (NTE = NetTableList; NTE != NULL; NTE = NTE->nte_next) {
		if (IP_ADDR_EQUAL(NTE->nte_addr, LocalAddr) &&
			(NTE->nte_flags & NTE_VALID)) {
			*MTU = NTE->nte_mss;
			return TRUE;
		}
	}
	
	// Special case in case the local address is a loopback address other than
	// 127.0.0.1.
	if (IP_LOOPBACK_ADDR(LocalAddr)) {
		*MTU = LoopNTE->nte_mss;
		return TRUE;
	}

    return FALSE;

}

//** IPUpdateRcvdOptions - Update options for use in replying.
//
//  Inert: IP option processing is removed from the stack.  Inbound
//  options are dropped at IPRcv entry, so OldOptions->ioi_options is
//  always NULL by the time any reply path runs.  We zero the reply
//  IPOptInfo and return.  The function symbol is kept because it is
//  wired into the exported `IPInfo` function-pointer table (`ipi_updateopts`)
//  that TCP / UDP / ICMP call through.
//
IP_STATUS
IPUpdateRcvdOptions(IPOptInfo *OldOptions, IPOptInfo *NewOptions, IPAddr Src, IPAddr LocalAddr)
{
    UNREFERENCED_PARAMETER(OldOptions);
    UNREFERENCED_PARAMETER(Src);
    UNREFERENCED_PARAMETER(LocalAddr);

    NewOptions->ioi_addr = NULL_IP_ADDR;
    NewOptions->ioi_flags &= ~IP_FLAG_SSRR;
    NewOptions->ioi_options = (uchar *)NULL;
    NewOptions->ioi_optlength = 0;
    return IP_SUCCESS;
}

//** IPInitOptions - Initialize an option buffer.
//
//	Called by an upper layer routine to initialize an option buffer. We fill
//	in the default values for TTL, TOS, and flags, and NULL out the options
//	buffer and size.
//
//	Input:	Options			- Pointer to IPOptInfo structure.
//	
//	Returns: Nothing.
//
void
IPInitOptions(IPOptInfo *Options)
{
    Options->ioi_addr = NULL_IP_ADDR;

	Options->ioi_ttl = (uchar)DefaultTTL;
	Options->ioi_tos = (uchar)DefaultTOS;
    Options->ioi_flags = 0;

    Options->ioi_options = (uchar *)NULL;
    Options->ioi_optlength = 0;
	
}

//** IPCopyOptions - Copy the user's options into IP header format.
//
//  Always refuses.  The stack does not emit IP options on the wire;
//  a caller that hands us an option buffer is asking for something
//  we cannot deliver, and the conservative response is to surface
//  that at the call site rather than silently accept + no-op.  All
//  three callers (`TCPCONN.C:1195`, `ADDR.C:1369`, `ICMP.C:895`)
//  already map `IP_BAD_OPTION` to `TDI_BAD_OPTION` and propagate it
//  back through AFD, so the failure is visible to userspace as the
//  setsockopt / sendmsg returning an error — not as a silently
//  ignored option followed by a packet on the wire that doesn't
//  match what the caller asked for.  Kept as a symbol because
//  `ipi_copyopts` is part of the exported `IPInfo` function-pointer
//  table.
//
IP_STATUS
IPCopyOptions(uchar *Options, uint Size, IPOptInfo *OptInfoPtr)
{
    UNREFERENCED_PARAMETER(Options);
    UNREFERENCED_PARAMETER(Size);

    OptInfoPtr->ioi_addr = NULL_IP_ADDR;
    OptInfoPtr->ioi_flags &= ~IP_FLAG_SSRR;
    OptInfoPtr->ioi_options = (uchar *)NULL;
    OptInfoPtr->ioi_optlength = 0;
    return IP_BAD_OPTION;
}

//**    IPFreeOptions - Free options we're done with.
//
//  Called by the upper layer when we're done with options. All we need to do is free
//  the options.
//
//  Input:  OptInfoPtr      - Pointer to IPOptInfo structure to be freed.
//
//  Returns: Status of attempt to free options.
//
IP_STATUS
IPFreeOptions(IPOptInfo *OptInfoPtr)
{
    if (OptInfoPtr->ioi_options) {
        // We have options to free. Save the pointer and zero the structure field before
        // freeing the memory to try and present race conditions with it's use.
        uchar   *TempPtr = OptInfoPtr->ioi_options;

        OptInfoPtr->ioi_options = (uchar *)NULL;
        CTEFreeMem(TempPtr);
        OptInfoPtr->ioi_optlength = 0;
        OptInfoPtr->ioi_addr = NULL_IP_ADDR;
        OptInfoPtr->ioi_flags &= ~IP_FLAG_SSRR;
    }
    return IP_SUCCESS;
}


//BUGBUG - After we're done testing, move BEGIN_INIT up here.

//**    ipgetinfo - Return pointers to our NetInfo structures.
//
//  Called by upper layer software during init. time. The caller
//  passes a buffer, which we fill in with pointers to NetInfo
//  structures.
//
//  Entry:
//      Buffer - Pointer to buffer to be filled in.
//      Size   - Size in bytes of buffer.
//
//  Returns:
//      Status of command.
//
IP_STATUS
IPGetInfo(IPInfo *Buffer, int Size)
{
    if (Size < sizeof(IPInfo))
        return IP_BUF_TOO_SMALL;        // Not enough buffer space.

    Buffer->ipi_version = IP_DRIVER_VERSION;
	Buffer->ipi_hsize = sizeof(IPHeader);
    Buffer->ipi_xmit = IPTransmit;
    Buffer->ipi_protreg = IPRegisterProtocol;
    Buffer->ipi_openrce = OpenRCE;
    Buffer->ipi_closerce = CloseRCE;
    Buffer->ipi_getaddrtype = IPGetAddrType;
    Buffer->ipi_getlocalmtu = IPGetLocalMTU;
    Buffer->ipi_getpinfo = IPGetPInfo;
    Buffer->ipi_checkroute = IPCheckRoute;
    Buffer->ipi_initopts = IPInitOptions;
    Buffer->ipi_updateopts = IPUpdateRcvdOptions;
    Buffer->ipi_copyopts = IPCopyOptions;
    Buffer->ipi_freeopts = IPFreeOptions;
    Buffer->ipi_qinfo = IPQueryInfo;
	Buffer->ipi_setinfo = IPSetInfo;
	Buffer->ipi_getelist = IPGetEList;

    return IP_SUCCESS;

}

//** IPTimeout - IP timeout handler.
//
//  The timeout routine called periodically to time out various things, such as entries
//  being reassembled and ICMP echo requests.
//
//  Entry:  Timer       - Timer being fired.
//          Context     - Pointer to NTE being time out.
//
//  Returns: Nothing.
//
void
IPTimeout(CTEEvent *Timer, void *Context)
{
    NetTableEntry       *NTE = STRUCT_OF(NetTableEntry, Timer, nte_timer);

    ICMPTimer(NTE);

    // Reassembly-timeout block stripped with IP reassembly per
    // IPSTACK-HARDENING.md.  Restart the periodic timer; Context==NTE
    // for normal repeating fires, Context==NULL on the first call after
    // (formerly) reaping reassembly state.
    CTEStartTimer(&NTE->nte_timer, IP_TIMEOUT, IPTimeout,
                  Context ? (void *)NULL : (void *)NTE);
}

//* IPSetNTEAddr - Set the IP address of an NTE.
//
//	Called by the DHCP client to set or delete the IP address of an NTE. We
//	make sure he's specifiying a valid NTE, then mark it up or down as needed,
//	notify the upper layers of the change if necessary, and then muck with
//	the routing tables.
//
//	Input:	Context				- Context of NTE to alter.
//			Addr				- IP address to set.
//			Mask				- Subnet mask for Addr.
//
//	Returns: TRUE if we changed the address, FALSE otherwise.
//
uint
IPSetNTEAddr(ushort Context, IPAddr Addr, IPMask Mask)
{
	NetTableEntry		*NTE;
	Interface			*IF;
	int	    			i;
	CTELockHandle		Handle;
	uint				(*CallFunc)(struct RouteTableEntry *, void *, void *);
	ULStatusProc		StatProc;

	// It's a valid index. See what we're trying to do.
	CTEGetLock(&RouteTableLock, &Handle);
	
	for (NTE = NetTableList; NTE != NULL; NTE = NTE->nte_next)
		if (NTE->nte_context == Context)
			break;
	
	if (NTE == NULL || NTE == LoopNTE) {
		// Can't alter the loopback NTE, or one we didn't find.
		CTEFreeLock(&RouteTableLock, Handle);
		return FALSE;
	}
	
	IF = NTE->nte_if;
	DHCPActivityCount++;

	if (IP_ADDR_EQUAL(Addr, NULL_IP_ADDR)) {
		// We're deleting an address.
		if (NTE->nte_flags & NTE_VALID) {
			// The address is currently valid. Fix that.

			NTE->nte_flags &= ~NTE_VALID;
			
			if (--(IF->if_ntecount) == 0) {
				// This is the last one, so we'll need to delete relevant
				// routes.
				CallFunc = DeleteRTEOnIF;
			} else
				CallFunc = InvalidateRCEOnIF;

			CTEFreeLock(&RouteTableLock, Handle);

			// Now call the upper layers, and tell them that address is
			// gone. We really need to do something about locking here.
#ifdef CHICAGO
			NotifyAddrChange(NTE->nte_addr, NTE->nte_mask, NTE->nte_pnpcontext,
				FALSE);
#else			
			for ( i = 0; i < NextPI; i++) {
				StatProc = IPProtInfo[i].pi_status;
				if (StatProc != NULL)
					(*StatProc)(IP_HW_STATUS, IP_ADDR_DELETED, NTE->nte_addr,
						NULL_IP_ADDR, NULL_IP_ADDR, 0, NULL);
			}
#endif

			// Call RTWalk to take the appropriate action on the RTEs.
			RTWalk(CallFunc, IF, NULL);
			
			// Delete the route to the address itself.			
			DeleteRoute(NTE->nte_addr, HOST_MASK, IPADDR_LOCAL,
				LoopNTE->nte_if);

			// Tell the lower interface this address is gone.
			(*IF->if_deladdr)(IF->if_lcontext, LLIP_ADDR_LOCAL, NTE->nte_addr,
				NULL_IP_ADDR);

			CTEGetLock(&RouteTableLock, &Handle);
		}
		
		DHCPActivityCount--;
		CTEFreeLock(&RouteTableLock, Handle);
		return TRUE;
	} else {
		uint	Status;

		// We're not deleting, we're setting the address.
		if (!(NTE->nte_flags & NTE_VALID)) {
			// The address is invalid. Save the info, mark him as valid,
			// and add the routes.
			NTE->nte_addr = Addr;
			NTE->nte_mask = Mask;
			NTE->nte_flags |= NTE_VALID;
			IF->if_ntecount++;
			CTEFreeLock(&RouteTableLock, Handle);
			if (AddNTERoutes(NTE))
				Status = TRUE;
			else
				Status = FALSE;
				
			// Need to tell the lower layer about it.
			if (Status) {
				Interface		*IF = NTE->nte_if;
				
	    		Status = (*IF->if_addaddr)(IF->if_lcontext, LLIP_ADDR_LOCAL,
	    			Addr, Mask);
			}
			
			if (!Status) {
				// Couldn't add the routes. Recurively mark this NTE as down.
				IPSetNTEAddr(NTE->nte_context, NULL_IP_ADDR, 0);
			} else {
#ifdef CHICAGO
				NotifyAddrChange(NTE->nte_addr, NTE->nte_mask,
					NTE->nte_pnpcontext, TRUE);
#endif
			}

			CTEGetLock(&RouteTableLock, &Handle);
		} else
			Status = FALSE;

		DHCPActivityCount--;
		CTEFreeLock(&RouteTableLock, Handle);
		return Status;
	}
}

#pragma BEGIN_INIT

extern NetTableEntry  *InitLoopback(IPConfigInfo *);

//** InitTimestamp - Intialize the timestamp for outgoing packets.
//
//  Called at initialization time to setup our first timestamp. The timestamp we use
//  is the in ms since midnite GMT at which the system started.
//
//  Input:  Nothing.
//
//  Returns: Nothing.
//
void
InitTimestamp()
{
    ulong   GMTDelta;               // Delta in ms from GMT.
    ulong   Now;                    // Milliseconds since midnight.

    TimeStamp = 0;

    if ((GMTDelta = GetGMTDelta()) == 0xffffffff) {     // Had some sort of error.
        TSFlag = 0x80000000;
        return;
    }

    if ((Now = GetTime()) > (24L*3600L*1000L)) {    // Couldn't get time since midnight.
        TSFlag = net_long(0x80000000);
        return;
    }

    TimeStamp = Now + GMTDelta - CTESystemUpTime();
    TSFlag = 0;

}
#pragma	END_INIT

#ifndef CHICAGO
#pragma BEGIN_INIT
#else
#pragma code_seg("_LTEXT", "LCODE")
#endif

//** InitNTE - Initialize an NTE.
//
//  This routine is called during initialization to initialize an NTE. We
//	allocate memory, NDIS resources, etc.
//
//
//  Entry: NTE      - Pointer to NTE to be initalized.
//
//  Returns: 0 if initialization failed, non-zero if it succeeds.
//
int
InitNTE(NetTableEntry *NTE)
{
	Interface		*IF;
	NetTableEntry	*PrevNTE;

    NTE->nte_echolist = NULL;
	NTE->nte_context = NextNTEContext++;

	// Now link him on the IF chain, and bump the count.
	IF = NTE->nte_if;
	PrevNTE = STRUCT_OF(NetTableEntry, &IF->if_nte, nte_ifnext);
	while (PrevNTE->nte_ifnext != NULL)
		PrevNTE = PrevNTE->nte_ifnext;
		
	PrevNTE->nte_ifnext = NTE;
	NTE->nte_ifnext = NULL;
	
	if (NTE->nte_flags & NTE_VALID) {
		IF->if_ntecount++;
	}

    CTEInitTimer(&NTE->nte_timer);
    CTEStartTimer(&NTE->nte_timer, IP_TIMEOUT, IPTimeout, (void *)NULL);
    return TRUE;
}

//** InitInterface - Initialize with an interface.
//
//  Called when we need to initialize with an interface. We set the appropriate NTE
//  info, then register our local address and any appropriate broadcast addresses
//  with the interface. We assume the NTE being initialized already has an interface
//  pointer set up for it. We also allocate at least one TD buffer for use on the interface.
//
//  Input:  NTE     - NTE to initialize with the interface.
//          ARPInfo - Pointer to structure describing interface entry points.
//
//  Returns: TRUE is we succeeded, FALSE if we fail.
//
int
InitInterface(NetTableEntry *NTE, LLIPBindInfo *ARPInfo)
{
    IPMask           netmask = IPNetMask(NTE->nte_addr);
    uchar           *TDBuffer;      // Pointer to tdbuffer
    PNDIS_PACKET    Packet;
    NDIS_HANDLE     TDbpool;        // Handle for TD buffer pool.
    NDIS_HANDLE     TDppool;
    PNDIS_BUFFER    TDBufDesc;      // Buffer descriptor for TDBuffer.
    NDIS_STATUS     Status;
    Interface       *IF;            // Interface for this NTE.
    CTELockHandle   Handle;


	CTEAssert(NTE->nte_mss > sizeof(IPHeader));
	CTEAssert(ARPInfo->lip_mss > sizeof(IPHeader));

    NTE->nte_mss = MIN(NTE->nte_mss, ARPInfo->lip_mss) - sizeof(IPHeader);

	CTERefillMem();
	
    // Allocate resources needed for xfer data calls. The TD buffer has to be as large
    // as any frame that can be received, even though our MSS may be smaller, because we
    // can't control what might be sent at us.
    TDBuffer = CTEAllocMem(ARPInfo->lip_mss - sizeof(IPHeader));
    if (TDBuffer == (uchar *)NULL)
        return FALSE;

    NdisAllocatePacketPool(&Status, &TDppool, 1, sizeof(TDContext));

    if (Status != NDIS_STATUS_SUCCESS) {
        CTEFreeMem(TDBuffer);
        return FALSE;
    }

    NdisAllocatePacket(&Status, &Packet, TDppool);
    if (Status != NDIS_STATUS_SUCCESS) {
        NdisFreePacketPool(TDppool);
        CTEFreeMem(TDBuffer);
        return FALSE;
    }

    CTEMemSet(Packet->ProtocolReserved, 0, sizeof(TDContext));

    NdisAllocateBufferPool(&Status, &TDbpool, 1);
    if (Status != NDIS_STATUS_SUCCESS) {
        NdisFreePacketPool(TDppool);
        CTEFreeMem(TDBuffer);
        return FALSE;
    }

    NdisAllocateBuffer(&Status,&TDBufDesc, TDbpool, TDBuffer, ARPInfo->lip_mss);
    if (Status != NDIS_STATUS_SUCCESS) {
        NdisFreeBufferPool(TDbpool);
        NdisFreePacketPool(TDppool);
        CTEFreeMem(TDBuffer);
        return FALSE;
    }

    NdisChainBufferAtFront(Packet, TDBufDesc);

    ((TDContext *)Packet->ProtocolReserved)->tdc_buffer = TDBuffer;


	if (NTE->nte_flags & NTE_VALID) {
		
		// Add our local IP address.
	    if (!(*ARPInfo->lip_addaddr)(ARPInfo->lip_context, LLIP_ADDR_LOCAL,
	    	NTE->nte_addr, NTE->nte_mask)) {
	        NdisFreeBufferPool(TDbpool);
	        NdisFreePacketPool(TDppool);
	        CTEFreeMem(TDBuffer);
	        return FALSE;                   // Couldn't add local address.
	    }
	}
	
    // Set up the broadcast addresses for this interface, iff we're the
	// 'primary' NTE on the interface.
	if (NTE->nte_flags & NTE_PRIMARY) {
		
	    if (!(*ARPInfo->lip_addaddr)(ARPInfo->lip_context, LLIP_ADDR_BCAST,
	    	NTE->nte_if->if_bcast, 0)) {
	        NdisFreeBufferPool(TDbpool);
	        NdisFreePacketPool(TDppool);
	        CTEFreeMem(TDBuffer);
	        return FALSE;                   // Couldn't add broadcast address.
	    }
	}
	
	if (ARPInfo->lip_flags & LIP_COPY_FLAG)
		NTE->nte_flags |= NTE_COPY;

    IF = NTE->nte_if;
    CTEGetLock(&IF->if_lock, &Handle);
    ((TDContext *)Packet->ProtocolReserved)->tdc_common.pc_link = IF->if_tdpacket;
    IF->if_tdpacket = Packet;
    CTEFreeLock(&IF->if_lock, Handle);

    return TRUE;
}

#ifndef CHICAGO
//* CleanAdaptTable - Clean up the adapter name table.
//
//
void
CleanAdaptTable()
{
    int         i = 0;

    while (AdptNameTable[i].nm_arpinfo != NULL) {
        CTEFreeMem(AdptNameTable[i].nm_arpinfo);
        CTEFreeString(&AdptNameTable[i].nm_name);
		if (AdptNameTable[i].nm_driver.Buffer != NULL)
        	CTEFreeString(&AdptNameTable[i].nm_driver);
        i++;
    }
}


//* OpenAdapters - Clean up the adapter name table.
//
//  Used at the end of initialization. We loop through and 'open' all the adapters.
//
//  Input: Nothing.
//
//  Returns: Nothing.
//
void
OpenAdapters()
{
    int         i = 0;
    LLIPBindInfo *ABI;

    while ((ABI = AdptNameTable[i++].nm_arpinfo) != NULL) {
        (*(ABI->lip_open))(ABI->lip_context);
    }
}


//*	IPRegisterDriver - Called during init time to register a driver.
//
//	Called during init time when we have a non-LAN (or non-ARPable) driver
//	that wants to register with us. We try to find a free slot in the table
//	to register him.
//
//	Input:	Name		- Pointer to the name of the driver to be registered.
//			Ptr			- Pointer to driver's registration function.
//
//	Returns: TRUE if we succeeded, FALSE if we fail.
//
uint
IPRegisterDriver(PNDIS_STRING Name, LLIPRegRtn Ptr)
{
	uint			i;
	
	CTERefillMem();

	// First, find a slot for him.
	for (i = 0; i < MaxIPNets; i++) {
		if (DriverNameTable[i].drm_driver.Buffer == NULL) {
			// Found a slot. Try and allocate and copy a string for him.
			if (!CTEAllocateString(&DriverNameTable[i].drm_driver,
				CTELengthString(Name)))
				return FALSE;
			// Got the space. Copy the string and the pointer.
			CTECopyString(&DriverNameTable[i].drm_driver, Name);
			DriverNameTable[i].drm_regptr = Ptr;
			NumRegDrivers++;
			return TRUE;
		}
	}

	
}

#endif

#ifdef NT

//*	GetLLRegPtr - Called during init time to get a lower driver's registration
//                routine.
//
//	Called during init time to locate the registration function of a
//  non-LAN (or non-ARPable) driver.
//
//	Input:	Name		- Pointer to the name of the driver to be registered.
//
//	Returns: A pointer to the driver's registration routine or NULL on failure.
//
LLIPRegRtn
GetLLRegPtr(PNDIS_STRING Name)
{
	NTSTATUS                  status;
	PFILE_OBJECT              fileObject;
	PDEVICE_OBJECT            deviceObject;
	LLIPIF_REGISTRATION_DATA  registrationData;
	IO_STATUS_BLOCK           ioStatusBlock;
	PIRP                      irp;
	KEVENT                    ioctlEvent;
extern POBJECT_TYPE          *IoDeviceObjectType;


	registrationData.RegistrationFunction = NULL;

	KeInitializeEvent(&ioctlEvent, SynchronizationEvent, FALSE);

	status = IoGetDeviceObjectPointer(
	             Name,
		         SYNCHRONIZE | GENERIC_READ | GENERIC_WRITE,
                 &fileObject,
				 &deviceObject
		         );

    if (status != STATUS_SUCCESS) {
		CTEPrint("IP failed to open the lower layer driver\n");
		return(NULL);
	}

	//
	// Reference the file object.
	//
	status = ObReferenceObjectByPointer(
	             deviceObject,
				 GENERIC_READ | GENERIC_WRITE,
				 *IoDeviceObjectType,
				 KernelMode
				 );

    if (status != STATUS_SUCCESS) {
        //
        // IoGetDeviceObjectPointer put a reference on the file object.
        //
        ObDereferenceObject(fileObject);
		return(NULL);
	}

    //
    // IoGetDeviceObjectPointer put a reference on the file object.
    //
    ObDereferenceObject(fileObject);

	irp = IoBuildDeviceIoControlRequest(
	          IOCTL_LLIPIF_REGISTER,
			  deviceObject,
			  NULL,             // input Buffer
			  0,                // input buffer length
              &registrationData,
			  sizeof(LLIPIF_REGISTRATION_DATA),
			  FALSE,            // not an InternalDeviceControl
			  &ioctlEvent,
			  &ioStatusBlock
			  );

   if (irp == NULL) {
        ObDereferenceObject(deviceObject);
	   return(NULL);
   }

   status = IoCallDriver(deviceObject, irp);

   if (status == STATUS_PENDING) {
       status = KeWaitForSingleObject(
	                &ioctlEvent,
					Executive,
					KernelMode,
					FALSE,          // not alertable
					NULL            // no timeout
					);
	
   }

   if (status != STATUS_SUCCESS) {
        ObDereferenceObject(deviceObject);
	   return(NULL);
   }

   ObDereferenceObject(deviceObject);

   if (registrationData.RegistrationFunction != NULL) {
	   //
	   // Cache the driver registration for future reference.
	   //
       IPRegisterDriver(Name, registrationData.RegistrationFunction);
   }

   return(registrationData.RegistrationFunction);

}  // GetLLRegPtr

#endif // NT


#ifndef CHICAGO

//*	FindRegPtr - Find a driver's registration routine.
//
//	Called during init time when we have a non-LAN (or non-ARPable) driver to
//	register with. We take in the driver name, and try to find a registration
//	pointer for the driver.
//
//	Input:	Name		- Pointer to the name of the driver to be found.
//
//	Returns: Pointer to the registration routine, or NULL if there is none.
//
LLIPRegRtn
FindRegPtr(PNDIS_STRING Name)
{
	uint			i;

	for (i = 0; i < NumRegDrivers; i++) {
		if (CTEEqualString(&(DriverNameTable[i].drm_driver), Name))
			return (LLIPRegRtn)(DriverNameTable[i].drm_regptr);
	}

#ifdef NT
    //
	// For NT, we open the lower driver and issue an IOCTL to get a pointer to
	// its registration function. We then cache this in the table for future
	// reference.
	//
	return(GetLLRegPtr(Name));
#else
	return NULL;
#endif // NT
}

#endif

#ifdef CHICAGO
#pragma BEGIN_INIT
#endif

//*	FreeNets - Free nets we have allocated.
//
//	Called during init time if initialization fails. We walk down our list
//	of nets, and free them.
//
//	Input:	Nothing.
//
//	Returns: Nothing.
//
void
FreeNets(void)
{
	NetTableEntry		*NTE;
	
	for (NTE = NetTableList; NTE != NULL; NTE = NTE->nte_next)
		CTEFreeMem(NTE);
}

#ifdef CHICAGO
#pragma	END_INIT
#pragma code_seg("_LTEXT", "LCODE")

extern uint OpenIFConfig(PNDIS_STRING ConfigName, NDIS_HANDLE *Handle);
extern uint GetGeneralIFConfig(IFGeneralConfig *GConfigInfo, NDIS_HANDLE Handle);
extern IFAddrList *GetIFAddrList(uint *NumAddr, NDIS_HANDLE Handle);
extern void CloseIFConfig(NDIS_HANDLE Handle);
extern void RequestDHCPAddr(ushort context);

#define	MAX_NOTIFY_CLIENTS			8

typedef	void (*AddrNotifyRtn)(IPAddr Addr, IPMask Mask, void *Context,
	uint Added);

AddrNotifyRtn	AddrNotifyTable[MAX_NOTIFY_CLIENTS];

//*	NotifyAddrChange - Notify clients of a change in addresses.
//
//	Called when we want to notify registered clients that an address has come
//	or gone. We loop through our AddrNotify table, calling each one.
//
//	Input:	Addr			- Addr that has changed.
//			Mask			- Mask that has changed.
//			Added			- True if the addr is coming, False if it's going.
//
//	Returns: Nothing.
//
void
NotifyAddrChange(IPAddr Addr, IPMask Mask, void *Context, uint Added)
{
	uint		i;
	
	for (i = 0; i < MAX_NOTIFY_CLIENTS; i++) {
		if (AddrNotifyTable[i] != NULL)
			(*(AddrNotifyTable[i]))(Addr, Mask, Context, Added);
	}
}

//*	RegisterAddrNotify	- Register an address notify routine.
//
//	A routine called to register an address notify routine.
//
//	Input:	Rtn			- Routine to register.
//			Register	- True to register, False to deregister.
//
//	Returns:	TRUE if we succeed, FALSE if we don't/
//
uint
RegisterAddrNotify(AddrNotifyRtn Rtn, uint Register)
{
	uint		i;
	AddrNotifyRtn	NewRtn, OldRtn;
	
	if (Register) {
		NewRtn = Rtn;
		OldRtn = NULL;
	} else {
		NewRtn = NULL;
		OldRtn = Rtn;
	}

	for (i = 0; i < MAX_NOTIFY_CLIENTS; i++) {
		if (AddrNotifyTable[i] == OldRtn) {
			AddrNotifyTable[i] = NewRtn;
			return TRUE;
		}
	}
	
	return FALSE;
}

//*	IPAddInterface - Add an interface.
//
//	Called when someone has an interface they want us to add. We read our
//	configuration information, and see if we have it listed. If we do,
//	we'll try to allocate memory for the structures we need. Then we'll
//	call back to the guy who called us to get things going. Finally, we'll
//	see if we have an address that needs to be DHCP'ed.
//
//	Input:	ConfigName				- Name of config info we're to read.
//			Context					- Context to pass to i/f on calls.
//			RegdRtn					- Routine to call to register.
//			BindInfo				- Pointer to bind information.
//
//	Returns: Status of attempt to add the interface.
//
IP_STATUS
IPAddInterface(PNDIS_STRING ConfigName, void *PNPContext, void *Context,
	LLIPRegRtn	RegRtn, LLIPBindInfo *BindInfo)
{
	IFGeneralConfig			GConfigInfo;	// General config info structure.
	IFAddrList				*AddrList;		// List of addresses for this I/F.
	uint					NumAddr;		// Number of IP addresses on this
											// interface.
	NetTableEntry			*NTE;			// Current NTE being initialized.
	uint					i;				// Index variable.
	Interface				*IF;			// Interface being added.
	NDIS_HANDLE				Handle;			// Configuration handle.
	
	CTERefillMem();
	
	//* First, try to get the network configuration information.
	if (!OpenIFConfig(ConfigName, &Handle))
		return IP_GENERAL_FAILURE;			// Couldn't get IFConfig.

	AddrList = NULL;
	IF = NULL;
	// Try to get our general config information.
	if (!GetGeneralIFConfig(&GConfigInfo, Handle)) {
		goto failure;
	}	

	// We got the general config into.. Now allocate an interface.
	IF = CTEAllocMem(InterfaceSize);
	if (IF == NULL) {
		goto failure;		
	}
	
	CTEMemSet(IF, 0, InterfaceSize);
    CTEInitLock(&IF->if_lock);
	
	// Initialize the broadcast we'll use.
    if (GConfigInfo.igc_zerobcast)
        IF->if_bcast = IP_ZERO_BCST;
    else
        IF->if_bcast = IP_LOCAL_BCST;

    IF->if_xmit = BindInfo->lip_transmit;
    IF->if_transfer = BindInfo->lip_transfer;
    IF->if_close = BindInfo->lip_close;
    IF->if_invalidate = BindInfo->lip_invalidate;
    IF->if_lcontext = BindInfo->lip_context;
    IF->if_addaddr = BindInfo->lip_addaddr;
    IF->if_deladdr = BindInfo->lip_deladdr;
    IF->if_qinfo = BindInfo->lip_qinfo;
    IF->if_setinfo = BindInfo->lip_setinfo;
    IF->if_getelist = BindInfo->lip_getelist;
    IF->if_tdpacket = NULL;
	IF->if_mtu = BindInfo->lip_mss - sizeof(IPHeader);
	IF->if_speed = BindInfo->lip_speed;
	IF->if_flags = BindInfo->lip_flags & LIP_P2P_FLAG ? IF_FLAGS_P2P : 0;
   	IF->if_addrlen = BindInfo->lip_addrlen;
	IF->if_addr = BindInfo->lip_addr;
	
	// Find out how many addresses we have, and get the address list.	
	AddrList = GetIFAddrList(&NumAddr, Handle);
	if (AddrList == NULL) {
		CTEFreeMem(IF);
		goto failure;
	}
		
	CloseIFConfig(Handle);
	
	IF->if_next = IFList;
	IFList = IF;
	if (FirstIF == NULL)
		FirstIF = IF;
	NumIF++;
	IF->if_index = NumIF;
	
	// Now loop through, initializing each NTE as we go. We don't hold any
	// locks while we do this, since this is for Chicago only, but when we
	// port this to NT we'll need to interlock it somehow.

	for (i = 0;i < NumAddr;i++) {
		NetTableEntry	*PrevNTE;
		IPAddr			NewAddr;
		
		NewAddr = net_long(AddrList[i].ial_addr);

        // If the address is invalid we're done. Fail the request.
        if (CLASSD_ADDR(NewAddr) || CLASSE_ADDR(NewAddr)) {
			goto failure_closed;
        }
		
		// See if we have an inactive one on the NetTableList. If we do, we'll
		// just recycle that. We will pull him out of the list. This is not
		// strictly MP safe, since other people could be walking the list while
		// we're doing this without holding a lock, but it should be harmless.
		// The removed NTE is marked as invalid, and his next pointer will
		// be nulled, so anyone walking the list might hit the end too soon,
		// but that's all. The memory is never freed, and the next pointer is
		// never pointed at freed memory. When we move this to NT we just
		// need to make sure that manipulating the list (putting things on and
		// off) is serialized.
		PrevNTE = STRUCT_OF(NetTableEntry, &NetTableList, nte_next);
		for (NTE = NetTableList; NTE != NULL; PrevNTE = NTE, NTE = NTE->nte_next)
			if (!(NTE->nte_flags & NTE_ACTIVE)) {
				PrevNTE->nte_next = NTE->nte_next;
				NTE->nte_next = NULL;
				NumNTE--;
				break;
			}
		
		// See if we got one.
		if (NTE == NULL) {
			// Didn't get one. Try to allocate one.
			NTE = CTEAllocMem(sizeof(NetTableEntry));
			if (NTE == NULL)
				goto failure_closed;
		}

		// Initialize the address and mask stuff
        NTE->nte_addr = NewAddr;
        NTE->nte_mask = net_long(AddrList[i].ial_mask);
        NTE->nte_mss = MAX(GConfigInfo.igc_mtu, 68);
		NTE->nte_pnpcontext = PNPContext;
		NTE->nte_if = IF;
		NTE->nte_flags = (IP_ADDR_EQUAL(NTE->nte_addr, NULL_IP_ADDR) ? 0 :
			NTE_VALID);
		NTE->nte_flags |= NTE_ACTIVE;
		
		if (i == 0) {
			NTE->nte_flags |= NTE_PRIMARY;
			
			// Pass our information to the underlying code.	
			if (!(*RegRtn)(ConfigName, NTE, IPRcv, IPSendComplete, IPStatus,
				IPTDComplete, IPRcvComplete, BindInfo, NumIF)) {
				
				// Couldn't register.
				goto failure_closed;
			}
		}
		
       	CTEInitLock(&NTE->nte_lock);
		
		NTE->nte_next = NetTableList;
		NetTableList = NTE;
		NumNTE++;
		
        if (!InitInterface(NTE, BindInfo)) {
			goto failure_closed;
        }

        if (!InitNTE(NTE)) {
			goto failure_closed;
        }

		if (!InitNTERouting(NTE, GConfigInfo.igc_numgws, GConfigInfo.igc_gw)) {
			// Couldn't add the routes for this NTE. Mark him as not valid.
			// Probably should log an event here.
			if (NTE->nte_flags & NTE_VALID) {
				NTE->nte_flags &= ~NTE_VALID;
				NTE->nte_if->if_ntecount--;
			}
		}

	}
	
	// We've initialized our NTEs. Now get the adapter open, and go through
	// again, calling DHCP if we need to.
		
	(*(BindInfo->lip_open))(BindInfo->lip_context);

	// Now walk through the NTEs we've added, and get addresses for them (or
	// tell clients about them). This code assumes that no one else has mucked
	// with the list while we're here.
	for (i = 0; i < NumAddr; i++, NTE = NTE->nte_next) {
		NotifyAddrChange(NTE->nte_addr, NTE->nte_mask, NTE->nte_pnpcontext,
			TRUE);
			
		if (IP_ADDR_EQUAL(NTE->nte_addr, NULL_IP_ADDR)) {
			// Call DHCP to get an address for this guy.
			RequestDHCPAddr(NTE->nte_context);
		}
	}
	
	
	CTEFreeMem(AddrList);
	return IP_SUCCESS;
	
failure:
	CloseIFConfig(Handle);
failure_closed:
	if (AddrList != NULL)
		CTEFreeMem(AddrList);
	return IP_GENERAL_FAILURE;
}

//*	IPDelInterface	- Delete an interface.
//
//	Called when we need to delete an interface that's gone away. We'll walk
//	the NTE list, looking for NTEs that are on the interface that's going
//	away. For each of those, we'll invalidate the NTE, delete routes on it,
//	and notify the upper layers that it's gone. When that's done we'll pull
//	the interface out of the list and free the memory.
//
//	Note that this code probably isn't MP safe. We'll need to fix that for
//	the port to NT.
//
//	Input:	Context				- Pointer to primary NTE on the interface.
//
//	Returns: Nothing.
//
void
IPDelInterface(void *Context)
{
	NetTableEntry		*NTE = (NetTableEntry *)Context;
	Interface			*IF, *PrevIF;
	CTELockHandle		Handle;
	PNDIS_PACKET		Packet;
	PNDIS_BUFFER		Buffer;
	uchar				*TDBuffer;
	/* ReassemblyHeader	*RH; — stripped with IP reassembly */
	EchoControl			*EC;
	EchoRtn				Rtn;
	
	IF = NTE->nte_if;
	
	CTEGetLock(&RouteTableLock, &Handle);
	
	for (NTE = NetTableList; NTE != NULL; NTE = NTE->nte_next) {
		if (NTE->nte_if == IF) {
			// This guy is on the interface, and needs to be deleted.
			if (NTE->nte_flags & NTE_VALID) {
				NTE->nte_flags &= ~NTE_VALID;
				CTEFreeLock(&RouteTableLock, Handle);

			
				// Now call the upper layers, and tell them that address is
				// gone.
#ifdef CHICAGO
				NotifyAddrChange(NTE->nte_addr, NTE->nte_mask,
					NTE->nte_pnpcontext, FALSE);
#endif
				// Call RTWalk to take the appropriate action on the RTEs.
				RTWalk(DeleteRTEOnIF, IF, NULL);
			
				// Delete the route to the address itself.			
				DeleteRoute(NTE->nte_addr, HOST_MASK, IPADDR_LOCAL,
					LoopNTE->nte_if);
			}
			
			NTE->nte_flags &= ~NTE_ACTIVE;
		    CTEStopTimer(&NTE->nte_timer);
			
			// Reassembly resource teardown stripped with IP reassembly per
			// IPSTACK-HARDENING.md; nte_ralist is never populated.
			
			// Now free any pending echo requests.
			EC = NTE->nte_echolist;
			while (EC != NULL) {
				NTE->nte_echolist = EC->ec_next;
        		Rtn = (EchoRtn)EC->ec_rtn;
        		(*Rtn)(EC, IP_ADDR_DELETED, NULL, 0, NULL);
				EC = NTE->nte_echolist;
			}
				
			CTEGetLock(&RouteTableLock, &Handle);
		}
	}
	
	CTEFreeLock(&RouteTableLock, Handle);
	
	// Free the TD resources on the IF.
	Packet = IF->if_tdpacket;
	CTEAssert(Packet != NULL);
	Buffer = Packet->Private.Head;
	TDBuffer = Buffer->VirtualAddress;
	NdisFreePacketPool(Packet->Private.Pool);
	NdisFreeBufferPool(Buffer->Pool);
	CTEFreeMem(TDBuffer);
	
	// Now walk the IFList, looking for this guy. When we find him, free him.
	PrevIF = STRUCT_OF(Interface, &IFList, if_next);
	while (PrevIF->if_next != IF && PrevIF->if_next != NULL)
		PrevIF = PrevIF->if_next;
	
	if (PrevIF->if_next != NULL) {
		PrevIF->if_next = IF->if_next;
		NumIF--;
		CTEFreeMem(IF);
	} else
		CTEAssert(FALSE);
		
}

#pragma BEGIN_INIT

#endif

//** ipinit - Initialize ourselves.
//
//  This routine is called during initialization from the OS-specific
//  init code. We need to check for the presence of the common xport
//  environment first.
//
//
//  Entry: Nothing.
//
//  Returns: 0 if initialization failed, non-zero if it succeeds.
//
int
IPInit()
{
    IPConfigInfo    *ci;            // Pointer to our IP configuration info.
    int             numnets;        // Number of nets active.
    int             i;
	uint			j;              // Counter variables.
    NetTableEntry   *nt;            // Pointer to current NTE.
    LLIPBindInfo	*ARPInfo;       // Info. returned from ARP.
    NDIS_STATUS     Status;
    Interface       *NetInterface;  // Interface for a particular net.
	LLIPRegRtn		RegPtr;
    NetTableEntry   *lastNTE;


    if (!CTEInitialize())
        return IP_INIT_FAILURE;

    CTERefillMem();

    if ((ci = IPGetConfig()) == NULL)
        return IP_INIT_FAILURE;

    MaxIPNets = ci->ici_numnets + 1;

	// First, initalize our loopback stuff.
    NetTableList = InitLoopback(ci);
	if (NetTableList == NULL)
		return IP_INIT_FAILURE;
	
    if (!ARPInit()) {
		CTEFreeMem(NetTableList);
        return IP_INIT_FAILURE;     // Couldn't initialize ARP.
    }

    CTERefillMem();
	if (!InitRouting(ci)) {
		CTEFreeMem(NetTableList);
		return IP_INIT_FAILURE;
	}

    /* RATimeout = DEFAULT_RA_TIMEOUT — stripped with IP reassembly */
#if 0
    CTEInitLock(&PILock);
#endif
    LastPI = IPProtInfo;

    CTEInitLock(&IPIDLock);

    // Forwarding removed (H-020): always allocate the smaller plain
    // Interface, never the RouteInterface that carried per-IF
    // forwarding send queues.
    InterfaceSize = sizeof(Interface);
		
	DeadGWDetect = ci->ici_deadgwdetect;
	PMTUDiscovery = ci->ici_pmtudiscovery;
	DefaultTTL = MIN(ci->ici_ttl, 255);
	DefaultTOS = ci->ici_tos & 0xfc;

    InitTimestamp();

#ifndef CHICAGO
	numnets = ci->ici_numnets;

    lastNTE = NetTableList;   // loopback is only one on the list
    CTEAssert(lastNTE != NULL);
    CTEAssert(lastNTE->nte_next == NULL);

    // Loop through the config. info, copying the addresses and masks.
    for (i = 0; i < numnets; i++) {

		CTERefillMem();
		nt = CTEAllocMem(sizeof(NetTableEntry));
		CTEMemSet(nt, 0, sizeof(NetTableEntry));
		
        nt->nte_addr = net_long(ci->ici_netinfo[i].nci_addr);
        nt->nte_mask = net_long(ci->ici_netinfo[i].nci_mask);
        nt->nte_mss = MAX(ci->ici_netinfo[i].nci_mtu, 68);
		nt->nte_flags = (IP_ADDR_EQUAL(nt->nte_addr, NULL_IP_ADDR) ? 0 :
			NTE_VALID);
		nt->nte_flags |= NTE_ACTIVE;

        CTEInitLock(&nt->nte_lock);
        // If the address is invalid, skip it.
        if (CLASSD_ADDR(nt->nte_addr) || CLASSE_ADDR(nt->nte_addr)) {
			CTEFreeMem(nt);
            continue;
        }

        // See if we're already bound to this adapter. If we are, use the same
        // interface. Otherwise assign a new one. We assume that the loopback
		// interface is IF 1, so there is one less than NumIF in the table.
        for (j = 0; j < NumIF - 1; j++) {
            if (CTEEqualString(&(AdptNameTable[j].nm_name),
            	&(ci->ici_netinfo[i].nci_name))) {
				
				// Names match. Now check driver/types.
				if (((ci->ici_netinfo[i].nci_type == NET_TYPE_LAN) &&
					(AdptNameTable[j].nm_driver.Buffer == NULL)) ||
					(CTEEqualString(&(AdptNameTable[j].nm_driver),
            		&(ci->ici_netinfo[i].nci_driver))))
                	break;     // Found a match
            }
        }

        if (j < (NumIF - 1)) {
			
			// Found a match above, so use that interface.			
			CTERefillMem();
            nt->nte_if = AdptNameTable[j].nm_interface;
            ARPInfo = AdptNameTable[j].nm_arpinfo;
            // If the Init of the interface or the NTE fails, we don't want to
            // close the interface, because another net is using it.

            if (!InitInterface(nt, ARPInfo)) {
				CTEFreeMem(nt);
                continue;
			}
            if (!InitNTE(nt)) {
				CTEFreeMem(nt);
                continue;
			}

        } else {                    // No match, create a new interface
		    CTEAssert(NumIF <= MaxIPNets);

		    if (NumIF == MaxIPNets) {
				continue;    // too many adapters
			}

			CTERefillMem();
            ARPInfo = CTEAllocMem(sizeof(LLIPBindInfo));
            if (ARPInfo == NULL) {
				CTEFreeMem(nt);
                continue;
			}
            NetInterface = CTEAllocMem(InterfaceSize);
            if (!NetInterface) {
                CTEFreeMem(ARPInfo);
				CTEFreeMem(nt);
                continue;
            }

            CTEMemSet(NetInterface, 0, InterfaceSize);

            nt->nte_if = NetInterface;
			nt->nte_flags |= NTE_PRIMARY;	// He is the primary NTE.

            CTEInitLock(&NetInterface->if_lock);

            // If this is a LAN, register with ARP.
            if (ci->ici_netinfo[i].nci_type == NET_TYPE_LAN)
		        RegPtr = ARPRegister;
 		    else
				RegPtr = FindRegPtr(&ci->ici_netinfo[i].nci_driver);

            if (RegPtr == NULL || !((*RegPtr)(&ci->ici_netinfo[i].nci_name,
            	nt, IPRcv, IPSendComplete, IPStatus, IPTDComplete,
            	IPRcvComplete, ARPInfo, NumIF))) {
                CTEFreeMem(ARPInfo);
                CTEFreeMem(NetInterface);
				CTEFreeMem(nt);
                continue;   // We're hosed, skip this net.
            }
            else {
		
		        if (ci->ici_netinfo[i].nci_zerobcast)
		            NetInterface->if_bcast = IP_ZERO_BCST;
		        else
		            NetInterface->if_bcast = IP_LOCAL_BCST;

                NetInterface->if_xmit = ARPInfo->lip_transmit;
                NetInterface->if_transfer = ARPInfo->lip_transfer;
                NetInterface->if_close = ARPInfo->lip_close;
                NetInterface->if_invalidate = ARPInfo->lip_invalidate;
                NetInterface->if_lcontext = ARPInfo->lip_context;
                NetInterface->if_addaddr = ARPInfo->lip_addaddr;
                NetInterface->if_deladdr = ARPInfo->lip_deladdr;
                NetInterface->if_qinfo = ARPInfo->lip_qinfo;
                NetInterface->if_setinfo = ARPInfo->lip_setinfo;
                NetInterface->if_getelist = ARPInfo->lip_getelist;
                NetInterface->if_tdpacket = NULL;
                NetInterface->if_index = ARPInfo->lip_index;
				NetInterface->if_mtu = ARPInfo->lip_mss - sizeof(IPHeader);
				NetInterface->if_speed = ARPInfo->lip_speed;
				NetInterface->if_flags = ARPInfo->lip_flags & LIP_P2P_FLAG ?
                    IF_FLAGS_P2P : 0;
                NetInterface->if_addrlen = ARPInfo->lip_addrlen;
                NetInterface->if_addr = ARPInfo->lip_addr;
				
				CTERefillMem();

                if (!InitInterface(nt, ARPInfo)) {
                    CTEFreeMem(ARPInfo);
                    CTEFreeMem(NetInterface);
					CTEFreeMem(nt);
                    continue;
                }

                if (!InitNTE(nt)) {
                    CTEFreeMem(ARPInfo);
                    CTEFreeMem(NetInterface);
					CTEFreeMem(nt);
                    continue;
                }

				CTERefillMem();
                if (!CTEAllocateString(&AdptNameTable[j].nm_name,
                    CTELengthString(&ci->ici_netinfo[i].nci_name))) {
                    CTEFreeMem(ARPInfo);
                    CTEFreeMem(NetInterface);
					CTEFreeMem(nt);
                    continue;
                }

				if (ci->ici_netinfo[i].nci_type != NET_TYPE_LAN) {
                	if (!CTEAllocateString(&AdptNameTable[j].nm_driver,
                    	CTELengthString(&ci->ici_netinfo[i].nci_driver))) {
    					CTEFreeString(&AdptNameTable[j].nm_name);
                    	CTEFreeMem(ARPInfo);
                    	CTEFreeMem(NetInterface);
						CTEFreeMem(nt);
                    	continue;
                	}
                	CTECopyString(&(AdptNameTable[j].nm_driver),
                    	&(ci->ici_netinfo[i].nci_driver));
				}
					
                CTECopyString(&(AdptNameTable[j].nm_name),
                    &(ci->ici_netinfo[i].nci_name));
                AdptNameTable[j].nm_interface = NetInterface;
                AdptNameTable[j].nm_arpinfo = ARPInfo;
				NetInterface->if_next = IFList;
				IFList = NetInterface;
				if (FirstIF == NULL)
					FirstIF = NetInterface;
				NumIF++;

#ifdef NT
                //
				// Write the interface context to the registry for DHCP et al
				//
	            if (ci->ici_netinfo[i].nci_reghandle != NULL) {
	            	NTSTATUS writeStatus;
					ulong    context = (ulong) nt->nte_context;

                    writeStatus = SetRegDWORDValue(
                                      ci->ici_netinfo[i].nci_reghandle,
                                      L"IPInterfaceContext",
                                      &context
                                      );

                    if (!NT_SUCCESS(writeStatus)) {
                        CTELogEvent(
	                        IPDriverObject,
	                        EVENT_TCPIP_DHCP_INIT_FAILED,
	                        2,
	                        1,
	                        &(ci->ici_netinfo[i].nci_name.Buffer),
	                        0,
	                        NULL
	                        );

                    	TCPTRACE((
                    	    "IP: Unable to write IPInterfaceContext value for adapter %ws\n"
	            			"    (status %lx). DHCP will be unable to configure this \n"
	            			"    adapter.\n",
                    		ci->ici_netinfo[i].nci_name.Buffer,
	            			writeStatus
                    		));
                    }
	            }
#endif // NT
            }
        }

		nt->nte_next = NULL;
        lastNTE->nte_next = nt;
        lastNTE = nt;
        NumNTE++;

		if (!InitNTERouting(nt, ci->ici_netinfo[i].nci_numgws,
			ci->ici_netinfo[i].nci_gw)) {
			// Couldn't add the routes for this NTE. Mark has as not valid.
			// Probably should log an event here.
			if (nt->nte_flags & NTE_VALID) {
				nt->nte_flags &= ~NTE_VALID;
				nt->nte_if->if_ntecount--;
			}
		}
    }

#endif

    if (NumNTE != 0) {         // We have an NTE, and loopback initialized.
        PNDIS_PACKET    Packet;

		// Forwarding removed (H-020): always report IP_NOT_FORWARDING
		// through MIB-II.  ci->ici_gateway is ignored.
		IPSInfo.ipsi_forwarding = IP_NOT_FORWARDING;
		IPSInfo.ipsi_defaultttl = DefaultTTL;
		/* ipReasmTimeout (RFC 2011): 0 reports "no reassembly support". */
		IPSInfo.ipsi_reasmtimeout = 0;

        // Allocate our packet pools.
        CTEInitLock(&HeaderLock);
		
		Packet = GrowIPPacketList();
		
		if (Packet == NULL) {
            CloseNets();
            FreeNets();
            IPFreeConfig(ci);
            return IP_INIT_FAILURE;
        }
		
		(void)FreeIPPacket(Packet);

        NdisAllocateBufferPool(&Status, &BufferPool, NUM_IP_NONHDR_BUFFERS);
        if (Status != NDIS_STATUS_SUCCESS) {
#ifdef DEBUG
            DEBUGCHK;
#endif
        }

		CTERefillMem();

        ICMPInit(DEFAULT_ICMP_BUFFERS);

		// InitGateway() removed with the forwarding strip (H-020).
		CTERefillMem();

        IPFreeConfig(ci);
        CTERefillMem();

#ifndef CHICAGO
        OpenAdapters();
        CleanAdaptTable();          // Clean up the adapter info we don't need.
#endif

        CTERefillMem();
		
        return IP_INIT_SUCCESS;
    }
    else {
        FreeNets();
        IPFreeConfig(ci);
        return IP_INIT_FAILURE;         // Couldn't initialize anything.
    }
}

#pragma END_INIT

