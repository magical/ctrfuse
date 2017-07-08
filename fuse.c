#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#define FUSE_USE_VERSION 26
#include <fuse.h>

#include "ncsd.h"

int getsize(ncsd_context *ctx) {
	char *buf;
	size_t bufsize;
	FILE *stream = open_memstream(&buf, &bufsize);
	if (stream == NULL) {
		perror("open_memstream");
		return 0;
	}

	ncsd_print(ctx, stream);

	if (fclose(stream) < 0) {
		perror("fclose");
		return 0;
	}
	free(buf);

	return bufsize;
}

int ctrfuse_getattr(const char *path, struct stat *stbuf)
{
	ncsd_context *ctx = (ncsd_context *)(fuse_get_context()->private_data);
	if (strcmp(path, "/") == 0 || strcmp(path, "/romfs") == 0 || strcmp(path, "/exefs") == 0) {
		stbuf->st_nlink = 2;
		stbuf->st_mode = S_IFDIR | 0555;
	} else {
		stbuf->st_nlink = 1;
		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_size = getsize(ctx);
	}
	return 0;
}

int ctrfuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	if (strcmp(path, "/") == 0) {
		filler(buf, "info", NULL, 0);
		filler(buf, "exefs", NULL, 0);
		filler(buf, "romfs", NULL, 0);
	}
	return 0;
}

int ctrfuse_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	ncsd_context *ctx = (ncsd_context *)(fuse_get_context()->private_data);

	char *buf2;
	size_t bufsize;
	FILE *stream = open_memstream(&buf2, &bufsize);
	if (stream == NULL) {
		perror("open_memstream");
		return -errno;
	}

	ncsd_print(ctx, stream);
	if (fclose(stream) < 0) {
		perror("fclose");
		return -errno;
	}

	if (offset > bufsize) {
		free(buf2);
		return 0;
	}

	if (size > bufsize - offset) {
		size = bufsize - offset;
	}

	memmove(buf, buf2+offset, size);
	free(buf2);
	return size;
}

struct fuse_operations fuse_ops =
{
	.getattr	= ctrfuse_getattr,
	//.opendir	= ctrfuse_opendir,
	.readdir	= ctrfuse_readdir,
	//.open		= ctrfuse_open,
	.read		= ctrfuse_read,
};

int main(int argc, char **argv)
{
	struct fuse_args args = FUSE_ARGS_INIT(0, NULL);
	int i, ret;
	char *filename;
	FILE *infile;
	off_t infilesize;

	if(argc < 3)
	{
		printf("Usage: %s file.nds mount_point [fuse_options]\n",argv[0]);
		return 1;
	}

	filename = argv[1];

	infile = fopen(filename, "rb");
	if (infile == 0) 
	{
		fprintf(stderr, "error: could not open input file!\n");
		return -1;
	}

	fseek(infile, 0, SEEK_END);
	infilesize = ftello(infile);
	fseek(infile, 0, SEEK_SET);

	ncsd_context ncsdctx;
	ncsd_init(&ncsdctx);
	ncsd_set_file(&ncsdctx, infile);
	ncsd_set_size(&ncsdctx, infilesize);
	//ncsd_set_usersettings(&ncsdctx, &ctx.usersettings);

	ncsd_process(&ncsdctx, 0);

	for(i=0;i<argc;i++)
	{
		if(i != 1) fuse_opt_add_arg(&args, argv[i]);
	}

	ret = fuse_main(args.argc, args.argv, &fuse_ops, &ncsdctx);

	fuse_opt_free_args(&args);
	fclose(infile);

	return ret;
}
