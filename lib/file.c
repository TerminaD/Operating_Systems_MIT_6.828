#include <inc/fs.h>
#include <inc/string.h>
#include <inc/lib.h>

#define debug 0		// TODO Debug

union Fsipc fsipcbuf __attribute__((aligned(PGSIZE)));

// Send an inter-environment request to the file server, and wait for
// a reply.  The request body should be in fsipcbuf, and parts of the
// response may be written back to fsipcbuf.
// type: request code, passed as the simple integer IPC value.
// dstva: virtual address at which to receive reply page, 0 if none.
// Returns result from the file server.
static int
fsipc(unsigned type, void *dstva)
{
	static envid_t fsenv;
	if (fsenv == 0)
		fsenv = ipc_find_env(ENV_TYPE_FS);

	static_assert(sizeof(fsipcbuf) == PGSIZE);

	if (debug) {
		if (type == 1)	// Open
			cprintf("[%08x] fsipc open %s, %d\n", thisenv->env_id, fsipcbuf.open.req_path, fsipcbuf.open.req_omode);
		else
			cprintf("[%08x] fsipc %d %08x\n", thisenv->env_id, type, *(uint32_t *)&fsipcbuf);
	}


	ipc_send(fsenv, type, &fsipcbuf, PTE_P | PTE_W | PTE_U);
	// cprintf("[%08x] ipc successfully sent\n", thisenv->env_id);

	int32_t ret = ipc_recv(NULL, dstva, NULL);
	// cprintf("[%08x] ipc successfully received\n", thisenv->env_id);
	return ret;
}

static int devfile_flush(struct Fd *fd);
static ssize_t devfile_read(struct Fd *fd, void *buf, size_t n);
static ssize_t devfile_write(struct Fd *fd, const void *buf, size_t n);
static int devfile_stat(struct Fd *fd, struct Stat *stat);
static int devfile_trunc(struct Fd *fd, off_t newsize);

struct Dev devfile =
{
	.dev_id =	'f',
	.dev_name =	"file",
	.dev_read =	devfile_read,
	.dev_close =	devfile_flush,
	.dev_stat =	devfile_stat,
	.dev_write =	devfile_write,
	.dev_trunc =	devfile_trunc
};

// Open a file (or directory).
//
// Returns:
// 	The file descriptor index on success
// 	-E_BAD_PATH if the path is too long (>= MAXPATHLEN)
// 	< 0 for other errors.
int
open(const char *path, int mode)
{
	// cprintf("[%08x][open]\n", thisenv->env_id);
	// Find an unused file descriptor page using fd_alloc.
	// Then send a file-open request to the file server.
	// Include 'path' and 'omode' in request,
	// and map the returned file descriptor page
	// at the appropriate fd address.
	// FSREQ_OPEN returns 0 on success, < 0 on failure.
	//
	// (fd_alloc does not allocate a page, it just returns an
	// unused fd address.  Do you need to allocate a page?)
	//
	// Return the file descriptor index.
	// If any step after fd_alloc fails, use fd_close to free the
	// file descriptor.
	cprintf("[open] path: %s\n", path);
	int r;
	struct Fd *fd;

	if (strlen(path) >= MAXPATHLEN)
		return -E_BAD_PATH;

	if ((r = fd_alloc(&fd)) < 0)
		return r;

	// for (int i = 0; i < 1024; i++)
	// 	fsipcbuf.open.req_path[i] = 0;
	strcpy(fsipcbuf.open.req_path, path);
	cprintf("[%08x][open] new path in fsipcbuf: %s\n", thisenv->env_id, fsipcbuf.open.req_path);
	fsipcbuf.open.req_omode = mode;

	if ((r = fsipc(FSREQ_OPEN, fd)) < 0) {
		// cprintf("[%08x][open] fsipc failed\n", thisenv->env_id);
		fd_close(fd, 0);
		return r;
	}
	// cprintf("[%08x][open] fsipc successful\n", thisenv->env_id);

	return fd2num(fd);
}

// Flush the file descriptor.  After this the fileid is invalid.
//
// This function is called by fd_close.  fd_close will take care of
// unmapping the FD page from this environment.  Since the server uses
// the reference counts on the FD pages to detect which files are
// open, unmapping it is enough to free up server-side resources.
// Other than that, we just have to make sure our changes are flushed
// to disk.
static int
devfile_flush(struct Fd *fd)
{
	fsipcbuf.flush.req_fileid = fd->fd_file.id;
	return fsipc(FSREQ_FLUSH, NULL);
}

// Read at most 'n' bytes from 'fd' at the current position into 'buf'.
//
// Returns:
// 	The number of bytes successfully read.
// 	< 0 on error.
static ssize_t
devfile_read(struct Fd *fd, void *buf, size_t n)
{
	// Make an FSREQ_READ request to the file system server after
	// filling fsipcbuf.read with the request arguments.  The
	// bytes read will be written back to fsipcbuf by the file
	// system server.

	int r;

	fsipcbuf.read.req_fileid = fd->fd_file.id;
	fsipcbuf.read.req_n = n;
	if ((r = fsipc(FSREQ_READ, NULL)) < 0)
		return r;
	assert(r <= n);
	assert(r <= PGSIZE);
	memmove(buf, fsipcbuf.readRet.ret_buf, r);
	return r;
}


// Write at most 'n' bytes from 'buf' to 'fd' at the current seek position.
//
// Returns:
//	 The number of bytes successfully written.
//	 < 0 on error.
static ssize_t
devfile_write(struct Fd *fd, const void *buf, size_t n)
{
	// Make an FSREQ_WRITE request to the file system server.  Be
	// careful: fsipcbuf.write.req_buf is only so large, but
	// remember that write is always allowed to write *fewer*
	// bytes than requested.
	// LAB 5: Your code here
	// TODO

	// cprintf("[devfile_write]\n");

	int r;
	int max_buf_size = PGSIZE - (sizeof(int) + sizeof(size_t));

	fsipcbuf.write.req_fileid = fd->fd_file.id;
	fsipcbuf.write.req_n = ((n > max_buf_size) ? max_buf_size : n);
	memcpy(fsipcbuf.write.req_buf, buf, fsipcbuf.write.req_n);

	if ((r = fsipc(FSREQ_WRITE, NULL)) < 0)
		return r;

	assert(r <= n);
	assert(r <= PGSIZE);

	// cprintf("[devfile_write] return value: 0x%x\n", r);
	return r;
}

static int
devfile_stat(struct Fd *fd, struct Stat *st)
{
	int r;

	fsipcbuf.stat.req_fileid = fd->fd_file.id;
	if ((r = fsipc(FSREQ_STAT, NULL)) < 0)
		return r;
	strcpy(st->st_name, fsipcbuf.statRet.ret_name);
	st->st_size = fsipcbuf.statRet.ret_size;
	st->st_isdir = fsipcbuf.statRet.ret_isdir;
	return 0;
}

// Truncate or extend an open file to 'size' bytes
static int
devfile_trunc(struct Fd *fd, off_t newsize)
{
	fsipcbuf.set_size.req_fileid = fd->fd_file.id;
	fsipcbuf.set_size.req_size = newsize;
	return fsipc(FSREQ_SET_SIZE, NULL);
}


// Synchronize disk with buffer cache
int
sync(void)
{
	// Ask the file server to update the disk
	// by writing any dirty blocks in the buffer cache.

	return fsipc(FSREQ_SYNC, NULL);
}

