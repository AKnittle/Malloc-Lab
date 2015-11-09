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
const struct boundary_tag FENCE = { 
	.inuse = 1, 
	.size = 0 
};

/* A C struct describing the beginning of each block. 
 * For implicit lists, used and free blocks have the same 
 * structure, so one struct will suffice for this example.
 * If each block is aligned at 4 mod 8, each payload will
 * be aligned at 0 mod 8.
 */
struct used_block {
    struct boundary_tag header; /* offset 0, at address 4 mod 8 */
    char payload[0];            /* offset 4, at address 0 mod 8 */
};

/* A C struct describing the beginning of each block. 
 * For implicit lists, used and free blocks have the same 
 * structure, so one struct will suffice for this example.
 * If each block is aligned at 4 mod 8, each payload will
 * be aligned at 0 mod 8.
 */
struct free_block {
    struct boundary_tag header; /* offset 0, at address 4 mod 8 */
    struct list_elem elem;		/* Double linked list elem in free block*/
    char payload[0];            /* offset 4, at address 0 mod 8 */
};


/*
 * A Struct used to hold all the explicit lists
 * Each index will be based on powers of 2. 
 * For example seg_list[1] starts at 32, seg_list[2]
 * would be 64, and seg_list[3] would be 128, and so
 * on.
 */
struct segregated_list
{
	//The size of 5 is arbitrary
	struct list segList[5];
};


/* Basic constants and macros */
#define WSIZE       4       /* Word and header/footer size (bytes) */
#define DSIZE       8       /* Doubleword size (bytes) */
#define MIN_BLOCK_SIZE_WORDS 4 /* Minimum block size in words */
#define CHUNKSIZE  (1<<10)  /* Extend heap by this amount (words) */

#define MAX(x, y) ((x) > (y)? (x) : (y))  

/* Global variables */
static struct list elist;		


/* Function prototypes for internal helper routines */
static struct free_block *extend_heap(size_t words);
static struct free_block *coalesce(struct free_block *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);


/* Return size of block is free */
static size_t blk_size(struct free_block *blk) { 
    return blk->header.size; 
}

/* Given a free block, obtain previous's block footer.
   Works for left-most block also. */
static struct boundary_tag * prev_blk_footer(struct free_block *blk) {
    return &blk->header - 1;
}

/* Given a free block, obtain next block's header.
   Works for left-most block also. */
static struct boundary_tag *next_blk_header(struct free_block *blk) {
    return (struct boundary_tag *)((size_t *)blk + blk->header.size);
}

/* Given a free_block, obtain pointer to previous free_block.
   Not meaningful for left-most block.
   This function should only be used when knowing that next block is a free_block*/
static struct free_block *prev_blk(struct free_block *blk) {
    struct boundary_tag *prevfooter = prev_blk_footer(blk);
    assert(prevfooter->size != 0);
    return (struct free_block *)((size_t *)blk - prevfooter->size);
}

/* Given a free_block, obtain pointer to next free_block.
   Not meaningful for right-most block. 
   This function should only be used when knowing that next block is a free_block*/
static struct free_block *next_blk(struct free_block *blk) {
    assert(blk_size(blk) != 0);
    return (struct free_block *)((size_t *)blk + blk->header.size);
}

/* Given a block, obtain its footer boundary tag */
static struct boundary_tag * get_footer(struct free_block *blk) {
    return (void *)((size_t *)blk + blk->header.size) 
                   - sizeof(struct boundary_tag);
}
/*Mark a block as used and set its size*/
static void mark_block_used(struct block *blk, int size)
{
	blk->header.inuse = 1;
	blk->header.size = size;
	* get_footer(blk) = blk->header;
}

/* Mark a block as free and set its size. */
static void mark_block_free(struct free_block *blk, int size) {
    blk->header.inuse = 0;
    blk->header.size = size;
    * get_footer(blk) = blk->header;    /* Copy header to footer */
}


/* 
 * mm_init - Initialize the memory manager 
 */
int mm_init(void) 
{
	/* Create the initial empty free explicit list */
	list_init(&elist);
	
    /* Create the initial empty heap */
    struct boundary_tag * initial = mem_sbrk(2 * sizeof(struct boundary_tag));
    if (initial == (void *)-1)
        return -1;

    initial[0] = FENCE;                     /* Prologue footer */    
    list_push_back(&elist, &((struct free_block *)&initial[1])->elem);
    initial[1] = FENCE;                     /* Epilogue header */

    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    if (extend_heap(CHUNKSIZE) == NULL) 
        return -1;
    return 0;
}

void *mm_malloc (size_t size)
{
	size_t awords;      /* Adjusted block size in words */
    size_t extendwords;  /* Amount to extend heap if no fit */
    struct used_block *bp;      

    if (elist.head.next == NULL){
        mm_init();
    }
    /* Ignore spurious requests */
    if (size == 0)
        return NULL;

    /* Adjust block size to include overhead and alignment reqs. */
    size += 2 * sizeof(struct boundary_tag);    			/* account for tags */
    size = (size + DSIZE - 1) & ~(DSIZE - 1);   			/* align to double word */
    awords = MAX(MIN_BLOCK_SIZE_WORDS, size/WSIZE);			/* respect minimum size */

    /* Search the free list for a fit */
    if ((bp = find_fit(awords)) != NULL) {
        place(bp, awords);
        return bp->payload;
    }

    /* No fit found. Get more memory and place the block */
    extendwords = MAX(awords,CHUNKSIZE);
    if ((bp = (void *)extend_heap(extendwords)) == NULL)  
        return NULL;
    place(bp, awords);
    return bp->payload;
}


void mm_free(void *ptr)
{	
	// check if null
	if (ptr == 0)
	{
		return;
	}
	// find block from user pointer
	struct free_block *blk = ptr - offsetof(struct used_block, payload);
	if(list !=  NULL)
	{
		mm_init();
	}
	mark_block_free(blk, blk_size(blk));
	//everytime free will be called we coalesce
	coalesce(blk);
}


/*
 * coalesce - Boundary tag coalescing. Return ptr to coalesced block
 */
static struct free_block *coalesce(struct free_block *bp) 
{
    bool prev_alloc = prev_blk_footer(bp)->inuse;
    bool next_alloc = next_blk_header(bp)->inuse;
    size_t size = blk_size(bp);

    if (prev_alloc && next_alloc) {            /* Case 1 */
		//Push this block to the list
		list_push_back(&elist, &bp->elem);
    }

    else if (prev_alloc && !next_alloc) {      /* Case 2 */
		//Combine two free blocks, remove next block from the list, push this block to the list
        mark_block_free(bp, size + blk_size(next_blk(bp)));
        list_push_back(&elist, &bp->elem);
        list_remove(&next_blk(bp)->elem);
    }

    else if (!prev_alloc && next_alloc) {      /* Case 3 */
		//Combine two free blocks, do not need to modify the list
        bp = prev_blk(bp);
        mark_block_free(bp, size + blk_size(bp));
    }

    else {                                     /* Case 4 */
		//Combine three free blocks, remove next block from the list
        mark_block_free(prev_blk(bp), 
                        size + blk_size(next_blk(bp)) + blk_size(prev_blk(bp)));
        list_remove(&next_blk(bp)->elem);
        bp = prev_blk(bp);
    }
    return bp;
}

void *mm_realloc(void *ptr, size_t size)
{
	return NULL;
}

static void *find_fit(size_t asize)
{
	return NULL;
}

//Simply states the block given is being used
//helper method for finding a properly sized block
static void place(void *bp, size_t asize)
{
	//ROUGH DRAFT: based off Back's code
	//first get the block size
	size_t csize = blk_size(bp);
	//Check if there's "extra space" 
	if((csize - asize) >= MIN_BLOCK_SIZE_WORDS)
	{
		//Break up block, reducing fragmentation
		mark_block_used(bp, asize);
		bp = next_blk(bp);
		mark_block_free(bp, csize-asize);
	}
	else
	{
		//Just the right size
		mark_block_used(bp, csize);
	}
}


/* 
 * extend_heap - Extend heap with free block and return its block pointer
 */
static struct free_block *extend_heap(size_t words) 
{
    void *bp;

    /* Allocate an even number of words to maintain alignment */
    words = (words + 1) & ~1;
    if (words < MIN_BLOCK_SIZE_WORDS) words = MIN_BLOCK_SIZE_WORDS;
    if ((long)(bp = mem_sbrk(words * WSIZE)) == -1)  
        return NULL;

    /* Initialize free block header/footer and the epilogue header.
     * Note that we scoop up the previous epilogue here. */
    struct free_block * blk = bp - sizeof(FENCE);
    mark_block_free(blk, words);
    next_blk(blk)->header = FENCE;

    /* Coalesce if the previous block was free */
    return coalesce(blk);
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
