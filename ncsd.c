#include <stdio.h>
#include <string.h>

#include "types.h"
#include "ncsd.h"
#include "utils.h"
#include "ctr.h"


void ncsd_init(ncsd_context* ctx)
{
	memset(ctx, 0, sizeof(ncsd_context));
}

void ncsd_set_offset(ncsd_context* ctx, u32 offset)
{
	ctx->offset = offset;
}

void ncsd_set_file(ncsd_context* ctx, FILE* file)
{
	ctx->file = file;
}

void ncsd_set_size(ncsd_context* ctx, u32 size)
{
	ctx->size = size;
}

void ncsd_set_usersettings(ncsd_context* ctx, settings* usersettings)
{
	ctx->usersettings = usersettings;
}

int ncsd_signature_verify(const void* blob, rsakey2048* key)
{
	u8* message = (u8*)blob + 0x100;
	u8* sig = (u8*)blob;
	u8 hash[0x20];

	ctr_sha_256(message, 0x100, hash);
	return ctr_rsa_verify_hash(sig, hash, key);
}

void ncsd_process(ncsd_context* ctx, u32 actions)
{
	fseek(ctx->file, ctx->offset, SEEK_SET);
	fread(&ctx->header, 1, 0x200, ctx->file);

	if (getle32(ctx->header.magic) != MAGIC_NCSD)
	{
		fprintf(stdout, "Error, NCSD segment corrupted\n");
		return;
	}


	if (actions & VerifyFlag)
	{
		if (ctx->usersettings)
			ctx->headersigcheck = ncsd_signature_verify(&ctx->header, &ctx->usersettings->keys.ncsdrsakey);
	}

	if (actions & InfoFlag)
		ncsd_print(ctx, stdout);

	ncch_set_file(&ctx->ncch, ctx->file);
	ncch_set_offset(&ctx->ncch, 0x4000);
	ncch_set_size(&ctx->ncch, ctx->size - 0x4000);
	ncch_set_usersettings(&ctx->ncch, ctx->usersettings);
	ncch_process(&ctx->ncch, actions);
}

unsigned int ncsd_get_mediaunit_size(ncsd_context* ctx)
{
	unsigned int mediaunitsize = settings_get_mediaunit_size(ctx->usersettings);

	if (mediaunitsize == 0)
		mediaunitsize = 1<<(9+ctx->header.flags[6]);

	return mediaunitsize;
}

void ncsd_print(ncsd_context* ctx, FILE* fp)
{
	char magic[5];
	ctr_ncsdheader* header = &ctx->header;
	unsigned int i;
	unsigned int mediaunitsize = ncsd_get_mediaunit_size(ctx);


	memcpy(magic, header->magic, 4);
	magic[4] = 0;

	fprintf(fp, "Header:                 %s\n", magic);
	if (ctx->headersigcheck == Unchecked)
		memdump(fp, "Signature:              ", header->signature, 0x100);
	else if (ctx->headersigcheck == Good)
		memdump(fp, "Signature (GOOD):       ", header->signature, 0x100);
	else
		memdump(fp, "Signature (FAIL):       ", header->signature, 0x100);       
	fprintf(fp, "Media size:             0x%08x\n", getle32(header->mediasize));
	fprintf(fp, "Media id:               %016llx\n", getle64(header->mediaid));
	//memdump(fp, "Partition FS type:      ", header->partitionfstype, 8);
	//memdump(fp, "Partition crypt type:   ", header->partitioncrypttype, 8);
	//memdump(fp, "Partition offset/size:  ", header->partitionoffsetandsize, 0x40);
	fprintf(fp, "\n");
	for(i=0; i<8; i++)
	{
		u32 partitionoffset = header->partitiongeometry[i].offset * mediaunitsize;
		u32 partitionsize = header->partitiongeometry[i].size * mediaunitsize;

		if (partitionsize != 0)
		{
			fprintf(fp, "Partition %d            \n", i);
			memdump(fp, " Id:                    ", header->partitionid+i*8, 8);
			fprintf(fp, " Area:                  0x%08X-0x%08X\n", partitionoffset, partitionoffset+partitionsize);
			fprintf(fp, " Filesystem:            %02X\n", header->partitionfstype[i]);
			fprintf(fp, " Encryption:            %02X\n", header->partitioncrypttype[i]);
			fprintf(fp, "\n");
		}
	}
	memdump(fp, "Extended header hash:   ", header->extendedheaderhash, 0x20);
	memdump(fp, "Additional header size: ", header->additionalheadersize, 4);
	memdump(fp, "Sector zero offset:     ", header->sectorzerooffset, 4);
	memdump(fp, "Flags:                  ", header->flags, 8);
	fprintf(fp, " > Mediaunit size:      0x%X\n", mediaunitsize);

}
