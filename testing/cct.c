#include <stdio.h>
#include <stdlib.h>

#include <asm/unistd.h> // syscall(__NR_gettid)
#include <sys/types.h> // pid_t
#include <fcntl.h>
#include <unistd.h> // getcwd
#include <limits.h> // PATH_MAX
#include <string.h>

#include "cct.h"

#if USE_MALLOC==0
	#include "allocator/pool.h"
	#define PAGE_SIZE		1024
	__thread pool_t     *cct_pool;
	__thread void       *cct_free_list;
#endif

// thread local storage
__thread int         cct_stack_idx;
__thread cct_node_t *cct_stack[STACK_MAX_DEPTH];
__thread cct_node_t *cct_root;
__thread UINT32      cct_nodes;


int hcct_getenv()
{
    return 0;
}

cct_node_t* hcct_get_root()
{
    return cct_root;
}

int hcct_init()
{

    cct_stack_idx   = 0;
    cct_nodes       = 1;
    
    #if USE_MALLOC    
    cct_stack[0]=(cct_node_t*)malloc(sizeof(cct_node_t));
    #else
    // initialize custom memory allocator
    cct_pool = pool_init(PAGE_SIZE, sizeof(cct_node_t), &cct_free_list);
    if (cct_pool == NULL) {
			printf("[hcct] error while initializing allocator... Quitting!\n");
			return -1;
	}

    pool_alloc(cct_pool, cct_free_list, cct_stack[0], cct_node_t);
    if (cct_stack[0] == NULL) {
			printf("[hcct] error while initializing cct root node... Quitting!\n");
			return -1;
	}
	#endif

    cct_stack[0]->first_child = NULL;
    cct_stack[0]->next_sibling = NULL;
    cct_root = cct_stack[0];

    return 0;
}

void hcct_enter(UINT32 routine_id, UINT16 call_site)
{

    cct_node_t *parent=cct_stack[cct_stack_idx++];
    cct_node_t *node;
    for (node=parent->first_child; node!=NULL; node=node->next_sibling)
        if (node->routine_id == routine_id &&
            node->call_site == call_site) break;

    if (node!=NULL) {
        cct_stack[cct_stack_idx]=node;
        node->counter++;
        return;
    }

    ++cct_nodes;
    #if USE_MALLOC
    node=(cct_node_t*)malloc(sizeof(cct_node_t));
    #else
    pool_alloc(cct_pool, cct_free_list, node, cct_node_t);
    #endif
    cct_stack[cct_stack_idx] = node;
    node->routine_id = routine_id;
    node->first_child = NULL;
    node->counter = 1;
    node->call_site =  call_site;
    node->next_sibling = parent->first_child;
    parent->first_child = node;
}

void hcct_exit()
{
    cct_stack_idx--;
}

void __attribute__((no_instrument_function)) hcct_dump_aux(FILE* out,
        cct_node_t* root, unsigned long *nodes, cct_node_t* parent)
{
        if (root==NULL) return;
        
        (*nodes)++;        
        cct_node_t* ptr;

		#if DUMP_TREE==1
		// Syntax: v <node id> <parent id> <counter> <context> <call site>
		fprintf(out, "v %lu %lu %lu %lu %hu\n", (unsigned long)root, (unsigned long)(parent),
		                                        root->counter, root->routine_id, root->call_site);
        #endif
        
        for (ptr = root->first_child; ptr!=NULL; ptr=ptr->next_sibling)
                hcct_dump_aux(out, ptr, nodes, root);		
}

void hcct_dump()
{
	#if DUMP_STATS==1 || DUMP_TREE==1
	unsigned long nodes=0;
	FILE* out;
	pid_t tid=syscall(__NR_gettid);	    
	
	    #if DUMP_TREE==1	    
	    int ds;
	    char dumpFileName[25]; // up to 20 for PID (CHECK)	    
	    sprintf(dumpFileName, "%d.log", tid);
        ds = open(dumpFileName, O_EXCL|O_CREAT|O_WRONLY, 0660);
        if (ds == -1) exit((printf("[hcct] ERROR: cannot create output file %s\n", dumpFileName), 1));
        out = fdopen(ds, "w");    
	    
	    char *path = malloc(PATH_MAX);
        readlink("/proc/self/exe", path, PATH_MAX);                    
	    char* cwd=getcwd(NULL, 0);
	    
	    // c <tool>
	    // c <command> <process/thread id> <working directory>
	    fprintf(out, "c cct\n");	    
	    fprintf(out, "c %s %d %s\n", path+strlen(cwd), tid, cwd);
	    
	    free(path);
	    free(cwd);
	    #endif
	
	hcct_dump_aux(out, hcct_get_root(), &nodes, NULL);
	
	    #if DUMP_TREE==1
	    fprintf(out, "p %lu\n", nodes); // for a sanity check in the analysis tool
	    fclose(out);
	    #endif
	    
	    #if DUMP_STATS==1
	    printf("[thread: %d] Total number of nodes: %lu\n", tid, nodes);		
        #endif
	#endif
	
}
