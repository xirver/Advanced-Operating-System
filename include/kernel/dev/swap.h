#include <paging.h> 
#include <spinlock.h> 

struct swap_info {
    struct list pages;
    struct spinlock lock;
};

struct clock_info {
	int accessed;
	physaddr_t pa;
};

struct rmap_info {
    struct page_info *page;
	physaddr_t disk_addr;
	physaddr_t pa;
};

void mru_swap_page(struct page_info *page);
void add_swap_page(struct page_info *page);
void remove_swap_page(struct page_info *page);
void initialize_swap_list(void);
void swap_thread(void);