#ifndef _EXHEADER_H_
#define _EXHEADER_H_

#include <stdio.h>
#include "types.h"
#include "ctr.h"

typedef struct
{
	u8 reserved[0x200];
	u8 programid[8];
	u8 reserved2[0x600 - 8];
} exheader_header;

typedef struct
{
	FILE* file;
	u8 key[16];
	u8 partitionid[8];
	u8 programid[8];
	u32 offset;
	u32 size;
	exheader_header header;
	ctr_aes_context aes;
} exheader_context;

void exheader_init(exheader_context* ctx);
void exheader_set_file(exheader_context* ctx, FILE* file);
void exheader_set_offset(exheader_context* ctx, u32 offset);
void exheader_set_size(exheader_context* ctx, u32 size);
void exheader_set_key(exheader_context* ctx, u8 key[16]);
void exheader_set_partitionid(exheader_context* ctx, u8 partitionid[8]);
void exheader_set_programid(exheader_context* ctx, u8 programid[8]);
int exheader_process(exheader_context* ctx, u32 actions);
void exheader_print(exheader_context* ctx);

#endif // _EXHEADER_H_
