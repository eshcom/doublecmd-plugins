#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <libgen.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <magic.h>
#include <linux/limits.h>
#include "wdxplugin.h"

#define _detectstring "EXT=\"*\""

typedef struct _field
{
	char *name;
	int type;
	char *unit;
} FIELD;

#define fieldcount (sizeof(fields)/sizeof(FIELD))

#define info_units "default|fast|folow symlinks|uncompress|uncompress and folow symlinks|all matches"
#define obj_units  "file|directory|character device|block device|named pipe|symlink|socket"

FIELD fields[] =
{
	{"Info",			ft_string,				info_units},
	{"MIME type",			ft_string,		  "default|folow symlinks"},
	{"MIME encoding",		ft_string,		  "default|folow symlinks"},
	{"Object type",			ft_multiplechoice,			 obj_units},
	{"Access rights in octal",	ft_numeric_32,				"xxxx|xxx"},
	{"User name",			ft_string,					""},
	{"User ID",			ft_numeric_32,					""},
	{"Group name",			ft_string,					""},
	{"Group ID",			ft_numeric_32,					""},
	{"Inode number",		ft_numeric_32,					""},
	{"Size",			ft_numeric_64,					""},
	{"Block size",			ft_numeric_32,					""},
	{"Number of blocks",		ft_numeric_64,					""},
	{"Number of hard links",	ft_numeric_32,					""},
	{"User access",			ft_boolean,	"execute (dir)|read|write|execute"},
	{"User access (string)",	ft_string,					""},
};

char* objtypevalue[7] =
{
	"file",
	"directory",
	"character device",
	"block device",
	"named pipe",
	"symlink",
	"socket"
};

char* strlcpy(char* p, const char* p2, int maxlen)
{
	if ((int)strlen(p2) >= maxlen)
	{
		strncpy(p, p2, maxlen);
		p[maxlen] = 0;
	}
	else
		strcpy(p, p2);

	return p;
}

int convertDecimalToOctal(int decimalNumber)
{
	int octalNumber = 0, i = 1;

	while (decimalNumber != 0)
	{
		octalNumber += (decimalNumber % 8) * i;
		decimalNumber /= 8;
		i *= 10;
	}

	return octalNumber;
}

int DCPCALL ContentGetSupportedField(int FieldIndex, char* FieldName, char* Units, int maxlen)
{
	if (FieldIndex < 0 || FieldIndex >= fieldcount)
		return ft_nomorefields;

	strlcpy(FieldName, fields[FieldIndex].name, maxlen - 1);
	strlcpy(Units, fields[FieldIndex].unit, maxlen - 1);
	return fields[FieldIndex].type;
}

int DCPCALL ContentGetDetectString(char* DetectString, int maxlen)
{
	strlcpy(DetectString, _detectstring, maxlen - 1);
	return 0;
}

int DCPCALL ContentGetValue(char* FileName, int FieldIndex, int UnitIndex, void* FieldValue, int maxlen, int flags)
{
	struct stat buf;
	const char *magic_full;
	magic_t magic_cookie;
	char access_str[3] = "---";
	int access_how;

	if (strcmp(basename(FileName), "..") == 0)
		return ft_fileerror;

	if (lstat(FileName, &buf) != 0)
		return ft_fileerror;

	struct passwd *pw = getpwuid(buf.st_uid);
	struct group  *gr = getgrgid(buf.st_gid);

	switch (FieldIndex)
	{
	case 0:
		switch (UnitIndex)
		{
		case 1:
			magic_cookie = magic_open(MAGIC_NO_CHECK_SOFT);
			break;

		case 2:
			magic_cookie = magic_open(MAGIC_SYMLINK);
			break;

		case 3:
			magic_cookie = magic_open(MAGIC_COMPRESS);;
			break;

		case 4:
			magic_cookie = magic_open(MAGIC_SYMLINK | MAGIC_COMPRESS);
			break;

		case 5:
			magic_cookie = magic_open(MAGIC_CONTINUE | MAGIC_SYMLINK | MAGIC_COMPRESS);
			break;

		default:
			magic_cookie = magic_open(MAGIC_NONE);
		}

		break;

	case 1:
		if (UnitIndex == 0)
			magic_cookie = magic_open(MAGIC_MIME_TYPE);
		else
			magic_cookie = magic_open(MAGIC_MIME_TYPE | MAGIC_SYMLINK);

		break;

	case 2:
		if (UnitIndex == 0)
			magic_cookie = magic_open(MAGIC_MIME_ENCODING);
		else
			magic_cookie = magic_open(MAGIC_MIME_ENCODING | MAGIC_SYMLINK);

		break;

	case 3:
		if (S_ISDIR(buf.st_mode))
			strlcpy((char*)FieldValue, objtypevalue[1], maxlen - 1);
		else if (S_ISCHR(buf.st_mode))
			strlcpy((char*)FieldValue, objtypevalue[2], maxlen - 1);
		else if (S_ISBLK(buf.st_mode))
			strlcpy((char*)FieldValue, objtypevalue[3], maxlen - 1);
		else if (S_ISFIFO(buf.st_mode))
			strlcpy((char*)FieldValue, objtypevalue[4], maxlen - 1);
		else if (S_ISLNK(buf.st_mode))
			strlcpy((char*)FieldValue, objtypevalue[5], maxlen - 1);
		else if (S_ISSOCK(buf.st_mode))
			strlcpy((char*)FieldValue, objtypevalue[6], maxlen - 1);
		else
			strlcpy((char*)FieldValue, objtypevalue[0], maxlen - 1);

		break;

	case 4:
		if (UnitIndex == 0)
			*(int*)FieldValue = convertDecimalToOctal(buf.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO | S_ISUID | S_ISGID | S_ISVTX));
		else
			*(int*)FieldValue = convertDecimalToOctal(buf.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO));

		break;

	case 5:
		strncpy((char*)FieldValue, pw->pw_name, maxlen - 1);
		break;

	case 6:
		*(int*)FieldValue = buf.st_uid;
		break;

	case 7:
		strncpy((char*)FieldValue, gr->gr_name, maxlen - 1);
		break;

	case 8:
		*(int*)FieldValue = buf.st_gid;
		break;

	case 9:
		*(int*)FieldValue = buf.st_ino;
		break;

	case 10:
		*(int*)FieldValue = buf.st_size;
		break;

	case 11:
		*(int*)FieldValue = buf.st_blksize;
		break;

	case 12:
		*(int*)FieldValue = buf.st_blocks;
		break;

	case 13:
		*(int*)FieldValue = buf.st_nlink;
		break;

	case 14:
	{
		switch (UnitIndex)
		{
		case 1:
			access_how = R_OK;
			break;

		case 2:
			access_how = W_OK;
			break;

		case 3:
			access_how = X_OK;
			break;

		default:
			if (S_ISDIR(buf.st_mode))
				access_how = X_OK;
			else
				return ft_fileerror;
		}

		if (access(FileName, access_how) == 0)
			*(int*)FieldValue = 1;
		else
			*(int*)FieldValue = 0;

		break;
	}

	case 15:
	{
		if (access(FileName, R_OK) == 0)
			access_str[0] = 'r';

		if (access(FileName, W_OK) == 0)
			access_str[1] = 'w';

		if (access(FileName, X_OK) == 0)
			access_str[2] = 'x';

		strlcpy((char*)FieldValue, access_str, maxlen - 1);

		break;
	}

	default:
		return ft_nosuchfield;
	}

	if ((FieldIndex >= 0) && (FieldIndex < 3))
	{
		if (magic_cookie == NULL)
		{
			printf("unable to initialize magic library\n");
			return ft_fileerror;
		}

		if (magic_load(magic_cookie, NULL) != 0)
		{
			printf("cannot load magic database - %s\n", magic_error(magic_cookie));
			magic_close(magic_cookie);
			return ft_fileerror;
		}

		magic_full = magic_file(magic_cookie, FileName);

		if (magic_full)
			strlcpy((char*)FieldValue, magic_full, maxlen - 1);
		else
			return ft_fieldempty;

		magic_close(magic_cookie);
	}

	return fields[FieldIndex].type;
}
