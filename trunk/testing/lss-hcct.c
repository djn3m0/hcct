#include <stdio.h>
#include <stdlib.h>
#include <asm/unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

#define _GNU_SOURCE
#include <errno.h>
extern char *program_invocation_name;
extern char *program_invocation_short_name;

#include "lss-hcct.h"

#if USE_MALLOC==0
#include "pool.h"
#endif

// swap macro
#define swap_and_decr(a,b) {            \
    lss_hcct_node_t *tmp = queue[a];    \
    queue[a] = queue[b];                \
    queue[b--] = tmp;                   \
}

// monitoring macros
#define IsMonitored(x)      ((x)->additional_info==1)
#define SetMonitored(x)     ((x)->additional_info=1)
#define UnsetMonitored(x)   ((x)->additional_info=0)

// global parameters set by hcct_getenv()
UINT32  epsilon;
double	epsilon_d;
char*   dumpPath;

// TLS
__thread UINT16				stack_idx;
__thread lss_hcct_node_t	*stack[STACK_MAX_DEPTH];
__thread lss_hcct_node_t	*hcct_root;
__thread UINT64				lss_enter_events;
// lazy priority queue
__thread UINT32				min, min_idx, num_queue_items, second_min_idx;
__thread lss_hcct_node_t	**queue;
__thread UINT16				queue_full;
#if USE_MALLOC==0
// custom allocator
__thread pool_t				*hcct_pool;
__thread void				*free_list;
#endif

#if BURSTING
extern UINT32	sampling_interval;
extern UINT32	burst_length;
extern UINT16	burst_on;   // enable or disable bursting

extern __thread UINT64				exhaustive_enter_events;
extern __thread UINT16				aligned;
extern __thread hcct_stack_node_t	shadow_stack[STACK_MAX_DEPTH];
extern __thread UINT16				shadow_stack_idx; // legal values: 0 to shadow_stack_idx-1
#elif PROFILE_TIME==1
extern UINT32						sampling_interval;

extern __thread UINT64				thread_tics;
extern __thread hcct_stack_node_t	shadow_stack[STACK_MAX_DEPTH];
extern __thread int					shadow_stack_idx; // legal values: 0 to shadow_stack_idx-1
#endif

// get parameters from environment variables
static void __attribute__((no_instrument_function)) hcct_getenv() {
	char *value;	
	double d;
    
	value=getenv("EPSILON");
	if (value == NULL || value[0]=='\0') {
	    epsilon=EPSILON;
	    epsilon_d=1.0/EPSILON;
    } else {
		d=strtod(value, NULL);
		if (d<=0 || d>1.0) { // 0 is an error code
            epsilon=EPSILON;
            epsilon_d=1.0/EPSILON;
            printf("[hcct] WARNING: invalid value specified for EPSILON, using default (%f) instead\n", epsilon_d);
        } else {
			epsilon=(UINT32)ceil(1.0/d);
			epsilon_d=d;
		}	
    }
        
    dumpPath=getenv("DUMPPATH");
	if (dumpPath == NULL || dumpPath[0]=='\0')
	    dumpPath=NULL;    
}

#if INLINE_UPD_MIN==0
static void update_min() __attribute__((no_instrument_function))
{

    #if 0 // O(n) - classic minimum search
    int i;
    min=0xFFFFFFFF;
    for (i=0; i<num_queue_items; ++i)
        if (queue[i]->counter < min) {
            min=queue[i]->counter;
            min_idx=i;
        }
    #else

    #if 1 // see paper for details
    int i=min_idx+1; // obviously, min has changed!
    
    for (; i < num_queue_items && queue[i]->counter != min; ++i);
        
    if (i>=num_queue_items) {
        min=0xFFFFFFFF; // actual min in the array is bigger than old min
        for (i=0; i<num_queue_items; ++i) {
            if (queue[i]->counter < min) {
                min=queue[i]->counter;
                min_idx=i;
            }
        }
    } else {
        min=queue[i]->counter;
        min_idx=i;
    }

    #else // another version

    int i=min_idx+1, j=0;
    UINT32 tmp=queue[min_idx]->counter;
    
    for (; i < num_queue_items && queue[i]->counter !=min; ++i) {
        if (queue[i]->counter < tmp) { // keeping track of minimum
            j=i;
            tmp=queue[i]->counter;
        }        
    }
    
    if (i>=num_queue_items) {
        min=queue[min_idx]->counter;
        for (i=0; i<min_idx; ++i) {
            if (queue[i]->counter < min) {
                min=queue[i]->counter;
                min_idx=i;
            }
        }
        if (tmp<min) {
            min=tmp;
            min_idx=j;
        }
    } else { 
        min=queue[i]->counter;
        min_idx=i;
    }
    #endif
    #endif
}
#endif

// to be used only on an unmonitored leaf!
static void __attribute__((no_instrument_function)) prune(lss_hcct_node_t* node) {

    lss_hcct_node_t *p, *c;

    if (node==hcct_root) return;
    p=node->parent;

    if (p->first_child==node) {
        p->first_child=node->next_sibling;
        if (p->first_child==NULL && IsMonitored(p)==0)
            prune(p); // cold node becomes a leaf: can be pruned!        
    } 
    else for (c=p->first_child; c!=NULL; c=c->next_sibling)
            if (c->next_sibling==node) {
                c->next_sibling=node->next_sibling;
                break;
            }
	#if USE_MALLOC==0
    pool_free(node, free_list);
    #else
    free(node);
    #endif
}

#if PROFILE_TIME==0
void hcct_enter(ADDRINT routine_id, ADDRINT call_site) {
#else
void hcct_enter(ADDRINT routine_id, ADDRINT call_site, UINT32 increment) {
#endif
	lss_hcct_node_t *parent=stack[stack_idx++];
	if (parent==NULL) {				
		#if SHOW_MESSAGES==1		
		printf("[hcct] rtn_enter event while HCCT not initialized yet for thread %ld\n", syscall(__NR_gettid));
		#endif
		
		hcct_init();		
		
		parent=stack[stack_idx];
		if (parent==NULL) {
			printf("[hcct] unable to initialize HCCT... Quitting!\n");
			exit(1);
		}
	}
			
	++lss_enter_events;
    
    lss_hcct_node_t *node;        
    for (node = parent->first_child; node != NULL; node = node->next_sibling)
        if (node->routine_id == routine_id &&
            node->call_site == call_site) break;
    
    if (node != NULL) {
		// node was already in the tree
        stack[stack_idx] = node;

		#if PROFILE_TIME==0
        if (IsMonitored(node)) {			
            node->counter++;
            return; // nothing left to do
        }
        #else
        if (increment==0) return;
        else if (IsMonitored(node)) {
			node->counter+=increment;
			return;
		}
        #endif
    } else {
		// add new child to parent		
        #if USE_MALLOC==0
        pool_alloc(hcct_pool, free_list, node, lss_hcct_node_t);
        #else
        node=(lss_hcct_node_t*)malloc(sizeof(lss_hcct_node_t));
        #endif
        if (node==NULL) {
			printf("[hcct] unable to allocate new node... Quitting!\n");
			exit(1);
		}
                
        node->routine_id    = routine_id;
        node->call_site     = call_site;
        node->first_child   = NULL;
        node->next_sibling  = parent->first_child;
        node->parent        = parent;
        parent->first_child = node;
        stack[stack_idx]  = node;
    }

	#if PROFILE_TIME==1	
	if (increment==0) { // new cold node along path to hot node yet to be added
		node->counter=0;
		UnsetMonitored(node);
		return;
	}
	#endif
    
    // mark node as monitored
    SetMonitored(node);

    // if table is not full
    if (!queue_full) {
        #if KEEP_EPS
        node->eps = 0;
        #endif
        #if PROFILE_TIME==0
        node->counter = 1;
        #else
        node->counter = increment;
        #endif
        queue[num_queue_items++] = node;
        queue_full = (num_queue_items == epsilon);
        return;
    }

    #if INLINE_UPD_MIN == 1
 
    // version with swaps
    #if 0
    while (++min_idx < epsilon)
        if (queue[min_idx]->counter == min) goto end;

    UINT32 i;
    min = queue[second_min_idx]->counter;
    min_idx = epsilon-1;
    swap_and_decr(second_min_idx, min_idx);
    for (i = 0; i <= min_idx; ++i) {
        if (queue[i]->counter > min) continue;
        if (queue[i]->counter < min)  {
            second_min_idx = i;
            min_idx = epsilon-1;
            min = queue[i]->counter;
        }
        swap_and_decr(i, min_idx);
    }
    end: 
    #endif

    #if UPDATE_MIN_SENTINEL == 1

    // version with second_min + sentinel
    while (queue[++min_idx]->counter > min);

    if (min_idx < epsilon) goto end;

    UINT32 i;
    min = queue[second_min_idx]->counter;
    for (min_idx = 0, i = 0; i < epsilon; ++i) {
        if (queue[i]->counter < min) {
            second_min_idx = min_idx;
            min_idx = i;
            min = queue[i]->counter;
        }
    }
    queue[epsilon]->counter = min;
    end: 

    // version with second_min
    #else
    while (++min_idx < epsilon)
        if (queue[min_idx]->counter == min) goto end;

    UINT32 i;
    min = queue[second_min_idx]->counter;
    for (min_idx = 0, i = 0; i < epsilon; ++i) {
        if (queue[i]->counter < min) {
            second_min_idx = min_idx;
            min_idx = i;
            min = queue[i]->counter;
        }
    }
    end: 
    #endif
    
    #else
    // no explicit inlining: -O3 should do it anyway, though
    update_min();
    #endif

    #if KEEP_EPS
    node->eps=min;
    #endif

	#if PROFILE_TIME==0
    node->counter = min + 1;
    #else
    node->counter = min + increment;
    #endif

    UnsetMonitored(queue[min_idx]);

    if (queue[min_idx]->first_child==NULL)
        prune(queue[min_idx]);

    queue[min_idx]=node;
}

void hcct_exit()
{
    stack_idx--;
}

void hcct_init() 
{
	// hcct_init might have been invoked already once before trace_begin() is executed
	if (hcct_root!=NULL) return;
   
	#if BURSTING==1
    if (sampling_interval==0) init_bursting();
    #elif PROFILE_TIME==1
    if (sampling_interval==0) init_sampling();
    #endif
   
	if (epsilon==0) hcct_getenv(); // will be executed only once
   
    stack_idx = 0;
    
    #if SHOW_MESSAGES==1
    pid_t tid=syscall(__NR_gettid);
    printf("[hcct] initializing data structures for thread %d\n", tid);
    #endif
    
    #if USE_MALLOC==0
    // initialize custom memory allocator
    hcct_pool = pool_init(PAGE_SIZE, sizeof(lss_hcct_node_t), &free_list);
    if (hcct_pool == NULL) {
			printf("[hcct] error while initializing allocator... Quitting!\n");
            exit(1);
	}
	#endif
    
    // create dummy root node
    #if USE_MALLOC==0    
	pool_alloc(hcct_pool, free_list, stack[0], lss_hcct_node_t);
	#else
	stack[0]=(lss_hcct_node_t*)malloc(sizeof(lss_hcct_node_t));
	#endif
    if (stack[0] == NULL) {
			printf("[hcct] error while initializing hcct root node... Quitting!\n");
			exit(1);
	}
    
    // initialize root node        
	stack[0]->first_child 	= NULL;
    stack[0]->next_sibling	= NULL;    
    stack[0]->routine_id	= 0;
    stack[0]->call_site		= 0;
    stack[0]->counter		= 1;
    stack[0]->parent		= NULL;
    
    hcct_root=stack[0];
    SetMonitored(hcct_root);
    
    // create lazy priority queue
    #if UPDATE_MIN_SENTINEL == 1
    queue = (lss_hcct_node_t**)malloc((epsilon+1)*sizeof(lss_hcct_node_t*));
    if (queue == NULL) {
			printf("[hcct] error while allocating lazy priority queue... Quitting!\n");
			exit(1);
	}		
    
    #if USE_MALLOC==0
    pool_alloc(hcct_pool, free_list, queue[epsilon], lss_hcct_node_t);
    #else
    queue[epsilon]=(lss_hcct_node_t*)malloc(sizeof(lss_hcct_node_t));
    #endif        
    if (queue[epsilon] == NULL) {
			printf("[hcct] error while allocating sentinel... Quitting!\n");
			exit(1);
	}
    queue[epsilon]->counter = min = 0;
    #else    
    queue = (lss_hcct_node_t**)malloc(epsilon*sizeof(lss_hcct_node_t*));
    if (queue == NULL) {
			printf("[hcct] error while allocating lazy priority queue... Quitting!\n");
			exit(1);
	}
    #endif    
    
    queue[0]        = hcct_root;
    num_queue_items = 1; // goes from 0 to epsilon
    queue_full      = 0;
    min_idx         = epsilon-1;
    second_min_idx  = 0;
    
    lss_enter_events = 0;
}

#if BURSTING==1
void hcct_align() {        
    // reset CCT internal stack
    stack_idx=0;
    
    #if UPDATE_ALONG_TREE==1
    // updates all nodes related to routines actually in the the stack
    int i;
    for (i=0; i<shadow_stack_idx; ++i)
        hcct_enter(shadow_stack[i].routine_id, shadow_stack[i].call_site);
    aligned=1;
    #else
    // uses hcct_enter only on current routine and on nodes that have to be created along the path
    lss_hcct_node_t *parent, *node;
    int i;
    
    // scan shadow stack
    #if 0
    for (i=0; i<shadow_stack_idx-1;) {
        parent=stack[stack_idx++];    
        for (node=parent->first_child; node!=NULL; node=node->next_sibling)
            if (node->routine_id == shadow_stack[i].routine_id &&
                node->call_site == shadow_stack[i].call_site) break;
        if (node!=NULL) {
            // No need to update counter here
            stack[stack_idx]=node;
            ++i;            
        } else {
            // I have to create additional nodes in the next for iteration
            --stack_idx;
            break;            
        }        
    }
    #else
    // should be better
    parent=stack[stack_idx];
    for (i=0; i<shadow_stack_idx-1;) {
        for (node=parent->first_child; node!=NULL; node=node->next_sibling)
            if (node->routine_id == shadow_stack[i].routine_id &&
                node->call_site == shadow_stack[i].call_site) break;
        if (node!=NULL) {
            // no need to update counter here
            stack[++stack_idx]=node;
            parent=node;
            ++i;            
        } else {
            // I have to create additional nodes in the next for iteration
            break;            
        }        
    }    
    #endif
    
    // update counters only for current routine and for new nodes created along the path
    for (; i<shadow_stack_idx; ++i)
        hcct_enter(shadow_stack[i].routine_id, shadow_stack[i].call_site);
    
    aligned=1;
    #endif
}
#elif PROFILE_TIME==1
void hcct_align(UINT32 increment) {
	// reset HCCT internal stack
    stack_idx=0;
	
	// walk the shadow stack
	int i;
    for (i=0; i<shadow_stack_idx-1; ++i)
        hcct_enter(shadow_stack[i].routine_id, shadow_stack[i].call_site, 0);
    hcct_enter(shadow_stack[i].routine_id, shadow_stack[i].call_site, increment);
	
}
#endif

#if DUMP_TREE==1
static void __attribute__((no_instrument_function)) hcct_dump_aux(FILE* out, lss_hcct_node_t* root, unsigned long *nodes)
{
	if (root==NULL) return;
        
	(*nodes)++;
	lss_hcct_node_t* ptr;

	// syntax: v <node id> <parent id> <counter> <routine_id> <call_site>
	// addresses in hexadecimal notation (useful for addr2line, and also to save bytes)
	fprintf(out, "v %lx %lx %lu %lx %lx\n", (unsigned long)root, (unsigned long)(root->parent),
	                                        root->counter, root->routine_id, root->call_site);
        
	for (ptr = root->first_child; ptr!=NULL; ptr=ptr->next_sibling)
		hcct_dump_aux(out, ptr, nodes);
}
#endif

#if USE_MALLOC==1
static void __attribute__((no_instrument_function)) free_hcct(lss_hcct_node_t* node) {	
   lss_hcct_node_t *ptr, *tmp;
    for (ptr=node->first_child; ptr!=NULL;) {
		tmp=ptr->next_sibling;
        free_cct(ptr);
        ptr=tmp;
    }    
    free(node);
}
#endif

void hcct_dump()
{
	pid_t tid;
			
	#if SHOW_MESSAGES==1
    tid=syscall(__NR_gettid);
    printf("[hcct] removing data structures for thread %d\n", tid);
    #endif
	
	#if DUMP_TREE==1
	unsigned long nodes=0;
	FILE* out;
	tid=syscall(__NR_gettid);	    
	
    int ds;	    
    char dumpFileName[BUFLEN+1];	    
    if (dumpPath==NULL) {	        
        sprintf(dumpFileName, "%s-%d.tree", program_invocation_short_name, tid);
    } else {            
		sprintf(dumpFileName, "%s/%s-%d.tree", dumpPath, program_invocation_short_name, tid);
    }
    ds = open(dumpFileName, O_EXCL|O_CREAT|O_WRONLY, 0660);
    if (ds == -1) exit((printf("[hcct] ERROR: cannot create output file %s\n", dumpFileName), 1));
    out = fdopen(ds, "w");    	    
	    	    	    
    #if BURSTING==1
	// c <tool> <epsilon> <sampling_interval> <burst_length> <exhaustive_enter_events>
	fprintf(out, "c lss-hcct-burst %f %lu %lu %llu\n", epsilon_d, sampling_interval, burst_length, exhaustive_enter_events);
	#elif PROFILE_TIME==1
	// c <tool> <epsilon> <sampling_interval> <thread_tics>
	fprintf(out, "c lss-hcct-time %f %lu %llu\n", epsilon_d, sampling_interval, thread_tics);
	#else
	// c <tool> <epsilon>
	fprintf(out, "c lss-hcct %f\n", epsilon_d);	    	    
	#endif
	    
	// c <command> <process/thread id> <working directory>
	char* cwd=getcwd(NULL, 0);
	fprintf(out, "c %s %d %s\n", program_invocation_name, tid, cwd);
	free(cwd);
	    	
	hcct_dump_aux(out, hcct_root, &nodes);
	
    // p <nodes> <enter_events>
    fprintf(out, "p %lu %llu\n", nodes, lss_enter_events); // #nodes used for a sanity check in the analysis tool
    fclose(out);

	#endif
    
	// free memory used by tree nodes and lazy priority queue
	#if USE_MALLOC==0
	pool_cleanup(hcct_pool);
	#else
	free_hcct(hcct_root);
	#endif	
	free(queue);
	
	// for instance, after trace_end() some events may still occur!	
	hcct_root=NULL; // see first instruction in hcct_init()
	stack_idx=0; stack[0]=NULL; // see if parent==NULL in hcct_enter()
		
}
