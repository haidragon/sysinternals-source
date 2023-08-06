
enum {
    VXDMON_closehandle		= -1,
    VXDMON_getversion		= 0,
    VXDMON_getstats		= 1,
    VXDMON_getzerostats         = 2,
    VXDMON_zerostats            = 3,
    VXDMON_hookservice		= 4,
    VXDMON_unhookservice	= 5,
    VXDMON_getoverhead		= 6,
};

enum {
    VXDMON_ERROR_NOSUCHSERVICE	= 1,
    VXDMON_ERROR_OUTOFMEMORY	= 2,
    VXDMON_ERROR_PAGELOCK	= 3,
    VXDMON_ERROR_NOTFOUND	= 4,
    VXDMON_ERROR_HOOK		= 5,
    VXDMON_ERROR_UNHOOK		= 6,
    VXDMON_ERROR_NOSUCHVXD	= 7,
};


#define CALLER_CNT	8

#pragma pack(1)
struct vxdmon_stats {
    DWORD			Ordinal;
    struct vxmon_stats	*	Next;
    DWORD			Enter;
    DWORD			Exit;
    _int64			Time;
    DWORD			CallerPtr;
    DWORD			Caller[ CALLER_CNT ];
};
#pragma pack()

