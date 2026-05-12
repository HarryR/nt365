/********************************************************************/
/**                     Microsoft LAN Manager                      **/
/**               Copyright(c) Microsoft Corp., 1990-1992          **/
/********************************************************************/
/* :ts=4 */

//** IPRTDEF.H - IP private routing definitions.
//
// This file contains all of the definitions private to the routing
// module.

//*	Route table entry.

struct RouteTableEntry {
	struct	RouteTableEntry	*rte_next;		// Next in hash chain.
	IPAddr					rte_dest;		// Destination of route.
	IPMask					rte_mask;		// Mask to use when examining route.
	IPAddr					rte_addr;		// First hop for this route.
	uint					rte_priority;	// Priority of this route:
											// essentially the number
											// of bits set in mask.
	uint					rte_metric;		// Metric of route. Lower
											// is better.
	uint					rte_mtu;		// MTU for this route.
	struct Interface		*rte_if;		// Outbound interface.
	RouteCacheEntry			*rte_rcelist;
	ushort					rte_type;		// Type of route.
	ushort					rte_flags;		// Flags for route.
	uint					rte_admintype;	// Admin type of route.
	uint					rte_proto;		// How we learned about the
											// route.
	uint					rte_valid;		// Up time (in seconds)	
											// route was last known to be
											// valid.
	uint					rte_mtuchange;	// System up time (in seconds)
											// MTU was changed.
}; /* RouteTableEntry */

#define	ADDR_FROM_RTE(R, A)	(IP_ADDR_EQUAL((R)->rte_addr, IPADDR_LOCAL) ? (A) : \
								(R)->rte_addr)
#define	IF_FROM_RTE(R)		((R)->rte_if)
#define	MTU_FROM_RTE(R)		((R)->rte_mtu)
#define	SRC_FROM_RTE(R)		((R)->rte_src)

#define	RTE_VALID			1
#define	RTE_INCREASE		2			// Set if last MTU change was an
										// increase.

#define	IP_ROUTE_TIMEOUT	60L*1000L	// Route timer fires once a minute.

#define	MTU_INCREASE_TIME	120			// Number of seconds after increase
										// to re-increase.
#define	MTU_DECREASE_TIME	600			// Number of seconds after decrease
										// to re-increase.
										
#define	MAX_ICMP_ROUTE_VALID	600		// Time to timeout an unused ICMP
										// derived route, in seconds.

#define	MIN_RT_VALID		60			// Minimum time a route is assumed 
										// to be valid, in seconds.

#define	HOST_ROUTE_PRI		32
#define	DEFAULT_ROUTE_PRI	0

typedef struct RouteTableEntry RouteTableEntry;

// FWQ / FWContext / RouteSendQ / RouteInterface removed.  These were
// the forwarding-path data structures (per-interface send queues,
// per-packet TD context).  Forwarding is gone (H-020).

extern IPMask  IPMaskTable[];
	
#define	IPNetMask(a)	IPMaskTable[(*(uchar *)&(a)) >> 4]

