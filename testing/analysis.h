#ifndef __ANALYSIS__
#define __ANALYSIS__

#include "common.h"

#define BUFLEN 512
#define DEFAULT_EPSILON 10000

// Discriminate tool used to create logfile
#define CCT_FULL    1
#define LSS_FULL    2
#define CCT_BURST   3
#define LSS_BURST   4

#define CCT_FULL_STRING     "cct"
#define LSS_FULL_STRING     "lss-hcct"
#define CCT_BURST_STRING    "cct-burst"
#define LSS_BURST_STRING    "lss-hcct-burst"

typedef struct hcct_sym_s hcct_sym_t;
struct hcct_sym_s {
	char*	name;	// addr2line or <unknown> or address
	char*	file;	// addr2line or range
	char*	image;	// from /proc/self/maps or program_short_name
};

typedef struct hcct_node_s hcct_node_t;
struct hcct_node_s {
    UINT32          routine_id;
    UINT32          call_site;
    UINT32          counter;
    hcct_sym_t*		routine_sym;
    hcct_sym_t*		call_site_sym;        
    hcct_node_t*    first_child;
    hcct_node_t*    next_sibling;
    hcct_node_t*    parent;
};

typedef struct hcct_pair_s hcct_pair_t;
struct hcct_pair_s {
    hcct_node_t*    node;
    UINT32          id;
};

typedef struct hcct_tree_s hcct_tree_t;
struct hcct_tree_s {
    hcct_node_t*    root;
    char			*short_name;
    char			*program_path;
    UINT32			tid;
    unsigned short  tool;    
    UINT32          nodes;
    UINT32          sampling_interval;
    UINT32          burst_length;
    double          epsilon;
    double          phi;
    UINT64			enter_events; // total number of [sampled] rtn enter events
    UINT64			burst_enter_events; /* total number of rtn enter events
									(0 if exhaustive analysis is performed) */
};

typedef struct hcct_map_s hcct_map_t;
struct hcct_map_s {
	UINT32		start;
	UINT32		end;	
	char*		pathname;
	UINT32		offset; // offset into the file - TODO
	hcct_map_t*	next;
};

#endif
