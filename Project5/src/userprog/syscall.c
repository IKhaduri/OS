#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "userprog/process.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/input.h"
#ifdef VM
#include "vm/file_mapping.h"
#include "vm/vm_util.h"
#endif


#define max(a, b) (((a) > (b)) ? (a) : (b))
#define min(a, b) (((a) < (b)) ? (a) : (b))

struct lock filesys_lock;

// Locks the file system
void filesys_lock_acquire(void) {
	lock_acquire(&filesys_lock);
}
// Unlocks the file system
void filesys_lock_release(void) {
	lock_release(&filesys_lock);
}

#ifdef VM
// Checks, if the user address is mappable
static int user_address_mappable(void *addr, unsigned int size) {
	char *ptr = ((char*)addr);
	while (size > 0) {
		if (ptr == NULL || (!is_user_vaddr((uint32_t*)ptr))) return false;
		if (addr_in_stack_range((const void*)ptr)) return false;
		size--;
		ptr++;
	}
	return true;
}
#endif

// Checks, if the user address is valid (by making sure, it's below PHYS_BASE and it's already mapped/allocated)
static int user_address_valid(void *addr) {
	struct thread *cur = thread_current();
	if (addr == NULL || (!is_user_vaddr((uint32_t*)addr))) return false;
	else if (pagedir_get_page(cur->pagedir, (uint32_t *)addr) != NULL) return true;
	else {
#ifdef VM
		if (suppl_pt_lookup(cur->suppl_page_table, addr) == NULL)
			if (stack_grow_needed((const void*)addr, (const void*)(cur->intr_stack)))
				if (suppl_table_alloc_user_page(cur, addr, true)) return true;
#endif
		return false;
	}
}

// Checks, if the given number of pointers in a row are valid (returns 0 even if one of them is not)
static int pointers_valid(const void *address, uint32_t count) {
	const char *addr = (const char*)address;
	uint32_t i;
	for (i = 0; i < count; i++)
		if (!user_address_valid((void*)(addr + i))) return 0;
	return 1;
}

#ifdef VM
// Checks, if pointers are all writable
static bool pointers_writable(const void *address, uint32_t count) {
	if (count % PAGE_SIZE != 0)
		count += PAGE_SIZE - (count % PAGE_SIZE);
	struct thread *cur = thread_current();
	const char *addr = (const char*)address;
	uint32_t i;
	for (i = 0; i < count; i += PAGE_SIZE) {
		void *dst = (void*)(addr + i);
		struct suppl_page *page = suppl_pt_lookup(cur->suppl_page_table, dst);
		if (page == NULL) continue;
		if (page->mapping != NULL && (!page->mapping->writable)) return false;
	}
	return true;
}
#endif

// Checks, if the given string is valid, by moving along it and verifying every single byte, stopping at '\0'.
static bool string_valid(const char *address) {
	while (true) {
		if (!user_address_valid((void*)address)) return false;
		if ((*address) == '\0') return true;
		address++;
	}
}




/**
Terminates Pintos by calling shutdown_power_off() (declared in
�devices/shutdown.h�). This should be seldom used, because you lose
some information about possible deadlock situations, etc.
*/
static void halt(void) {
	shutdown_power_off();
}

/**
Terminates the current user program, returning status to the kernel. If the process�s
parent waits for it (see below), this is the status that will be returned. Conventionally,
a status of 0 indicates success and nonzero values indicate errors.
*/
static void exit(int status) {
	thread_current()->exit_status = status;
	thread_exit();
}





/**
Runs the executable whose name is given in cmd line, passing any given arguments,
and returns the new process�s program id (pid). Must return pid -1, which otherwise
should not be a valid pid, if the program cannot load or run for any reason. Thus,
the parent process cannot return from the exec until it knows whether the child
process successfully loaded its executable. You must use appropriate synchronization
to ensure this.
*/
static pid_t exec(const char *cmd_line) {
	if (!string_valid(cmd_line)) exit(-1);
	else {
		pid_t p = process_execute(cmd_line);
		if (p != TID_ERROR) {
			struct thread *cur_thread = thread_current();
			struct thread *child = get_child_by_pid(cur_thread, p);
			if (child == NULL) {
				return -1;
			}
			else {
				sema_down(&child->load_lock);
				if (!child->load_status) {
					return -1;
				}
			}
			return p;
		}
		else return -1;
	}
	return -1;
}


/**
Waits for a child process pid and retrieves the child�s exit status.
If pid is still alive, waits until it terminates. Then, returns the status that pid passed
to exit. If pid did not call exit(), but was terminated by the kernel (e.g. killed due
to an exception), wait(pid) must return -1. It is perfectly legal for a parent process
to wait for child processes that have already terminated by the time the parent calls
wait, but the kernel must still allow the parent to retrieve its child�s exit status, or
learn that the child was terminated by the kernel.
wait must fail and return -1 immediately if any of the following conditions is true:
	� pid does not refer to a direct child of the calling process. pid is a direct child
	of the calling process if and only if the calling process received pid as a return
	value from a successful call to exec.
	Note that children are not inherited: if A spawns child B and B spawns child
	process C, then A cannot wait for C, even if B is dead. A call to wait(C) by
	process A must fail. Similarly, orphaned processes are not assigned to a new
	parent if their parent process exits before they do.
	� The process that calls wait has already called wait on pid. That is, a process
	may wait for any given child at most once.
Processes may spawn any number of children, wait for them in any order, and may
even exit without having waited for some or all of their children. Your design should
consider all the ways in which waits can occur. All of a process�s resources, including
its struct thread, must be freed whether its parent ever waits for it or not, and
regardless of whether the child exits before or after its parent.
You must ensure that Pintos does not terminate until the initial process exits.
The supplied Pintos code tries to do this by calling process_wait() (in
�userprog/process.c�) from main() (in �threads/init.c�). We suggest that you
implement process_wait() according to the comment at the top of the function
and then implement the wait system call in terms of process_wait().
Implementing this system call requires considerably more work than any of the rest.
*/
static int wait(pid_t pid) {
	return process_wait(pid);
}


/**
Creates a new file called file initially initial size bytes in size. Returns true if successful,
false otherwise. Creating a new file does not open it: opening the new file is
a separate operation which would require a open system call.
*/
static bool create(const char *file, unsigned initial_size) {
	if (!string_valid(file)) exit(-1);
	else {
		lock_acquire(&filesys_lock);
		bool rv = filesys_create(file, initial_size);
		lock_release(&filesys_lock);
		return rv;
	}
	return false;
}


/**
Deletes the file called file. Returns true if successful, false otherwise. A file may be
removed regardless of whether it is open or closed, and removing an open file does
not close it. See [Removing an Open File], page 35, for details.
*/
static bool remove(const char *file) {
	if (!string_valid(file)) exit(-1);
	else {
		lock_acquire(&filesys_lock);
		bool rv = filesys_remove(file);
		lock_release(&filesys_lock);
		return rv;
	}
	return false;
}


/**
Opens the file called file. Returns a nonnegative integer handle called a �file descriptor�
(fd), or -1 if the file could not be opened.
File descriptors numbered 0 and 1 are reserved for the console: fd 0 (STDIN_FILENO) is
standard input, fd 1 (STDOUT_FILENO) is standard output. The open system call will
never return either of these file descriptors, which are valid as system call arguments
only as explicitly described below.
Each process has an independent set of file descriptors. File descriptors are not
inherited by child processes.
When a single file is opened more than once, whether by a single process or different
processes, each open returns a new file descriptor. Different file descriptors for a single
file are closed independently in separate calls to close and they do not share a file
position.
*/
static int open(const char *file) {
	if (!string_valid(file)) exit(-1);
	else {
		lock_acquire(&filesys_lock);

		struct thread *this_thread = thread_current();
		
		file_descriptor fd = thread_get_free_fd(this_thread);
		if (fd >= 0) {
			struct file *opened_file = filesys_open(file);
			if (opened_file != NULL) {
				if (!thread_set_file(this_thread, opened_file, fd)) {
					file_close(opened_file);
					fd = -1;
				}
			}
			else {
				fd = -1;
			}
		}

		lock_release(&filesys_lock);
		
		return fd;
	}
	return -1;
}


/**
Returns the size, in bytes, of the file open as fd.
*/
static int filesize(int fd) {
	lock_acquire(&filesys_lock);
	struct file *file_ptr = thread_get_file(thread_current(), fd);
	int rv = ((file_ptr != NULL) ? file_length(file_ptr) : (-1));
	lock_release(&filesys_lock);
	return rv;
}


/**
Reads size bytes from the file open as fd into buffer. Returns the number of bytes
actually read (0 at end of file), or -1 if the file could not be read (due to a condition
other than end of file). Fd 0 reads from the keyboard using input_getc().
*/
static int read(int fd, void *buffer, unsigned size) {
	if (!pointers_valid(buffer, size)) exit(-1);
#ifdef VM
	if (!pointers_writable(buffer, size)) exit(-1);
#endif
	else if (fd == STDIN_FILENO) {
		// Read from standard input
		unsigned int i;
		char *addr = buffer;
		for (i = 0; i < size; ++i)
			addr[i] = input_getc();
		return size;
	}
	else if (fd == STDOUT_FILENO) return 0;
	else {
		lock_acquire(&filesys_lock);
		struct file *file_ptr = thread_get_file(thread_current(), fd);
		int rv = ((file_ptr != NULL) ? file_read(file_ptr, (void*)buffer, size) : 0);
		lock_release(&filesys_lock);
		return rv;
	}
	return 0;
}


/**
Writes size bytes from buffer to the open file fd. Returns the number of bytes actually
written, which may be less than size if some bytes could not be written.
Writing past end-of-file would normally extend the file, but file growth is not implemented
by the basic file system. The expected behavior is to write as many bytes as
possible up to end-of-file and return the actual number written, or 0 if no bytes could
be written at all.
Fd 1 writes to the console. Your code to write to the console should write all of buffer
in one call to putbuf(), at least as long as size is not bigger than a few hundred
bytes. (It is reasonable to break up larger buffers.) Otherwise, lines of text output
by different processes may end up interleaved on the console, confusing both human
readers and our grading scripts.
*/
#define CHUNCK_SIZE 100  // 100 bytes per chunck
static int write(int fd, const void *buffer, unsigned size) {
	if (!pointers_valid(buffer, size)) exit(-1);
	else if (fd == STDOUT_FILENO) {
		// Write to standard output by chuncks of CHUCK_SIZE
		const char *addr = buffer;
		unsigned int rem_size = size;
		while (rem_size > 0) {
			unsigned int to_write = min(CHUNCK_SIZE, rem_size);
			putbuf(addr, to_write);
			rem_size -= to_write;
			addr += to_write;
		}
		return size;
	}
	else if (fd == STDIN_FILENO) return 0;
	else {
		lock_acquire(&filesys_lock);
		struct file *file_ptr = thread_get_file(thread_current(), fd);
		int rv = ((file_ptr != NULL) ? file_write(file_ptr, (void*)buffer, size) : 0);
		lock_release(&filesys_lock);
		return rv;
	}
	return 0;
}


/**
Changes the next byte to be read or written in open file fd to position, expressed in
bytes from the beginning of the file. (Thus, a position of 0 is the file�s start.)
A seek past the current end of a file is not an error. A later read obtains 0 bytes,
indicating end of file. A later write extends the file, filling any unwritten gap with
zeros. (However, in Pintos files have a fixed length until project 4 is complete, so
writes past end of file will return an error.) These semantics are implemented in the
file system and do not require any special effort in system call implementation.
*/
static void seek(int fd, unsigned position) {
	lock_acquire(&filesys_lock);

	struct file *file_ptr = thread_get_file(thread_current(), fd);

	if (file_ptr != NULL)
		file_seek(file_ptr, position);

	lock_release(&filesys_lock);
}


/**
Returns the position of the next byte to be read or written in open file fd, expressed
in bytes from the beginning of the file.
*/
static unsigned tell(int fd) {
	lock_acquire(&filesys_lock);

	struct file *file_ptr = thread_get_file(thread_current(), fd);
	int rv = ((file_ptr != NULL) ? file_tell(file_ptr) : 0);
	
	lock_release(&filesys_lock);

	return rv;
}


/**
Closes file descriptor fd. Exiting or terminating a process implicitly closes all its open
file descriptors, as if by calling this function for each one.
*/
static int close(int fd) {
	int rv = -1;
	struct thread *t = thread_current();
	struct file *fl = thread_get_file(t, fd);
	if (fl != NULL) {
		thread_set_file_force(t, NULL, fd); // File is closed automatically inside of this function.
		rv = 0;
	}
	return rv;
}

#ifdef VM
/**
Maps the given file descriptor to the given virtual address
*/
static int mmap(int fd, void *vaddr) {
	struct thread *t = thread_current();
	struct file *fl = thread_get_file(t, fd);
	if (fl == NULL) return (-1);
	uint32_t file_sz = (uint32_t)filesize(fd);
	//printf("File size: %d\n", file_sz);
	if (!user_address_mappable(vaddr, (int)file_sz)) return (-1);
	return file_mappings_map(t, fl, vaddr, 0, file_sz, 0, true, true);
}
/**
Unmaps given file mapping
*/
static int munmap(int map_id) {
	return file_mappings_unmap(thread_current(), map_id);
}
#endif


#ifdef FILESYS
/**
Changes the current working directory of the process to dir, which may be relative
or absolute. Returns true if successful, false on failure.
*/
static bool chdir(const char *dir UNUSED) {
	return true;
}

/**
Creates the directory named dir, which may be relative or absolute. Returns true if
successful, false on failure. Fails if dir already exists or if any directory name in dir,
besides the last, does not already exist. That is, mkdir("/a/b/c") succeeds only if
�/a/b� already exists and �/a/b/c� does not.
*/
static bool mkdir(const char *dir UNUSED) {
	return true;
}

/**
Reads a directory entry from file descriptor fd, which must represent a directory. If
successful, stores the null-terminated file name in name, which must have room for
READDIR_MAX_LEN + 1 bytes, and returns true. If no entries are left in the directory,
returns false.
�.� and �..� should not be returned by readdir.
If the directory changes while it is open, then it is acceptable for some entries not to
be read at all or to be read multiple times. Otherwise, each directory entry should
be read once, in any order.
READDIR_MAX_LEN is defined in �lib/user/syscall.h�. If your file system supports
longer file names than the basic file system, you should increase this value from the
default of 14.
*/
static bool readdir(int fd UNUSED, char *name UNUSED) {
	return true;
}

/**
Returns true if fd represents a directory, false if it represents an ordinary file.
*/
static bool isdir(int fd UNUSED) {
	return true;
}

/**
Returns the inode number of the inode associated with fd, which may represent an
ordinary file or a directory.
An inode number persistently identifies a file or directory. It is unique during the
file�s existence. In Pintos, the sector number of the inode is suitable for use as an
inode number.
*/
static int inumber(int fd UNUSED) {
	return -1;
}

#endif






#define ESP ((uint32_t*)f->esp)
#define PARAM(id) (ESP + id)
#define I_PARAM(id) (*PARAM(id))
#define S_PARAM(id) ((char*)I_PARAM(id))
#define V_PARAM(id) ((void*)I_PARAM(id))
#define EAX (f->eax)

static int check_args(struct intr_frame *f, int start, int end) {
	while (start < end) {
		if (!pointers_valid(PARAM(start), sizeof(void*))) return 0;
		start++;
	}
	return 1;
}

static void halt_handler(struct intr_frame *f UNUSED) {
	halt();
}
static void exit_handler(struct intr_frame *f) {
	if (!check_args(f, 1, 2)) exit(-1);
	else exit(I_PARAM(1));
}
static void exec_handler(struct intr_frame *f) {
	if (!check_args(f, 1, 2)) exit(-1);
	else EAX = exec(S_PARAM(1));
}
static void wait_handler(struct intr_frame *f) {
	if (!check_args(f, 1, 2)) exit(-1);
	else EAX = wait(I_PARAM(1));
}
static void create_handler(struct intr_frame *f) {
	if (!check_args(f, 1, 3)) exit(-1);
	else EAX = create(S_PARAM(1), I_PARAM(2));
}
static void remove_handler(struct intr_frame *f) {
	if (!check_args(f, 1, 2)) exit(-1);
	else EAX = remove(S_PARAM(1));
}
static void open_handler(struct intr_frame *f) {
	if (!check_args(f, 1, 2)) exit(-1);
	else EAX = open(S_PARAM(1));
}
static void filesize_handler(struct intr_frame *f) {
	if (!check_args(f, 1, 2)) exit(-1);
	else EAX = filesize(I_PARAM(1));
}
static void read_handler(struct intr_frame *f) {
	if (!check_args(f, 1, 4)) exit(-1);
	else EAX = read(I_PARAM(1), V_PARAM(2), I_PARAM(3));
}
static void write_handler(struct intr_frame *f) {
	if (!check_args(f, 1, 4)) exit(-1);
	else EAX = write(I_PARAM(1), V_PARAM(2), I_PARAM(3));
}
static void seek_handler(struct intr_frame *f) {
	if (!check_args(f, 1, 3)) exit(-1);
	else seek(I_PARAM(1), I_PARAM(2));
}
static void tell_handler(struct intr_frame *f) {
	if (!check_args(f, 1, 2)) exit(-1);
	else EAX = tell(I_PARAM(1));
}
static void close_handler(struct intr_frame *f) {
	if (!check_args(f, 1, 2)) exit(-1);
	else EAX = close(I_PARAM(1));
}
#ifdef VM
static void mmap_handler(struct intr_frame *f) {
	if (!check_args(f, 1, 3)) exit(-1);
	else EAX = mmap(I_PARAM(1), V_PARAM(2));
}
static void munmap_handler(struct intr_frame *f) {
	if (!check_args(f, 1, 2)) exit(-1);
	else EAX = munmap(I_PARAM(1));
}
#endif
#ifdef FILESYS
static void chdir_handler(struct intr_frame *f) {
	if (!check_args(f, 1, 2)) exit(-1);
	else EAX = chdir(S_PARAM(1));
}
static void mkdir_handler(struct intr_frame *f) {
	if (!check_args(f, 1, 2)) exit(-1);
	else EAX = mkdir(S_PARAM(1));
}
static void readdir_handler(struct intr_frame *f) {
	if (!check_args(f, 1, 3)) exit(-1);
	else EAX = readdir(I_PARAM(1), S_PARAM(2));
}
static void isdir_handler(struct intr_frame *f) {
	if (!check_args(f, 1, 2)) exit(-1);
	else EAX = isdir(I_PARAM(1));
}
static void inumber_handler(struct intr_frame *f) {
	if (!check_args(f, 1, 2)) exit(-1);
	else EAX = inumber(I_PARAM(1));
}
#endif

#define MAX_SYS_CALL_ID \
				max( \
					max( \
						max( \
							max(\
								max(SYS_HALT, SYS_EXIT), \
								max(SYS_EXEC, SYS_WAIT) \
							), \
							max( \
								max(SYS_CREATE, SYS_REMOVE), \
								max(SYS_OPEN, SYS_FILESIZE) \
							) \
						), \
						max( \
							max( \
								max(SYS_READ, SYS_WRITE), \
								max(SYS_SEEK, SYS_TELL) \
							), \
							max( \
								max(SYS_CLOSE, SYS_MMAP), \
								max(SYS_MUNMAP, SYS_CHDIR) \
							) \
						) \
					), \
					max( \
						max(SYS_MKDIR, SYS_READDIR), \
						max(SYS_ISDIR, SYS_INUMBER) \
					) \
				)

#define SYS_COUNT (MAX_SYS_CALL_ID + 1)
static const int sys_count = SYS_COUNT;
typedef void(*sys_handler)(struct intr_frame*);
static sys_handler sys_handlers[SYS_COUNT];
static bool sys_initialized = false;

static void init_sys_handlers(void) {
	if ((!sys_initialized) || (sys_handlers[SYS_HALT] != halt_handler)) {
		int i;
		for (i = 0; i < sys_count; i++)
			sys_handlers[i] = NULL;
		sys_handlers[SYS_HALT] = halt_handler;
		sys_handlers[SYS_EXIT] = exit_handler;
		sys_handlers[SYS_EXEC] = exec_handler;
		sys_handlers[SYS_WAIT] = wait_handler;
		sys_handlers[SYS_CREATE] = create_handler;
		sys_handlers[SYS_REMOVE] = remove_handler;
		sys_handlers[SYS_OPEN] = open_handler;
		sys_handlers[SYS_FILESIZE] = filesize_handler;
		sys_handlers[SYS_READ] = read_handler;
		sys_handlers[SYS_WRITE] = write_handler;
		sys_handlers[SYS_SEEK] = seek_handler;
		sys_handlers[SYS_TELL] = tell_handler;
		sys_handlers[SYS_CLOSE] = close_handler;
#ifdef VM
		sys_handlers[SYS_MMAP] = mmap_handler;
		sys_handlers[SYS_MUNMAP] = munmap_handler;
#endif
#ifdef FILESYS
		sys_handlers[SYS_CHDIR] = chdir_handler;
		sys_handlers[SYS_MKDIR] = mkdir_handler;
		sys_handlers[SYS_READDIR] = readdir_handler;
		sys_handlers[SYS_ISDIR] = isdir_handler;
		sys_handlers[SYS_INUMBER] = inumber_handler;
#endif
		sys_initialized = true;
	}
}


static void syscall_handler(struct intr_frame *);

void
syscall_init(void)
{
	intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
	init_sys_handlers();
	lock_init(&filesys_lock);
}

static void
syscall_handler(struct intr_frame *f)
{
#ifdef VM
	struct thread *cur = thread_current();
	cur->intr_stack = (uint8_t*)f->esp;
#endif
	if (!pointers_valid(ESP, sizeof(int))) exit(-1);
	init_sys_handlers();
	int syscall_id = *ESP;
	if (syscall_id >= 0 && syscall_id < sys_count && sys_handlers[syscall_id] != NULL) {
		sys_handlers[syscall_id](f);
	}
}