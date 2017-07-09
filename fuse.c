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
#include "exefs.h"

enum {
	Root,
	ExefsDir,
	ExefsSection,
	Info,
};

struct node {
	int type;
	void* ctx;
	char* name;

	struct node* next;
	struct node* child;

	// exefs
	int section;

	// for virtual files
	void* data;
	size_t size;
};

struct context {
	ncsd_context ncsd;
	time_t mtime;
	struct node* root;
};

const char* strip_prefix(const char* path);
int path_has_prefix(const char* path, const char* name);

struct node* lookup(struct context* ctx, const char* path) {
	struct node* node = ctx->root;
	struct node* x;

	if (strcmp(path, "") == 0 || path[0] != '/') {
		return NULL;
	}

	if (strcmp(path, "/") == 0) {
		return node;
	}

	while (node != NULL && path[0] != '\0') {
		for (x = node->child; x != NULL; x = x->next) {
			if (path_has_prefix(path, x->name)) {
				path = strip_prefix(path);
				break;
			}
		}
		node = x;
	}

	return node;
}

const char* strip_prefix(const char* path) {
	if (*path == '/') {
		path++;
	}
	while (*path != '/' && *path != '\0') {
		path++;
	}
	if (*path == '/') {
		path++;
	}
	return path;
}

int path_has_prefix(const char* path, const char* name) {
	int i;
	if (path[0] == '/') {
		path++;
	}
	for (i = 0; path[i] != '\0' && path[i] != '/' && name[i] != '\0'; i++) {
		if (path[i] != name[i]) {
			return 0;
		}
	}
	return (path[i] == '\0' || path[i] == '/') && name[i] == '\0';
}

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
	struct context* ctx = fuse_get_context()->private_data;
	if (strcmp(path, "/") == 0 || strcmp(path, "/romfs") == 0 || strcmp(path, "/exefs") == 0) {
		stbuf->st_nlink = 2;
		stbuf->st_mode = S_IFDIR | 0555;
	} else if (strcmp(path, "/info") == 0) {
		stbuf->st_nlink = 1;
		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_size = getsize(&ctx->ncsd);
	} else {
		struct node* node = lookup(ctx, path);
		if (node == NULL) {
			return -ENOENT;
		}
		stbuf->st_nlink = 1;
		stbuf->st_mode = S_IFREG | 0444;
		if (node->type == ExefsSection) {
			stbuf->st_size = node->size;
		}
	}
	return 0;
}

int ctrfuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
	struct context* ctx = fuse_get_context()->private_data;

	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	if (strcmp(path, "/") == 0) {
		filler(buf, "info", NULL, 0);
		filler(buf, "exefs", NULL, 0);
		filler(buf, "romfs", NULL, 0);
		return 0;
	}

	if (strcmp(path, "/exefs") == 0) {
		struct node* node = lookup(ctx, path);
		if (node == NULL) {
			return -ENOENT;
		}
		struct node* x;
		for (x = node->child; x != NULL; x = x->next) {
			filler(buf, x->name, NULL, 0);
		}
		return 0;
	}

	return -ENOENT;
}

int ctrfuse_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	struct context* ctx = fuse_get_context()->private_data;

	if (strcmp(path, "/info") == 0) {
		char *buf2;
		size_t bufsize;
		FILE *stream = open_memstream(&buf2, &bufsize);
		if (stream == NULL) {
			perror("open_memstream");
			return -errno;
		}

		ncsd_print(&ctx->ncsd, stream);
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
	} else {
		struct node* node = lookup(ctx, path);
		if (node->type == ExefsSection) {
			exefs_context* exefsctx = &ctx->ncsd.ncch.exefs;
			int section = node->section;
			return exefs_read(exefsctx, section, RawFlag, buf, offset, size);
		}
	}
	return 0; // ????
}

struct node* newnode(int type, const char* name) {
	struct node* node = malloc(sizeof(struct node));
	memset(node, 0, sizeof(struct node));
	node->type = type;
	node->name = strdup(name);
	return node;
}

void make_nodes(struct context* ctx) {
	int i;
	ctx->root = newnode(Root, "/");
	ctx->root->child = newnode(ExefsDir, "exefs");

	exefs_context* exefs = &ctx->ncsd.ncch.exefs;
	struct node** tail = &ctx->root->child->child;
	for (i = 0; i < 8; i++) {
		if (getle32(exefs->header.section[i].size)) {
			char name[sizeof exefs->header.section[i].name + 5];
			memset(name, 0, sizeof name);
			strncpy(name, (char*)exefs->header.section[i].name, sizeof exefs->header.section[i].name);
			strcat(name, ".bin");

			struct node* node = newnode(ExefsSection, name[0] == '.' ? name+1 : name);
			node->ctx = exefs;
			node->size = getle32(exefs->header.section[i].size);
			node->section = i;
			*tail = node;
			tail = &node->next;
		}
	}
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
	struct context ctx;

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

	ncsd_init(&ctx.ncsd);
	ncsd_set_file(&ctx.ncsd, infile);
	ncsd_set_size(&ctx.ncsd, infilesize);
	//ncsd_set_usersettings(&ctx.ncsd, &ctx.usersettings);

	ncsd_process(&ctx.ncsd, 0);
	make_nodes(&ctx);

	for(i=0;i<argc;i++)
	{
		if(i != 1) fuse_opt_add_arg(&args, argv[i]);
	}

	ret = fuse_main(args.argc, args.argv, &fuse_ops, &ctx);

	fuse_opt_free_args(&args);
	fclose(infile);

	return ret;
}
