/********************************************************************/
/**                     Microsoft LAN Manager                      **/
/**               Copyright(c) Microsoft Corp., 1990-1992          **/
/********************************************************************/
/* :ts=4 */

//***	iprcv.c - IP receive routines.
//
//	This module contains all receive related IP routines.
//


#include	"oscfg.h"
#include	"cxport.h"
#include	"ndis.h"
#include	"ip.h"
#include	"ipdef.h"
#include	"info.h"
#include	"iproute.h"

extern IP_STATUS SendICMPErr(IPAddr, IPHeader UNALIGNED *, uchar, uchar, ulong);

/* extern uchar RATimeout — stripped with IP reassembly */
extern	NDIS_HANDLE BufferPool;
#if 0
EXTERNAL_LOCK(PILock)
#endif
extern	ProtInfo IPProtInfo[];				// Protocol information table.
extern	ProtInfo *LastPI;					// Last protinfo structure looked at.
extern	int	NextPI;							// Next PI field to be used.
extern	NetTableEntry	*NetTableList;		// Pointer to the net table.

DEBUGSTRING(RcvFile, "iprcv.c");

//* FindUserRcv - Find the receive handler to be called for a particular protocol.
//
//	This functions takes as input a protocol value, and returns a pointer to
//	the receive routine for that protocol.
//
//	Input:	NTE			- Pointer to NetTableEntry to be searched
//			Protocol	- Protocol to be searched for.
//			UContext	- Place to returns UL Context value.
//
//	Returns: Pointer to the receive routine.
//
ULRcvProc
FindUserRcv(uchar Protocol)
{
	ULRcvProc			RcvProc;
	int					i;
#if 0
	CTELockHandle		Handle;


	CTEGetLock(&PILock, &Handle);
#endif

	if (LastPI->pi_protocol == Protocol) {
		RcvProc = LastPI->pi_rcv;
#if 0
		CTEFreeLock(&PILock, Handle);
#endif
		return RcvProc;
	}

	RcvProc = (ULRcvProc)NULL;
	for ( i = 0; i < NextPI; i++)
		if (IPProtInfo[i].pi_protocol == Protocol) {
			LastPI = &IPProtInfo[i];
			RcvProc = IPProtInfo[i].pi_rcv;
			break;
		}

#if 0
	CTEFreeLock(&PILock, Handle);
#endif
	return RcvProc;

}

//* IPRcvComplete - Handle a receive complete.
//
//	Called by the lower layer when receives are temporarily done.
//
//	Entry:	Nothing.
//
//	Returns: Nothing.
//
void
IPRcvComplete(void)
{
	void				(*ULRcvCmpltProc)(void);
	int					i;
#if 0	
	CTELockHandle		Handle;


	CTEGetLock(&PILock, &Handle);
#endif
	for (i = 0; i < NextPI; i++) {
		if ((ULRcvCmpltProc = IPProtInfo[i].pi_rcvcmplt) != NULL) {
#if 0
			CTEFreeLock(&PILock, Handle);
#endif
			(*ULRcvCmpltProc)();
#if 0
			CTEGetLock(&PILock, &Handle);
#endif
		}
	}
#if 0
	CTEFreeLock(&PILock, Handle);
#endif

}
//
// MicroNT: IP-layer fragment reassembly removed per IPSTACK-HARDENING.md.
//
// Inbound fragments are dropped at the dispatch hook in IPRcv (counter
// bumped, packet released).  TCP segments at MSS; UDP / ICMP applications
// that hand down oversized payloads either chunk themselves or fail.  No
// legitimate workload on a single-NIC internet-exposed host has cause to
// deliver fragmented packets here, and the reassembly code had a
// well-known history of memory-corruption / single-packet-kill bugs
// across the late-1990s SP-era (Teardrop, Bonk, NewTear, Ping of Death;
// findings H-001..H-005 in IPSTACK-HARDENING.md).
//
// Stripped: FindRH, FreeRH, ReassembleFragment, RATDComplete, IPReassemble.
// See `git log -- src/NT/PRIVATE/NTOS/TDI/TCPIP/IP/IPRCV.C` for the
// original code if archaeology is needed.  (MS LANMan, 1990-1992;
// individual attribution declined.)
//

//* ParseRcvdOptions - Validate incoming options.
//
//	Called during reception handling to validate incoming options. We make sure that everything
//	is OK as best we can, and find indices for any source route option.
//
//	Input:	OptInfo		- Pointer to option info. structure.
//			Index		- Pointer to optindex struct to be filled in.
//			
//
//	Returns: Index of error if any, MAX_OPT_SIZE if no errors.
//
uchar
ParseRcvdOptions(IPOptInfo *OptInfo, OptIndex *Index)
{
	uint		i= 0;			// Index variable.
	uchar		*Options = OptInfo->ioi_options;
	uint		OptLength = (uint)OptInfo->ioi_optlength;		
	uchar		Length;			// Length of option.
	uchar		Pointer;		// Pointer field, for options that use it.

	while(i < OptLength && *Options != IP_OPT_EOL) {
		if (*Options == IP_OPT_NOP) {
			i++;
			Options++;
			continue;
		}
		if (((Length = Options[IP_OPT_LENGTH]) + i) > OptLength) {
			return (uchar)i + (uchar)IP_OPT_LENGTH;	// Length exceeds options length.
		}

		Pointer = Options[IP_OPT_DATA] - 1;

		if (*Options == IP_OPT_TS) {
			if (Length < (MIN_TS_PTR - 1))
				return (uchar)i + (uchar)IP_OPT_LENGTH;
			Index->oi_tsindex == (uchar)i;		
		} else {
			if (Length < (MIN_RT_PTR - 1))
				return (uchar)i + (uchar)IP_OPT_LENGTH;
				
			if (*Options == IP_OPT_LSRR || *Options == IP_OPT_SSRR) { 	
				// A source route option
				if (Pointer < Length) {	// Route not complete
					
					if ((Length - Pointer) < sizeof(IPAddr))
						return (uchar)i + (uchar)IP_OPT_LENGTH;
					
					Index->oi_srtype = *Options;
					Index->oi_srindex = (uchar)i;
				}
			} else {
				if (*Options == IP_OPT_RR) {
					if (Pointer < Length)
						Index->oi_rrindex = (uchar)i;
				}
			}
		}

		i += Length;
		Options += Length;
	}

 	return MAX_OPT_SIZE;
}

//* BCastRcv - Receive a broadcast or multicast packet.
//
//	Called when we have to receive a broadcast packet. We loop through the NTE table,
//	calling the upper layer receive protocol for each net which matches the receive I/F
//	and for which the destination address is a broadcast.
//
//	Input:	RcvProc		- The receive procedure to be called.
//			SrcNTE		- NTE on which the packet was originally received.
//			DestAddr	- Destination address.
//			SrcAddr		- Source address of packet.
//			Data		- Pointer to received data.
//			DataLength	- Size in bytes of data
//			Protocol	- Upper layer protocol being called.
//			OptInfo		- Pointer to received IP option info.
//
//	Returns: Nothing.
//
void
BCastRcv(ULRcvProc RcvProc, NetTableEntry *SrcNTE, IPAddr DestAddr,  IPAddr SrcAddr,
	IPRcvBuf *Data, uint DataLength, uchar Protocol, IPOptInfo *OptInfo)
{
	NetTableEntry		*CurrentNTE;
	const Interface		*SrcIF = SrcNTE->nte_if;


	for (CurrentNTE = NetTableList; CurrentNTE != NULL; CurrentNTE = CurrentNTE->nte_next) {
		if ((CurrentNTE->nte_flags & NTE_ACTIVE) &&
			(CurrentNTE->nte_if == SrcIF) &&
			IS_BCAST_DEST(IsBCastOnNTE(DestAddr, CurrentNTE)))
			(*RcvProc)(SrcNTE, DestAddr, SrcAddr, CurrentNTE->nte_addr, Data,
				DataLength, TRUE, Protocol, OptInfo);
	}
}

//*	DeliverToUser - Deliver data to a user protocol.
//
//	This procedure is called when we have determined that an incoming packet belongs
//	here, and any options have been processed. We accept it for upper layer processing,
//	which means looking up the receive procedure and calling it, or passing it to BCastRcv
//	if neccessary.
//
//	Input:	SrcNTE			- Pointer to NTE on which packet arrived.
//			DestNTE			- Pointer to NTE that is accepting packet.
//			Header			- Pointer to IP header of packet.
//			Data			- Pointer to IPRcvBuf chain.
//			DataLength		- Length in bytes of upper layer data.
//			OptInfo			- Pointer to Option information for this receive.
//			DestType		- Type of destination - LOCAL, BCAST.
//
//	Returns: Nothing.
void
DeliverToUser(NetTableEntry *SrcNTE, NetTableEntry *DestNTE,
    IPHeader UNALIGNED *Header, IPRcvBuf *Data, uint DataLength,
    IPOptInfo *OptInfo, uchar DestType)
{
	ULRcvProc		rcv;

#ifdef DEBUG
	if (DestType >= DEST_REMOTE)
		DEBUGCHK;
#endif
		
	// Process this request right now. Look up the protocol. If we
	// find it, copy the data if we need to, and call the protocol's
	// receive handler. If we don't find it, send an ICMP
	// 'protocol unreachable' message.
  	rcv = FindUserRcv(Header->iph_protocol);
  	if (rcv != NULL) {
		IPSInfo.ipsi_indelivers++;
		if (DestType == DEST_LOCAL) {				
			if ((*rcv)(SrcNTE,Header->iph_dest,  Header->iph_src, DestNTE->nte_addr,
					Data, DataLength, FALSE, Header->iph_protocol, OptInfo) != IP_SUCCESS)
				SendICMPErr(DestNTE->nte_addr, Header, ICMP_DEST_UNREACH, PORT_UNREACH, 0);
					
				return;					// Just return out of here now.
		} else
			BCastRcv(rcv, SrcNTE, Header->iph_dest,  Header->iph_src,  Data, DataLength,
				Header->iph_protocol, OptInfo);
			
	} else {
		
		IPSInfo.ipsi_inunknownprotos++;
		// If we get here, we didn't find a matching protocol. Send an ICMP message.
		SendICMPErr(DestNTE->nte_addr, Header, ICMP_DEST_UNREACH, PROT_UNREACH,  0);
	}
					
}

// FreeRH, ReassembleFragment, RATDComplete, IPReassemble — stripped.
// See comment above (formerly FindRH).

		
	
//* CheckLocalOptions - Check the options received with a packet.
//
//	A routine called when we've received a packet for this host and want to examine
//	it for options. We process the options, and return TRUE or FALSE depending on whether
//	or not it's for us.
//
//	Input:	SrcNTE		- Pointer to NTE this came in on.
//			Header		- Pointer to incoming header.
//			OptInfo		- Place to put opt info.
//			DestType	- Type of incoming packet.
//
//	Returns: DestType - Local or remote.
//
uchar
CheckLocalOptions(NetTableEntry *SrcNTE, IPHeader UNALIGNED *Header,
    IPOptInfo *OptInfo, uchar DestType)
{
	uint			HeaderLength;					// Length in bytes of header.
	OptIndex		Index;
	uchar			ErrIndex;

#ifdef	DEBUG
	if (DestType >= DEST_REMOTE)
		DEBUGCHK;
#endif

	
	HeaderLength = (Header->iph_verlen & (uchar)~IP_VER_FLAG) << 2;

#ifdef DEBUG
	if (HeaderLength <= sizeof(IPHeader))
		DEBUGCHK;
#endif
			
	OptInfo->ioi_options = (uchar *)(Header + 1);				
	OptInfo->ioi_optlength = HeaderLength - sizeof(IPHeader); 						
	

	// We have options of some sort.  Validate them.  ParseRcvdOptions
	// errors are silently dropped (no ICMP_PARAM_PROBLEM reply) to
	// avoid advertising stack fingerprint over invalid input.
	Index.oi_srindex = MAX_OPT_SIZE;
	if ((ErrIndex = ParseRcvdOptions(OptInfo, &Index)) < MAX_OPT_SIZE) {
		return DEST_INVALID;
	}

	// H-006: source-routed packets are dropped on receive.  LSRR/SSRR
	// are not honoured.  Forwarding is gone, and the only legitimate
	// use of an inbound source-route on a single-NIC guest is L2
	// impersonation.  ParseRcvdOptions still records the SR index so
	// we can silently drop here.
	if (Index.oi_srindex != MAX_OPT_SIZE)
		return DEST_INVALID;

	return DEST_LOCAL;

}

//* TDUserRcv - Completion routing for a user transfer data.
//
//	This is the completion handle for TDs invoked because we need to give data to a
//	upper layer client. All we really do is call the upper layer handler with
//	the data.
//
//	Input:	NetContext	- Pointer to the net table entry on which we received this.
//			Packet		- Packet we received into.
//			Status		- Final status of copy.
//			DataSize 	- Size in bytes of data transferred.
//
//	Returns: Nothing
//
void
TDUserRcv(void *NetContext, PNDIS_PACKET Packet, NDIS_STATUS Status, uint DataSize)
{
	NetTableEntry	*NTE = (NetTableEntry *)NetContext;
	Interface		*SrcIF;
	TDContext		*Context = (TDContext *)Packet->ProtocolReserved;
	CTELockHandle	Handle;
	uchar			DestType;
	IPRcvBuf		RcvBuf;
	IPOptInfo		OptInfo;
	IPHeader		*Header;

	if (Status == NDIS_STATUS_SUCCESS) {
		Header = (IPHeader *)Context->tdc_header;
		OptInfo.ioi_ttl = Header->iph_ttl;
		OptInfo.ioi_tos = Header->iph_tos;
		OptInfo.ioi_flags = (net_short(Header->iph_offset) >> 13) & IP_FLAG_DF;
		if (Context->tdc_hlength != sizeof(IPHeader)) {
			OptInfo.ioi_options = (uchar *)(Header + 1);				
			OptInfo.ioi_optlength = Context->tdc_hlength - sizeof(IPHeader);
		} else {
			OptInfo.ioi_options = (uchar *)NULL;				
			OptInfo.ioi_optlength = 0; 						
		}
		
		DestType = Context->tdc_dtype;
		RcvBuf.ipr_next = NULL;
		RcvBuf.ipr_owner = IPR_OWNER_IP;
		RcvBuf.ipr_buffer = (uchar *)Context->tdc_buffer;
		RcvBuf.ipr_size = DataSize;

		DeliverToUser(NTE, Context->tdc_nte, Header, &RcvBuf, DataSize, &OptInfo, DestType);
		// Broadcast-forwarding removed with H-020; nothing to relay.
	}
	
	SrcIF = NTE->nte_if;
	CTEGetLockAtDPC(&SrcIF->if_lock, &Handle);
	
	Context->tdc_common.pc_link = SrcIF->if_tdpacket;
	SrcIF->if_tdpacket = Packet;
	CTEFreeLockFromDPC(&SrcIF->if_lock, Handle);

	return;

}


//*	IPRcv - Receive an incoming IP datagram.
//
//	This is the routine called by the link layer module when an incoming IP
//	datagram is to be processed. We validate the datagram (including doing
//	the xsum), copy and process incoming options, and decide what to do with it.
//
//	Entry:	MyContext	- The context valued we gave to the link layer.
//			Data		- Pointer to the data buffer.
//			DataSize	- Size in bytes of the data buffer.
//			TotalSize	- Total size in bytes available.
//			LContext1	- 1st link context.
//			LContext2	- 2nd link context.
//			BCast		- Indicates whether or not packet was received on bcast address.
//			
//	Returns: Nothing.
//
void
IPRcv(void *MyContext, void *Data, uint DataSize, uint TotalSize, NDIS_HANDLE LContext1,
	uint LContext2, uint BCast)
{
	IPHeader UNALIGNED *IPH = (IPHeader UNALIGNED *)Data;
	NetTableEntry	*NTE = (NetTableEntry *)MyContext;	// Local NTE received on
	NetTableEntry	*DestNTE;							// NTE to receive on.
	Interface		*RcvIF;								// Interface corresponding to NTE.
	CTELockHandle	Handle;
	PNDIS_PACKET	TDPacket;							// NDIS packet used for TD.
	TDContext		*TDC = (TDContext *)NULL; 			// Transfer data context.
	NDIS_STATUS		Status;
	IPAddr			DAddr;								// Dest. IP addr. of received packet.
	uint			HeaderLength;						// Size in bytes of received header.
	uint			IPDataLength;						// Length in bytes of IP (including UL) data in packet.
	IPOptInfo		OptInfo;							// Incoming header information.
	uchar			DestType;							// Type (LOCAL, REMOTE, SR) of Daddr.
	IPRcvBuf		RcvBuf;

	CTECheckMem(RcvFile);								// Check heap status.

	IPSInfo.ipsi_inreceives++;
	
	// Make sure we actually have data.
	if (DataSize) {
	
		// Check the header length, the xsum and the version. If any of these
		// checks fail silently discard the packet.
		HeaderLength = ((IPH->iph_verlen & (uchar)~IP_VER_FLAG) << 2);
		if (HeaderLength >= sizeof(IPHeader) && HeaderLength <= DataSize &&
			xsum(Data, HeaderLength) == (ushort)0xffff) {
	
			// Check the version, and sanity check the total length.		
			IPDataLength = (uint)net_short(IPH->iph_length);
			if ((IPH->iph_verlen & IP_VER_FLAG) == IP_VERSION &&
				IPDataLength > sizeof(IPHeader) && IPDataLength <= TotalSize) {
				
				IPDataLength -= HeaderLength;
				Data = (uchar *)Data + HeaderLength;
				DataSize -= HeaderLength;
				
				DAddr = IPH->iph_dest;
				DestNTE = NTE;
				
				// Find local NTE, if any.
				DestType = GetLocalNTE(DAddr, &DestNTE);	
	
				if (BCast && !IS_BCAST_DEST(DestType)) {
					IPSInfo.ipsi_inhdrerrors++;
					return;				// Non bcast packet on bcast address.
				}
	
				OptInfo.ioi_ttl = IPH->iph_ttl;
				OptInfo.ioi_tos = IPH->iph_tos;
				OptInfo.ioi_flags = (net_short(IPH->iph_offset) >> 13) &
					IP_FLAG_DF;
				OptInfo.ioi_options = (uchar *)NULL;				
				OptInfo.ioi_optlength = 0; 						
	
				if (DestType < DEST_REMOTE) {
					// It's either local or some sort of broadcast.
				
					// The data probably belongs at this station. If there
					// aren't any options, it definetly belongs here, and we'll
					// dispatch it either to our reasssmbly code or to the
					// deliver to user code. If there are options, we'll check
					// them and then either handle the packet locally or pass it
					// to our forwarding code.
						
					if (HeaderLength != sizeof(IPHeader)) {		
						// We have options.
						
						uchar	NewDType;
						NewDType = CheckLocalOptions(NTE, IPH, &OptInfo,
							DestType);
						if (NewDType != DEST_LOCAL) {
							if (NewDType == DEST_REMOTE)
								goto forward;
							else {
								IPSInfo.ipsi_inhdrerrors++;
								return;				// Bad Options.
							}
						}
					}
	
					// No options.  See if it's a fragment — if it is, drop it
					// (IP-layer reassembly removed per IPSTACK-HARDENING.md).
					if ((IPH->iph_offset & ~(IP_DF_FLAG | IP_RSVD_FLAG)) == 0) {
							
						// We don't have a fragment. If the data all fits,
						// handle it here. Otherwise transfer data it.
	
#ifdef VXD							
						if (IPDataLength > DataSize) {	
							// Data isn't all in the buffer.
#else
						// Make sure data is all in buffer, and directly
						// accesible.
						if ((IPDataLength > DataSize) ||
							!(NTE->nte_flags & NTE_COPY)) {	
#endif
							// The data isn't all here. Transfer data it.
							RcvIF = NTE->nte_if;
							CTEGetLockAtDPC(&RcvIF->if_lock, &Handle);
							TDPacket = RcvIF->if_tdpacket;
					
							if (TDPacket != (PNDIS_PACKET)NULL) {
						
								TDC = (TDContext *)TDPacket->ProtocolReserved;
								RcvIF->if_tdpacket = TDC->tdc_common.pc_link;
								CTEFreeLockFromDPC(&RcvIF->if_lock, Handle);
	
								TDC->tdc_nte = DestNTE;
								TDC->tdc_dtype = DestType;
								TDC->tdc_hlength = (uchar)HeaderLength;
								CTEMemCopy(TDC->tdc_header, IPH,
									HeaderLength + 8);
								Status = (*(RcvIF->if_transfer))(
									RcvIF->if_lcontext, LContext1,
									LContext2, HeaderLength, IPDataLength,
									TDPacket, &IPDataLength);
							
								// Check the status. If it's success, call the
								// receive procedure. Otherwise, if it's pending
								// wait for the callback.
								Data = TDC->tdc_buffer;
								if (Status != NDIS_STATUS_PENDING) { 	
									if (Status != NDIS_STATUS_SUCCESS) {
										IPSInfo.ipsi_indiscards++;
										CTEGetLockAtDPC(&RcvIF->if_lock, &Handle);
										TDC->tdc_common.pc_link =
											RcvIF->if_tdpacket;
										RcvIF->if_tdpacket = TDPacket;
										CTEFreeLockFromDPC(&RcvIF->if_lock,
											Handle);
										return;
									}
								} else
									return;			// Status is pending.
							} else {				// Couldn't get a packet.
								IPSInfo.ipsi_indiscards++;
								CTEFreeLockFromDPC(&RcvIF->if_lock, Handle);
								return;
							}
						}
								
						RcvBuf.ipr_next = NULL;
						RcvBuf.ipr_owner = IPR_OWNER_IP;
						RcvBuf.ipr_buffer = (uchar *)Data;
						RcvBuf.ipr_size = IPDataLength;
						// When we get here, we have the whole packet. Deliver
						// it.
						DeliverToUser(NTE, DestNTE, IPH, &RcvBuf, IPDataLength,
							&OptInfo, DestType);
						// Broadcast-forwarding removed with H-020;
						// packet is delivered locally and discarded.
						if (TDC != NULL) {
							CTEGetLockAtDPC(&RcvIF->if_lock, &Handle);
							TDC->tdc_common.pc_link = RcvIF->if_tdpacket;
							RcvIF->if_tdpacket = TDPacket;
							CTEFreeLockFromDPC(&RcvIF->if_lock, Handle);
						}
						return;
					} else {
						// Fragment received. IP-layer reassembly is removed
						// per IPSTACK-HARDENING.md — drop and count.  The
						// ipsi_reasmreqds field is repurposed as "fragments
						// dropped at receive" since the reassembly path no
						// longer exists; SNMP observers see the same
						// MIB-II OID with a slightly different semantic.
						IPSInfo.ipsi_reasmreqds++;
						return;
					}
	
				}
	
				// H-020: forwarding removed.  Anything not destined
				// for us is dropped and counted.  The `forward:` label
				// is retained as a goto target for CheckLocalOptions
				// rejects (which set DestType=DEST_REMOTE so options
				// would have been re-validated by the forwarding path).
forward:
				IPSInfo.ipsi_inaddrerrors++;
				return;
			}										// Bad version		
		} 											// Bad checksum
	
	}											// No data
			
	IPSInfo.ipsi_inhdrerrors++;
}

//*	IPTDComplete - IP Transfer data complete handler.
//
//	This is the routine called by the link layer when a transfer data completes.
//
//	Entry:	MyContext	- Context value we gave to the link layer.
//			Packet		- Packet we originally gave to transfer data.
//			Status		- Final status of command.
//			BytesCopied	- Number of bytes copied.
//
//	Exit: Nothing
//
void
IPTDComplete(void *MyContext, PNDIS_PACKET Packet, NDIS_STATUS Status, uint BytesCopied)
{
	TDContext		*TDC = (TDContext *)Packet->ProtocolReserved;

	// PACKET_FLAG_RA branch removed with IP reassembly; PACKET_FLAG_FW
	// branch removed with IP forwarding (H-020).  Only the
	// regular user-receive completion path remains.
	(void)TDC;
	TDUserRcv(MyContext, Packet, Status, BytesCopied);
}
