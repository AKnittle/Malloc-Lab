#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <assert.h>

#include "mm.h"
#include "memlib.h"
#include "list.h"

struct boundary_tag {
    int inuse:1;        // inuse bit
    int size:31;        // size of block, in words
};

/* FENCE is used for heap prologue/epilogue. */
const struct boundary_tag FENCE = { .inuse = 1, .size = 0 };

/* A C struct describing the beginning of each block. 
 * For implicit lists, used and free blocks have the same 
 * structure, so one struct will suffice for this example.
 * If each block is aligned at 4 mod 8, each payload will
 * be aligned at 0 mod 8.
 */
struct block {
    struct boundary_tag header; /* offset 0, at address 4 mod 8 */
    struct list_elem elem;		/* Double linked list elem */
    char payload[0];            /* offset 4, at address 0 mod 8 */
};

/* Basic constants and macros */
#define WSIZE       4       /* Word and header/footer size (bytes) */
#define DSIZE       8       /* Doubleword size (bytes) */
#define MIN_BLOCK_SIZE_WORDS 4 /* Minimum block size in words */
#define CHUNKSIZE  (1<<10)  /* Extend heap by this amount (words) */

#define MAX(x, y) ((x) > (y)? (x) : (y))  

static 

/* Global variables */
static struct block *heap_listp = 0;  /* Pointer to first block */

/* 
 * mm_init - Initialize the memory manager 
 */
int mm_init(void) 
{
    /* Create the initial empty heap */
    struct boundary_tag * initial = mem_sbrk(2 * sizeof(struct boundary_tag));
    if (initial == (void *)-1)
        return -1;

    /* We use a slightly different strategy than suggested in the book.
     * Rather than placing a min-sized prologue block at the beginning
     * of the heap, we simply place two fences.
     * The consequence is that coalesce() must call prev_blk_footer()
     * and not prev_blk() - prev_blk() cannot be called on the left-most
     * block.
     */
    initial[0] = FENCE;                     /* Prologue footer */
    
    heap_listp = (struct block *)&initial[1];
    initial[1] = FENCE;                     /* Epilogue header */


    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    if (extend_heap(CHUNKSIZE) == NULL) 
        return -1;
    return 0;
}


team_t team = {
    /* Team name */
    "Jue+Andrew",
    /* First member's full name */
    "Jue Hou",
    "hjue@vt.edu",
    /* Second member's full name */
    "Andrew K Knittle",
    "andrk11@vt.edu",
}; 