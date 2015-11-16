/* 
 * Simple, 32-bit allocator based on segregated free
 * lists, first fit placement, and boundary tag coalescing.
 * Blocks must be aligned to doubleword (8 byte) boundaries. 
 * Minimum block size is 16 bytes. 
 *
 * The segregated list is organized based on free lists size,
 * segList[n] should contain free lists ranging from size 2^n
 * to 2^(n+1) - 1. Each segregated list is ordered by the list size.
 * 
 */

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

//STRUCTS: ------------------------------------------------
struct boundary_tag {
    int inuse:1;        // inuse bit
    int size:31;        // size of block, in words
};

/* FENCE is used for heap prologue/epilogue. */
const struct boundary_tag FENCE = { 
	.inuse = 1, 
	.size = 0 
};

/* A block filled in with a payload
 * If each block is aligned at 4 mod 8, each payload will
 * be aligned at 0 mod 8.
 */
struct used_block {
    struct boundary_tag header; /* offset 0, at address 4 mod 8 */
    char payload[0];            /* offset 4, at address 0 mod 8 */
};

/* A block that has no filled in payload, and contains pointers
 * (where the payload will be located) to the next and previous
 * free blocks
 * If each block is aligned at 4 mod 8, each payload will
 * be aligned at 0 mod 8.
 */
struct free_block {
    struct boundary_tag header; /* offset 0, at address 4 mod 8 */
    struct list_elem elem;		/* Double linked list elem in free block*/
    char payload[0];            /* offset 4, at address 0 mod 8 */
};
//--------------------------------------------------------


/* Basic constants and macros */
#define WSIZE       4       /* Word and header/footer size (bytes) */
#define DSIZE       8       /* Doubleword size (bytes) */
#define MIN_BLOCK_SIZE_WORDS 4 /* Minimum block size in words */
#define CHUNKSIZE  (1<<8)  /* Extend heap by this amount (words) */
#define NLISTS		20		/* Number of segregated free lists */

#define MAX(x, y) ((x) > (y)? (x) : (y))
#define MIN(x, y) ((x) < (y)? (x) : (y))


/*
 * If DEBUG defined enable printf's and print functions
 */
#define DEBUG

#define CHECKHEAP


/* Global variables */	
static struct list segList[NLISTS];	


/* Function prototypes for internal helper routines */
static struct free_block *extend_heap(size_t words);
static struct free_block *coalesce(struct free_block *bp);
static void *find_fit(size_t asize);
static void *place(void *bp, size_t asize);
static void insert(void *bp, size_t asize);
#ifdef CHECKHEAP
static int mm_check(void);
static bool check_list_mark();
static bool check_coalescing();
static bool check_inList();
static bool check_cont();
static bool valid_heap_address();
#endif
#ifdef DEBUG
static void print_list(struct list *elist, int n);
static void print_seg();
static void print_heap();
#endif


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
    //assert(prevfooter->size != 0);
    return (struct free_block *)((size_t *)blk - prevfooter->size);
}

/* Given a free_block, obtain pointer to next free_block.
   Not meaningful for right-most block. 
   This function should only be used when knowing that next block is a free_block*/
static struct free_block *next_blk(struct free_block *blk) {
    //assert(blk_size(blk) != 0);
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

/* Check if a tag is the FENCE tag */
static bool is_fence(void * tag) {
	return (((struct boundary_tag *)tag)->inuse != 0 && ((struct boundary_tag *)tag)->size == 0);
}

/* Return the block pointer with a given list_elem */
static struct free_block *get_blk(struct list_elem * e) {
	return (struct free_block *)((size_t *)e - sizeof(struct boundary_tag) / WSIZE);
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
   //TODO: GET RID OF THIS LINE! ONLY TO FOR TEST
    mm_check();
   //TODO: --------------------------------------
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
        bp = place(blk, awords);
        return bp->payload;
    }

    /* No fit found. Get more memory and place the block */
    extendwords = MAX(awords,CHUNKSIZE);
    if ((bp = (void *)extend_heap(extendwords)) == NULL)  
        return NULL;
    blk = bp;
    bp = place(blk, awords);
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
	if(segList[0].head.next == NULL)
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
	size_t osize = size;
	
	/* If the pointer block is NULL, realloc should be the same as malloc */
	if (ptr == NULL) 
		return mm_malloc(size);
		
	/* If the size if 0, free the block and return 0 */
	if (size == 0) {
		mm_free(ptr);
		return NULL;
	}
	
	size_t oldsize;
	size_t extendwords;
	
	struct used_block*oldblock = ptr - offsetof(struct used_block, payload);
    oldsize = oldblock->header.size;
	
	size += 2 * sizeof(struct boundary_tag);    			/* account for tags */
    size = (size + DSIZE - 1) & ~(DSIZE - 1);   			/* align to double word */
    size_t asize = MAX(MIN_BLOCK_SIZE_WORDS, size/WSIZE);	/* respect minimum size */
	
	// In the following four cases we can eliminate copying the payload	
	
	/* Case 1: new size is smaller than oldsize, split block and return ptr */
	if (asize <= oldsize) {
		if (oldsize - asize >= MIN_BLOCK_SIZE_WORDS) {
			mark_block_used(oldblock, asize);
			struct free_block *next_bp = (struct free_block *)((size_t *)oldblock + asize);
			mark_block_free(next_bp, oldsize - asize);
			insert(next_bp, blk_size(next_bp));
		}
		return ptr;
			
	}
	
	//Get next block
	struct free_block *next_bp = (struct free_block *)((size_t *)oldblock + oldsize);
	
	/* Case 2: ptr is the last block in the heap, extend the heap and coalesce mutually */
	if (is_fence(next_bp)) {
		extendwords = MAX(asize - oldsize,CHUNKSIZE);
		if ((next_bp = (void *)extend_heap(extendwords)) == NULL)  
			return NULL;
		list_remove(&next_bp->elem);
		mark_block_used(oldblock, oldsize + blk_size(next_bp));
		return ptr;
	}
	
	// If next block is free
	if (next_bp->header.inuse == 0) {
		
		/* Case 3: next block is a free block and have enough space to reallocate,
			coalesce two blocks mutually */
		if (asize <= oldsize + blk_size(next_bp)) {
			size_t next_size = blk_size(next_bp);
			if (oldsize + next_size - asize >= MIN_BLOCK_SIZE_WORDS) {
				list_remove(&next_bp->elem);
				mark_block_used(oldblock, asize);
				struct free_block * new_blk = (struct free_block *)((size_t *) oldblock + oldblock->header.size);
				mark_block_free(new_blk, oldsize + next_size - asize);
				insert(new_blk, blk_size(new_blk));
			}
			else {
				list_remove(&next_bp->elem);
				mark_block_used(oldblock, oldsize + next_size);
			}
			return ptr;	
		}
		
		/* Case 4: next block is a free block but do not have enough space to reallocate,
			but next block is the last block in the heap,
			extend the heap and coalesce two blocks mutually */
		else {
			if (is_fence(next_blk(next_bp))) {
				extendwords = MAX(asize - oldsize - blk_size(next_bp),CHUNKSIZE);
				if ((void *)extend_heap(extendwords) == NULL)  
					return NULL;
				list_remove(&next_bp->elem);
				mark_block_used(oldblock, oldsize + blk_size(next_bp));
				return ptr;
			}			
		}
	}
	
    void *newptr = mm_malloc(osize);

    /* If malloc() fails, the original block is left untouched  */
    if(!newptr) {
        return 0;
    }

    /* Copy the old data. */
    oldsize *= WSIZE;
    memcpy(newptr, ptr, oldsize);

    /* Free the old block. */
    mm_free(ptr);
    return newptr;
	
}

/*
 * find_fit - Find the first fit block from the list containing the block
 * 			  with size larger than requesting
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
			bp = get_blk(e);
			if (blk_size(bp) >= asize) return bp;
		}
	}
	return NULL;
}

/* 
 * place - Place block of asize words at start of free block bp 
 *         and split if remainder would be at least minimum block size
 */
static void *place(void *bp, size_t asize)
{
	void * new;
	//first get the block size
	size_t csize = blk_size(bp);
	//Check if there's "extra space" 
	if((csize - asize) >= MIN_BLOCK_SIZE_WORDS)
	{
		//Break up block, reducing fragmentation
		
		//Remove the original block, mark first part as used
		/*list_remove(&((struct free_block*)bp)->elem);		
		mark_block_used((struct used_block*)bp, asize);
		
		// Mark the remaind block as free and insert to appropriate list
		bp = ((size_t *)bp + ((struct used_block*)bp)->header.size);
		mark_block_free((struct free_block*)bp, csize-asize);		
		insert(bp, blk_size(bp));
		*/
		
		
		mark_block_free((struct free_block*)bp, csize-asize);
		new = ((size_t *)bp + ((struct free_block*)bp)->header.size);
		mark_block_used((struct used_block*)new, asize);
		return new;
	}
	else
	{
		//Just the right size
		list_remove(&((struct free_block*)bp)->elem);
		mark_block_used(bp, csize);
		return bp;
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

#ifdef CHECKHEAP
/* 
 * checkheap - Use helper methods to check heap consistency
 */
static int mm_check()  
{ 
	/* Check if every block in the free list is marked as free? */
	if (!check_list_mark()) return -1;
	
	/* Check if there are any contiguous free blocks that somehow escaped coalescing? */
	if (!check_coalescing()) return -1;
	
	/* Check if every free block actually is in the free list? */
	if (!check_inList()) return -1;
		
	/* Check if each block in the heap are back to back */
	if (!check_cont()) return -1;		
			
	return 0;
}

/* Check if every block in the free list is marked as free? */
static bool check_list_mark()
{
	int count = 0;
	for (; count < NLISTS; count++) {
		struct free_block *bp;
		if (!list_empty(&segList[count])) {
			struct list_elem * e = list_begin (&segList[count]);
			for (; e!= list_end (&segList[count]); e = list_next (e)) {
				bp = get_blk(e);
				if (bp->header.inuse != 0) return false;
			}
		}
	}
	return true;
}


/* Check if there are any contiguous free blocks that somehow escaped coalescing? */
static bool check_coalescing()
{
	int count = 0;
	for (; count < NLISTS; count++) {
		struct free_block *bp;
		if (!list_empty(&segList[count])) {
			struct list_elem * e = list_begin (&segList[count]);
			for (; e!= list_end (&segList[count]); e = list_next (e)) {
				bp = get_blk(e);
				bool prev_alloc = prev_blk_footer(bp)->inuse;
				bool next_alloc = next_blk_header(bp)->inuse;
				if (prev_alloc == false || 
					next_alloc == false) return false;
			}
		}
	}
	return true;
}

/* Check if every free block actually is in the free list? */
static bool check_inList()
{
	struct free_block * start = mem_heap_lo();
	struct free_block * n = (struct free_block *)((size_t *)start + 1);
	int count = 0;
	for (; !is_fence(n); n = (struct free_block *)((size_t *)n + blk_size(n)))
	{
		if (n->header.inuse == 0 )
		{
			 if (n->elem.prev == NULL 
				|| n->elem.next == NULL) return false;
		}
		count++;
	}
	return true;
}

/* Check if each block in the heap are back to back */
static bool check_cont()
{
	struct free_block * start = mem_heap_lo();
	struct free_block * n = (struct free_block *)((size_t *)start + 1);
	int count = 0;
	for (; !is_fence(n); n = (struct free_block *)((size_t *)n + blk_size(n)))
	{
		if (n->header.inuse != 0 &&
		 n->header.inuse != -1) return false;
		count++;
	}
	return true;
}

static bool valid_heap_address()
{
        //Andrew: UNDER CONSTRUCTION
        struct free_block * start = mem_heap_lo();
        struct free_block * end = mem_heap_hi();
        struct free_block * n = (struct free_block *)((size_t *)start + 1);
        int count = 0;
        for (; !is_fence(n); n = (struct free_block *)((size_t *)n + blk_size(n)))
        {
                //check if between addresses of low and high of heap
                if (n < start || n > end)
                {
					printf("Out of bounds\n");
                    return false;
                }
               count++;
        }
        return true;
}


#endif

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
	
	/* Insert to a list with the order of size */
	if (list_empty(&segList[count])) list_push_front(&segList[count], e);
	else {
		struct list_elem * el = list_begin (&segList[count]);
		size_t size = blk_size(get_blk(el));
		while (el!= list_end (&segList[count])) {
			if (asize <= size) {
				break;
			}
			el = list_next(el);
			size = blk_size(get_blk(el));
		}
		list_insert(el, e);
	}
	
}

#ifdef DEBUG

/* 
 * Helper print function for dubugging 
 */
static void print_list(struct list *elist, int n)
{
	struct free_block *bp;
	if (!list_empty(elist)) {
		struct list_elem * e = list_begin (elist);
		for (; e!= list_end (elist); e = list_next (e)) {
			bp = (struct free_block *)((size_t *)e - sizeof(struct boundary_tag) / WSIZE);
			printf("In segList[%d]: \n", n);
			printf("%s block at %p with size %d\n",
				(bp->header.inuse)?"Used":"Free", bp, bp->header.size);
		}
	}
}

/* 
 * Helper print function for dubugging 
 */
static void print_seg()
{
	int count = 0;
	for (; count < NLISTS; count++) {
		print_list(&segList[count], count);
	}
}

/* 
 * Helper print function for dubugging 
 */
static void print_heap()
{
	struct free_block * start = mem_heap_lo();
	struct free_block * n = (struct free_block *)((size_t *)start + 1);
	int count = 0;
	for (; !is_fence(n); n = (struct free_block *)((size_t *)n + blk_size(n)))
	{
		printf("%dth %s block with size %d\n", 
			count, (n->header.inuse)?"Used":"Free", blk_size(n));
		count++;
	}
	
}
#endif

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
