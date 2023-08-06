// CPing.cpp: implementation of the CPing class.
//
//////////////////////////////////////////////////////////////////////

#include <Winsock2.h>
#include <windows.h>
#include <comdef.h>



//
// IP header definition
//
typedef struct iphdr {
	unsigned int	h_len:4;			// length of the header
	unsigned int	version:4;			// Version of IP
	unsigned char	tos;				// Type of service
	unsigned short	total_len;			// total length of packet
	unsigned short	ident;				// unique identifier
	unsigned short	frag_and_flags;		// flags
	unsigned char	ttl;				// time to live value
	unsigned char	proto;				// protocol (TCP, UDP, etc.)
	unsigned short	checksum;			// IP checksum
	unsigned int	sourceIP;			// source IP address
	unsigned int	destIP;				// destination IP address
} IpHeader;

//
// ICMP header definition
//
typedef struct icmphdr {
	BYTE	i_type;						// ICMP packet type
	BYTE	i_code;						// type subcode
	USHORT	i_chksum;					// packet checksum
	USHORT	i_id;						// unique packet ID
	USHORT	i_seq;						// packet sequence number
	ULONG	timestamp;					// timestamp
} IcmpHeader;

//
// ICMP message types
//
#define ICMP_ECHOREPLY	0
#define ICMP_ECHO		8

#define ICMP_MIN		8

//
// Default and maximum packet sizes
//
#define DEFAULT_PACKET_SIZE	32
#define MAX_PACKET_SIZE		1024




static void FillIcmpData( char * icmp_data, int datasize )
{
	IcmpHeader	*	icmphdr;
	char		*	datapart;

	icmphdr = (IcmpHeader *)icmp_data;

	icmphdr->i_type  = ICMP_ECHO;
	icmphdr->i_code  = 0;
	icmphdr->i_id    = (USHORT)GetCurrentProcessId();
	icmphdr->i_chksum= 0;
	icmphdr->i_seq   = 0;

	datapart = icmp_data + sizeof IcmpHeader;

	//
	// Place some junk data into the buffer
	//
	memset( datapart, 'E', datasize - sizeof IcmpHeader );

	return;
}

static USHORT Checksum(USHORT *buffer, int size)
{
	unsigned long	cksum=0;

	while (size > 1)
	{
		cksum += *buffer++;
		size  -= sizeof(USHORT);
	}
	if (size)
		cksum += *(UCHAR *)buffer;
	cksum =  (cksum >> 16) + (cksum & 0xffff);
	cksum += (cksum >> 16);

	return (USHORT)(~cksum);
}

static bool DecodeResponse( char * data, int nbytes, struct sockaddr_in * addr )
{
	IpHeader	*iphdr;
	IcmpHeader	*icmphdr;
	DWORD		 ptime=0;

	unsigned short	iphdrlen;

	iphdr = (IpHeader *)data;
	iphdrlen = iphdr->h_len * 4;

	if ( nbytes < iphdrlen + ICMP_MIN )
		return false;

    //
	// Find the start of the ICMP header in the reply packet
	//
	icmphdr = (IcmpHeader *)(data + iphdrlen);
    
	//
	// Check the packet type to make sure its a ICMP reply
	//
	if ( icmphdr->i_type != ICMP_ECHOREPLY )
		return false;

	// Check to see if this is our packet
	//
	if ( icmphdr->i_id != (USHORT)GetCurrentProcessId() )
		return false;

	return true;
}


bool PingAddress( const _bstr_t & destaddr, int dwTimeoutMS )
{
	int				nb;
	static USHORT	seq_no = 0;
	
	SOCKET	m_sICMP = WSASocket( AF_INET, SOCK_RAW, IPPROTO_ICMP, NULL, 0, WSA_FLAG_OVERLAPPED );
	if ( m_sICMP == INVALID_SOCKET )
		return false;

	// Set the receive and send timeout values
	//
	int timeout = dwTimeoutMS;
	nb = setsockopt( m_sICMP, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof timeout );
	if ( nb == SOCKET_ERROR )
		return false;

	timeout = dwTimeoutMS;
	nb = setsockopt( m_sICMP, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof timeout );
	if ( nb == SOCKET_ERROR )
		return false;

	//
	// Try to resolve the given address
	//
	struct hostent	*	hp = NULL;
	struct sockaddr_in	dest;
	dest.sin_addr.s_addr = inet_addr( destaddr );
	dest.sin_family = AF_INET;

	int datasize = DEFAULT_PACKET_SIZE + sizeof IcmpHeader;
	
	//
	// Allocate the ICMP packet along with a receive buffer.
	//
	char	icmp_data[ MAX_PACKET_SIZE ] = { 0 };
	char	recvbuf[ MAX_PACKET_SIZE ] = { 0 };

	//
	// Initialize the ICMP header
	//
	FillIcmpData( icmp_data, datasize );

	//
	// Loop for the requested number of packets
	//
	do {
		// Fill in some more data in the ICMP header
		//
		((IcmpHeader *)icmp_data)->i_chksum		= 0;
		((IcmpHeader *)icmp_data)->timestamp	= GetTickCount();
		((IcmpHeader *)icmp_data)->i_seq		= seq_no++;
		((IcmpHeader *)icmp_data)->i_chksum		= Checksum( (USHORT *)icmp_data, datasize );

		//
		// Send the ICMP packet to the destination
		//
		nb = sendto( m_sICMP, icmp_data, datasize, 0, (struct sockaddr *)&dest, sizeof dest );
		if ( nb == SOCKET_ERROR )  {
			if ( WSAGetLastError() == WSAETIMEDOUT )
				continue;
			return false;
		}

		//
		// There is data pending. Read the packet.
		//
		struct sockaddr_in	from;
		int					fromlen = sizeof from;
		nb = recvfrom( m_sICMP, recvbuf, MAX_PACKET_SIZE, 0, (struct sockaddr *)&from, &fromlen );
		if ( nb == SOCKET_ERROR )  {
			if ( WSAGetLastError() == WSAETIMEDOUT )  {		
				// timed out
				continue;
			}
			HeapFree (GetProcessHeap(), 0, icmp_data);
			HeapFree (GetProcessHeap(), 0, recvbuf);

			return false;
		}
		bool ok = DecodeResponse( recvbuf, nb, &from );
		return ok;

	} while ( false );

	return false;
}
