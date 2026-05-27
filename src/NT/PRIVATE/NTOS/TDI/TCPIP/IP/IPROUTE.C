/********************************************************************/
/**                     Microsoft LAN Manager                      **/
/**               Copyright(c) Microsoft Corp., 1990-1992          **/
/********************************************************************/
/* :ts=4 */

//***   iproute.c - IP routing routines.
//
//  This file contains all the routines related to IP routing, including
//  routing table lookup and management routines.

#include    "oscfg.h"
#include    "cxport.h"
#include    "ndis.h"
#include    "ip.h"
#include    "ipdef.h"
#include    "ipinit.h"
#include    "info.h"
#include	"iproute.h"
#include	"iprtdef.h"
#include	"ipxmit.h"

extern  NetTableEntry   *NetTableList;       // Pointer to the net table.

extern	NetTableEntry	*LoopNTE;			// Pointer to loopback NTE.
extern	Interface		LoopInterface;		// Pointer to loopback interface.

extern IP_STATUS SendICMPErr(IPAddr, IPHeader UNALIGNED *, uchar, uchar, ulong);
extern void ULMTUNotify(IPAddr Dest, IPAddr Src, uchar Prot, void *Ptr,
		uint NewMTU);

extern	Interface		*IFList;
extern	Interface		*FirstIF;

#define ROUTE_TABLE_SIZE    32              // Size of the route table.
DEFINE_LOCK_STRUCTURE(RouteTableLock)

#define NO_SR               0

RouteTableEntry     *RouteTable[ROUTE_TABLE_SIZE];

// Forwarding (IPForward, Redirect, FWContext pool) was removed per
// IPSTACK-HARDENING.md.  MicroNT runs single-NIC with no router role:
// no inbound source routing, no ICMP redirects accepted, no
// forwarded-broadcast support.  See decision log entries for H-006,
// H-009, H-020.  Invariant I1 retired (the forwarding code path it
// guarded no longer exists).

uint			DefGWConfigured;			// Number of default gateways configed.
uint			DefGWActive;				// Number of def. gateways active.

uint			DeadGWDetect;
uint			PMTUDiscovery;

IPMask  IPMaskTable[] = {
    CLASSA_MASK,
    CLASSA_MASK,
    CLASSA_MASK,
    CLASSA_MASK,
    CLASSA_MASK,
    CLASSA_MASK,
    CLASSA_MASK,
    CLASSA_MASK,
    CLASSB_MASK,
    CLASSB_MASK,
    CLASSB_MASK,
    CLASSB_MASK,
    CLASSC_MASK,
    CLASSC_MASK,
    CLASSD_MASK,
    CLASSE_MASK };

uint	MTUTable[] = {
	
	65535 - sizeof(IPHeader),
	32000 - sizeof(IPHeader),
	17914 - sizeof(IPHeader),
	8166 - sizeof(IPHeader),
	4352 - sizeof(IPHeader),
	2002 - sizeof(IPHeader),
	1492 - sizeof(IPHeader),
	1006 - sizeof(IPHeader),
	508 - sizeof(IPHeader),
	296 - sizeof(IPHeader),
	68 - sizeof(IPHeader)
};

CTETimer	IPRouteTimer;

#ifdef NT
#ifdef ALLOC_PRAGMA
//
// Make init code disposable.
//
int InitRouting(IPConfigInfo    *ci);

#pragma alloc_text(INIT, InitRouting)

#endif // ALLOC_PRAGMA
#endif // NT


#define	InsertAfterRTE(P, R)	(R)->rte_next = (P)->rte_next;\
								(P)->rte_next = (R)

#define	InsertRTE(R)	{\
						RouteTableEntry			*__P__; \
						__P__ = FindInsertPoint((R)); \
						InsertAfterRTE(__P__, (R)); \
						}

#define RemoveRTE(P, R)	(P)->rte_next = (R)->rte_next;

//*	GetHashMask - Get mask to use with address when hashing.
//
//	Called when we need to decide on the mask to use when hashing. If the 
//	supplied mask is the host mask or the default mask, we'll use that. Else
//	if the supplied mask is at least as specific as the net mask, we'll use the
//	net mask. Otherwise we drop back to the default mask.
//
//	Input:	Destination			- Destination we'll be hashing on.
//			Mask				- Caller supplied mask.
//
//	Returns:	Mask to use.
//
IPMask
GetHashMask(IPAddr Destination, IPMask Mask)
{
	IPMask		NetMask;
	
	if (Mask == HOST_MASK || Mask == DEFAULT_MASK)
		return Mask;
		
	NetMask = IPNetMask(Destination);
	
	if ((NetMask & Mask) == NetMask)
		return NetMask;
		
	return DEFAULT_MASK;
	
}

//** AddrOnIF - Check to see if a given address is local to an IF
//
//	Called when we want to see if a given address is a valid local address
//	for an interface. We walk down the chain of NTEs in the interface, and
//	see if we get a match. We assume the caller holds the RouteTableLock
//	at this point.
//
//	Input:	IF			- Interface to check.
//			Addr		- Address to check.
//
//	Returns: TRUE if Addr is an address for IF, FALSE otherwise.
//
uint
AddrOnIF(Interface *IF, IPAddr Addr)
{
	NetTableEntry		*NTE;

	NTE = IF->if_nte;
	while (NTE != NULL) {
		if ((NTE->nte_flags & NTE_VALID) && IP_ADDR_EQUAL(NTE->nte_addr, Addr))
			return TRUE;
		else
			NTE = NTE->nte_ifnext;
	}
	
	return FALSE;
}			

//** BestNTEForIF - Find the 'best match' NTE on a given interface.
//
//  This is a utility function that takes an  address and tries to find the
//  'best match' NTE on a given interface. This is really only useful when we
//	have multiple IP addresses on a single interface.
//
//  Input:  Address     - Source address of packet.
//          IF      	- Pointer to IF to be searched.
//
//  Returns: The 'best match' NTE.
//
NetTableEntry *
BestNTEForIF(IPAddr Address, Interface *IF)
{
    NetTableEntry   *CurrentNTE, *FoundNTE;

	if (IF->if_nte != NULL) {
		// Walk the list of NTEs, looking for a valid one.
		CurrentNTE = IF->if_nte;
		FoundNTE = NULL;
		do {
			if (CurrentNTE->nte_flags & NTE_VALID) {
	            if (IP_ADDR_EQUAL(Address & CurrentNTE->nte_mask,
	            	CurrentNTE->nte_addr & CurrentNTE->nte_mask))
	                return CurrentNTE;
				else
					if (FoundNTE == NULL)
						FoundNTE = CurrentNTE;
				
			}
			
			CurrentNTE = CurrentNTE->nte_ifnext;
		} while (CurrentNTE != NULL);
		
		// If we found a match, or we didn't and the destination is not
		// a broadcast, return the result. We have special case code to
		// handle broadcasts, since the interface doesn't really matter there.
		if (FoundNTE != NULL || (!IP_ADDR_EQUAL(Address, IP_LOCAL_BCST) &&
			!IP_ADDR_EQUAL(Address, IP_ZERO_BCST)))
			return FoundNTE;		
	
	} 
	
	// An 'anonymous' I/F, or the address we're reaching is a broadcast and the
	// first interface has no address. Find a valid (non-loopback) address.
	for (CurrentNTE = NetTableList; CurrentNTE != NULL;
		CurrentNTE = CurrentNTE->nte_next) {
		if (CurrentNTE != LoopNTE && (CurrentNTE->nte_flags & NTE_VALID))
			return CurrentNTE;

	}
	
	return NULL;
			
}			

//** IsBCastonNTE - Determine if the specified addr. is a bcast on a spec. NTE.
//
//  This routine is called when we need to know if an address is a broadcast
//  on a particular net. We check in the order we expect to be most common - a
//	subnet bcast, an all ones broadcast, and then an all subnets broadcast.  We
//	return the type of broadcast it is, or return DEST_LOCAL if it's not a
//	broadcast.
//
//  Entry:  Address     - Address in question.
//          NTE         - NetTableEntry to check Address against.
//
//  Returns: Type of broadcast.
//
uchar
IsBCastOnNTE(IPAddr Address, NetTableEntry *NTE)
{
    IPMask      	Mask;
	IPAddr			BCastAddr;

	BCastAddr = NTE->nte_if->if_bcast;

	if (NTE->nte_flags & NTE_VALID) {
	
		Mask = NTE->nte_mask;
	    if (IP_ADDR_EQUAL(Address, (NTE->nte_addr & Mask) | (BCastAddr & ~Mask)))
	        return DEST_SN_BCAST;
	
	    // See if it's an all subnet's broadcast. Multicast is dropped:
	    // we have no use case and the IGMP path is stripped.
		if (!CLASSD_ADDR(Address)) {
		    Mask = IPNetMask(Address);
		
		    if (IP_ADDR_EQUAL(Address,
		    	(NTE->nte_addr & Mask) | (BCastAddr & ~Mask)))
		        return DEST_BCAST;
		}
	}

    // A global bcast is certainly a bcast on this net.
    if (IP_ADDR_EQUAL(Address, BCastAddr))
        return DEST_BCAST;

    return DEST_LOCAL;

}

//** GetAddrType - Return the type of a specified address.
//
//  This function takes an input address, and determines what type it is. An
//  address can be local, bcast, remote, or remote bcast.
//
//  Input: Address      - Address to be check.
//
//  Returns: Destination type.
//
uchar
GetAddrType(IPAddr Address)
{
    NetTableEntry   *NTE;       // Pointer to current NTE.
    IPMask          Mask;       // Mask for address.
    uchar           Result;     // Result of broadcast check.
    IPMask          SNMask;


    if (!CLASSE_ADDR(Address)) {
        // See if it's one of our local addresses, or a broadcast
        // on a local address.
        NTE = NetTableList;
        do {

            if (IP_ADDR_EQUAL(NTE->nte_addr, Address) &&
				(NTE->nte_flags & NTE_VALID))
                return DEST_LOCAL;

            if ((Result = IsBCastOnNTE(Address, NTE)) != DEST_LOCAL)
                return Result;

            // See if the destination has a valid host part.
			if (NTE->nte_flags & NTE_VALID) {
	            SNMask = NTE->nte_mask;
	            if (IP_ADDR_EQUAL(Address & SNMask, NTE->nte_addr & SNMask)) {
	            	// On this subnet. See if the host part is invalid.
	
	                if (IP_ADDR_EQUAL(Address & SNMask, Address))
	                    return DEST_INVALID;        // Invalid 0 host part.
	            }
			}
            NTE = NTE->nte_next;
        } while (NTE != NULL);

        // It's not a local address, see if it's loopback.
        if (IP_LOOPBACK(Address))
            return DEST_LOCAL;

		// Multicast is not supported: drop class D destinations.
		if (CLASSD_ADDR(Address))
			return DEST_INVALID;
				
        Mask = IPNetMask(Address);

        // Now check remote broadcast. When we get here we know that the
		// address is not a global broadcast, a subnet broadcast for a subnet
		// of which we're a member, or an all-subnets broadcast for a net of
		// which we're a member. Since we're avoiding making assumptions about
		// all subnet of a net having the same mask, we can't really check for
		// a remote subnet broadcast. We'll use the net mask and see if it's
		// a remote all-subnet's broadcast.
        if (IP_ADDR_EQUAL(Address, (Address & Mask) | (IP_LOCAL_BCST & ~Mask)))
            return DEST_REM_BCAST;

        // Check for invalid 0 parts. All we can do from here is see if he's
		// sending to a remote net with all zero subnet and host parts. We
		// can't check to see if he's sending to a remote subnet with an all
		// zero host part.
        if (IP_ADDR_EQUAL(Address, Address & Mask) ||
        	IP_ADDR_EQUAL(Address, NULL_IP_ADDR))
            return DEST_INVALID;

        // Must be remote.
        return DEST_REMOTE;
    }

    return DEST_INVALID;
}

//** IPHash - IP hash function.
//
// This is the function to compute the hash index from a masked address.
//
//  Input:  Address - Masked address to be hashed.
//
//  Returns: Hashed value.
//
uint
IPHash(IPAddr Address)
{
    uchar   *i = (uchar *)&Address;
    return (i[0] + i[1] + i[2] + i[3]) & (ROUTE_TABLE_SIZE-1);
}

//** GetLocalNTE - Get the local NTE for an incoming packet.
//
//  Called during receive processing to find a matching NTE for a packet.
//  First we check against the NTE we received it on, then against any NTE.
//
//  Input:  Address     - The dest. address of the packet.
//          NTE         - Pointer to NTE packet was received on - filled in on exit w/correct
//                          NTE.
//
//  Returns: DEST_LOCAL if the packet is destined for this host, DEST_REMOTE if it needs to
//          be routed, DEST_SN_BCAST or DEST_BCAST if it's some sort of a broadcast.
uchar
GetLocalNTE(IPAddr Address, NetTableEntry **NTE)
{
    NetTableEntry   *LocalNTE = *NTE;
    IPMask          Mask;
    uchar           Result;
    int             i;
    Interface       *LocalIF;
    NetTableEntry   *OriginalNTE;

    // Quick check to see if it is for the NTE it came in on (the common case).
    if (IP_ADDR_EQUAL(Address, LocalNTE->nte_addr) &&
		(LocalNTE->nte_flags & NTE_VALID))
        return DEST_LOCAL;                              // For us, just return.

    // Now check to see if it's a broadcast of some sort on the interface it
    // came in on.
    if ((Result = IsBCastOnNTE(Address, LocalNTE)) != DEST_LOCAL)
        return Result;

    // The common cases failed us. Loop through the NetTable and see if
    // it is either a valid local address or is a broadcast on one of the NTEs
    // on the incoming interface. We won't check the NTE we've already looked
    // at. We look at all NTEs, including the loopback NTE, because a loopback
    // frame could come through here. Also, frames from ourselves to ourselves
    // will come in on the loopback NTE.

    i = 0;
    LocalIF = LocalNTE->nte_if;
    OriginalNTE = LocalNTE;
    LocalNTE = NetTableList;
    do {
        if (LocalNTE != OriginalNTE) {
            if (IP_ADDR_EQUAL(Address, LocalNTE->nte_addr) &&
				(LocalNTE->nte_flags & NTE_VALID)) {
                *NTE = LocalNTE;
                return DEST_LOCAL;		// For us, just return.
            }

            // If this NTE is on the same interface as the NTE it arrived on,
            // see if it's a broadcast.
            if (LocalIF == LocalNTE->nte_if)
                if ((Result = IsBCastOnNTE(Address, LocalNTE)) != DEST_LOCAL) {
                    *NTE = LocalNTE;
                    return Result;
                }

        }

        LocalNTE = LocalNTE->nte_next;

    } while (LocalNTE != NULL);

    // It's not a local address, see if it's loopback.
    if (IP_LOOPBACK(Address)) {
		*NTE = LoopNTE;
        return DEST_LOCAL;
	}

	// Multicast is not supported: drop class D destinations.
	if (CLASSD_ADDR(Address))
		return DEST_INVALID;
	
    // It's not local. Check to see if maybe it's a net broadcast for a net
    // of which we're not a member. If so, return remote bcast. We can't check
	// for subnet broadcast of subnets for which we're not a member, since we're
	// not making assumptions about all subnets of a single net having the
	// same mask. If we're here it's not a subnet broadcast for a net of which
	// we're a member, so we don't know a subnet mask for it. We'll just use
	// the net mask.
    Mask = IPNetMask(Address);
    if (IP_ADDR_EQUAL(Address, (Address & Mask) |
		((*NTE)->nte_if->if_bcast & ~Mask)))
        return DEST_REM_BCAST;

    // If it's too the 0 address, or a Class E address, or has an all-zero
    // subnet and net part, it's invalid.


    if (IP_ADDR_EQUAL(Address, IP_ZERO_BCST) ||
    	IP_ADDR_EQUAL(Address, (Address & Mask)) ||
        CLASSE_ADDR(Address))
        return DEST_INVALID;

    return DEST_REMOTE;
}


//** FindSpecificRTE - Look for a particular RTE.
//
//	Called when we're adding a route and want to find a particular RTE.
//	We take in the destination, mask, first hop, and src addr, and search
//	the appropriate routeing table chain. We assume the caller has the
//	RouteTableLock held. If we find the match, we'll return a pointer to the
//	RTE with the RTE lock held, as well as a pointer to the previous RTE in
//	the chain.
//
//	Input:	Dest			- Destination to search for.
//			Mask			- Mask for destination.
//			FirstHop		- FirstHop to Dest.
//			OutIF			- Pointer to outgoing interface structure.
//			PrevRTE			- Place to put PrevRTE, if found.
//
//	Returns: Pointer to matching RTE if found, or NULL if not.
//
RouteTableEntry *
FindSpecificRTE(IPAddr Dest, IPMask Mask, IPAddr FirstHop, Interface *OutIF,
	RouteTableEntry **PrevRTE)
{
	uint			Index;
	IPMask			HashMask;
	RouteTableEntry	*TempRTE, *CurrentRTE;

	HashMask = GetHashMask(Dest, Mask);
	
	Index = IPHash(Dest & HashMask);
	TempRTE = STRUCT_OF(RouteTableEntry, &RouteTable[Index], rte_next);
	CurrentRTE = TempRTE->rte_next;
	
	// Walk the table, looking for a match.
	while (CurrentRTE != NULL) {
		// See if everything matches.
		if (IP_ADDR_EQUAL(CurrentRTE->rte_dest,Dest) &&
			CurrentRTE->rte_mask == Mask &&
			IP_ADDR_EQUAL(CurrentRTE->rte_addr, FirstHop) &&
			CurrentRTE->rte_if == OutIF)
			break;
		
		TempRTE = CurrentRTE;
		CurrentRTE = CurrentRTE->rte_next;
	}
	
	*PrevRTE = TempRTE;
	return CurrentRTE;

}

//** FindRTE - Find a matching RTE in a hash table chain.
//
//	Called when we want to find a matching RTE. We take in a destination,
//	a source, a hash index, and a maximum priority, and walk down the
//	chain specified by the index looking for a matching RTE. If we can find
//	one, we'll keep looking hoping for a match on the source address.
//
//	The caller must hold the RouteTableLock before calling this function.
//
//	Input:	Dest				- Destination we're trying to reach.
//			Source				- Source address to match.
//			Index				- Index of chain to search.
//			MaxPri				- Maximum acceptable priority.
//			MinPri				- Minimum acceptable priority.
//
//	Returns: Pointer to RTE if found, or NULL if not.
//
RouteTableEntry *
FindRTE(IPAddr Dest, IPAddr Source, uint Index, uint MaxPri, uint MinPri)
{
	RouteTableEntry				*CurrentRTE;
	uint						RTEPri;
	uint						Metric;
	RouteTableEntry				*FoundRTE;
	
	// First walk down the chain, skipping those RTEs that have a
	// a priority greater than what we want.
	
	CurrentRTE = RouteTable[Index];
	
	for (;;) {
		if (CurrentRTE == NULL)
			return NULL;			// Hit end of chain, bounce out.
			
		if (CurrentRTE->rte_priority <= MaxPri)
			break;					// He's a possible match.
		
		// Priority is too big. Try the next one.
		CurrentRTE = CurrentRTE->rte_next;
	}
	
	FoundRTE = NULL;
	
	// When we get here, we have a locked RTE with a priority less than or
	// equal to what was specifed.
	// Examine it, and if it doesn't match try the next one. If it does match
	// we'll stash it temporarily and keep looking for one that matches the
	// specifed source.
	for (;;) {
		
		// The invariant at the top of this loop is that CurrentRTE points to
		// a candidate RTE, locked with the handle in CurrentHandle.

		if (CurrentRTE->rte_flags & RTE_VALID) {
			// He's valid. Make sure he's at least the priority we need. If
			// he is, see if he matches. Otherwise we're done.

			if (CurrentRTE->rte_priority < MinPri) {
				// His priority is too small. Since the list is in sorted order,
				// all following routes must have too low a priority, so we're
				// done.
				return NULL;
			}

			if (IP_ADDR_EQUAL(Dest & CurrentRTE->rte_mask, CurrentRTE->rte_dest)) {
				// He's valid for this route. Save the current information,
				// and look for a matching source.
 				FoundRTE = CurrentRTE;

				if (!IP_ADDR_EQUAL(Source, NULL_IP_ADDR) &&
					!AddrOnIF(CurrentRTE->rte_if, Source)) {
					RTEPri = CurrentRTE->rte_priority;
					Metric = CurrentRTE->rte_metric;
					
					CurrentRTE = CurrentRTE->rte_next;
					
					// We've save the info. Starting at the next RTE, look for
					// an RTE that matches both the mask criteria and the source
					// address. The search will terminate when we hit the end
					// of the list, or the RTE we're examing has a different
					// (presumably lesser) priority (or greater metric), or we
					// find a match.
					while (CurrentRTE != NULL &&
						CurrentRTE->rte_priority == RTEPri &&
						CurrentRTE->rte_metric == Metric) {
						
						
						// Skip invalid route types.
						if (CurrentRTE->rte_flags & RTE_VALID) {
							if (IP_ADDR_EQUAL(Dest & CurrentRTE->rte_mask,
								CurrentRTE->rte_dest)) {
								if (AddrOnIF(CurrentRTE->rte_if, Source)) {
									// He matches the source. Free the old lock,
									// and break out.
									FoundRTE = CurrentRTE;
									break;
								}
							}
									
						}
						CurrentRTE = CurrentRTE->rte_next;
					}
				}
					
				// At this point, FoundRTE points to the RTE we want to return,
				// and *Handle has the lock handle for the RTE. Break out.
				break;
			}
				
		}
		
		CurrentRTE = CurrentRTE->rte_next;
		
		if (CurrentRTE != NULL) {
			continue;
		} else
			break;
	}
	
	return FoundRTE;
}

//* ValidateDefaultGWs - Mark all default gateways as valid.
//
//	Called to one or all of our default gateways as up. The caller specifies
//	the IP address of the one to mark as up, or NULL_IP_ADDR if they're all
//	supposed to be marked up. We return a count of how many we marked as
//	valid.
//
//	Input: IP address of G/W to mark as up.
//
//	Returns: Count of gateways marked as up.
//
uint
ValidateDefaultGWs(IPAddr Addr)
{
	RouteTableEntry		*RTE;
	uint				Count = 0;
	uint				Now = CTESystemUpTime() / 1000L;
	
	RTE = RouteTable[IPHash(0)];
	
	while (RTE != NULL) {
		if (RTE->rte_mask == DEFAULT_MASK && !(RTE->rte_flags & RTE_VALID) &&
			(IP_ADDR_EQUAL(Addr, NULL_IP_ADDR) ||
			IP_ADDR_EQUAL(Addr, RTE->rte_addr))) {
			RTE->rte_flags |= RTE_VALID;
			RTE->rte_valid = Now;
			Count++;
		}
		RTE = RTE->rte_next;
	}	

	DefGWActive += Count;
	return Count;
}

//** LookupRTE - Lookup a routeing table entry.
//
//  This routine looks up a routing table entry, and returns with the entry
//	locked if it finds one. If it doesn't find one, it returns NULL. This
//	routine assumes that the routing table is locked when it is called.
//
//  The routeing table is organized as an open hash table. The table contains
//	routes to hosts, subnets, and nets. Host routes are hashed on the host
//	address, other non-default routes on the destination anded with the net
//	mask, and default routes wind up in bucket 0. Within each bucket chain
//	the routes are sorted with the greatest priority (i.e. number of bits in the
//	route mask) first, and within each priority class the routes are sorted
//	with the lowest metric first. The caller may specify a maximum priority
//  for the route to be found. We look for routes in order of most specific to
//	least specifc, i.e. first host routes, then other non-default routes, and
//	finally default routes. We give preference to routes that are going out
//	on an interface with an address that matches the input source address.
//
//	It might be worthile in the future to split this up into multiple tables,
//	so that we have a table for host routes, a table for non-default routes,
//	and a list of default routes.
//
//  Entry:  Address     - Address for which a route is to be found.
//          Src         - IPAddr of source (may be 0).
//          MaxPri		- Maximum priority of route to find.
//
//  Returns: A pointer to the locked RTE if we find one, or NULL if we don't
//
RouteTableEntry *
LookupRTE(IPAddr Address,  IPAddr Src, uint MaxPri)
{
	RouteTableEntry		*RTE;
	
	// First try to find a host route, if we're allowed to.
	if (MaxPri == HOST_ROUTE_PRI) {
		RTE = FindRTE(Address, Src, IPHash(Address), MaxPri, MaxPri);
		if (RTE != NULL)
			return RTE;
	}
	
	// Don't have or weren't allowed to find a host route. See if we can
	// find a non-default route.
	if (MaxPri > DEFAULT_ROUTE_PRI) {
		RTE = FindRTE(Address, Src, IPHash(Address & IPNetMask(Address)),
			MaxPri, DEFAULT_ROUTE_PRI + 1);
		if (RTE != NULL)
			return RTE;
	}
	
	// No non-default route. Try a default route.
	RTE = FindRTE(Address, Src, IPHash(0), MaxPri, DEFAULT_ROUTE_PRI);
	return RTE;
		
}

//* FindInsertPoint - Find out where to insert an RTE in the table.
//
//	Called to find out where to insert an RTE. We hash into the table,
//	and walk the chain until we hit the end or we find an RTE with a
//	lesser priority or a greater metric. Once we find the appropriate spot
//	we return a pointer to the RTE immediately prior to the one we want to
//	insert. We assume the caller holds the lock on the route table when calling
//	this function.
//
//	Input:	InsertRTE		- RTE we're going to (eventually) insert.
//
//	Returns: Pointer to RTE in insert after.
//
RouteTableEntry *
FindInsertPoint(RouteTableEntry *InsertRTE)
{
	RouteTableEntry				*PrevRTE, *CurrentRTE;
	IPMask						HashMask, Mask;
	uint						Priority, Metric;
	uint						Index;

	Priority = InsertRTE->rte_priority;
	Metric = InsertRTE->rte_metric;

	// First figure out where he should go. We'll hash on the whole address
	// if the mask allows us to, or on the net portion if this is a non-default
	// route.

	Mask = InsertRTE->rte_mask;
	HashMask = GetHashMask(InsertRTE->rte_dest, Mask);
	
	Index = IPHash(InsertRTE->rte_dest & HashMask);

	PrevRTE = STRUCT_OF(RouteTableEntry, &RouteTable[Index], rte_next);
	CurrentRTE = PrevRTE->rte_next;
	
	// Walk the table, looking for a place to insert it.
	while (CurrentRTE != NULL) {
		if (CurrentRTE->rte_priority < Priority) {
			break;
		}

		if (CurrentRTE->rte_priority == Priority) {
			// Priorities match. Check the metrics.
			if (CurrentRTE->rte_metric > Metric) {
				// Our metric is smaller than his, so we're done.
				break;
			}
		}

		// Either his priority is greater than ours or his metric is less
		// than or equal to ours. Check the next one.
		PrevRTE = CurrentRTE;
		CurrentRTE = CurrentRTE->rte_next;
	}

	// At this point, we've either found the correct spot or hit the end
	// of the list.
	return PrevRTE;
	
}


//** LookupNextHop - Look up the next hop
//
//  Called when we need to find the next hop on our way to a destination. We
//	call LookupRTE to find it, and return the appropriate information.
//
//  Entry:  Destination     - IP address we're trying to reach.
//          Src             - Source address of datagram being routed.
//          NextHop         - Pointer to IP address of next hop (returned).
//			MTU				- Pointer to where to return max MTU used on the
//								route.
//
//  Returns: Pointer to outgoing interface if we found one, NULL otherwise.
//
Interface *
LookupNextHop(IPAddr Destination, IPAddr Src, IPAddr *NextHop, uint *MTU)
{
    CTELockHandle   TableLock;      // Lock handle for routing table.
    RouteTableEntry *Route;         // Pointer to route table entry for route.
	Interface		*IF;

    CTEGetLock(&RouteTableLock, &TableLock);
    Route = LookupRTE(Destination, Src, HOST_ROUTE_PRI);

    if (Route != (RouteTableEntry *)NULL) {
        IF = Route->rte_if;
		
		// If this is a direct route, send straight to the destination.
        *NextHop = IP_ADDR_EQUAL(Route->rte_addr, IPADDR_LOCAL) ? Destination :
			Route->rte_addr;
			
		*MTU = Route->rte_mtu;
        CTEFreeLock(&RouteTableLock, TableLock);
        return IF;          		
    } else {                        // Couldn't find a route.
        CTEFreeLock(&RouteTableLock, TableLock);
        return NULL;
    }
}

//* RTReadNext - Read the next route in the table.
//
//  Called by the GetInfo code to read the next route in the table. We assume
//  the context passed in is valid, and the caller has the RouteTableLock.
//
//  Input:  Context     - Pointer to a RouteEntryContext.
//          Buffer      - Pointer to an IPRouteEntry structure.
//
//  Returns: TRUE if more data is available to be read, FALSE is not.
//
uint
RTReadNext(void *Context, void *Buffer)
{
    RouteEntryContext   *REContext = (RouteEntryContext *)Context;
    IPRouteEntry        *IPREntry = (IPRouteEntry *)Buffer;
    RouteTableEntry     *CurrentRTE;
    uint                i;
    uint                Now = CTESystemUpTime() / 1000L;
	Interface			*IF;
	NetTableEntry		*SrcNTE;

	CurrentRTE = REContext->rec_rte;
	
	// Fill in the buffer.
	IF = CurrentRTE->rte_if;

	IPREntry->ire_dest = CurrentRTE->rte_dest;
	IPREntry->ire_index = IF->if_index;
	IPREntry->ire_metric1 = CurrentRTE->rte_metric;
	IPREntry->ire_metric2 = IRE_METRIC_UNUSED;
	IPREntry->ire_metric3 = IRE_METRIC_UNUSED;
	IPREntry->ire_metric4 = IRE_METRIC_UNUSED;
	IPREntry->ire_metric5 = IRE_METRIC_UNUSED;
	if (IP_ADDR_EQUAL(CurrentRTE->rte_addr, IPADDR_LOCAL)) {
		SrcNTE = BestNTEForIF(CurrentRTE->rte_dest, IF);
		if (IF->if_nte != NULL && SrcNTE != NULL)
			IPREntry->ire_nexthop = SrcNTE->nte_addr;
		else
			IPREntry->ire_nexthop = IPREntry->ire_dest;
	} else {
		IPREntry->ire_nexthop = CurrentRTE->rte_addr;
	}
	IPREntry->ire_type = (CurrentRTE->rte_flags & RTE_VALID ?
		CurrentRTE->rte_type : IRE_TYPE_INVALID);
	IPREntry->ire_proto = CurrentRTE->rte_proto;	
	IPREntry->ire_age = Now - CurrentRTE->rte_valid;
	IPREntry->ire_mask = CurrentRTE->rte_mask;

    // We've filled it in. Now update the context.
    if (CurrentRTE->rte_next != NULL) {
        REContext->rec_rte = CurrentRTE->rte_next;
        return TRUE;
    } else {
        // The next RTE is NULL. Loop through the RouteTable looking for a new
        // one.
        i = REContext->rec_index + 1;
        while (i < ROUTE_TABLE_SIZE) {
            if (RouteTable[i] != NULL) {
                REContext->rec_rte = RouteTable[i];
                REContext->rec_index = i;
                return TRUE;
                break;
            } else
                i++;
        }

        REContext->rec_index = 0;
        REContext->rec_rte = NULL;
        return FALSE;
    }

}

//* RTValidateContext - Validate the context for reading the route table.
//
//  Called to start reading the route table sequentially. We take in
//  a context, and if the values are 0 we return information about the
//  first route in the table. Otherwise we make sure that the context value
//  is valid, and if it is we return TRUE.
//  We assume the caller holds the route table lock.
//
//  Input:  Context     - Pointer to a RouteEntryContext.
//          Valid       - Where to return information about context being
//                          valid.
//
//  Returns: TRUE if more data to be read in table, FALSE if not. *Valid set
//      to TRUE if input context is valid
//
uint
RTValidateContext(void *Context, uint *Valid)
{
    RouteEntryContext   *REContext = (RouteEntryContext *)Context;
    uint                i;
    RouteTableEntry     *TargetRTE;
    RouteTableEntry     *CurrentRTE;

    i = REContext->rec_index;
    TargetRTE = REContext->rec_rte;

    // If the context values are 0 and NULL, we're starting from the beginning.
    if (i == 0 && TargetRTE == NULL) {
        *Valid = TRUE;
        do {
            if ((CurrentRTE = RouteTable[i]) != NULL) {
                break;
            }
            i++;
        } while (i < ROUTE_TABLE_SIZE);

        if (CurrentRTE != NULL) {
            REContext->rec_index = i;
            REContext->rec_rte = CurrentRTE;
            return TRUE;
        } else
            return FALSE;

    } else {

        // We've been given a context. We just need to make sure that it's
        // valid.

        if (i < ROUTE_TABLE_SIZE) {
            CurrentRTE = RouteTable[i];
            while (CurrentRTE != NULL) {
                if (CurrentRTE == TargetRTE) {
                    *Valid = TRUE;
                    return TRUE;
                    break;
                } else {
                    CurrentRTE = CurrentRTE->rte_next;
                }
            }

        }

        // If we get here, we didn't find the matching RTE.
        *Valid = FALSE;
        return FALSE;

    }

}

//*	InvalidateRCEChain - Invalidate the RCEs on an RCE.
//
//	Called to invalidate the RCE chain on an RTE. We assume the caller holds
//	the route table lock.
//
//	Input:	RTE			- RTE on which to invalidate RCEs.
//
//	Returns: Nothing.
//
void
InvalidateRCEChain(RouteTableEntry *RTE)
{
    CTELockHandle   RCEHandle;      // Lock handle for RCE being updated.
    RouteCacheEntry *TempRCE, *CurrentRCE;
	Interface		*OutIF;

	OutIF = RTE->rte_if;

	// If there is an RCE chain on this RCE, invalidate the RCEs on it. We still
	// hold the RouteTableLock, so RCE closes can't happen.


    CurrentRCE = RTE->rte_rcelist;
	RTE->rte_rcelist = NULL;

	// Walk down the list, nuking each RCE.
    while (CurrentRCE != NULL) {

        CTEGetLock(&CurrentRCE->rce_lock, &RCEHandle);

		if (CurrentRCE->rce_valid) {
			CTEAssert(CurrentRCE->rce_rte == RTE);
			CurrentRCE->rce_valid = FALSE;
        	CurrentRCE->rce_rte = (RouteTableEntry *)OutIF;
			if (CurrentRCE->rce_usecnt == 0)
        		(*(OutIF->if_invalidate))(OutIF->if_lcontext, CurrentRCE);
		} else
			CTEAssert(FALSE);

        TempRCE = CurrentRCE->rce_next;
        CTEFreeLock(&CurrentRCE->rce_lock, RCEHandle);
        CurrentRCE = TempRCE;
    }

}

//*	DeleteRTE - Delete an RTE.
//
//	Called when we need to delete an RTE. We assume the caller has the
//	RouteTableLock. We'll splice out the RTE, invalidate his RCEs, and
//	free the memory.
//
//	Input:	PrevRTE				- RTE in 'front' of one being deleted.
//			RTE					- RTE to be deleted.
//
//	Returns: Nothing.
//
void
DeleteRTE(RouteTableEntry *PrevRTE, RouteTableEntry *RTE)
{
	PrevRTE->rte_next = RTE->rte_next;		// Splice him from the table.
	IPSInfo.ipsi_numroutes--;

	if (RTE->rte_mask == DEFAULT_MASK) {
		// We're deleting a default route.
		DefGWConfigured--;
		DefGWActive--;
	}
	
	InvalidateRCEChain(RTE);
    // Free the old route.
    CTEFreeMem(RTE);

}

//*	DeleteRTEOnIF - Delete all RTEs on a particular IF.
//
//	A function called by RTWalk when we want to delete all RTEs on a particular
//	inteface. We just check the I/F of each RTE, and if it matches we return
//	FALSE.
//
//	Input:	RTE			- RTE to check.
//			Context 	- Interface on which we're deleting.
//
//	Returns: FALSE if we want to delete it, TRUE otherwise.
//
uint
DeleteRTEOnIF(RouteTableEntry *RTE, void *Context, void *Context1)
{
	Interface		*IF = (Interface *)Context;

	if (RTE->rte_if == IF && !IP_ADDR_EQUAL(RTE->rte_dest, IF->if_bcast))
		return FALSE;
	else
		return TRUE;
	
}

//*	InvalidateRCEOnIF - Invalidate all RCEs on a particular IF.
//
//	A function called by RTWalk when we want to invalidate all RCEs on a
//	particular inteface. We just check the I/F of each RTE, and if it
//	matches we call InvalidateRCEChain to invalidate the RCEs.
//
//	Input:	RTE			- RTE to check.
//			Context 	- Interface on which we're invalidating.
//
//	Returns: TRUE.
//
uint
InvalidateRCEOnIF(RouteTableEntry *RTE, void *Context, void *Context1)
{
	Interface		*IF = (Interface *)Context;

	if (RTE->rte_if == IF)
		InvalidateRCEChain(RTE);
	
	return TRUE;
	
}

//*	SetMTUOnIF - Set the MTU on an interface.
//
//	Called when we need to set the MTU on an interface.
//
//	Input:	RTE			- RTE to check.
//			Context		- Pointer to a context.
//			Context1	- Pointer to the new MTU.
//
//	Returns: TRUE.
//
uint
SetMTUOnIF(RouteTableEntry *RTE, void *Context, void *Context1)
{
	uint		NewMTU = *(uint *)Context1;
	Interface	*IF = (Interface *)Context;
	
	if (RTE->rte_if == IF)
		RTE->rte_mtu = NewMTU;
	
	return TRUE;
}

//*	SetMTUToAddr - Set the MTU to a specific address.
//
//	Called when we need to set the MTU to a specific address. We set the MTU
//	for all routes that use the specified address as a first hop to the new
//	MTU.
//
//	Input:	RTE			- RTE to check.
//			Context		- Pointer to a context.
//			Context1	- Pointer to the new MTU.
//
//	Returns: TRUE.
//
uint
SetMTUToAddr(RouteTableEntry *RTE, void *Context, void *Context1)
{
	uint		NewMTU = *(uint *)Context1;
	IPAddr		Addr = *(IPAddr *)Context;
	
	if (IP_ADDR_EQUAL(RTE->rte_addr, Addr))
		RTE->rte_mtu = NewMTU;
	
	return TRUE;
}

//*	RTWalk - Routine to walk the route table.
//
//	This routine walks the route table, calling the specified function
//	for each entry. If the called function returns FALSE, the RTE is
//	deleted.
//
//	Input:	CallFunc			- Function to call for each entry.
//			Context				- Context value to pass to each call.
//
//	Returns: Nothing.
//
void
RTWalk(uint (*CallFunc)(struct RouteTableEntry *, void *, void *),
	void *Context, void *Context1)
{
	uint			i;
	CTELockHandle	Handle;
	RouteTableEntry	*RTE, *PrevRTE;

	CTEGetLock(&RouteTableLock, &Handle);
	
	for (i = 0; i < ROUTE_TABLE_SIZE; i++) {
		
		PrevRTE = STRUCT_OF(RouteTableEntry, &RouteTable[i], rte_next);
		RTE = RouteTable[i];
		while (RTE != NULL) {
			if (!(*CallFunc)(RTE, Context, Context1)) {
				DeleteRTE(PrevRTE, RTE);
			} else {
				PrevRTE = RTE;
			}
			RTE = PrevRTE->rte_next;
		}
	}

	CTEFreeLock(&RouteTableLock, Handle);
}

//** AttachRCEToRTE - Attach an RCE to an RTE.
//
//  This procedure takes an RCE, finds the appropriate RTE, and attaches it.
//	We check to make sure that the source address is still valid.
//
//  Entry:  RCE             - RCE to be attached.
//
//  Returns: TRUE if we attach it, false if we don't.
//
uint
AttachRCEToRTE(RouteCacheEntry *RCE)
{
    CTELockHandle   TableHandle, RCEHandle;
	RouteTableEntry	*RTE;
	NetTableEntry	*NTE;


    CTEGetLock(&RouteTableLock, &TableHandle);
	
	for (NTE = NetTableList; NTE != NULL; NTE = NTE->nte_next)
		if ((NTE->nte_flags & NTE_VALID) &&
			IP_ADDR_EQUAL(RCE->rce_src, NTE->nte_addr))
			break;

	if (NTE == NULL) {
		// Didn't find a match.
		CTEFreeLock(&RouteTableLock, TableHandle);
		return FALSE;
	}

	RTE = LookupRTE(RCE->rce_dest, RCE->rce_src, HOST_ROUTE_PRI);
	
	// See if we found an RTE.
    if (RTE != NULL) {
		
		// Yep, we found one. Get the lock on the RCE, and make sure he's
		// not pointing at an RTE already.
        CTEGetLock(&RCE->rce_lock, &RCEHandle);
		if (RCE->rce_usecnt == 0) {
			// Nobody is using him, so we can link him up.
			if (!RCE->rce_valid) {
				Interface			*IF;
				// He's not valid. Invalidate the lower layer info, just in
				// case.
				IF = (Interface *)RCE->rce_rte;
				(*(IF->if_invalidate))(IF->if_lcontext, RCE);
	            RCE->rce_rte = RTE;
				RCE->rce_valid = TRUE;
	            RCE->rce_next = RTE->rte_rcelist;
	            RTE->rte_rcelist = RCE;
        	}
		}

		// Free the locks and we're done.
        CTEFreeLock(&RCE->rce_lock, RCEHandle);
        CTEFreeLock(&RouteTableLock, TableHandle);
        return TRUE;
    } else {
        // No route! Fail the call.
        CTEFreeLock(&RouteTableLock, TableHandle);
        return FALSE;
    }

}
//** IPGetPInfo - Get information..
//
//	Called by an upper layer to get information about a path. We return the
//	MTU of the path and the maximum link speed to be expected on the path.
//
//	Input:	Dest		- Destination address.
//			Src			- Src address.
//			NewMTU		- Where to store path MTU (may be NULL).
//			MaxPathSpeed - Where to store maximum path speed (may be NULL).
//
//	Returns: Status of attempt to get new MTU.
//
IP_STATUS		
IPGetPInfo(IPAddr Dest, IPAddr Src, uint *NewMTU, uint *MaxPathSpeed)
{
	CTELockHandle		Handle;
	RouteTableEntry		*RTE;
	IP_STATUS			Status;
	
	CTEGetLock(&RouteTableLock, &Handle);
   	RTE = LookupRTE(Dest, Src, HOST_ROUTE_PRI);
	if (RTE != NULL) {
		if (NewMTU != NULL)
			*NewMTU = RTE->rte_mtu;
		if (MaxPathSpeed != NULL)
			*MaxPathSpeed = RTE->rte_if->if_speed;
		Status = IP_SUCCESS;
	} else
		Status = IP_DEST_HOST_UNREACHABLE;
	
	CTEFreeLock(&RouteTableLock, Handle);
	return Status;
	
}

//** IPCheckRoute - Check that a route is valid.
//
//	Called by an upper layer when it believes a route might be invalid.
//	We'll check if we can. If the upper layer is getting there through a
//	route derived via ICMP (presumably a redirect) we'll check to see
//	if it's been learned within the last minute. If it has, it's assumed
//	to still be valid. Otherwise, we'll mark it as down and try to find
//	another route there. If we can, we'll delete the old route. Otherwise
//	we'll leave it. If the route is through a default gateway we'll switch
//	to another one if we can. Otherwise, we'll just leave - we don't mess
//	with manually configured routes.
//
//	Input:	Dest			- Destination to be reached.
//			Src				- Src we're sending from.
//	
//	Returns: Nothing.
//
void
IPCheckRoute(IPAddr Dest, IPAddr Src)
{
	RouteTableEntry			*RTE;
	RouteTableEntry			*NewRTE;
	RouteTableEntry			*TempRTE;
	CTELockHandle			Handle;
	uint					Now = CTESystemUpTime() / 1000L;
	
	if (DeadGWDetect) {
		// We are doing dead G/W detection. Get the lock, and try and
		// find the route.
		CTEGetLock(&RouteTableLock, &Handle);
    	RTE = LookupRTE(Dest, Src, HOST_ROUTE_PRI);
		if (RTE != NULL && ((Now - RTE->rte_valid) > MIN_RT_VALID)) {
			
			// Found a route, and it's older than the minimum valid time. If it
			// goes through a G/W, and is a route we learned via ICMP or is a
			// default route, do something with it.
			if (!IP_ADDR_EQUAL(RTE->rte_addr, IPADDR_LOCAL)) {
				// It's not through a G/W.
				
				if (RTE->rte_proto == IRE_PROTO_ICMP) {
				
					// Came from ICMP. Mark as invalid, and then make sure
					// we have another route there.
					RTE->rte_flags &= ~RTE_VALID;
    				NewRTE = LookupRTE(Dest, Src, HOST_ROUTE_PRI);
					
					if (NewRTE == NULL) {
						// Can't get there any other way so leave this
						// one alone.
						RTE->rte_flags |= RTE_VALID;
					} else {
						// There is another route, so destroy this one. Use
						// FindSpecificRTE to find the previous RTE.
						TempRTE = FindSpecificRTE(RTE->rte_dest, RTE->rte_mask,
							RTE->rte_addr, RTE->rte_if, &NewRTE);
						CTEAssert(TempRTE == RTE);
						DeleteRTE(NewRTE, TempRTE);
					}
				} else {
					if (RTE->rte_mask == DEFAULT_MASK) {
						
						// This is a default gateway. If we have more than one
						// configured move to the next one.
						
						if (DefGWConfigured > 1) {
							// Have more than one. Try the next one. First
							// invalidate any RCEs on this G/W.
							
							InvalidateRCEChain(RTE);
							if (DefGWActive == 1) {
								// No more active. Revalidate all of them,
								// and try again.
								ValidateDefaultGWs(NULL_IP_ADDR);
								CTEAssert(DefGWActive == DefGWConfigured);
							} else {
								// More than one active, so invalidate this
								// one, and move to the next one. Stamp the
								// next one with a valid time of Now, so we
								// don't move from him too easily.
								--DefGWActive;
								RTE->rte_flags &= ~RTE_VALID;
								RTE = FindRTE(Dest, Src, IPHash(0),
									DEFAULT_ROUTE_PRI, DEFAULT_ROUTE_PRI);
								if (RTE == NULL) {
									// No more default gateways! This is bad.
									CTEAssert(FALSE);
									ValidateDefaultGWs(NULL_IP_ADDR);
									CTEAssert(DefGWActive == DefGWConfigured);
								} else {
									CTEAssert(RTE->rte_mask == DEFAULT_MASK);
									RTE->rte_valid = Now;
								}
							}
						}
					}
				}
			}
		}
		CTEFreeLock(&RouteTableLock, Handle);
	}	
}						
						
					
//** FindRCE - Find an RCE on an RTE.
//
//  A routine to find an RCE that's chained on an RTE. We assume the lock
//  is held on the RTE.
//
//  Entry:  RTE             - RTE to search.
//          Dest            - Destination address of RTE to find.
//          Src             - Source address of RTE to find.
//
//  Returns: Pointer to RTE found, or NULL.
//
RouteCacheEntry *
FindRCE(RouteTableEntry *RTE, IPAddr Dest, IPAddr Src)
{
    RouteCacheEntry     *CurrentRCE;

    for (CurrentRCE = RTE->rte_rcelist; CurrentRCE != NULL;
        CurrentRCE = CurrentRCE->rce_next)
        if (IP_ADDR_EQUAL(CurrentRCE->rce_dest, Dest))
            if (IP_ADDR_EQUAL(Src, NULL_IP_ADDR) ||
                IP_ADDR_EQUAL(CurrentRCE->rce_src, Src))
                break;

    return CurrentRCE;

}

//** OpenRCE - Open an RCE for a specific route.
//
//  Called by the upper layer to open an RCE. We look up the type of the address
//  - if it's invalid, we return 'Destination invalid'. If not, we look up the
//  route, fill in the RCE, and link it on the correct RTE.
//
//  As an added bonus, this routine will return the local address to use
//  to reach the destination.
//
//  Entry:  Address         - Address for which we are to open an RCE.
//          Src             - Source address we'll be using.
//          RCE             - Pointer to where to return pointer to RCE.
//          Type            - Pointer to where to return destination type.
//          MSS             - Pointer to where to return MSS for route.
//          OptInfo         - Pointer to option information, such as TOS and
//                              any source routing info.
//
//  Returns: Source IP address to use. This will be NULL_IP_ADDR if the
//          specified destination is unreachable for any reason.
//
IPAddr
OpenRCE(IPAddr Address, IPAddr Src, RouteCacheEntry **RCE, uchar *Type,
    ushort *MSS, IPOptInfo *OptInfo)
{
    RouteTableEntry     *RTE;           // Pointer to RTE to put RCE on.
    CTELockHandle       TableLock;
    uchar               LocalType;


    if (!IP_ADDR_EQUAL(OptInfo->ioi_addr, NULL_IP_ADDR))
        Address = OptInfo->ioi_addr;

	CTEGetLock(&RouteTableLock, &TableLock);

	// Make sure we're not in DHCP update.
	if (DHCPActivityCount != 0) {
		// We are updating DHCP. Just fail this now, since we're in an
		// indeterminate state.
		CTEFreeLock(&RouteTableLock, TableLock);
		return NULL_IP_ADDR;
	}

    LocalType = GetAddrType(Address);

    *Type = LocalType;

    // If the specified address isn't invalid, continue.
    if (LocalType != DEST_INVALID) {
        RouteCacheEntry     *NewRCE;

		// If he's specified a source address, loop through the NTE table
		// now and make sure it's valid.
		if (!IP_ADDR_EQUAL(Src, NULL_IP_ADDR)) {
			NetTableEntry	*NTE;

			for (NTE = NetTableList; NTE != NULL; NTE = NTE->nte_next)
				if ((NTE->nte_flags & NTE_VALID) &&
					IP_ADDR_EQUAL(Src, NTE->nte_addr))
					break;

			if (NTE == NULL) {
				// Didn't find a match.
				CTEFreeLock(&RouteTableLock, TableLock);
				return NULL_IP_ADDR;
			}
		}

        // Find the route for this guy. If we can't find one, return NULL.
		RTE = LookupRTE(Address, Src, HOST_ROUTE_PRI);

        if (RTE != (RouteTableEntry *)NULL) {
            IPAddr          NewSrc;
            CTELockHandle   RCEHandle;
            RouteCacheEntry *OldRCE;
			NetTableEntry	*SrcNTE;

			// We found one.
            *MSS = (ushort)RTE->rte_mtu;			// Return the route MTU.
			
			if (IP_LOOPBACK_ADDR(Src) && (RTE->rte_if != &LoopInterface)) {
				// The upper layer is sending from a loopback address, but the
				// destination isn't reachable through the loopback interface.
				// Fail the request.
				CTEFreeLock(&RouteTableLock, TableLock);
				return NULL_IP_ADDR;
			}

            // We have the RTE. Fill in the RCE, and link it on the RTE.
            if (!IP_ADDR_EQUAL(RTE->rte_addr, IPADDR_LOCAL))
                *Type |= DEST_OFFNET_BIT;   // Tell upper layer it's off
                                            // net.

            // First, see if an RCE already exists for this.
            if ((OldRCE = FindRCE(RTE, Address, Src)) == NULL) {

                // If the destination is local to this host (i.e.
                // loopback), we'll use the destination address as the
                // source.
                if (LocalType == DEST_LOCAL)
                    NewSrc = Address;
                else {
                    SrcNTE = BestNTEForIF(Address, RTE->rte_if);
					if (SrcNTE == NULL) {
						// Can't find an address! Fail the request.
						CTEFreeLock(&RouteTableLock, TableLock);
						return NULL_IP_ADDR;
					}
					NewSrc = SrcNTE->nte_addr;
				}

                // Don't have an existing RCE. See if we can get a new one,
                // and fill it in.

		        NewRCE = CTEAllocMem(sizeof(RouteCacheEntry));
		        *RCE = NewRCE;

                if (NewRCE != NULL) {
                    CTEMemSet(NewRCE, 0, sizeof(RouteCacheEntry));

                    // Save the source address for this one. We use the
                    // caller specified address if provided, otherwise we
                    // use the best local address.
                    if (IP_ADDR_EQUAL(Src, NULL_IP_ADDR))
                        NewRCE->rce_src = NewSrc;
                    else
                        NewRCE->rce_src = Src;

                    NewRCE->rce_dtype = LocalType;
                    NewRCE->rce_cnt = 1;
                    CTEInitLock(&NewRCE->rce_lock);
                    NewRCE->rce_dest = Address;
                    NewRCE->rce_rte = RTE;
					NewRCE->rce_valid = TRUE;
                    NewRCE->rce_next = RTE->rte_rcelist;
                    RTE->rte_rcelist = NewRCE;
                }

                CTEFreeLock(&RouteTableLock, TableLock);
                return NewSrc;
            } else {
                // We have an existing RCE. We'll return his source as the
                // valid source, bump the reference count, free the locks
                // and return.
                CTEGetLock(&OldRCE->rce_lock, &RCEHandle);
                NewSrc = OldRCE->rce_src;
                OldRCE->rce_cnt++;
                *RCE = OldRCE;
                CTEFreeLock(&OldRCE->rce_lock, RCEHandle);
                CTEFreeLock(&RouteTableLock, TableLock);
                return NewSrc;
            }
        } else {
            CTEFreeLock(&RouteTableLock, TableLock);
            return NULL_IP_ADDR;
        }
    }

	CTEFreeLock(&RouteTableLock, TableLock);
    return NULL_IP_ADDR;
}

//* CloseRCE - Close an RCE.
//
//  Called by the upper layer when it wants to close the RCE. We unlink it from
//  the RTE.
//
//  Entry:  RCE     - Pointer to the RCE to be closed.
//
//  Exit: Nothing.
//
void
CloseRCE(RouteCacheEntry *RCE)
{
    RouteTableEntry     *RTE;               // Route on which RCE is linked.
    RouteCacheEntry     *PrevRCE;
    CTELockHandle       TableLock; 			// Lock handles used.
    CTELockHandle       RCEHandle;
    Interface           *IF;

    if (RCE != NULL) {
        CTEGetLock(&RouteTableLock, &TableLock);
        CTEGetLock(&RCE->rce_lock, &RCEHandle);

        if (--RCE->rce_cnt == 0) {
			CTEAssert(RCE->rce_usecnt == 0);
        	if (RCE->rce_valid) {
            	// The RCE is valid, so we have a valid RTE in the pointer
            	// field. Walk down the RTE rcelist, looking for this guy.
			
				RTE = RCE->rce_rte;
				IF = RTE->rte_if;

            	PrevRCE = STRUCT_OF(RouteCacheEntry, &RTE->rte_rcelist,
            		rce_next);

            	// Walk down the list until we find him.
            	while (PrevRCE != NULL) {
                	if (PrevRCE->rce_next == RCE)
                    	break;
                	PrevRCE = PrevRCE->rce_next;
            	}

				CTEAssert(PrevRCE != NULL);
            	PrevRCE->rce_next = RCE->rce_next;
			} else
				IF = (Interface *)RCE->rce_rte;
				
            (*(IF->if_invalidate))(IF->if_lcontext, RCE);
            CTEFreeLock(&RCE->rce_lock, RCEHandle);
            CTEFreeMem(RCE);
        }
		else {
            CTEFreeLock(&RCE->rce_lock, RCEHandle);
        }

        CTEFreeLock(&RouteTableLock, TableLock);

    }

}


//* LockedAddRoute - Add a route to the routing table.
//
//  Called by AddRoute to add a route to the routing table. We assume the
//	route table lock is already held. If the route to be added already exists
//  we update it. Routes are identified by a (Destination, Mask, FirstHop,
//	Interface) tuple. If an exact match exists we'll update the metric, which
//	may cause us to promote RCEs from other RTEs, or we may be demoted in which
//	case we'll invalidate our RCEs and let them be reassigned at transmission
//	time.
//
//	If we have to create a new RTE we'll do so, and find the best previous
//	RTE, and promote RCEs from that one to the new one.
//
//	The route table is an open hash structure. Within each hash chain the
//	RTEs with the longest masks (the 'priority') come first, and within
//	each priority the RTEs with the smallest metric come first.
//
//
//  Entry:  Destination     - Destination address for which route is being
//								added.
//			Mask			- Mask for destination.
//			FirstHop		- First hop for address. Could be IPADDR_LOCAL.
//			OutIF			- Pointer to outgoing I/F.
//			MTU				- Maximum MTU for this route.
//			Metric			- Metric for this route.
//			Proto			- Protocol type to store in route.
//			AType			- Administrative type of route.
//
//  Returns: Status of attempt to add route.
//
IP_STATUS
LockedAddRoute(IPAddr Destination, IPMask Mask, IPAddr FirstHop,
	Interface *OutIF, uint MTU, uint Metric, uint Proto, uint AType)
{
	uint				RouteType;			// SNMP route type.
	RouteTableEntry		*NewRTE, *OldRTE;	// Entries for new and previous
											// RTEs.
	RouteTableEntry		*PrevRTE;			// Pointer to previous RTE.
	CTELockHandle		RCEHandle;			// Lock handle for RCEs.
	uint				OldMetric;			// Previous metric in use.
	uint				OldPriority;		// Priority of previous route to
											// destination.
	RouteCacheEntry		*CurrentRCE;		// Current RCE being examined.
	RouteCacheEntry		*PrevRCE;			// Previous RCE examined.
	Interface			*IF;				// Interface being added on.
	uint				Priority;			// Priority of the route.
	uint				TempMask;			// Temporary copy of the mask.
	uint				Now = CTESystemUpTime() / 1000L;	// System up time,
											// in seconds.
	uint				MoveAny;			// TRUE if we'll move any RCE.
	ushort				OldFlags;

	// First do some consistency checks. Make sure that the Mask and
	// Destination agree.
	if (!IP_ADDR_EQUAL(Destination & Mask, Destination))
		return IP_BAD_DESTINATION;
	
	if (AType != ATYPE_PERM && AType != ATYPE_OVERRIDE && AType != ATYPE_TEMP)
		return IP_BAD_REQ;

	RouteType = IP_ADDR_EQUAL(FirstHop, IPADDR_LOCAL) ? IRE_TYPE_DIRECT :
		IRE_TYPE_INDIRECT;

	
	// If the outgoing interface has NTEs attached but none are valid, fail
	// this request unless it's a request to add the broadcast route.
	if (OutIF->if_ntecount == 0 && OutIF->if_nte != NULL &&
		!IP_ADDR_EQUAL(Destination, OutIF->if_bcast) ) {
		// This interface has NTEs attached, but none are valid. Fail the
		// request.
		return IP_BAD_REQ;
	}
		

	// First look and see if the RTE already exists.
	NewRTE = FindSpecificRTE(Destination, Mask, FirstHop, OutIF, &PrevRTE);

	if (NewRTE != NULL) {
		
		// The RTE already exists, and we have a lock on it. See if we're
		// updating the metric. If we're not, just return. Otherwise
		// we'll remove the RTE and update the metric. If the metric is
		// increasing, walk down the RCE chain on the RTE and invalidate
		// the RCE back pointers so that they'll be revalidated upon
		// transmits, and then reinsert the RTE. If the metric is
		// decreasing we'll just fall through to the case below where we'll
		// promote RCEs and insert the RTE.


		// Update the things that won't cause routing table motion.
		NewRTE->rte_mtu = MTU;
		NewRTE->rte_proto = Proto;
		NewRTE->rte_valid = Now;
		NewRTE->rte_mtuchange = Now;
		OldFlags = NewRTE->rte_flags;
		
		// We always turn off the increase flag when a route is updated, so
		// that we take the long timeout to try to increase it. We also
		// always turn on the valid flag.
		NewRTE->rte_flags = (OldFlags & ~RTE_INCREASE) | RTE_VALID;

		// Need to update the metric, which will cause this RTE to move
		// in the table.
		OldMetric = NewRTE->rte_metric;			// Save the old one.
		RemoveRTE(PrevRTE, NewRTE);				// Pull him from the chain.
		NewRTE->rte_metric = Metric;

		// Now see if we're increasing or decreasing the metric, or
		// re-validating a previously invalid route. By definition, if the
		// route wasn't valid before this there can't have been any RCEs
		// attached to it, so we'll fall through to the promotion code
		// and see if we can move any onto it. Otherwise, if we're
		// demoting this route invalidate any RCEs on it.
		
		if (Metric >= OldMetric && (OldFlags & RTE_VALID)) {
			// We're increasing it, or leaving it the same. His valid state
			// may have changed, so we'll we'll invalidate any RCEs on him
			// now in any case.
			InsertRTE(NewRTE);
			
			InvalidateRCEChain(NewRTE);
			
			return IP_SUCCESS;
		}

		// The metric is less than the old metric, so we're promoting this
		// RTE. Fall through to the code below that deals with this, after
		// saving the priority of the RTE.
		Priority = NewRTE->rte_priority;

	} else {
		
		// Didn't find a matching RTE. Allocate a new one, and fill it in.
		NewRTE = CTEAllocMem(sizeof(RouteTableEntry));
		
		if (NewRTE == NULL) {
			// Couldn't get the memory.
			return IP_NO_RESOURCES;
		}

		IPSInfo.ipsi_numroutes++;

		// Fill him in, and take the lock on him. To do this we'll need to
		// calculate the priority.
		Priority = 0;
		TempMask = Mask;
		
		while (TempMask) {
			Priority += TempMask & 1;
			TempMask >>= 1;
		}

		// Now initialize all of his fields.
		NewRTE->rte_priority = Priority;

		NewRTE->rte_dest = Destination;
		NewRTE->rte_mask = Mask;
		if (Mask == DEFAULT_MASK) {
			// We're adding a default route.
			DefGWConfigured++;
			DefGWActive++;
		}
		NewRTE->rte_addr = FirstHop;
		NewRTE->rte_metric = Metric;
		NewRTE->rte_mtu = MTU;
		NewRTE->rte_if = OutIF;
		NewRTE->rte_rcelist = NULL;
		NewRTE->rte_type = (ushort)RouteType;
		NewRTE->rte_flags = RTE_VALID;
		NewRTE->rte_proto = Proto;
		NewRTE->rte_valid = Now;
		NewRTE->rte_mtuchange = Now;
		NewRTE->rte_admintype = AType;
	}

	// At this point, NewRTE points to an initialized RTE that is not in the
	// table. We hold the lock on NewRTE and on the RouteTable. First we'll
	// find where we'll eventually insert the new RTE (we can't insert it
	// yet, because we'll still need to search the table again and we can't
	// do that while we hold a lock on an element in the table). Then we'll
	// search the table for the next best route (i.e. a route with a priority
	// less than or equal to ours), and if we find one we'll promote RCEs from
	// that route to us. We'll actually have to search a chain of RTEs.
	
	PrevRTE = FindInsertPoint(NewRTE);
	OldRTE = LookupRTE(Destination, NULL_IP_ADDR, Priority);

	// If we found one, try to promote from him.
	if (OldRTE != NULL) {
		
		// Found another RTE, and we have the lock on it. Starting with him,
		// walk down the chain. At the start of this loop we know that the
		// route described by OldRTE can reach our destination. If his metric
		// is better than ours, we're done and we'll just insert our route.
		// If his priority and metric are equal to ours we'll promote only
		// those RCEs that exactly match our source address. Otherwise either
		// his priority or metric is worse than ours, and we'll promote all
		// appropriate RCEs.
		//
		// Since we specified the maximum priority in the call to LookupRTE
		// as our priority, we know the priority of the route we found
		// can't be greater than our priority.
		
		OldMetric = OldRTE->rte_metric;
		OldPriority = OldRTE->rte_priority;

		// We'll do the search if his priority is less than ours, or his
		// metric is > (i.e. worse than) our metric, or his metric equals
		// our metric and the old route is on a different interface. We
		// know OldPriority is <= our Priority, since we specified our
		// priority in the call to LookupRTE above.
		
		CTEAssert(OldPriority <= Priority);
		
		if (OldPriority < Priority || OldMetric > Metric ||
			(OldMetric == Metric && OldRTE->rte_if != OutIF)) {
			
			// We're going to search. Figure out the mask to use in comparing
			// source addresses.
			if (OldPriority == Priority && OldMetric == Metric)
				MoveAny = FALSE;		// Only promote an exact match.
			else
				MoveAny = TRUE;			// Promote any match.

			for (;;) {
				IF = OldRTE->rte_if;

				PrevRCE = STRUCT_OF(RouteCacheEntry, &OldRTE->rte_rcelist,
					rce_next);
				CurrentRCE = PrevRCE->rce_next;
				// Walk the list, promoting any that match.
				while (CurrentRCE != NULL) {
					CTEGetLock(&CurrentRCE->rce_lock, &RCEHandle);
					
					// If the masked source address matches our masked address,
					// and the destinations match, promote him.
					if ((MoveAny || AddrOnIF(OutIF, CurrentRCE->rce_src)) &&
						IP_ADDR_EQUAL(CurrentRCE->rce_dest & Mask, Destination)) {
						// He matches. Pull him from the list, update him, and
						// put him on the NewRTE.
						CTEAssert(CurrentRCE->rce_valid);
						PrevRCE->rce_next = CurrentRCE->rce_next;
						if (CurrentRCE->rce_usecnt == 0) {
							// Noone's currently using him, so move him.
							CurrentRCE->rce_next = NewRTE->rte_rcelist;
							NewRTE->rte_rcelist = CurrentRCE;
							CurrentRCE->rce_rte = NewRTE;
                			(*(IF->if_invalidate))(IF->if_lcontext, CurrentRCE);
						} else {
							CurrentRCE->rce_valid = FALSE;
							CurrentRCE->rce_rte = (RouteTableEntry *)IF;
						}
						
					} else {
						// Doesn't match. Try the next one.
						PrevRCE = CurrentRCE;
					}
					CTEFreeLock(&CurrentRCE->rce_lock, RCEHandle);
					CurrentRCE = PrevRCE->rce_next;
				}

				// We've walked the RCE list on that old RTE. Look at the
				// next one. If it has the same priority and metric as the
				// old one, and also matches our destination, check it also.
				OldRTE = OldRTE->rte_next;
				if (OldRTE != NULL) {
					// We have another one. Check it out.
					if (OldRTE->rte_priority == Priority &&
						OldRTE->rte_metric == OldMetric &&
						IP_ADDR_EQUAL(Destination & OldRTE->rte_mask,
							OldRTE->rte_dest))
						continue;			// It matches, so try to promote
											// RCEs.
				}
				break;	// Exit out of the for (;;) loop.
			}
		} else {
			// OldRTE is a better route than the one we're inserting, so don't
			// do anything.
		}
	}

	// At this point we're promoted any routes we need to, we hold the lock
	// on NewRTE which still isn't inserted, and PrevRTE describes where to
	// insert NewRTE. Insert it, free the lock, and return success.
	InsertAfterRTE(PrevRTE, NewRTE);
	return IP_SUCCESS;

}

//* AddRoute - Add a route to the routing table.
//
//	This is just a shell for the real add route routine. All we do is take
//	the route table lock, and call the LockedAddRoute routine to deal with
//	the request. This is done this way because there are certain routines that
//	need to be able to atomically examine and add routes.
//
//  Entry:  Destination     - Destination address for which route is being
//								added.
//			Mask			- Mask for destination.
//			FirstHop		- First hop for address. Could be IPADDR_LOCAL.
//			OutIF			- Pointer to outgoing I/F.
//			MTU				- Maximum MTU for this route.
//			Metric			- Metric for this route.
//			Proto			- Protocol type to store in route.
//			AType			- Administrative type of route.
//
//  Returns: Status of attempt to add route.
//
IP_STATUS
AddRoute(IPAddr Destination, IPMask Mask, IPAddr FirstHop,
	Interface *OutIF, uint MTU, uint Metric, uint Proto, uint AType)
{
	CTELockHandle			TableHandle;
	IP_STATUS				Status;
	
	CTEGetLock(&RouteTableLock, &TableHandle);
	Status = LockedAddRoute(Destination, Mask, FirstHop, OutIF, MTU, Metric,
		Proto, AType);
	
	CTEFreeLock(&RouteTableLock, TableHandle);
	return Status;
}

//* DeleteRoute - Delete a route from the routing table.
//
//  Called by upper layer or management code to delete a route from the routing
//	table. If we can't find the route we return an error. If we do find it, we
//	remove it, and invalidate any RCEs associated with it. These RCEs will be
//	reassigned the next time they're used. A route is uniquely identified by
//	a (Destination, Mask, FirstHop, Interface) tuple.
//
//  Entry:  Destination     - Destination address for which route is being
//								deleted.
//			Mask			- Mask for destination.
//          FirstHop        - First hop on way to Destination. -1 means route is
//								local.
//			OutIF			- Outgoing interface for route.
//
//  Returns: Status of attempt to delete route.
//
IP_STATUS
DeleteRoute(IPAddr Destination, IPMask Mask, IPAddr FirstHop, Interface *OutIF)
{
    RouteTableEntry *RTE;                   // RTE being deleted.
    RouteTableEntry *PrevRTE;               // Pointer to RTE in front of one
    										// being deleted.
    CTELockHandle   TableLock;              // Lock handle for table.


    // Look up the route by calling FindSpecificRTE. If we can't find it,
	// fail the call.
    CTEGetLock(&RouteTableLock, &TableLock);
	RTE = FindSpecificRTE(Destination, Mask, FirstHop, OutIF, &PrevRTE);

	if (RTE == NULL) {
		// Didn't find the route, so fail the call.
		CTEFreeLock(&RouteTableLock, TableLock);
		return IP_BAD_ROUTE;
	}

#ifndef NT
//
// Disable admin check for NT, because the RAS server needs to be able to
// delete a subnet route. This ability is restricted to Administrators only
// by NT security checks.
//
//
	if (RTE->rte_admintype == ATYPE_PERM) {
		// Can't delete a permanent route.
		CTEFreeLock(&RouteTableLock, TableLock);
		return IP_BAD_REQ;
	}
#endif
	
	// When we reach here we hold the lock on the RTE to be deleted, and
	// PrevRTE points to the RTE immediately ahead of ours in the table.
	// Call DeleteRTE to delete him.
	DeleteRTE(PrevRTE, RTE);

    CTEFreeLock(&RouteTableLock, TableLock);
    return IP_SUCCESS;


}

// Redirect() removed.  ICMP redirect reception is not honoured on
// MicroNT (H-009 INVALID).  ICMP.C drops type-5 packets at dispatch
// without calling into the route table.  The original handler
// would add a host route via the new first-hop, which on a hostile
// L2 lets a peer redirect any destination through an attacker-
// controlled gateway with no authentication beyond a source IP match.

//* GetRaisedMTU - Get the next largest MTU in table..
//
//	A utility function to search the MTU table for a larger value.
//
//	Input:	PrevMTU				- MTU we're currently using. We want the
//									next largest one.
//
//	Returns: New MTU size.
//
uint
GetRaisedMTU(uint PrevMTU)
{
	uint			i;
	
	for (i = (sizeof(MTUTable)/sizeof(uint)) - 1; i != 0; i--) {
		if (MTUTable[i] > PrevMTU)
			break;
	}
	
	return MTUTable[i];
}

//* GuessNewMTU - Guess a new MTU, giving a DG size too big.
//
//	A utility function to search the MTU table. As input we take in an MTU
//	size we believe to be too large, and search the table looking for the
//	next smallest one.
//
//	Input:	TooBig				- Size that's too big.
//
//	Returns: New MTU size.
//
uint
GuessNewMTU(uint TooBig)
{
	uint			i;
	
	for (i = 0; i < ((sizeof(MTUTable)/sizeof(uint)) - 1); i++)
		if (MTUTable[i] < TooBig)
			break;
	
	return MTUTable[i];
}

//*	RouteFragNeeded - Handle being told we need to fragment.
//
//	Called when we receive some external indication that we need to fragment
//	along a particular path. If we're doing MTU discovery we'll try to
//	update the route, if we can. We'll also notify the upper layers about
//	the new MTU.
//
//	Input:	IPH				- Pointer to IP Header of datagram needing
//								fragmentation.
//			NewMTU			- New MTU to be used (may be 0).
//
//	Returns: Nothing.
//
void
RouteFragNeeded(IPHeader UNALIGNED *IPH, ushort NewMTU)
{
	uint			OldMTU;
	CTELockHandle	Handle;
	RouteTableEntry	*RTE;
	ushort			HeaderLength;	
	
	// If we're not doing PMTU discovery, don't do anything.
	if (PMTUDiscovery) {
		
		// We're doing PMTU discovery. Correct the given new MTU for the IP
		// header size, which we don't save as we track MTUs.
		if (NewMTU != 0)
			NewMTU -= sizeof(IPHeader);
			
		HeaderLength = (IPH->iph_verlen & (uchar)~IP_VER_FLAG) << 2;
		
		// Get the current routing information.

		CTEGetLock(&RouteTableLock, &Handle);
	
		// Find an RTE for the destination.
    	RTE = LookupRTE(IPH->iph_dest, IPH->iph_src, HOST_ROUTE_PRI);
		
		// If we couldn't find one, or the existing MTU is less than the new
		// MTU, give up now.
		
		if (RTE == NULL || (OldMTU = RTE->rte_mtu) < NewMTU) {
			// No RTE, or an invalid new MTU. Just bail out now.
			CTEFreeLock(&RouteTableLock, Handle);
			return;
		}
		
		// If the new MTU is zero, figure out what the new MTU should be.
		if (NewMTU == 0) {
			ushort			DGLength;
			
			// The new MTU is zero. We'll make a best guess what the new
			// MTU should be. We have the RTE for this route already.
			
			
			// Get the length of the datagram that triggered this. Since we'll
			// be comparing it against MTU values that we track without the
			// IP header size included, subtract off that amount.
			DGLength = (ushort)net_short(IPH->iph_length) - sizeof(IPHeader);
			
			// We may need to correct this as per RFC 1191 for dealing with
			// old style routers.
			if (DGLength >= OldMTU) {
				// The length of the datagram sent is not less than our
				// current MTU estimate, so we need to back it down (assuming
				// that the sending route has incorrectly added in the header
				// length).
				DGLength -= HeaderLength;
				
			}
			
			// If it's still larger than our current MTU, use the current
			// MTU. This could happen if the upper layer sends a burst of
			// packets which generate a sequence of ICMP discard messages. The
			// first one we receive will cause us to lower our MTU. We then
			// want to discard subsequent messages to avoid lowering it
			// too much. This could conceivably be a problem if our
			// first adjustment still results in an MTU that's too big,
			// but we should converge adequately fast anyway, and it's
			// better than accidentally underestimating the MTU.
			
			if (DGLength > OldMTU)
				NewMTU = OldMTU;
			else
				// Move down the table to the next lowest MTU.
				NewMTU = GuessNewMTU(DGLength);
		}
		
		// We have the new MTU. Now add it to the table as a host route.
		if (NewMTU != OldMTU)
			LockedAddRoute(IPH->iph_dest, HOST_MASK, RTE->rte_addr, RTE->rte_if,
				NewMTU, RTE->rte_metric, IRE_PROTO_ICMP, ATYPE_OVERRIDE);
			
		CTEFreeLock(&RouteTableLock, Handle);
		
		// We've added the route. Now notify the upper layers of the change.
		ULMTUNotify(IPH->iph_dest, IPH->iph_src, IPH->iph_protocol,
			(void *)((uchar *)IPH + HeaderLength), NewMTU);

	}
}

//** IPRouteTimeout - IP routeing timeout handler.
//
//  The IP routeing timeout routine, called once a minute. We look at all
//	host routes, and if we raise the MTU on them we do so.
//
//  Entry:  Timer       - Timer being fired.
//          Context     - Pointer to NTE being time out.
//
//  Returns: Nothing.
//
void
IPRouteTimeout(CTEEvent *Timer, void *Context)
{
	uint			Now = CTESystemUpTime() / 1000L;
	CTELockHandle	Handle;
	uint			i;
	RouteTableEntry	*RTE, *PrevRTE;
	uint			RaiseMTU, Delta;
	Interface		*IF;
	IPAddr			Dest;
	uint			NewMTU;
	NetTableEntry	*NTE;
	
	CTEGetLock(&RouteTableLock, &Handle);
	
	for (i = 0; i < ROUTE_TABLE_SIZE; i++) {
		// Walk down each chain, looking at the host routes. If we're
		// doing PMTU discovery, see if we can raise the MTU.
		PrevRTE = STRUCT_OF(RouteTableEntry, &RouteTable[i], rte_next);
		RTE = RouteTable[i];
		while (RTE != NULL && RTE->rte_mask == HOST_MASK) {
			// Make sure he's valid.
			if (RTE->rte_flags & RTE_VALID) {
				if (PMTUDiscovery) {
					// Check to see if we can raise the MTU on this guy.
					Delta = Now - RTE->rte_mtuchange;
					
					if (RTE->rte_flags & RTE_INCREASE)
						RaiseMTU = (Delta >= MTU_INCREASE_TIME ? 1 : 0);
					else
						RaiseMTU = (Delta >= MTU_DECREASE_TIME ? 1 : 0);
					
					if (RaiseMTU) {
						// We need to raise this MTU. Set his change time to
						// Now, so we don't do this again, and figure out
						// what the new MTU should be.
						RTE->rte_mtuchange = Now;
						IF = RTE->rte_if;
						if (RTE->rte_mtu < IF->if_mtu) {
							
							RTE->rte_flags |= RTE_INCREASE;
							// This is a candidate for change. Figure out
							// what it should be.
							NewMTU = MIN(GetRaisedMTU(RTE->rte_mtu),
									IF->if_mtu);
							RTE->rte_mtu = NewMTU;
							Dest = RTE->rte_dest;
							
							// We have the new MTU. Free the lock, and walk
							// down the NTEs on the I/F. For each NTE,
							// call up to the upper layer and tell him what
							// his new MTU is.
							CTEFreeLock(&RouteTableLock, Handle);
							NTE = IF->if_nte;
							while (NTE != NULL) {
								if (NTE->nte_flags & NTE_VALID) {
									ULMTUNotify(Dest, NTE->nte_addr, 0, NULL,
										MIN(NewMTU, NTE->nte_mss));
								}
								NTE = NTE->nte_ifnext;
							}
							
							// We've notified everyone. Get the lock again,
							// and start from the first element of this
							// chain in case something's changed after we
							// free the lock. We've updated the mtuchange
							// time of this RTE, so we won't hit him again.									
							CTEGetLock(&RouteTableLock, &Handle);
							PrevRTE = STRUCT_OF(RouteTableEntry, &RouteTable[i],
								rte_next);
							RTE = RouteTable[i];
							continue;
						} else
							RTE->rte_flags &= ~RTE_INCREASE;
					}
				}
				// If this route came in via ICMP, and we have no RCEs on it,
				// and it's at least 10 minutes old, delete it.
				if (RTE->rte_proto == IRE_PROTO_ICMP &&
					RTE->rte_rcelist == NULL &&
					(Now - RTE->rte_valid) > MAX_ICMP_ROUTE_VALID) {
						// He needs to be deleted. Call DeleteRTE to do this.
						DeleteRTE(PrevRTE, RTE);
						RTE = PrevRTE->rte_next;
						continue;
				}
			}
			PrevRTE = RTE;
			RTE = RTE->rte_next;
		}
	}
	
	CTEFreeLock(&RouteTableLock, Handle);
	CTEStartTimer(&IPRouteTimer, IP_ROUTE_TIMEOUT, IPRouteTimeout, NULL);

}


//*	AddNTERoutes - Add the routes for an NTE.
//
//	Called during initalization or during DHCP address assignment to add
//	routes. We add routes for the address of the NTE, including routes
//	to the subnet and the address itself.
//
//	Input:	NTE			- NTE for which to add routes.
//
//	Returns: TRUE if they were all added, FALSE if not.
//
uint
AddNTERoutes(NetTableEntry *NTE)
{
	IPMask			Mask, SNMask;
	Interface		*IF;
	CTELockHandle	Handle;
	IPAddr			AllSNBCast;
	IP_STATUS		Status;

	// First, add the route to the address itself. This is a route through
	// the loopback interface.

	if (AddRoute(NTE->nte_addr, HOST_MASK, IPADDR_LOCAL, LoopNTE->nte_if,
		LOOPBACK_MSS, 1, IRE_PROTO_LOCAL, ATYPE_OVERRIDE) != IP_SUCCESS)
		return FALSE;

	Mask = IPNetMask(NTE->nte_addr);
	IF = NTE->nte_if;

	// Now add the route for the all-subnet's broadcast, if one doesn't already
	// exist. There is special case code to handle this in SendIPBCast, so the
	// exact interface we add this on doesn't really matter.

    CTEGetLock(&RouteTableLock, &Handle);
	AllSNBCast = (NTE->nte_addr & Mask) | (IF->if_bcast & ~Mask);
    if (LookupRTE(AllSNBCast, NULL_IP_ADDR, HOST_ROUTE_PRI) == NULL) {
		
		Status = LockedAddRoute(AllSNBCast, HOST_MASK, IPADDR_LOCAL, IF,
			NTE->nte_mss, 1, IRE_PROTO_LOCAL, ATYPE_PERM);
		
	} else
		Status = IP_SUCCESS;

	CTEFreeLock(&RouteTableLock, Handle);
	
	if (Status != IP_SUCCESS)
		return FALSE;
	
	// And finally the route to the subnet.
    SNMask = NTE->nte_mask;
    if (AddRoute(NTE->nte_addr & SNMask, SNMask, IPADDR_LOCAL, NTE->nte_if,
		NTE->nte_mss, 1, IRE_PROTO_LOCAL, ATYPE_PERM) != IP_SUCCESS)
		return FALSE;
	
	return TRUE;
}
	

#ifndef	CHICAGO
#pragma BEGIN_INIT
#endif

uint	BCastMinMTU = 0xffff;

//*	InitNTERouting -  do per NTE route initialization.
//
//	Called when we need to initialize per-NTE routing. For the specified NTE,
//	call AddNTERoutes to  add a route for a net bcast, subnet bcast, and local
//	attached subnet. The net bcast entry is sort of a filler - net and
//	global bcasts are always handled specially. For this reason we specify
//	the FirstInterface when adding the route. Subnet bcasts are assumed to
//	only go out on one interface, so the actual interface to be used is
//	specifed. If two interfaces are on the same subnet the last interface is
//	the one that will be used.
//
//	Input:	NTE			- NTE for which routing is to be initialized.
//			NumGWs		- Number of default gateways to add.
//			GWList		- List of default gateways.
//
//	Returns: TRUE if we succeed, FALSE if we don't.
//
uint
InitNTERouting(NetTableEntry *NTE, uint NumGWs, IPAddr *GWList)
{
	uint		i;
	Interface	*IF;
	
	CTERefillMem();
	if (NTE != LoopNTE) {
		BCastMinMTU = MIN(BCastMinMTU, NTE->nte_mss);
	
		IF = NTE->nte_if;
		AddRoute(IF->if_bcast, HOST_MASK, IPADDR_LOCAL,  FirstIF,
			BCastMinMTU, 1, IRE_PROTO_LOCAL, ATYPE_PERM);   // Route for local
															// bcast.
        if (NTE->nte_flags & NTE_VALID) {
			if (!AddNTERoutes(NTE))
				return FALSE;
		
	        // Now add the default routes that are present on this net. We
			// don't check for errors here, but we should probably
			// log an error.
	        for (i = 0; i < NumGWs;i++) {
				IPAddr		GWAddr;
				
				GWAddr = net_long(GWList[i]);
				
				if (IP_ADDR_EQUAL(GWAddr, NTE->nte_addr)) {
					AddRoute(NULL_IP_ADDR, DEFAULT_MASK,
						IPADDR_LOCAL, NTE->nte_if, NTE->nte_mss, 1,
						IRE_PROTO_LOCAL, ATYPE_OVERRIDE);
				} else
					AddRoute(NULL_IP_ADDR, DEFAULT_MASK,
						net_long(GWList[i]), NTE->nte_if, NTE->nte_mss, 1,
						IRE_PROTO_LOCAL, ATYPE_OVERRIDE);
			}
		}
	}
	return TRUE;
}

#ifdef CHICAGO
#pragma	BEGIN_INIT
#endif

//* InitRouting - Initialize our routing table.
//
//  Called during initialization to initialize the routing table.
//
//  Entry: Nothing.
//
//  Returns: True if we succeeded, False if we didn't.
//
int
InitRouting(IPConfigInfo    *ci)
{
    int             i;

    CTERefillMem();

    CTEInitLock(&RouteTableLock);
	
	DefGWConfigured = 0;
	DefGWActive = 0;

    for (i = 0; i < ROUTE_TABLE_SIZE; i++)
        RouteTable[i] = (RouteTableEntry *)NULL;

    // We've created at least one net. We need to add routing table entries for
    // the global broadcast address, as well as for subnet and net broadcasts,
    // and routing entries for the local subnet. We alse need to add a loopback
    // route for the loopback net. Below, we'll add a host route for ourselves
    // through the loopback net.
    AddRoute(LOOPBACK_ADDR & CLASSA_MASK, CLASSA_MASK, IPADDR_LOCAL,
    	LoopNTE->nte_if, LOOPBACK_MSS, 1, IRE_PROTO_LOCAL, ATYPE_PERM);
    											// Route for loopback.

	CTEInitTimer(&IPRouteTimer);

	CTEStartTimer(&IPRouteTimer, IP_ROUTE_TIMEOUT, IPRouteTimeout, NULL);
    return TRUE;

}

// InitGateway() removed.  Forwarding is gone, so there is nothing to
// initialize.  See decision log entry for H-020.
#pragma END_INIT

