#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <unistd.h>

#include "common.h"
#include "symbols.h"
#include "tree.h"

// auxiliary methods (for internal use only)
static void freeNode(hcct_node_t* node);
static void freeTreeAux(hcct_node_t* node);
static void pruneTreeAboveCounterAux(hcct_node_t* node, UINT32 min_counter, UINT32* nodes);
static void pruneTreeAtDepthAux(hcct_node_t* node, UINT32 current_depth, UINT32 max_depth, UINT32* nodes);
static void solveTreeFromExistingMapAux(hcct_node_t* node, syms_map_t* syms_map, UINT32* solved);
static void solveTreeFromMapsFileAux(hcct_node_t* node, syms_map_t* sym_map, UINT32* solved, UINT32 nodes);
static void sortChildrenByDecreasingFrequencyAux(hcct_node_t* node, hcct_node_t** buffer);
static void getAvgDegreeAux(hcct_node_t* node, UINT32* internal_nodes, UINT32* sum);
static UINT32 getMaxDegreeAux(hcct_node_t* node);

/* Load a tree dump from file */
hcct_tree_t* loadTree(char* filename) {
    char buf[BUFLEN + 1];
    char *s;

    FILE* logfile = fopen(filename, "r");
    if (logfile == NULL) {
        printf("[ERROR] The specified file %s does not exist!\n", filename);
        return NULL;
    }

    printf("Input logfile: %s\n", filename);

    hcct_tree_t *tree = calloc(1, sizeof (hcct_tree_t)); // zeros are useful

    // syntax of the first two rows
    // c <tool> [ <parameter1> <parameter2> ... ]
    // c <command> <process/thread id> <working directory>

    // first row
    if (fgets(buf, BUFLEN, logfile) == NULL) {
        printf("Error: broken logfile\n");
        exit(1);
    }
    s = strtok(buf, " ");
    if (strcmp(s, "c")) {
        printf("Error: wrong format!\n");
        exit(1);
    }
    s = strtok(NULL, " ");

    if (strcmp(s, CCT_FULL_STRING) == 0) {
        tree->tool = CCT_FULL;
        tree->exhaustive_enter_events = 0;
        printf("Log generated by tool: %s\n", CCT_FULL_STRING); // CCT_FULL_STRING ends with \n
    } else if (strcmp(s, LSS_FULL_STRING) == 0) {
        tree->tool = LSS_FULL;
        tree->exhaustive_enter_events = 0;
        s = strtok(NULL, " ");
        tree->epsilon = strtod(s, &s);
        if (tree->epsilon == 0) {
            setlocale(LC_NUMERIC, "");
            tree->epsilon = strtod(s, &s);
        }
        printf("Log generated by tool: %s\n", LSS_FULL_STRING);
        printf("Parameters: epsilon=%f\n", tree->epsilon);
    } else if (strcmp(s, CCT_BURST_STRING) == 0) {
        tree->tool = CCT_BURST;
        s = strtok(NULL, " ");
        tree->sampling_interval = strtoul(s, &s, 0);
        s++;
        tree->burst_length = strtoul(s, &s, 0);
        s++;
        tree->exhaustive_enter_events = strtoull(s, &s, 0);
        printf("Log generated by tool: %s\n", CCT_BURST_STRING);
        printf("Parameters: sampling_interval=%lu burst_length=%lu exhaustive_enter_events=%llu\n",
                tree->sampling_interval, tree->burst_length, tree->exhaustive_enter_events);
    } else if (strcmp(s, LSS_BURST_STRING) == 0) {
        tree->tool = LSS_BURST;
        s = strtok(NULL, " ");
        tree->epsilon = strtod(s, &s);
        if (tree->epsilon == 0) {
            setlocale(LC_NUMERIC, "");
            tree->epsilon = strtod(s, &s);
        }
        s++;
        tree->sampling_interval = strtoul(s, &s, 0);
        s++;
        tree->burst_length = strtoul(s, &s, 0);
        s++;
        tree->exhaustive_enter_events = strtoull(s, &s, 0);
        printf("Log generated by tool: %s\n", LSS_BURST_STRING);
        printf("Parameters: epsilon=%f sampling_interval=%lu (ns) burst_length=%lu (ns) exhaustive_enter_events=%llu\n",
                tree->epsilon, tree->sampling_interval, tree->burst_length, tree->exhaustive_enter_events);
    } else if (strcmp(s, CCT_TIME_STRING) == 0) {
        tree->tool = CCT_TIME;
        s = strtok(NULL, " ");
        tree->sampling_interval = strtoul(s, &s, 0);
        s++;
        tree->thread_tics = strtoull(s, &s, 0);
        printf("Log generated by tool: %s\n", CCT_TIME_STRING);
        printf("Parameters: sampling_interval=%lu (ns) thread_tics=%llu\n",
                tree->sampling_interval, tree->thread_tics);
    } else if (strcmp(s, LSS_TIME_STRING) == 0) {
        tree->tool = LSS_TIME;
        s = strtok(NULL, " ");
        tree->epsilon = strtod(s, &s);
        if (tree->epsilon == 0) {
            setlocale(LC_NUMERIC, "");
            tree->epsilon = strtod(s, &s);
        }
        s++;
        tree->sampling_interval = strtoul(s, &s, 0);
        s++;
        tree->thread_tics = strtoull(s, &s, 0);
        printf("Log generated by tool: %s\n", LSS_TIME_STRING);
        printf("Parameters: epsilon=%f sampling_interval=%lu (ns) thread_tics=%llu\n",
                tree->epsilon, tree->sampling_interval, tree->thread_tics);
    }

    // second row
    if (fgets(buf, BUFLEN, logfile) == NULL) {
        printf("Error: broken logfile\n");
        exit(1);
    }
    s = strtok(buf, " ");
    if (strcmp(s, "c")) {
        printf("Error: wrong format!\n");
        exit(1);
    }

    // TODO some refactoring here
    char* short_name, *tid, *program_path;
    short_name = strtok(NULL, " ");
    tid = strtok(NULL, " ");
    program_path = strtok(NULL, "\n");

    // remove ./ from program name
    if (short_name[0] == '.' && short_name[1] == '/')
        tree->short_name = strdup(short_name + 2);
    else
        tree->short_name = strdup(short_name);
    tree->program_path = strdup(program_path);
    tree->tid = (pid_t) strtol(tid, NULL, 0);

    printf("Instrumented program:");
    printf(" %s in %s (TID: %d)\n", tree->short_name, tree->program_path, tree->tid);

    // stack used to reconstruct the tree
    hcct_pair_t tree_stack[STACK_MAX_DEPTH];
    int stack_idx = 0;
    UINT32 nodes = 0;

    ADDRINT node_id, parent_id;

    // create root node
    hcct_node_t* root = calloc(1, sizeof(hcct_node_t));

    if (fgets(buf, BUFLEN, logfile) == NULL) {
        printf("Error: broken logfile\n");
        exit(1);
    }
    s = strtok(buf, " ");
    if (strcmp(s, "v")) {
        printf("Error: wrong format!\n");
        exit(1);
    }

    tree_stack[0].node = root;
    tree_stack[0].id = strtoul(&buf[2], &s, 16); // node_id
    s++; // skip " " character
    if (strtoul(s, &s, 16) != 0) { // parent_id should be 0
        printf("Error: root node cannot have a parent node!\n");
        exit(1);
    }
    s++;

    root->counter = strtoul(s, &s, 0);
    s++;
    root->routine_id = strtoul(s, &s, 16);
    s++;
    root->call_site = strtoul(s, &s, 16);
    if (root->counter != 1 || root->routine_id != 0 || root->call_site != 0) {
        printf("Error: there's something strange in root node data (counter, routine_id or call_site)!\n");
        exit(1);
    }

    nodes++;

    // syntax: v <node id> <parent id> <counter> <routine_id> <call_site>
    hcct_node_t *node, *parent, *tmp;
    while (1) {
        if (fgets(buf, BUFLEN, logfile) == NULL) {
            printf("Error: broken logfile\n");
            exit(1);
        }

        s = strtok(buf, " ");
        if (strcmp(s, "p") == 0) break;
        if (strcmp(s, "v")) {
            printf("Error: wrong format!\n");
            exit(1);
        }

        node = malloc(sizeof (hcct_node_t));
        node->first_child = NULL;
        node->next_sibling = NULL;
        node->routine_sym = NULL;
        node->call_site_sym = NULL;
        node->extra_info = NULL;

        node_id = strtoul(&buf[2], &s, 16);
        node->node_id = node_id;
        s++;
        parent_id = strtoul(s, &s, 16);
        s++;
        node->counter = strtoul(s, &s, 0);
        s++;
        node->routine_id = strtoul(s, &s, 16);
        s++;
        node->call_site = strtoul(s, &s, 16);

        // attach node to the tree
        while (tree_stack[stack_idx].id != parent_id && stack_idx >= 0)
            stack_idx--;
        if (tree_stack[stack_idx].id != parent_id) {
            printf("Error: log file is broken - missing node(s)\n");
            exit(1);
        }
        parent = tree_stack[stack_idx].node;
        node->parent = parent;
        if (parent->first_child == NULL)
            parent->first_child = node;
        else {
            // attach as rightmost child (tree has been dumped using a depth-first visit)
            for (tmp = parent->first_child; tmp->next_sibling != NULL; tmp = tmp->next_sibling);
            tmp->next_sibling = node;
        }
        stack_idx++;
        tree_stack[stack_idx].node = node;
        tree_stack[stack_idx].id = node_id;
        nodes++;
    }

    // syntax: p <num_nodes> <sum_of_tree_counters>
    tree->nodes = strtoul(&buf[2], &s, 0);
    if (tree->nodes != nodes) { // Sanity check
        printf("Error: log file is broken - wrong number of nodes\n");
        exit(1);
    }
    s++;

    printf("Total number of nodes: %lu\n", tree->nodes);

    if (tree->tool == CCT_FULL || tree->tool == LSS_FULL) {
        tree->exhaustive_enter_events = strtoull(s, NULL, 0);
        printf("Total number of rtn_enter_events: %llu\n", tree->exhaustive_enter_events);
    } else if (tree->tool == CCT_BURST || tree->tool == LSS_BURST) {
        tree->sampled_enter_events = strtoull(s, NULL, 0);
        printf("Total number of rtn_enter_events: %llu\n", tree->exhaustive_enter_events);
        printf("Total number of sampled rtn_enter_events: %llu\n", tree->sampled_enter_events);
    } else if (tree->tool == CCT_TIME || tree->tool == LSS_TIME) {
        tree->sum_of_tics = strtoull(s, NULL, 0);
        printf("Sum of counters along the tree: %llu\n", tree->sum_of_tics);
    }

    tree->root = root;

    printf("Tree has been built.\n");

    fclose(logfile);

    return tree;
}

/* Free memory in use by the tree */
void freeTree(hcct_tree_t* tree) {
    if (tree == NULL) return;
    if (tree->root != NULL) // for safety
        freeTreeAux(tree->root);
    free(tree->short_name);
    free(tree->program_path);
    free(tree);
    printf("Tree has been deleted from memory.\n");
}

/* Remove from a tree every node s.t. node->counter <= min_counter (SpaceSaving algorithm) */
void pruneTreeAboveCounter(hcct_tree_t* tree, UINT32 min_counter) {
    pruneTreeAboveCounterAux(tree->root, min_counter, &tree->nodes);
}
/* Prune nodes over a given depth*/
void pruneTreeAtDepth(hcct_tree_t* tree, UINT32 depth) {
    pruneTreeAtDepthAux(tree->root, 0, depth, &tree->nodes);
}

/* Solve addresses using pre-computed symbols map*/
void solveTreeFromExistingMap(hcct_tree_t* tree, syms_map_t* syms_map) {
    // handle <dummy> case
    ADDRINT dummy_addr = 0;
    sym_t* dummy_sym = malloc(sizeof(sym_t));
    dummy_sym->name = strdup("<dummy>");
    dummy_sym->file = strdup("<dummy>");
    dummy_sym->image = strdup("<dummy>");
    dummy_sym->offset = 0;

    manuallyAddSymbol(syms_map, dummy_addr, dummy_sym);

    UINT32 solved = 0;
    solveTreeFromExistingMapAux(tree->root, syms_map, &solved);
    printf("%lu/%lu symbols have been solved using pre-computed information!\n", 2*solved, 2*tree->nodes);
}

/* Try to solve symbols in a tree and return a map of symbols*/
syms_map_t* solveTreeFromMapsFile(hcct_tree_t* tree, char* mapfile, char* program) {
    // check existance of the executable associated with the tree
    if (access(program, F_OK) != 0) {
        printf("\nERROR: program %s does not exists, cannot solve symbols!\n", program);
        exit(1);
    }

    syms_map_t* syms_map = initializeSymbols(program, mapfile); // TODO: handle dummy case

    // handle <dummy> case
    ADDRINT dummy_addr = 0;
    sym_t* dummy_sym = malloc(sizeof(sym_t));
    dummy_sym->name = strdup("<dummy>");
    dummy_sym->file = strdup("<dummy>");
    dummy_sym->image = strdup("<dummy>");
    dummy_sym->offset = 0;

    manuallyAddSymbol(syms_map, dummy_addr, dummy_sym);

    UINT32 solved = 0;
    solveTreeFromMapsFileAux(tree->root, syms_map, &solved, tree->nodes);
    printf("\r%lu/%lu symbols have been solved using addr2line on-the-fly!\n", 2*solved, 2*tree->nodes);

    return syms_map;
}

// helper function to support address resolution using either pre-computed information or addr2line on-the-fly
void solveTree(hcct_tree_t* tree, char* program, char* maps_file, char* syms_file, syms_map_t** syms_map_ptr) {
    if (*syms_map_ptr == NULL) {
        if (maps_file == NULL) {
            FILE* file = fopen(syms_file, "r");
            if (file == NULL) {
                printf("The specified symbols file %s does not exist!\n", syms_file);
                exit(1);
            }
            *syms_map_ptr = readMapFromFile(file);
            fclose(file);

            solveTreeFromExistingMap(tree, *syms_map_ptr);
        } else {
            *syms_map_ptr = solveTreeFromMapsFile(tree, maps_file, program);
        }
    } else {
        solveTreeFromExistingMap(tree, *syms_map_ptr);
    }
}

void sortChildrenByDecreasingFrequency(hcct_tree_t* tree) {
    hcct_node_t** buffer = malloc(NODE_BUFFER_SIZE * sizeof(hcct_node_t*));
    sortChildrenByDecreasingFrequencyAux(tree->root, buffer);
    free(buffer);
}

double getAvgDegree(hcct_tree_t* tree) {
    UINT32 internal_nodes = 0;
    UINT32 sum = 0;
    getAvgDegreeAux(tree->root, &internal_nodes, &sum);
    return sum/(double)internal_nodes;
}

UINT32 getMaxDegree(hcct_tree_t* tree) {
    return getMaxDegreeAux(tree->root);
}

double getPhi(hcct_tree_t* tree, double candidatePhi) {
    double phi = candidatePhi;
    if (tree->tool == CCT_FULL || tree->tool == CCT_BURST || tree->tool == CCT_TIME) {
        if (phi <= 0 || phi >= 1.0) {
            phi = 1.0 / (EPSILON / 5.0);
            printf("WARNING: invalid value for PHI, using default (%f) instead.\n", phi);
        }
    } else if (tree->tool == LSS_FULL || tree->tool == LSS_BURST || tree->tool == LSS_TIME) {
        if (phi == 0) phi = tree->epsilon * 5;
        else if (phi <= tree->epsilon) {
            phi = tree->epsilon * 5;
            printf("WARNING: PHI must be greater than EPSILON, using PHI=5*EPSILON instead\n");
        }
    } else {
        printf("\nSorry, this has not been implemented yet!\n");
        exit(1);
    }
    return phi;
}

UINT32 getMinThreshold(hcct_tree_t* tree, double phi) {
    if (tree->tool == CCT_BURST || tree->tool == LSS_BURST)
        return (UINT32) (phi * tree->sampled_enter_events);
    else if (tree->tool == CCT_TIME || tree->tool == LSS_TIME)
        return (UINT32) (phi * tree->thread_tics);
    else // CCT_FULL, LSS_FULL
        return (UINT32) (phi * tree->exhaustive_enter_events);
}

UINT64 sumCountersInSubtree(hcct_node_t* node) {
    UINT64 sum = node->counter;
    hcct_node_t* child;
    for (child = node->first_child; child != NULL; child = child->next_sibling) {
        sum += sumCountersInSubtree(child);
    }
    return sum;
}

double getAdjustFactor(hcct_tree_t* first_tree, hcct_tree_t* second_tree) {
    UINT64 first_events, second_events;

    if (first_tree->tool == CCT_FULL || first_tree->tool == LSS_FULL) {
        first_events = first_tree->exhaustive_enter_events;
    } else if (first_tree->tool == CCT_BURST || first_tree->tool == LSS_BURST) {
        first_events = first_tree->sampled_enter_events;
    } else {
        printf("[ERROR] Sorry, this feature has not been implemented yet!\n");
        return 1;
    }

    if (second_tree->tool == CCT_FULL || second_tree->tool == LSS_FULL) {
        second_events = second_tree->exhaustive_enter_events;
    } else if (second_tree->tool == CCT_BURST || second_tree->tool == LSS_BURST) {
        second_events = second_tree->sampled_enter_events;
    } else {
        printf("[ERROR] Sorry, this feature has not been implemented yet!\n");
        return 1;
    }

    return first_events/(double)second_events;
}

/*
 * Auxiliary methods
 */

static void pruneTreeAboveCounterAux(hcct_node_t* node, UINT32 min_counter, UINT32* nodes) {
    hcct_node_t *ptr, *tmp;

    for (ptr = node->first_child; ptr != NULL;) {
        tmp = ptr->next_sibling;
        pruneTreeAboveCounterAux(ptr, min_counter, nodes);
        ptr = tmp;
    }

    if (node->counter <= min_counter && node->first_child == NULL && node->routine_id != 0) {
        // detach node from the tree
        if (node->parent->first_child == node) {
            node->parent->first_child = node->next_sibling;
        } else {
            for (tmp = node->parent->first_child; tmp != NULL; tmp = tmp->next_sibling) {
                if (tmp->next_sibling == node) {
                    tmp->next_sibling = node->next_sibling;
                    break;
                }
            }
        }
        --(*nodes);
        freeNode(node);
    }
}

static void pruneTreeAtDepthAux(hcct_node_t* node, UINT32 current_depth, UINT32 max_depth, UINT32* nodes) {
    hcct_node_t* child = node->first_child;
    if (current_depth == max_depth) {
        hcct_node_t* tmp;
        for ( ; child != NULL ; child = tmp) {
            tmp = child->next_sibling;
            pruneTreeAboveCounterAux(child, (UINT32)-1, nodes); // dirty :-)
        }
    } else {
        for ( ; child != NULL ; child = child->next_sibling) {
            pruneTreeAtDepthAux(child, current_depth + 1, max_depth, nodes);
        }
    }
}

static void solveTreeFromExistingMapAux(hcct_node_t* node, syms_map_t* syms_map, UINT32* solved) {
    node->routine_sym = getSolvedSymbol(node->routine_id, syms_map);
    if (node->routine_sym == NULL) {
        printf("\r[ERROR] Unable to find symbol for address %lx from the symbols file. Exiting...\n", node->routine_id);
        exit(1);
    }
    node->call_site_sym = getSolvedSymbol(node->call_site, syms_map);
    if (node->call_site_sym == NULL) {
        printf("\r[ERROR] Unable to find symbol for address %lx from the symbols file. Exiting...\n", node->call_site);
        exit(1);
    }

    ++(*solved);

    hcct_node_t* child;
    for (child = node->first_child; child != NULL; child = child->next_sibling)
        solveTreeFromExistingMapAux(child, syms_map, solved);
}

static void solveTreeFromMapsFileAux(hcct_node_t* node, syms_map_t* sym_map, UINT32* solved, UINT32 nodes) {
    node->routine_sym = solveAddress(node->routine_id, sym_map);
    node->call_site_sym = solveAddress(node->call_site, sym_map);
    if (((*solved)++ % SOLVE_PROGRESS_INDICATOR) == 0) {
        printf("\r%lu/%lu symbols solved", *solved*2, nodes*2);
        fflush(0);
    }
    hcct_node_t *child;
    for (child = node->first_child; child != NULL; child = child->next_sibling)
        solveTreeFromMapsFileAux(child, sym_map, solved, nodes);
}

static void freeTreeAux(hcct_node_t* node) {
    if (node == NULL) return;
    hcct_node_t *ptr, *tmp;
    for (ptr = node->first_child; ptr != NULL;) {
        tmp = ptr->next_sibling;
        freeTreeAux(ptr);
        ptr = tmp;
    }

    freeNode(node);
}

static void freeNode(hcct_node_t* node) {
    if (node->extra_info != NULL) {
        free(node->extra_info);
    }
    free(node);
}

static void sortChildrenByDecreasingFrequencyAux(hcct_node_t* node, hcct_node_t** buffer) {
    // return on leaves
    if (node->first_child == NULL) return;

    hcct_node_t* tmp = node->first_child;

    // sort only nodes with more than one child
    if (tmp->next_sibling != NULL) {
        int children = 0;
        int i = 0, j;

        // create a list of nodes
        while(1) {
            buffer[children++] = tmp;
            if (children == NODE_BUFFER_SIZE) {
                printf("ERROR: maximum degree is bigger than %d, use a larger buffer!\n", NODE_BUFFER_SIZE - 1);
                exit(1);
            }

            if (tmp->next_sibling == NULL) break;

            // check whether the array is already sorted in a non-increasing way
            if (tmp->next_sibling->counter < tmp->counter) {
                i = 1; // i is a flag
            }
            tmp = tmp->next_sibling;
        }

        // sort only if is needed
        if (i) {
            // perform insertion sort
            for (i = 1; i < children; ++i) {
                tmp = buffer[i];
                j = i-1;
                while (j >= 0 && buffer[j]->counter > tmp->counter) {
                    buffer[j+1] = buffer[j];
                    --j;
                }
                buffer[j+1] = tmp;
            }

            // reassign pointers
            node->first_child = buffer[--children];
            for (i = children; i > 0; --i) { // non-increasing order
                buffer[i]->next_sibling = buffer[i-1];
            }
            buffer[0]->next_sibling = NULL;
        }
    }

    for (tmp = node->first_child; tmp != NULL; tmp = tmp->next_sibling) {
        sortChildrenByDecreasingFrequencyAux(tmp, buffer);
    }
}

static void getAvgDegreeAux(hcct_node_t* node, UINT32* internal_nodes, UINT32* sum) {
    UINT32 degree = 0;
    hcct_node_t* child;
    for (child = node->first_child; child != NULL; child = child->next_sibling) {
        if (child->first_child != NULL) {
            getAvgDegreeAux(child, internal_nodes, sum); // won't be called on leaves
        }
        ++degree;
    }
    ++(*internal_nodes);
    (*sum) += degree;
}

static UINT32 getMaxDegreeAux(hcct_node_t* node) {
    UINT32 tmp;
    UINT32 this = 0;
    UINT32 max = 0;
    hcct_node_t* child;
    for (child = node->first_child; child != NULL; child = child->next_sibling) {
        ++this;
        tmp = getMaxDegreeAux(child);
        if (tmp > max) {
            max = tmp;
        }
    }
    if (this > max) {
        max = this;
    }
    return max;
}