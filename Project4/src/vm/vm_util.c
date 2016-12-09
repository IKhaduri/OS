#include "vm_util.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "userprog/pagedir.h"
#include "threads/synch.h"

#define VM_UTIL_MAX_STACK_OFFSET 32
#define VM_MAX_STACK_SIZE (1024 * 1024 * 8)
#define VM_STACK_END (PHYS_BASE - VM_MAX_STACK_SIZE)

static struct list page_list;
static struct list_elem *page_elem;

static struct semaphore eviction_lock;

void vm_itil_init(void) {
	list_init(&page_list);
	sema_init(&eviction_lock, 1);
	page_elem = NULL;
}

void undo_suppl_page_registration(struct suppl_page *page) {
	sema_down(&eviction_lock);
	list_remove(&page->list_elem);
	sema_up(&eviction_lock);
}
void register_suppl_page(struct suppl_page *page) {
	if (page->kaddr != 0) {
		sema_down(&eviction_lock);
		list_push_front(&page_list, &page->list_elem);
		sema_up(&eviction_lock);
	}
}

bool stack_grow_needed(const void *addr, const void *esp) {
	return (is_user_vaddr(addr) && ((const char*)addr) >= ((const char*)esp - VM_UTIL_MAX_STACK_OFFSET) && ((uint32_t)addr) >= ((uint32_t)VM_STACK_END));
}

#define EVICTION_GET_PAGE struct suppl_page *page = list_entry(page_elem, struct suppl_page, list_elem); ASSERT(page != NULL)
#define EVICTION_GET_PAGE_TYPE bool modified = suppl_page_dirty(page); bool referenced = suppl_page_accessed(page)
#define EVICTION_EVICT_PAGE \
	pagedir_clear_page(page->pagedir, (void*)page->vaddr); \
	palloc_free_page((void*)page->kaddr); \
	page->kaddr = 0; \
	page_elem = list_next(page_elem); \
	if (page_elem == list_end(&page_list)) \
		page_elem = NULL; \
	list_remove(&page->list_elem); \
	/*printf("page evicted (addr: %u)\n", ((uint32_t)page->vaddr / PAGE_SIZE)); */ \
	sema_up(&eviction_lock); \
	return true
#define EVICTION_MOVE_TO_SWAP \
	swap_page spage = swap_get_page(); \
	if (spage == SWAP_NO_PAGE){ \
		sema_up(&eviction_lock); \
		return false; \
	} \
	swap_load_page_to_swap(spage, (void*)page->vaddr); \
	page->saddr = spage; \
	page->location = PG_LOCATION_SWAP; \
	/*printf("page->saddr = %d; page->vaddr = %d\n", (int)page->saddr, (int)page->vaddr); /* */
#define EVICTION_EVICT_NOT_MODIFIED \
	/*printf("NOT MODIFIED....\n"); /* */\
	if (page->mapping != NULL && page->mapping->fl_writable) \
		page->location = PG_LOCATION_FILE; \
	else{ \
		EVICTION_MOVE_TO_SWAP; \
	} \
	EVICTION_EVICT_PAGE
#define EVICTION_EVICT_MODIFIED \
	/*printf("MODIFIED....\n"); /* */\
	if (page->mapping != NULL && page->mapping->fl_writable) { \
		suppl_page_load_to_file(page); \
		page->location = PG_LOCATION_FILE; \
	} \
	else { \
		EVICTION_MOVE_TO_SWAP; \
	} \
	EVICTION_EVICT_PAGE
#define EVICTION_MOVE_TO_NEXT page_elem = list_next(page_elem); if (page_elem == list_end(&page_list)) page_elem = list_begin(&page_list); if (page_elem == terminal) break

static bool evict_page(void) {
	sema_down(&eviction_lock);
	if (list_empty(&page_list)) {
		sema_up(&eviction_lock);
		return false;
	}
	//PANIC("########################## WILLING TO EVICT ###################################\n");
	if (page_elem == NULL || page_elem == list_end(&page_list))
		page_elem = list_begin(&page_list);
	struct list_elem *terminal = page_elem;
	while (true) {
		EVICTION_GET_PAGE;
		EVICTION_GET_PAGE_TYPE;
		if ((!referenced) && (!modified)) {
			//printf("############### THROWING THE PAGE AWAY ###########################\n");
			/*EVICTION_EVICT_MODIFIED; /*/ EVICTION_EVICT_NOT_MODIFIED; //*/
		}
		EVICTION_MOVE_TO_NEXT;
	}
	while (true){
		EVICTION_GET_PAGE;
		EVICTION_GET_PAGE_TYPE;
		if ((!referenced) && (modified)) {
			//PANIC("############### EVICTING THE PAGE ###########################\n");
			EVICTION_EVICT_MODIFIED;
		}
		EVICTION_MOVE_TO_NEXT;
	}
	while (true) {
		EVICTION_GET_PAGE;
		bool modified = suppl_page_dirty(page);
		if ((!modified)) {
			//PANIC("############### THROWING THE PAGE AWAY ###########################\n");
			/*EVICTION_EVICT_MODIFIED; /*/ EVICTION_EVICT_NOT_MODIFIED; //*/
		}
		EVICTION_MOVE_TO_NEXT;
	}
	EVICTION_GET_PAGE;
	//PANIC("############### EVICTING THE PAGE ###########################\n");
	EVICTION_EVICT_MODIFIED;
	//PANIC("########################### EVICTION NEEDED ###########################\n");
	//return false;
}

#undef EVICTION_GET_PAGE
#undef EVICTION_GET_PAGE_TYPE
#undef EVICTION_MOVE_TO_SWAP
#undef EVICTION_EVICT_PAGE
#undef EVICTION_EVICT_NOT_MODIFIED
#undef EVICTION_MOVE_TO_NEXT

void *evict_and_get_kaddr(void) {
	//printf("evicting...\n");
	if (!evict_page()) return NULL;
	void* kpage = palloc_get_page(PAL_USER | PAL_ZERO);
	//if (kpage == NULL) PANIC("ERROR");
	//else printf("ok...\n");
	return kpage;
}



bool restore_page_from_swap(struct suppl_page *page) {
	//PANIC("################### RESTORING ######################\n");
	sema_down(&eviction_lock);
	//printf("Restoring...\n");
	ASSERT(page->location == PG_LOCATION_SWAP && page->saddr != SWAP_NO_PAGE);
	void* kpage = palloc_get_page(PAL_USER | PAL_ZERO);
	if (kpage == NULL) {
		sema_up(&eviction_lock);
		kpage = evict_and_get_kaddr();
		sema_down(&eviction_lock);
		if (kpage == NULL) {
			sema_up(&eviction_lock);
			return false;
		}
	}
	if (!pagedir_set_page(page->pagedir, (void*)page->vaddr, kpage, true)) {
		palloc_free_page(kpage);
		sema_up(&eviction_lock);
		return false;
	}
	swap_load_page_to_ram(page->saddr, (void*)page->vaddr);
	swap_free_page(page->saddr);
	page->saddr = SWAP_NO_PAGE;
	page->kaddr = ((uint32_t)kpage);
	page->location = PG_LOCATION_RAM;
	sema_up(&eviction_lock);
	register_suppl_page(page);
	//printf("done...\n");
	return true;
}
