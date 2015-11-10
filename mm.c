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
#define NLISTS		20		/* Number of segregated free lists */

#define MAX(x, y) ((x) > (y)? (x) : (y))
#define MIN(x, y) ((x) < (y)? (x) : (y))

/* Global variables */
//static struct list elist;	
static struct list segList[NLISTS];	


/* Function prototypes for internal helper routines */
static struct free_block *extend_heap(size_t words);
static struct free_block *coalesce(struct free_block *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
static void insert(void *bp, size_t asize);
//static void print_list();


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
static struct boundary_tag * get_footer(void *blk) {
    return (void *)((size_t *)blk + ((struct free_block*)blk)->header.size) 
                   - sizeof(struct boundary_tag);
}
/*Mark a block as used and set its size*/
static void mark_block_used(struct used_block *blk, int size)
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

/* Initialize all segregated free lists */
static void init_lists() {
	int count = 0;
	for (; count < NLISTS; count++) list_init(&segList[count]);
}


/* 
 * mm_init - Initialize the memory manager 
 */
int mm_init(void) 
{
	/* Initial all segregated free explicit list */
	init_lists();
	
    /* Create the initial empty heap */
    struct boundary_tag * initial = mem_sbrk(2 * sizeof(struct boundary_tag));
    if (initial == (void *)-1)
        return -1;

    initial[0] = FENCE;                     /* Prologue footer */    
    initial[1] = FENCE;                     /* Epilogue header */

    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    struct free_block *bp = extend_heap(CHUNKSIZE);
    if (bp == NULL) 
        return -1;
    //list_push_back(&elist, &bp->elem);
    //printf("In mm_init: ");
    //print_list();
    return 0;
}

void *mm_malloc (size_t size)
{
	size_t awords;      /* Adjusted block size in words */
    size_t extendwords;  /* Amount to extend heap if no fit */
    struct used_block *bp;
    struct used_block *blk;      

    if (segList[0].head.next == NULL){
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
		blk = bp;
        place(blk, awords);
        //printf("In mm_malloc: ");
		//print_list();
		//printf("%s block at %p with size %d, payload is at %p\n",
			//(bp->header.inuse)?"Used":"Free", bp, bp->header.size, bp->payload);
        return bp->payload;
    }

    /* No fit found. Get more memory and place the block */
    extendwords = MAX(awords,CHUNKSIZE);
    if ((bp = (void *)extend_heap(extendwords)) == NULL)  
        return NULL;
    blk = bp;
    place(blk, awords);
    //printf("In mm_malloc: ");
    //print_list();
    //printf("%s block at %p with size %d, payload is at %p\n",
			//(bp->header.inuse)?"Used":"Free", bp, bp->header.size, bp->payload);
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
	if(segList[0].head.next ==  NULL)
	{
		mm_init();
	}
	mark_block_free(blk, blk_size(blk));
	//everytime free will be called we coalesce
	coalesce(blk);
	//printf("In mm_free: ");
	//print_list();
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
		insert(bp, size);
    }

    else if (prev_alloc && !next_alloc) {      /* Case 2 */
		//Combine two free blocks, remove next block from the list, push new block to the list
		list_remove(&next_blk(bp)->elem);
        mark_block_free(bp, size + blk_size(next_blk(bp)));        
        insert(bp, blk_size(bp));
    }

    else if (!prev_alloc && next_alloc) {      /* Case 3 */
		//Combine two free blocks, remove the previous block from a list
		//INser the new block into appropriate list
        bp = prev_blk(bp);
        list_remove(&bp->elem);
        mark_block_free(bp, size + blk_size(bp));
        insert(bp, blk_size(bp));
    }

    else {                                     /* Case 4 */
		//Combine three free blocks, remove both previous and next block from their list
		//Insert the new blcok into appropriate list
		list_remove(&next_blk(bp)->elem);
		list_remove(&prev_blk(bp)->elem);
        mark_block_free(prev_blk(bp), 
                        size + blk_size(next_blk(bp)) + blk_size(prev_blk(bp)));
        bp = prev_blk(bp);
        insert(bp, blk_size(bp));
    }
    return bp;
}

void *mm_realloc(void *ptr, size_t size)
{
	/* If the pointer block is NULL, realloc should be the same as malloc */
	if (ptr == NULL) 
		return mm_malloc(size);
		
	/* If the size if 0, free the block and return 0 */
	if (size == 0) {
		mm_free(ptr);
		return NULL;
	}
	
	size_t oldsize;
    void *newptr = mm_malloc(size);

    /* If malloc() fails, the original block is left untouched  */
    if(!newptr) {
        return 0;
    }

    /* Copy the old data. */
    struct used_block *oldblock = ptr - offsetof(struct used_block, payload);
    oldsize = (oldblock->header.size) * WSIZE;
    memcpy(newptr, ptr, MIN(oldsize, size));

    /* Free the old block. */
    mm_free(ptr);
    //printf("In mm_realloc: ");
	//print_list();
    return newptr;
	
}

/*
 * find_fit - Find the first fit block from the list containing the block
 * with size larger than requesting
 */
static void *find_fit(size_t asize)
{	
	struct free_block *bp;
	int currentlist = 0;
	size_t csize = asize;
	// Search for the starting point
	while ((currentlist < NLISTS - 1) && (csize > 1)) {
		csize >>= 1;
		currentlist++;
	}
	for (; currentlist < NLISTS; currentlist++) {
		if (list_empty(&segList[currentlist])) continue;
		
		//Search within the list
		struct list_elem * e = list_begin (&segList[currentlist]);
			for (; e!= list_end (&segList[currentlist]); e = list_next (e)) {
			bp = (struct free_block *)((size_t *)e - sizeof(struct boundary_tag) / WSIZE);
			if (blk_size(bp) >= asize) return bp;
		}
	}
	return NULL;
}

/* 
 * place - Place block of asize words at start of free block bp 
 *         and split if remainder would be at least minimum block size
 */
static void place(void *bp, size_t asize)
{
	//first get the block size
	size_t csize = blk_size(bp);
	//Check if there's "extra space" 
	if((csize - asize) >= MIN_BLOCK_SIZE_WORDS)
	{
		//Break up block, reducing fragmentation
		
		//Remove the original block, mark first part as used
		list_remove(&((struct free_block*)bp)->elem);		
		mark_block_used((struct used_block*)bp, asize);
		
		// Mark the remaind block as free and insert to appropriate list
		bp = ((size_t *)bp + ((struct used_block*)bp)->header.size);
		mark_block_free((struct free_block*)bp, csize-asize);		
		insert(bp, blk_size(bp));
		
	}
	else
	{
		//Just the right size
		list_remove(&((struct free_block*)bp)->elem);
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

/*
 * insert - Insert a block pointer into an appropriate segregated list. 
 * 			The n-th list spanning byte sizes from 2^n to 2^(n+1)-1.
 * 			The new block will be push to the front of the list.
 */
static void insert(void *bp, size_t asize)
{
	int count = 0;
	struct list_elem *e = &((struct free_block*)bp)->elem;
	
	// Choosing a list with the appropriate size range
	while ((count < NLISTS - 1) && (asize > 1)) {
		asize >>= 1;
		count++;
	}
	list_push_front(&segList[count], e);
	
}

/*static void print_list()
{
	struct free_block *bp;
	if (!list_empty(&elist)) {
		struct list_elem * e = list_begin (&elist);
		for (; e!= list_end (&elist); e = list_next (e)) {
			bp = (struct free_block *)((size_t *)e - sizeof(struct boundary_tag) / WSIZE);
			printf("%s block at %p with size %d\n",
				(bp->header.inuse)?"Used":"Free", bp, bp->header.size);
		}
	}
	else printf("List is empty\n");
}*/

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
