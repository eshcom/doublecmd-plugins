#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <archive.h>
#include <archive_entry.h>
#include <string.h>
#include "wcxplugin.h"
#include "extension.h"

#include <linux/limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libgen.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <ftw.h>
#include <fnmatch.h>
#include <dlfcn.h>
#include <limits.h>

#include <glib.h>

typedef struct sArcData
{
	struct archive *archive;
	struct archive_entry *entry;
	char arcname[PATH_MAX + 1];
	tChangeVolProc gChangeVolProc;
	tProcessDataProc gProcessDataProc;
} tArcData;

typedef tArcData* ArcData;
typedef void *HINSTANCE;

#define BUFF_SIZE 8192

tChangeVolProc gChangeVolProc  = NULL;
tProcessDataProc gProcessDataProc = NULL;
tExtensionStartupInfo* gStartupInfo = NULL;
static char gOptions[PATH_MAX];
static char gEncryption[PATH_MAX] = "traditional";
static char gLFMPath[PATH_MAX];
static bool gMtreeClasic = false;
static bool gMtreeCheckFS = false;
static bool gReadCompat2x = false;
static bool gReadIgnoreCRC = false;
static bool gReadMacExt = false;
static bool gReadDisableJoliet = false;
static bool gReadDisableRockridge = false;
static bool gCanHandleRAW = false;
static bool gOnlyTarFormat = false;
static bool gReadSkipDot = true;
static bool gNotabene = true;
static char gReadCharset[256];
static int  gTarFormat = ARCHIVE_FORMAT_TAR;
static int  gCpioFormat = ARCHIVE_FORMAT_CPIO;
static bool gArFormatBsd = false;
static bool gSharFormatDump = false;

GKeyFile *gCfg;
gchar *gCfgPath = NULL;


static void config_get_options(void)
{
	gchar *last_opts = g_key_file_get_string(gCfg, "Global", "LastOptions", NULL);

	if (last_opts)
	{
		g_strlcpy(gOptions, last_opts, PATH_MAX);
		g_free(last_opts);
	}

	gchar *encrypt = g_key_file_get_string(gCfg, "Global", "Encryption", NULL);

	if (encrypt)
	{
		g_strlcpy(gEncryption, encrypt, PATH_MAX);
		g_free(encrypt);
	}

	gMtreeClasic = g_key_file_get_boolean(gCfg, "Global", "MtreeClasic", NULL);
	gMtreeCheckFS = g_key_file_get_boolean(gCfg, "Global", "MtreeCheckFS", NULL);
	gCanHandleRAW = g_key_file_get_boolean(gCfg, "Global", "CanHandleRAW", NULL);
	gNotabene = g_key_file_get_boolean(gCfg, "Global", "ShowDisclaimer", NULL);
	gReadSkipDot = g_key_file_get_boolean(gCfg, "Global", "ReadSkipDot", NULL);
	gTarFormat = g_key_file_get_integer(gCfg, "Global", "TarFormat", NULL);
	gOnlyTarFormat = g_key_file_get_boolean(gCfg, "Global", "OpenOnlyTar", NULL);

	gCpioFormat = g_key_file_get_integer(gCfg, "Global", "CpioFormat", NULL);
	gArFormatBsd = g_key_file_get_boolean(gCfg, "Global", "ArFormatBsd", NULL);
	gSharFormatDump = g_key_file_get_boolean(gCfg, "Global", "SharFormatDump", NULL);
}

static void config_set_options(void)
{
	g_key_file_set_string(gCfg, "Global", "LastOptions", gOptions);
	g_key_file_set_boolean(gCfg, "Global", "ReadSkipDot", gReadSkipDot);
	g_key_file_set_boolean(gCfg, "Global", "MtreeClasic", gMtreeClasic);
	g_key_file_set_boolean(gCfg, "Global", "MtreeCheckFS", gMtreeCheckFS);
	g_key_file_set_boolean(gCfg, "Global", "CanHandleRAW", gCanHandleRAW);
	g_key_file_set_boolean(gCfg, "Global", "ShowDisclaimer", gNotabene);
	g_key_file_set_boolean(gCfg, "Global", "OpenOnlyTar", gOnlyTarFormat);

	g_key_file_set_integer(gCfg, "Global", "TarFormat", gTarFormat);
	g_key_file_set_integer(gCfg, "Global", "CpioFormat", gCpioFormat);
	g_key_file_set_boolean(gCfg, "Global", "ArFormatBsd", gArFormatBsd);
	g_key_file_set_boolean(gCfg, "Global", "SharFormatDump", gSharFormatDump);

	g_key_file_set_string(gCfg, "Global", "Encryption", gEncryption);
}

void DCPCALL ExtensionInitialize(tExtensionStartupInfo* StartupInfo)
{
	Dl_info dlinfo;
	const char* lfm_name = "dialog.lfm";

	if (gStartupInfo == NULL)
	{
		gStartupInfo = malloc(sizeof(tExtensionStartupInfo));
		memcpy(gStartupInfo, StartupInfo, sizeof(tExtensionStartupInfo));
	}

	memset(&dlinfo, 0, sizeof(dlinfo));

	if (dladdr(lfm_name, &dlinfo) != 0)
	{
		g_strlcpy(gLFMPath, dlinfo.dli_fname, PATH_MAX);
		char *pos = strrchr(gLFMPath, '/');

		if (pos)
			strcpy(pos + 1, lfm_name);
	}
}

void DCPCALL ExtensionFinalize(void* Reserved)
{
	if (gStartupInfo != NULL)
		free(gStartupInfo);

	gStartupInfo = NULL;


	if (gCfg != NULL)
		g_key_file_free(gCfg);

	if (gCfgPath)
		g_free(gCfgPath);

	gCfg = NULL;
	gCfgPath = NULL;
}

void DCPCALL PackSetDefaultParams(PackDefaultParamStruct* dps)
{
	gCfg = g_key_file_new();

	gchar *cfg_dir = g_path_get_dirname(dps->DefaultIniName);
	gCfgPath = g_strdup_printf("%s/libarchive_crap.ini", cfg_dir);
	g_free(cfg_dir);

	if (g_key_file_load_from_file(gCfg, gCfgPath, G_KEY_FILE_KEEP_COMMENTS, NULL))
		config_get_options();
	else
	{
		g_key_file_set_string(gCfg, ".liz", "read_cmd", "lizard -d");
		g_key_file_set_string(gCfg, ".liz", "write_cmd", "lizard");
		g_key_file_set_string(gCfg, ".liz", "signature", "06 22 4D 18");
		config_set_options();
		g_key_file_save_to_file(gCfg, gCfgPath, NULL);
	}
}

static int errmsg(const char *msg, long flags)
{
	if (gStartupInfo)
		return gStartupInfo->MessageBox(msg ? (char*)msg : "Unknown error", NULL, flags);
	else
	{
		printf("(gStartupInfo->MessageBox, gStartupInfo == NULL): %s", msg);
		return ID_ABORT;
	}
}

static void remove_file(const char *file)
{
	if (remove(file) == -1)
	{
		int errsv = errno;
		printf("remove file: %s: %s\n", file, strerror(errsv));
	}
}

static int nftw_remove_cb(const char *file, const struct stat *bif, int tflag, struct FTW *ftwbuf)
{
	remove_file(file);
	return 0;
}

static void remove_target(const char *filename)
{
	struct stat st;

	if (lstat(filename, &st) == 0)
	{
		if S_ISDIR(st.st_mode)
			nftw(filename, nftw_remove_cb, 13, FTW_DEPTH | FTW_PHYS);
		else
			remove_file(filename);
	}
}

static unsigned char* hex_to_uchar(char* str, size_t *res_size)
{
	char *p;
	size_t i = 0;
	unsigned int chr;

	size_t len = strlen(str);

	if (len < 2)
		return NULL;

	*res_size = (len + 1) / 3;
	unsigned char *result = (unsigned char*)malloc(*res_size);

	for (p = str; *p; p += 3, i++)
	{
		if (sscanf(p, "%02X", &chr) != 1)
			break;

		result[i] = (unsigned char)chr;
	}

	return result;
}

static void add_read_programs_from_cfg(struct archive *a, const char*ext)
{
	gsize length;
	size_t signature_size;
	gchar *hex = NULL, *cmd = NULL;
	gchar **groups = g_key_file_get_groups(gCfg, &length);

	if (groups)
	{
		for (int i = 0; i < length; i++)
		{
			if (groups[i][0] == '.')
			{
				signature_size = 0;
				hex = g_key_file_get_string(gCfg, groups[i], "signature", NULL);
				cmd = g_key_file_get_string(gCfg, groups[i], "read_cmd", NULL);

				if (cmd)
				{
					if (hex)
					{
						unsigned char *signature = hex_to_uchar(hex, &signature_size);
						archive_read_support_filter_program_signature(a, cmd, signature, signature_size);
						free(signature);
					}
					else if (ext != NULL && strcasecmp(ext, groups[i]) == 0)
						archive_read_support_filter_program(a, cmd);
				}
			}
		}

		g_strfreev(groups);
	}
}

static bool mtree_opts_nodata(void)
{
	bool result = true;

	if (gOptions[0] != '\0')
	{
		if (strstr(gOptions, "dironly") != NULL)
			result = true;
		else if (strstr(gOptions, "all") != NULL)
			result = false;
		else if (strstr(gOptions, "cksum") != NULL)
			result = false;
		else if (strstr(gOptions, "md5") != NULL)
			result = false;
		else if (strstr(gOptions, "rmd160") != NULL)
			result = false;
		else if (strstr(gOptions, "sha1") != NULL)
			result = false;
		else if (strstr(gOptions, "sha256") != NULL)
			result = false;
		else if (strstr(gOptions, "sha384") != NULL)
			result = false;
		else if (strstr(gOptions, "sha512") != NULL)
			result = false;
	}

	return result;
}

static int archive_set_format_filter(struct archive *a, const char*ext)
{
	int ret;

	if (g_key_file_has_key(gCfg, ext, "write_cmd", NULL))
	{
		ret = archive_write_set_format_raw(a);
		gchar *cmd = g_key_file_get_string(gCfg, ext, "write_cmd", NULL);

		if (cmd)
			ret = archive_write_add_filter_program(a, cmd);
		else
			ret = ARCHIVE_FATAL;
	}
	else
	if (strcasecmp(ext, ".tzst") == 0)
	{
		ret = archive_write_set_format(a, gTarFormat);
		ret = archive_write_add_filter_zstd(a);
	}
	else if (strcasecmp(ext, ".zst") == 0)
	{
		ret = archive_write_set_format_raw(a);
		ret = archive_write_add_filter_zstd(a);
	}
	else if (strcasecmp(ext, ".mtree") == 0)
	{
		if (gMtreeClasic)
			ret = archive_write_set_format_mtree_classic(a);
		else
			ret = archive_write_set_format_mtree(a);
	}
	else if (strcasecmp(ext, ".lz4") == 0)
	{
		ret = archive_write_set_format_raw(a);
		ret = archive_write_add_filter_lz4(a);
	}
	else if (strcasecmp(ext, ".lz") == 0)
	{
		ret = archive_write_set_format_raw(a);
		ret = archive_write_add_filter_lzip(a);
	}
	else if (strcasecmp(ext, ".lzo") == 0)
	{
		ret = archive_write_set_format_raw(a);
		ret = archive_write_add_filter_lzop(a);
	}
	else if (strcasecmp(ext, ".lrz") == 0)
	{
		ret = archive_write_set_format_raw(a);
		ret = archive_write_add_filter_lrzip(a);
	}
	else if (strcasecmp(ext, ".grz") == 0)
	{
		ret = archive_write_set_format_raw(a);
		ret = archive_write_add_filter_grzip(a);
	}
	else if (strcasecmp(ext, ".lzma") == 0)
	{
		ret = archive_write_set_format_raw(a);
		ret = archive_write_add_filter_lzma(a);
	}
	else if (strcasecmp(ext, ".b64u") == 0)
	{
		ret = archive_write_set_format_raw(a);
		ret = archive_write_add_filter_b64encode(a);
	}
	else if (strcasecmp(ext, ".uue") == 0)
	{
		ret = archive_write_set_format_raw(a);
		ret = archive_write_add_filter_uuencode(a);
	}
	else if (strcasecmp(ext, ".gz") == 0)
	{
		ret = archive_write_set_format_raw(a);
		ret = archive_write_add_filter_gzip(a);
	}
	else if (strcasecmp(ext, ".xz") == 0)
	{
		ret = archive_write_set_format_raw(a);
		ret = archive_write_add_filter_xz(a);
	}
	else if (strcasecmp(ext, ".z") == 0)
	{
		ret = archive_write_set_format_raw(a);
		ret = archive_write_add_filter_compress(a);
	}
	else if (strcasecmp(ext, ".bz2") == 0)
	{
		ret = archive_write_set_format_raw(a);
		ret = archive_write_add_filter_bzip2(a);
	}
	else if (strcasecmp(ext, ".warc") == 0)
	{
		ret = archive_write_set_format_warc(a);
	}
	else if (strcasecmp(ext, ".xar") == 0)
	{
		ret = archive_write_set_format_xar(a);
	}
	else if (strcasecmp(ext, ".cpio") == 0)
	{
		ret = archive_write_set_format(a, gCpioFormat);
	}
	else if (strcasecmp(ext, ".ar") == 0)
	{
		if (gArFormatBsd)
			ret = archive_write_set_format_ar_bsd(a);
		else
			ret = archive_write_set_format_ar_svr4(a);
	}
	else if (strcasecmp(ext, ".shar") == 0 || strcasecmp(ext, ".run") == 0)
	{
		if (gSharFormatDump)
			ret = archive_write_set_format_shar_dump(a);
		else
			ret = archive_write_set_format_shar(a);
	}
	else if (strcasecmp(ext, ".tar") == 0)
	{
		ret = archive_write_set_format(a, gTarFormat);
	}
	else if (strcasecmp(ext, ".tgz") == 0)
	{
		ret = archive_write_set_format(a, gTarFormat);
		ret = archive_write_add_filter_gzip(a);
	}
	else if (strcasecmp(ext, ".tbz2") == 0)
	{
		ret = archive_write_set_format(a, gTarFormat);
		ret = archive_write_add_filter_bzip2(a);
	}
	else if (strcasecmp(ext, ".txz") == 0)
	{
		ret = archive_write_set_format(a, gTarFormat);
		ret = archive_write_add_filter_xz(a);
	}
	else
		ret = archive_write_set_format_filter_by_ext(a, ext);

	return ret;
}

const char *archive_password_cb(struct archive *a, void *data)
{
	static char pass[PATH_MAX];

	if (gStartupInfo->InputBox("Double Commander", "Please enter the password:", true, pass, PATH_MAX - 1))
		return pass;
	else
		return NULL;
}

int archive_repack_existing(struct archive *a, char* filename, char** tmpfn, int ofd, char* headlist, char* subpath, int flags, const char *ext)
{
	size_t size;
	la_int64_t offset;
	const void *buff;
	bool skip_file;
	char fname[PATH_MAX];
	char infile[PATH_MAX];
	char rmfile[PATH_MAX];
	char *msg, *skiplist;
	struct archive_entry *entry;
	int result = E_SUCCESS, ret, ret_header;
	size_t fsize, csize, prcnt;

	if (archive_filter_code(a, 0) == ARCHIVE_FILTER_UU)
		return E_NOT_SUPPORTED;
	else if (archive_format(a) == ARCHIVE_FORMAT_RAW)
	{
		if (strcasestr(filename, ".tar.") == NULL || archive_write_set_format(a, gTarFormat)  < ARCHIVE_OK)
			return E_NOT_SUPPORTED;
	}

	if (gNotabene && errmsg("Options for compression, encryption etc will be LOST. Are you sure you want this?", MB_YESNO | MB_ICONWARNING) != ID_YES)
		return E_EABORTED;
	else
	{
		g_strlcpy(infile, filename, PATH_MAX);
		*tmpfn = tempnam(dirname(infile), "arc_");

		if (gOptions[0] != '\0')
		{
			asprintf(&msg, "Use these options '%s'?", gOptions);

			if (errmsg(msg, MB_YESNO | MB_ICONQUESTION) == ID_YES)
				if (archive_write_set_options(a, gOptions) < ARCHIVE_OK)
					errmsg(archive_error_string(a), MB_OK | MB_ICONWARNING);

			free(msg);
		}
	}


	struct archive *org = archive_read_new();

	archive_read_support_filter_all(org);

	add_read_programs_from_cfg(org, ext);

	archive_read_support_format_raw(org);

	archive_read_support_format_all(org);

	archive_read_set_passphrase_callback(org, NULL, archive_password_cb);

	if (archive_read_open_filename(org, filename, 10240) < ARCHIVE_OK)
	{
		errmsg(archive_error_string(org), MB_OK | MB_ICONERROR);
		result = E_EREAD;
	}
	else
	{

		if (*tmpfn == NULL || access(*tmpfn, F_OK) != -1)
			result = E_EWRITE;
		else if ((ofd = open(*tmpfn, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) == -1)
			result = E_EWRITE;
		else if (archive_write_open_fd(a, ofd) < ARCHIVE_OK)
		{
			errmsg(archive_error_string(a), MB_OK | MB_ICONERROR);
			result = E_EWRITE;
		}

		while (result == E_SUCCESS && (ret_header = archive_read_next_header(org, &entry)) == ARCHIVE_OK)
		{
			fsize = archive_entry_size(entry);
			csize = 0;

			skiplist = headlist;
			g_strlcpy(infile, archive_entry_pathname(entry), PATH_MAX);
			skip_file = false;

			while (*skiplist)
			{
				g_strlcpy(fname, skiplist, PATH_MAX);

				if (!(flags & PK_PACK_SAVE_PATHS))
				{
					gchar *bname = g_path_get_basename(fname);
					g_strlcpy(fname, bname, PATH_MAX);
					g_free(bname);
				}

				if (!subpath)
					strcpy(rmfile, fname);
				else
					snprintf(rmfile, PATH_MAX, "%s/%s", subpath, fname);

				if (strncmp(rmfile + strlen(rmfile) - 4, "/*.*", 4) == 0)
					rmfile[strlen(rmfile) - 2] = 0;

				if (strcmp(rmfile, infile) == 0 || fnmatch(rmfile, infile, FNM_CASEFOLD) == 0)
				{
					skip_file = true;
					break;
				}

				while (*skiplist++);
			}

			if (!skip_file)
			{
				archive_write_header(a, entry);

				while ((ret = archive_read_data_block(org, &buff, &size, &offset)) != ARCHIVE_EOF)
				{
					if (ret < ARCHIVE_OK)
					{
						errmsg(archive_error_string(org), MB_OK | MB_ICONERROR);
						result = E_EABORTED;
						break;
					}
					else if (archive_write_data(a, buff, size) < ARCHIVE_OK)
					{
						errmsg(archive_error_string(a), MB_OK | MB_ICONERROR);
						result = E_EWRITE;
						break;
					}

					if (fsize > 0)
					{
						csize += size;
						prcnt = csize * 100 / fsize;
					}
					else
						prcnt = 0;

					if (gProcessDataProc(infile, -(1000 + prcnt)) == 0)
					{
						result = E_EABORTED;
						break;
					}
				}
			}
		}

		archive_read_close(org);
	}

	archive_read_free(org);

	if (ret_header < ARCHIVE_OK)
		result = E_EREAD;

	if (result != E_SUCCESS)
	{
		if (*tmpfn != NULL)
		{
			remove_file(*tmpfn);
			free(*tmpfn);
			*tmpfn = NULL;
		}
	}

	return result;
}

static void checkbox_get_option(uintptr_t pDlg, char* DlgItemName, const char* optstr, bool defval, char *string)
{
	bool chk = (bool)gStartupInfo->SendDlgMsg(pDlg, DlgItemName, DM_GETCHECK, 0, 0);

	if ((chk && !defval) || (!chk && defval))
	{
		if (strlen(string) > 0 && string[strlen(string) - 1] != ':')
			strcat(string, ",");

		if (!chk && defval)
			strcat(string, "!");

		strcat(string, optstr);
	}
}

static void textfield_get_option(uintptr_t pDlg, char* DlgItemName, const char* optstr, char *string)
{
	char *tmpval = malloc(PATH_MAX);
	memset(tmpval, 0, PATH_MAX);
	g_strlcpy(tmpval, (char*)gStartupInfo->SendDlgMsg(pDlg, DlgItemName, DM_GETTEXT, 0, 0), PATH_MAX);

	if (tmpval[0] != '\0')
	{
		if (strlen(string) > 0 && string[strlen(string) - 1] != ':')
			strcat(string, ",");

		snprintf(string, PATH_MAX, "%s%s=%s", strdup(string), optstr, tmpval);
	}

	free(tmpval);
}

static void listbox_get_extentions(uintptr_t pDlg)
{
	gint i;
	gsize length;

	length = gStartupInfo->SendDlgMsg(pDlg, "lbExternalExt", DM_LISTGETCOUNT, 0, 0);

	if (length > 0)
	{
		for (i = length - 1; i != -1; i--)
			gStartupInfo->SendDlgMsg(pDlg, "lbExternalExt", DM_LISTDELETE, i, 0);
	}

	gchar **groups = g_key_file_get_groups(gCfg, &length);

	for (i = 0; i < length; i++)
	{
		if (groups[i][0] == '.')
			gStartupInfo->SendDlgMsg(pDlg, "lbExternalExt", DM_LISTADDSTR, (intptr_t)groups[i], 0);
	}

	if (groups)
		g_strfreev(groups);
}

static void ed_external_get_options(uintptr_t pDlg)
{
	gchar* cfg_value = NULL, *ext = NULL;

	int i = gStartupInfo->SendDlgMsg(pDlg, "lbExternalExt", DM_LISTGETITEMINDEX, 0, 0);

	if (i < 0)
	{
		ext = (char*)gStartupInfo->SendDlgMsg(pDlg, "edExternalExt", DM_GETTEXT, 0, 0);

		if (!ext)
			return;
	}
	else
		ext = (char*)gStartupInfo->SendDlgMsg(pDlg, "lbExternalExt", DM_LISTGETITEM, i, 0);

	if (ext && ext[0] == '.')
	{
		gStartupInfo->SendDlgMsg(pDlg, "pnlExtenalEdit", DM_ENABLE, 1, 0);
		gStartupInfo->SendDlgMsg(pDlg, "btnExternalDelete", DM_ENABLE, 1, 0);
		gStartupInfo->SendDlgMsg(pDlg, "edExternalExt", DM_SETTEXT, (intptr_t)ext, 0);
		cfg_value = g_key_file_get_string(gCfg, ext, "write_cmd", NULL);
		gStartupInfo->SendDlgMsg(pDlg, "edExternalWrite", DM_SETTEXT, (intptr_t)cfg_value, 0);
		cfg_value = g_key_file_get_string(gCfg, ext, "read_cmd", NULL);
		gStartupInfo->SendDlgMsg(pDlg, "edExternalRead", DM_SETTEXT, (intptr_t)cfg_value, 0);
		cfg_value = g_key_file_get_string(gCfg, ext, "signature", NULL);
		gStartupInfo->SendDlgMsg(pDlg, "edExternalHEX", DM_SETTEXT, (intptr_t)cfg_value, 0);
	}
}

static bool check_hex_signature(char* str)
{
	char val[3];
	unsigned int chr;
	size_t len = strlen(str);

	if (len < 2)
		return false;

	if (len % 3 == 1)
		return false;

	for (size_t i = 0; i + 3 <= len + 1; i += 3)
	{
		g_strlcpy(val, str + i, 3);

		if (sscanf(val, "%02X", &chr) != 1)
			return false;

		if (val[2] != '\0' && val[2] != ' ')
			return false;
	}

	return true;
}

static void ed_external_clear(uintptr_t pDlg)
{
	gStartupInfo->SendDlgMsg(pDlg, "edExternalExt", DM_SETTEXT, 0, 0);
	gStartupInfo->SendDlgMsg(pDlg, "edExternalWrite", DM_SETTEXT, 0, 0);
	gStartupInfo->SendDlgMsg(pDlg, "edExternalRead", DM_SETTEXT, 0, 0);
	gStartupInfo->SendDlgMsg(pDlg, "edExternalHEX", DM_SETTEXT, 0, 0);
}

intptr_t DCPCALL DlgProc(uintptr_t pDlg, char* DlgItemName, intptr_t Msg, intptr_t wParam, intptr_t lParam)
{
	GError *err = NULL;
	int numval;
	bool bval;
	char string[PATH_MAX];
	memset(string, 0, PATH_MAX);

	switch (Msg)
	{
	case DN_INITDIALOG:
		if (g_key_file_load_from_file(gCfg, gCfgPath, G_KEY_FILE_KEEP_COMMENTS, NULL))
			config_get_options();

		gStartupInfo->SendDlgMsg(pDlg, "edOptions", DM_SETTEXT, (intptr_t)gOptions, 0);
		gStartupInfo->SendDlgMsg(pDlg, "chkClassic", DM_SETCHECK, (intptr_t)gMtreeClasic, 0);
		snprintf(string, PATH_MAX, "%s", archive_version_details());
		gStartupInfo->SendDlgMsg(pDlg, "lblInfo", DM_SETTEXT, (intptr_t)string, 0);
		gStartupInfo->SendDlgMsg(pDlg, "cbZISOCompLvl", DM_ENABLE, 0, 0);

		switch (gTarFormat)
		{
		case ARCHIVE_FORMAT_TAR_USTAR:
			numval = 1;
			break;

		case ARCHIVE_FORMAT_TAR_PAX_INTERCHANGE:
			numval = 2;
			break;

		case ARCHIVE_FORMAT_TAR_PAX_RESTRICTED:
			numval = 3;
			break;

		case ARCHIVE_FORMAT_TAR_GNUTAR:
			numval = 4;
			break;

		default:
			numval = 0;
		}

		gStartupInfo->SendDlgMsg(pDlg, "cbTarFormat", DM_LISTSETITEMINDEX, numval, 0);

		switch (gCpioFormat)
		{
		case ARCHIVE_FORMAT_CPIO_POSIX:
			numval = 1;
			break;

		case ARCHIVE_FORMAT_CPIO_BIN_LE:
			numval = 2;
			break;

		case ARCHIVE_FORMAT_CPIO_BIN_BE:
			numval = 3;
			break;

		case ARCHIVE_FORMAT_CPIO_SVR4_NOCRC:
			numval = 4;
			break;

		case ARCHIVE_FORMAT_CPIO_SVR4_CRC:
			numval = 5;
			break;

		case ARCHIVE_FORMAT_CPIO_AFIO_LARGE:
			numval = 6;
			break;

		default:
			numval = 0;
		}

		gStartupInfo->SendDlgMsg(pDlg, "cbCpioFormat", DM_LISTSETITEMINDEX, numval, 0);

		if (gArFormatBsd)
			numval = 1;
		else
			numval = 0;

		gStartupInfo->SendDlgMsg(pDlg, "cbArFormat", DM_LISTSETITEMINDEX, numval, 0);

		if (gSharFormatDump)
			numval = 1;
		else
			numval = 0;

		gStartupInfo->SendDlgMsg(pDlg, "cbSharFormat", DM_LISTSETITEMINDEX, numval, 0);

		gStartupInfo->SendDlgMsg(pDlg, "cbReadCharset", DM_SETTEXT, (intptr_t)gReadCharset, 0);
		gStartupInfo->SendDlgMsg(pDlg, "chkReadCompat2x", DM_SETCHECK, (intptr_t)gReadCompat2x, 0);
		gStartupInfo->SendDlgMsg(pDlg, "chkReadMtreeckfs", DM_SETCHECK, (intptr_t)gMtreeCheckFS, 0);
		gStartupInfo->SendDlgMsg(pDlg, "chkReadMacExt", DM_SETCHECK, (intptr_t)gReadMacExt, 0);
		gStartupInfo->SendDlgMsg(pDlg, "chkReadJoliet", DM_SETCHECK, (intptr_t)gReadDisableJoliet, 0);
		gStartupInfo->SendDlgMsg(pDlg, "chkReadRockridge", DM_SETCHECK, (intptr_t)gReadDisableRockridge, 0);
		gStartupInfo->SendDlgMsg(pDlg, "chkReadIgnoreCRC", DM_SETCHECK, (intptr_t)gReadIgnoreCRC, 0);
		gStartupInfo->SendDlgMsg(pDlg, "chkReadDetectRAW", DM_SETCHECK, (intptr_t)gCanHandleRAW, 0);
		gStartupInfo->SendDlgMsg(pDlg, "chkReadSkipDot", DM_SETCHECK, (intptr_t)gReadSkipDot, 0);
		gStartupInfo->SendDlgMsg(pDlg, "chkDisclaimer", DM_SETCHECK, (intptr_t)gNotabene, 0);

		gStartupInfo->SendDlgMsg(pDlg, "chkReadTarOnly", DM_SETCHECK, (intptr_t)gOnlyTarFormat, 0);

		gStartupInfo->SendDlgMsg(pDlg, "cbEncrypt", DM_SETTEXT, (intptr_t)gEncryption, 0);

		listbox_get_extentions(pDlg);

		break;

	case DN_CLICK:
		if (strncmp(DlgItemName, "btnOK", 5) == 0)
		{
			g_strlcpy(gOptions, (char*)gStartupInfo->SendDlgMsg(pDlg, "edOptions", DM_GETTEXT, 0, 0), PATH_MAX);
			gMtreeClasic = (bool)gStartupInfo->SendDlgMsg(pDlg, "chkClassic", DM_GETCHECK, 0, 0);
			g_strlcpy(gEncryption, (char*)gStartupInfo->SendDlgMsg(pDlg, "cbEncrypt", DM_GETTEXT, 0, 0), PATH_MAX);

			g_strlcpy(gReadCharset, (char*)gStartupInfo->SendDlgMsg(pDlg, "cbReadCharset", DM_GETTEXT, 0, 0), 255);
			gMtreeCheckFS = (bool)gStartupInfo->SendDlgMsg(pDlg, "chkReadMtreeckfs", DM_GETCHECK, 0, 0);
			gReadCompat2x = (bool)gStartupInfo->SendDlgMsg(pDlg, "chkReadCompat2x", DM_GETCHECK, 0, 0);
			gReadIgnoreCRC = (bool)gStartupInfo->SendDlgMsg(pDlg, "chkReadIgnoreCRC", DM_GETCHECK, 0, 0);
			gReadMacExt = (bool)gStartupInfo->SendDlgMsg(pDlg, "chkReadMacExt", DM_GETCHECK, 0, 0);
			gReadDisableJoliet = (bool)gStartupInfo->SendDlgMsg(pDlg, "chkReadJoliet", DM_GETCHECK, 0, 0);
			gReadDisableRockridge = (bool)gStartupInfo->SendDlgMsg(pDlg, "chkReadRockridge", DM_GETCHECK, 0, 0);
			gCanHandleRAW = (bool)gStartupInfo->SendDlgMsg(pDlg, "chkReadDetectRAW", DM_GETCHECK, 0, 0);
			gOnlyTarFormat = (bool)gStartupInfo->SendDlgMsg(pDlg, "chkReadTarOnly", DM_GETCHECK, 0, 0);
			gReadSkipDot = (bool)gStartupInfo->SendDlgMsg(pDlg, "chkReadSkipDot", DM_GETCHECK, 0, 0);
			gNotabene = (bool)gStartupInfo->SendDlgMsg(pDlg, "chkDisclaimer", DM_GETCHECK, 0, 0);
			gArFormatBsd = (bool)gStartupInfo->SendDlgMsg(pDlg, "cbArFormat", DM_LISTGETITEMINDEX, 0, 0);
			gSharFormatDump = (bool)gStartupInfo->SendDlgMsg(pDlg, "cbSharFormat", DM_LISTGETITEMINDEX, 0, 0);

			numval = (int)gStartupInfo->SendDlgMsg(pDlg, "cbTarFormat", DM_LISTGETITEMINDEX, 0, 0);

			if (numval > -1)
			{
				switch (numval)
				{
				case 1:
					gTarFormat = ARCHIVE_FORMAT_TAR_USTAR;
					break;

				case 2:
					gTarFormat = ARCHIVE_FORMAT_TAR_PAX_INTERCHANGE;
					break;

				case 3:
					gTarFormat = ARCHIVE_FORMAT_TAR_PAX_RESTRICTED;
					break;

				case 4:
					gTarFormat = ARCHIVE_FORMAT_TAR_GNUTAR;
					break;

				default:
					gTarFormat = ARCHIVE_FORMAT_TAR;
				}
			}

			numval = (int)gStartupInfo->SendDlgMsg(pDlg, "cbCpioFormat", DM_LISTGETITEMINDEX, 0, 0);

			if (numval > -1)
			{
				switch (numval)
				{
				case 1:
					gCpioFormat = ARCHIVE_FORMAT_CPIO_POSIX;
					break;

				case 2:
					gCpioFormat = ARCHIVE_FORMAT_CPIO_BIN_LE;
					break;

				case 3:
					gCpioFormat = ARCHIVE_FORMAT_CPIO_BIN_BE;
					break;

				case 4:
					gCpioFormat = ARCHIVE_FORMAT_CPIO_SVR4_NOCRC;
					break;

				case 5:
					gCpioFormat = ARCHIVE_FORMAT_CPIO_SVR4_CRC;
					break;

				case 6:
					gCpioFormat = ARCHIVE_FORMAT_CPIO_AFIO_LARGE;
					break;

				default:
					gCpioFormat = ARCHIVE_FORMAT_CPIO;
				}
			}

			config_set_options();
			g_key_file_save_to_file(gCfg, gCfgPath, NULL);

			gStartupInfo->SendDlgMsg(pDlg, DlgItemName, DM_CLOSE, ID_OK, 0);
		}
		else if (strncmp(DlgItemName, "btnCancel", 8) == 0)
		{
			if (g_key_file_load_from_file(gCfg, gCfgPath, G_KEY_FILE_KEEP_COMMENTS, NULL))
				config_get_options();
		}
		else if (strncmp(DlgItemName, "btnEditINI", 8) == 0)
		{
			g_key_file_save_to_file(gCfg, gCfgPath, NULL);
			snprintf(string, PATH_MAX, "xdg-open \"%s\"", gCfgPath);
			system(string);

			errmsg("Click OK when done editing.", MB_OK | MB_ICONINFORMATION);

			if (g_key_file_load_from_file(gCfg, gCfgPath, G_KEY_FILE_KEEP_COMMENTS, NULL))
				config_get_options();
		}
		else if (strncmp(DlgItemName, "btnClear", 8) == 0)
			gStartupInfo->SendDlgMsg(pDlg, "edOptions", DM_SETTEXT, 0, 0);
		else if (strcmp(DlgItemName, "lbExternalExt") == 0 || strcmp(DlgItemName, "btnExternalReload") == 0)
		{
			ed_external_get_options(pDlg);
			gStartupInfo->SendDlgMsg(pDlg, "btnExternalReload", DM_ENABLE, 0, 0);
			gStartupInfo->SendDlgMsg(pDlg, "btnExternalSave", DM_ENABLE, 0, 0);
		}
		else if (strcmp(DlgItemName, "btnExternalAddNew") == 0)
		{
			gStartupInfo->SendDlgMsg(pDlg, "pnlExtenalEdit", DM_ENABLE, 1, 0);
			gStartupInfo->SendDlgMsg(pDlg, "btnExternalDelete", DM_ENABLE, 0, 0);
			gStartupInfo->SendDlgMsg(pDlg, "btnExternalSave", DM_ENABLE, 0, 0);
			gStartupInfo->SendDlgMsg(pDlg, "btnExternalReload", DM_ENABLE, 0, 0);
			ed_external_clear(pDlg);
			g_strlcpy(string, ".", PATH_MAX);
			gStartupInfo->SendDlgMsg(pDlg, "edExternalExt", DM_SETTEXT, (intptr_t)string, 0);
		}
		else if (strcmp(DlgItemName, "btnExternalDelete") == 0)
		{
			gchar *ext = (char*)gStartupInfo->SendDlgMsg(pDlg, "edExternalExt", DM_GETTEXT, 0, 0);

			if (!g_key_file_remove_group(gCfg, ext, &err))
			{
				errmsg((err)->message, MB_OK | MB_ICONERROR);
			}
			else
			{
				ed_external_clear(pDlg);
				gStartupInfo->SendDlgMsg(pDlg, "pnlExtenalEdit", DM_ENABLE, 0, 0);
				gStartupInfo->SendDlgMsg(pDlg, "btnExternalDelete", DM_ENABLE, 0, 0);
				gStartupInfo->SendDlgMsg(pDlg, "btnExternalReload", DM_ENABLE, 0, 0);
				gStartupInfo->SendDlgMsg(pDlg, "btnExternalSave", DM_ENABLE, 0, 0);
			}

			listbox_get_extentions(pDlg);
		}
		else if (strcmp(DlgItemName, "btnExternalSave") == 0)
		{
			gchar *ext = (char*)gStartupInfo->SendDlgMsg(pDlg, "edExternalExt", DM_GETTEXT, 0, 0);

			if (ext && ext[0] == '.' && ext[1] != '\0')
			{
				g_strlcpy(string, (char*)gStartupInfo->SendDlgMsg(pDlg, "edExternalWrite", DM_GETTEXT, 0, 0), PATH_MAX);

				if (string[0] != '\0')
					g_key_file_set_string(gCfg, ext, "write_cmd", string);

				g_strlcpy(string, (char*)gStartupInfo->SendDlgMsg(pDlg, "edExternalRead", DM_GETTEXT, 0, 0), PATH_MAX);

				if (string[0] != '\0')
					g_key_file_set_string(gCfg, ext, "read_cmd", string);

				g_strlcpy(string, (char*)gStartupInfo->SendDlgMsg(pDlg, "edExternalHEX", DM_GETTEXT, 0, 0), PATH_MAX);

				if (string[0] != '\0')
				{
					if (check_hex_signature(string) == false)
						errmsg("Invalid hex value.", MB_OK | MB_ICONERROR);
					else
						g_key_file_set_string(gCfg, ext, "signature", string);
				}
			}
			else
				errmsg("Missing or incorrect file extension.", MB_OK | MB_ICONERROR);

			gStartupInfo->SendDlgMsg(pDlg, "btnExternalSave", DM_ENABLE, 0, 0);
			gStartupInfo->SendDlgMsg(pDlg, "btnExternalReload", DM_ENABLE, 0, 0);
			listbox_get_extentions(pDlg);
		}
		else if (strncmp(DlgItemName, "edExternal", 10) == 0)
			gStartupInfo->SendDlgMsg(pDlg, "btnExternalSave", DM_ENABLE, 1, 0);

		break;

	case DN_CHANGE:
		if (strncmp(DlgItemName, "chkMtree", 8) == 0)
		{
			bval = (bool)gStartupInfo->SendDlgMsg(pDlg, "chkMtreeAll", DM_GETCHECK, 0, 0);
			gStartupInfo->SendDlgMsg(pDlg, "gbMtreeAttr", DM_ENABLE, (intptr_t)!bval, 0);
			gStartupInfo->SendDlgMsg(pDlg, "gbMtreeCkSum", DM_ENABLE, (intptr_t)!bval, 0);

			g_strlcpy(string, "mtree:", PATH_MAX);

			if (bval)
				checkbox_get_option(pDlg, "chkMtreeAll", "all", false, string);
			else
			{
				checkbox_get_option(pDlg, "chkMtreeCksum", "cksum", false, string);
				checkbox_get_option(pDlg, "chkMtreeDevice", "device", true, string);
				checkbox_get_option(pDlg, "chkMtreeFlags", "flags", true, string);
				checkbox_get_option(pDlg, "chkMtreeGid", "gid", true, string);
				checkbox_get_option(pDlg, "chkMtreeGname", "gname", true, string);
				checkbox_get_option(pDlg, "chkMtreeLink", "link", true, string);
				checkbox_get_option(pDlg, "chkMtreeMd5", "md5", false, string);
				checkbox_get_option(pDlg, "chkMtreeMode", "mode", true, string);
				checkbox_get_option(pDlg, "chkMtreeNlink", "nlink", true, string);
				checkbox_get_option(pDlg, "chkMtreeRmd160", "rmd160", false, string);
				checkbox_get_option(pDlg, "chkMtreeSha1", "sha1", false, string);
				checkbox_get_option(pDlg, "chkMtreeSha256", "sha256", false, string);
				checkbox_get_option(pDlg, "chkMtreeSha384", "sha384", false, string);
				checkbox_get_option(pDlg, "chkMtreeSha512", "sha512", false, string);
				checkbox_get_option(pDlg, "chkMtreeSize", "size", true, string);
				checkbox_get_option(pDlg, "chkMtreeTime", "time", true, string);
				checkbox_get_option(pDlg, "chkMtreeUid", "uid", true, string);
				checkbox_get_option(pDlg, "chkMtreeUname", "uname", true, string);
				checkbox_get_option(pDlg, "chkMtreeResdevice", "resdevice", false, string);
			}

			checkbox_get_option(pDlg, "chkMtreeDirsOnly", "dironly", false, string);
			checkbox_get_option(pDlg, "chkMtreeUseSet", "use-set", false, string);
			checkbox_get_option(pDlg, "chkMtreeIndent", "indent", false, string);

			if (strcmp(string, "mtree:") != 0)
				gStartupInfo->SendDlgMsg(pDlg, "edOptions", DM_SETTEXT, (intptr_t)string, 0);
			else
				gStartupInfo->SendDlgMsg(pDlg, "edOptions", DM_SETTEXT, 0, 0);
		}
		else if (strncmp(DlgItemName, "cb7Z", 4) == 0)
		{
			numval = (int)gStartupInfo->SendDlgMsg(pDlg, "cb7ZCompression", DM_LISTGETITEMINDEX, 0, 0);

			if (numval > -1)
			{
				snprintf(string, PATH_MAX, "7zip:compression=%s", (char*)gStartupInfo->SendDlgMsg(pDlg, "cb7ZCompression", DM_GETTEXT, 0, 0));
			}
			else
				g_strlcpy(string, "7zip:", PATH_MAX);

			numval = (int)gStartupInfo->SendDlgMsg(pDlg, "cb7ZCompLvl", DM_LISTGETITEMINDEX, 0, 0);

			if (numval > -1)
			{
				if (strlen(string) > 0 && string[strlen(string) - 1] != ':')
					strcat(string, ",");

				snprintf(string, PATH_MAX, "%scompression-level=%d", strdup(string), numval);
			}

			if (strcmp(string, "7zip:") != 0)
				gStartupInfo->SendDlgMsg(pDlg, "edOptions", DM_SETTEXT, (intptr_t)string, 0);
			else
				gStartupInfo->SendDlgMsg(pDlg, "edOptions", DM_SETTEXT, 0, 0);

		}
		else if (strstr(DlgItemName, "Zip") != NULL)
		{
			numval = (int)gStartupInfo->SendDlgMsg(pDlg, "cbZipCompression", DM_LISTGETITEMINDEX, 0, 0);

			if (numval > -1)
			{
				snprintf(string, PATH_MAX, "zip:compression=%s", (char*)gStartupInfo->SendDlgMsg(pDlg, "cbZipCompression", DM_GETTEXT, 0, 0));
			}
			else
				g_strlcpy(string, "zip:", PATH_MAX);

			numval = (int)gStartupInfo->SendDlgMsg(pDlg, "cbZipCompLvl", DM_LISTGETITEMINDEX, 0, 0);

			if (numval > -1)
			{
				if (strlen(string) > 0 && string[strlen(string) - 1] != ':')
					strcat(string, ",");

				snprintf(string, PATH_MAX, "%scompression-level=%d", strdup(string), numval);
			}

			textfield_get_option(pDlg, "cbZipCharset", "hdrcharset", string);
			checkbox_get_option(pDlg, "chkZipExperimental", "experimental", false, string);
			checkbox_get_option(pDlg, "chkZipFakeCRC32", "fakecrc32", false, string);
			checkbox_get_option(pDlg, "chkZip64", "zip64", true, string);


			if (strcmp(string, "zip:") != 0)
				gStartupInfo->SendDlgMsg(pDlg, "edOptions", DM_SETTEXT, (intptr_t)string, 0);
			else
				gStartupInfo->SendDlgMsg(pDlg, "edOptions", DM_SETTEXT, 0, 0);
		}
		else if (strstr(DlgItemName, "ISO") != NULL)
		{
			g_strlcpy(string, "iso9660:", PATH_MAX);
			textfield_get_option(pDlg, "edISOVolumeID", "volume-id", string);
			textfield_get_option(pDlg, "edISOAbstractFile", "abstract-file", string);
			textfield_get_option(pDlg, "edISOApplicationID", "application-id", string);
			textfield_get_option(pDlg, "edISOCopyrightFile", "copyright-file", string);
			textfield_get_option(pDlg, "edISOPublisher", "publisher", string);
			textfield_get_option(pDlg, "edISOBiblioFile", "biblio-file", string);

			numval = (int)gStartupInfo->SendDlgMsg(pDlg, "cbISOLevel", DM_LISTGETITEMINDEX, 0, 0);

			if (numval > -1)
			{
				if (strlen(string) > 0 && string[strlen(string) - 1] != ':')
					strcat(string, ",");

				snprintf(string, PATH_MAX, "%siso-level=%d", strdup(string), numval + 1);
			}

			checkbox_get_option(pDlg, "chkISOAllowVernum", "allow-vernum", true, string);
			checkbox_get_option(pDlg, "chkISOJoliet", "joliet", true, string);
			checkbox_get_option(pDlg, "chkISOLimitDepth", "limit-depth", true, string);
			checkbox_get_option(pDlg, "chkISOLimitDirs", "limit-dirs", true, string);
			checkbox_get_option(pDlg, "chkISOPad", "pad", true, string);
			checkbox_get_option(pDlg, "chkISORockridge", "rockridge", true, string);

			textfield_get_option(pDlg, "edISOBoot", "boot", string);
			textfield_get_option(pDlg, "edISOBootCatalog", "boot-catalog", string);
			textfield_get_option(pDlg, "cbISOBootType", "boot-type", string);
			checkbox_get_option(pDlg, "chkISOBootInfoTable", "boot-info-table", false, string);


			numval = (int)gStartupInfo->SendDlgMsg(pDlg, "cbISOBootType", DM_LISTGETITEMINDEX, 0, 0);

			if (numval > 1)
			{
				gStartupInfo->SendDlgMsg(pDlg, "edISOBootLoadSeg", DM_ENABLE, 0, 0);
				gStartupInfo->SendDlgMsg(pDlg, "edISOBootLoadSize", DM_ENABLE, 0, 0);

			}
			else
			{
				gStartupInfo->SendDlgMsg(pDlg, "edISOBootLoadSeg", DM_ENABLE, 1, 0);
				gStartupInfo->SendDlgMsg(pDlg, "edISOBootLoadSize", DM_ENABLE, 1, 0);
				textfield_get_option(pDlg, "edISOBootLoadSeg", "boot-load-seg", string);
				textfield_get_option(pDlg, "edISOBootLoadSize", "boot-load-size", string);
			}

			checkbox_get_option(pDlg, "chkZISOfs", "zisofs", false, string);
			bval = (bool)gStartupInfo->SendDlgMsg(pDlg, "chkZISOfs", DM_GETCHECK, 0, 0);

			if (bval)
			{
				gStartupInfo->SendDlgMsg(pDlg, "cbZISOCompLvl", DM_ENABLE, 1, 0);

				numval = (int)gStartupInfo->SendDlgMsg(pDlg, "cbZISOCompLvl", DM_LISTGETITEMINDEX, 0, 0);

				if (numval > -1)
				{
					if (strlen(string) > 0 && string[strlen(string) - 1] != ':')
						strcat(string, ",");

					snprintf(string, PATH_MAX, "%scompression-level=%d", strdup(string), numval);
				}
			}
			else
				gStartupInfo->SendDlgMsg(pDlg, "cbZISOCompLvl", DM_ENABLE, 0, 0);

			if (strcmp(string, "iso9660:") != 0)
				gStartupInfo->SendDlgMsg(pDlg, "edOptions", DM_SETTEXT, (intptr_t)string, 0);
			else
				gStartupInfo->SendDlgMsg(pDlg, "edOptions", DM_SETTEXT, 0, 0);

		}
		else if (strstr(DlgItemName, "Filters") != NULL)
		{
			if (strcmp(DlgItemName, "cbFiltersZSTDComprLvl") == 0)
			{
				g_strlcpy(string, "zstd:", PATH_MAX);

				numval = (int)gStartupInfo->SendDlgMsg(pDlg, "cbFiltersZSTDComprLvl", DM_LISTGETITEMINDEX, 0, 0);

				if (numval > -1)
				{
					gStartupInfo->SendDlgMsg(pDlg, "cbFiltersCompLvl", DM_SETTEXT, 0, 0);
					snprintf(string, PATH_MAX, "%scompression-level=%d", strdup(string), numval + 1);
				}
			}
			else if (strstr(DlgItemName, "FiltersLZ4") != NULL)
			{
				g_strlcpy(string, "lz4:", PATH_MAX);
				checkbox_get_option(pDlg, "chkFiltersLZ4StreamChksum", "stream-checksum", true, string);
				checkbox_get_option(pDlg, "chkFiltersLZ4BlockChksum", "block-checksum", true, string);
				checkbox_get_option(pDlg, "chkFiltersLZ4BlockDependence", "block-dependence", false, string);
				textfield_get_option(pDlg, "cbFiltersLZ4BlockSize", "block-size", string);
			}
			else if (strcmp(DlgItemName, "chkFiltersGZTimestamp") == 0)
			{
				g_strlcpy(string, "gzip:", PATH_MAX);
				checkbox_get_option(pDlg, "chkFiltersGZTimestamp", "timestamp", true, string);
			}
			else if (strcmp(DlgItemName, "edFiltersLZMAThreads") == 0)
			{
				g_strlcpy(string, "lzma:", PATH_MAX);
				textfield_get_option(pDlg, "edFiltersLZMAThreads", "threads", string);
			}
			else if (strcmp(DlgItemName, "cbFiltersLrzipCompr") == 0)
			{
				g_strlcpy(string, "lrzip:", PATH_MAX);
				textfield_get_option(pDlg, "cbFiltersLrzipCompr", "compression", string);
			}

			numval = (int)gStartupInfo->SendDlgMsg(pDlg, "cbFiltersCompLvl", DM_LISTGETITEMINDEX, 0, 0);

			if (numval > -1)
			{
				if (strlen(string) > 0 && string[strlen(string) - 1] == ':')
					snprintf(string, PATH_MAX, "compression-level=%d", numval);
				else
				{
					if (strlen(string) > 0 && string[strlen(string) - 1] != '\0')
						strcat(string, ",");

					snprintf(string, PATH_MAX, "%scompression-level=%d", strdup(string), numval);
				}
			}

			char *tmpval = (char *)gStartupInfo->SendDlgMsg(pDlg, "cbFiltersFormatCharset", DM_GETTEXT, 0, 0);

			if (tmpval[0] != '\0')
			{
				if (strlen(string) > 0 && string[strlen(string) - 1] == ':')
					snprintf(string, PATH_MAX, "hdrcharset=%s", tmpval);
				else
				{
					if (strlen(string) > 0 && string[strlen(string) - 1] != '\0')
						strcat(string, ",");

					snprintf(string, PATH_MAX, "%shdrcharset=%s", strdup(string), tmpval);
				}
			}

			if (strlen(string) > 0 && string[strlen(string) - 1] != ':')
				gStartupInfo->SendDlgMsg(pDlg, "edOptions", DM_SETTEXT, (intptr_t)string, 0);
			else
				gStartupInfo->SendDlgMsg(pDlg, "edOptions", DM_SETTEXT, 0, 0);
		}

		break;

	case DM_KEYUP:
		if (strncmp(DlgItemName, "edExternal", 10) == 0)
		{
			gStartupInfo->SendDlgMsg(pDlg, "btnExternalSave", DM_ENABLE, 1, 0);
			gchar *ext = (char*)gStartupInfo->SendDlgMsg(pDlg, "edExternalExt", DM_GETTEXT, 0, 0);

			if (ext && g_key_file_has_group(gCfg, ext))
				gStartupInfo->SendDlgMsg(pDlg, "btnExternalReload", DM_ENABLE, 1, 0);
			else
				gStartupInfo->SendDlgMsg(pDlg, "btnExternalReload", DM_ENABLE, 0, 0);
		}

		break;
	}

	if (err)
		g_error_free(err);

	return 0;
}

HANDLE DCPCALL OpenArchive(tOpenArchiveData *ArchiveData)
{
	tArcData * handle;
	handle = malloc(sizeof(tArcData));

	if (handle == NULL)
	{
		ArchiveData->OpenResult = E_NO_MEMORY;
		return E_SUCCESS;
	}

	memset(handle, 0, sizeof(tArcData));
	handle->archive = archive_read_new();

	if (archive_read_set_passphrase_callback(handle->archive, NULL, archive_password_cb) < ARCHIVE_OK)
		errmsg(archive_error_string(handle->archive), MB_OK | MB_ICONERROR);

	archive_read_support_filter_all(handle->archive);
	const char *ext = strrchr(ArchiveData->ArcName, '.');
	add_read_programs_from_cfg(handle->archive, ext);
	archive_read_support_format_raw(handle->archive);

	if (gOnlyTarFormat)
		archive_read_support_format_by_code(handle->archive, gTarFormat);
	else
		archive_read_support_format_all(handle->archive);

	g_strlcpy(handle->arcname, ArchiveData->ArcName, PATH_MAX);

	if (gMtreeCheckFS && strcasestr(ArchiveData->ArcName, ".mtree") != NULL)
		archive_read_set_options(handle->archive, "checkfs");
	else if ((gReadDisableJoliet || gReadDisableRockridge) && strcasestr(ArchiveData->ArcName, ".iso") != NULL)
	{
		if (gReadDisableJoliet)
			archive_read_set_options(handle->archive, "!joliet");

		if (gReadDisableRockridge)
			archive_read_set_options(handle->archive, "!rockridge");
	}
	else
	{
		if (gReadIgnoreCRC)
			archive_read_set_options(handle->archive, "ignorecrc32");

		if (gReadMacExt)
			archive_read_set_options(handle->archive, "mac-ext");

		if (gReadCompat2x)
			archive_read_set_options(handle->archive, "compat-2x");

		if (gReadCharset[0] != '\0')
			archive_read_set_format_option(handle->archive, NULL, "hdrcharset", gReadCharset);
	}

	int r = archive_read_open_filename(handle->archive, ArchiveData->ArcName, 10240);

	if (r != ARCHIVE_OK)
	{
		archive_read_close(handle->archive);
		archive_read_free(handle->archive);
		free(handle);
		ArchiveData->OpenResult = E_UNKNOWN_FORMAT;
		return E_SUCCESS;
	}

	return (HANDLE)handle;
}

int DCPCALL ReadHeader(HANDLE hArcData, tHeaderData *HeaderData)
{
	return E_NOT_SUPPORTED;
}

int DCPCALL ReadHeaderEx(HANDLE hArcData, tHeaderDataEx *HeaderDataEx)
{
	int ret;
	int64_t size;
	memset(HeaderDataEx, 0, sizeof(&HeaderDataEx));
	ArcData handle = (ArcData)hArcData;
	char *filename = NULL;
	char arcname[PATH_MAX + 1];
	struct stat st;

	while ((ret = archive_read_next_header(handle->archive, &handle->entry)) == ARCHIVE_RETRY ||
	                (gReadSkipDot && (ret == ARCHIVE_OK && strcmp(".", archive_entry_pathname(handle->entry)) == 0)))
	{
		if (ret == ARCHIVE_RETRY && errmsg(archive_error_string(handle->archive),
		                                   MB_RETRYCANCEL | MB_ICONWARNING) != ID_RETRY)
			return E_EABORTED;
	}

	if (ret == ARCHIVE_FATAL)
	{
		errmsg(archive_error_string(handle->archive), MB_OK | MB_ICONERROR);
		return E_BAD_ARCHIVE;
	}

	if (ret != ARCHIVE_EOF)
	{
		if (ret == ARCHIVE_WARN)
			printf("libarchive: %s\n", archive_error_string(handle->archive));

		if (archive_format(handle->archive) == ARCHIVE_FORMAT_RAW)
		{
			g_strlcpy(arcname, handle->arcname, PATH_MAX);
			filename = basename(arcname);

			if (filename)
			{
				char *dot = strrchr(filename, '.');

				if (dot != NULL)
					*dot = '\0';

				g_strlcpy(HeaderDataEx->FileName, filename, sizeof(HeaderDataEx->FileName) - 1);

				HeaderDataEx->UnpSizeHigh = 0xFFFFFFFF;
				HeaderDataEx->UnpSize = 0xFFFFFFFE;
				HeaderDataEx->FileAttr = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

				if (stat(handle->arcname, &st) == 0)
				{
					HeaderDataEx->PackSizeHigh = (st.st_size & 0xFFFFFFFF00000000) >> 32;
					HeaderDataEx->PackSize = st.st_size & 0x00000000FFFFFFFF;
					HeaderDataEx->FileTime = st.st_mtime;
				}
			}
			else
				g_strlcpy(HeaderDataEx->FileName, "<!!!ERROR!!!>", sizeof(HeaderDataEx->FileName) - 1);
		}
		else
		{
			filename = (char*)archive_entry_pathname(handle->entry);

			if (!filename)
				g_strlcpy(HeaderDataEx->FileName, "<!!!ERROR!!!>", sizeof(HeaderDataEx->FileName) - 1);
			else
			{
				if (filename[0] == '/')
					g_strlcpy(HeaderDataEx->FileName, filename + 1, sizeof(HeaderDataEx->FileName) - 1);
				else if (gReadSkipDot && filename[0] == '.' && filename[1] == '/')
					g_strlcpy(HeaderDataEx->FileName, filename + 2, sizeof(HeaderDataEx->FileName) - 1);
				else
					g_strlcpy(HeaderDataEx->FileName, filename, sizeof(HeaderDataEx->FileName) - 1);
			}

			size = archive_entry_size(handle->entry);
			HeaderDataEx->PackSizeHigh = (size & 0xFFFFFFFF00000000) >> 32;
			HeaderDataEx->PackSize = size & 0x00000000FFFFFFFF;
			HeaderDataEx->UnpSizeHigh = (size & 0xFFFFFFFF00000000) >> 32;
			HeaderDataEx->UnpSize = size & 0x00000000FFFFFFFF;
			HeaderDataEx->FileTime = archive_entry_mtime(handle->entry);
			HeaderDataEx->FileAttr = archive_entry_mode(handle->entry);

			if (archive_entry_is_encrypted(handle->entry))
				HeaderDataEx->Flags |= RHDF_ENCRYPTED;
		}

		return E_SUCCESS;
	}

	return E_END_ARCHIVE;
}

int DCPCALL ProcessFile(HANDLE hArcData, int Operation, char *DestPath, char *DestName)
{
	int ret;
	int result = E_SUCCESS;
	size_t size;
	la_int64_t offset;
	const void *buff;
	struct archive *a;
	const char *pathname;
	char filename[PATH_MAX];
	ArcData handle = (ArcData)hArcData;

	if (Operation != PK_SKIP && !DestPath)
	{
		pathname = archive_entry_pathname(handle->entry);

		if (!pathname)
			g_strlcpy(filename, handle->arcname, PATH_MAX);
		else
			g_strlcpy(filename, pathname, PATH_MAX);

		if (Operation == PK_EXTRACT)
		{
			int fd = open(DestName, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

			if (fd == -1)
				return E_ECREATE;
			else
				close(fd);

			int flags = ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_FFLAGS;

			a = archive_write_disk_new();
			archive_entry_set_pathname(handle->entry, DestName);
			archive_write_disk_set_options(a, flags);
			archive_write_disk_set_standard_lookup(a);
			archive_write_header(a, handle->entry);
		}

		while ((ret = archive_read_data_block(handle->archive, &buff, &size, &offset)) != ARCHIVE_EOF)
		{
			if (ret < ARCHIVE_OK)
			{
				if (Operation == PK_TEST)
				{
					printf("libarchive: %s\n", archive_error_string(handle->archive));
					result = E_EREAD;
				}
				else
				{
					errmsg(archive_error_string(handle->archive), MB_OK | MB_ICONERROR);
					result = E_EABORTED;
				}

				break;
			}
			else if (Operation == PK_EXTRACT && archive_write_data_block(a, buff, size, offset) < ARCHIVE_OK)
			{
				//printf("libarchive: %s\n", archive_error_string(a));
				//result = E_EWRITE;
				errmsg(archive_error_string(a), MB_OK | MB_ICONERROR);
				result = E_EABORTED;
				break;
			}
			else if (handle->gProcessDataProc && handle->gProcessDataProc(filename, size) == 0)
			{
				result = E_EABORTED;
				break;
			}
		}

		if (Operation == PK_EXTRACT)
		{
			archive_write_finish_entry(a);
			archive_write_close(a);
			archive_write_free(a);
		}
	}

	return result;
}

int DCPCALL CloseArchive(HANDLE hArcData)
{
	ArcData handle = (ArcData)hArcData;
	archive_read_close(handle->archive);
	archive_read_free(handle->archive);
	free(handle);
	return E_SUCCESS;
}

void DCPCALL SetProcessDataProc(HANDLE hArcData, tProcessDataProc pProcessDataProc)
{
	ArcData handle = (ArcData)hArcData;

	if ((int)(long)hArcData == -1 || !handle)
		gProcessDataProc = pProcessDataProc;
	else
		handle->gProcessDataProc = pProcessDataProc;
}

void DCPCALL SetChangeVolProc(HANDLE hArcData, tChangeVolProc pChangeVolProc1)
{
	ArcData handle = (ArcData)hArcData;

	if ((int)(long)hArcData == -1 || !handle)
		gChangeVolProc = pChangeVolProc1;
	else
		handle->gChangeVolProc = pChangeVolProc1;
}

BOOL DCPCALL CanYouHandleThisFile(char *FileName)
{
	const char *ext = strrchr(FileName, '.');
	struct archive *a = archive_read_new();
	archive_read_support_filter_all(a);
	add_read_programs_from_cfg(a, ext);

	if (gCanHandleRAW)
		archive_read_support_format_raw(a);

	if (gOnlyTarFormat)
		archive_read_support_format_by_code(a, gTarFormat);
	else
		archive_read_support_format_all(a);

	int r = archive_read_open_filename(a, FileName, 10240);
	archive_read_close(a);
	archive_read_free(a);

	if (r != ARCHIVE_OK)
		return false;
	else
		return true;
}

int DCPCALL GetPackerCaps(void)
{
	return PK_CAPS_NEW | PK_CAPS_SEARCHTEXT | PK_CAPS_BY_CONTENT | PK_CAPS_OPTIONS;
}

void DCPCALL ConfigurePacker(HWND Parent, HINSTANCE DllInstance)
{
	if (!gStartupInfo)
		printf("ConfigurePacker: gStartupInfo == NULL\n");
	else if (access(gLFMPath, F_OK) != 0)
	{
		if (g_key_file_load_from_file(gCfg, gCfgPath, G_KEY_FILE_KEEP_COMMENTS, NULL))
			config_get_options();

		char *msg;
		asprintf(&msg, "%s\nCurrent Options ($ man archive_write_set_options):", archive_version_details());
		gStartupInfo->InputBox("Double Commander", msg, false, gOptions, PATH_MAX - 1);
		free(msg);

		g_key_file_load_from_file(gCfg, gCfgPath, G_KEY_FILE_KEEP_COMMENTS, NULL);
		g_key_file_set_string(gCfg, "Global", "LastOptions", gOptions);
		g_key_file_save_to_file(gCfg, gCfgPath, NULL);
	}
	else
		gStartupInfo->DialogBoxLFMFile(gLFMPath, DlgProc);
}

int DCPCALL PackFiles(char *PackedFile, char *SubPath, char *SrcPath, char *AddList, int Flags)
{
	struct archive_entry *entry;
	struct stat st;
	char buff[BUFF_SIZE];
	int fd, ofd = -1, ret, id;
	ssize_t len;
	char fname[PATH_MAX];
	char infile[PATH_MAX];
	char pkfile[PATH_MAX];
	char link[PATH_MAX + 1];
	int result = E_SUCCESS;
	char *msg, *rmlist = NULL, *tmpfn = NULL;
	struct passwd *pw;
	struct group  *gr;
	size_t csize, prcnt;

	const char *ext = strrchr(PackedFile, '.');

	if (!ext)
		return E_NOT_SUPPORTED;

	if (Flags & PK_PACK_MOVE_FILES)
		rmlist = AddList;

	struct archive *a = archive_write_new();

	if ((ret = archive_set_format_filter(a, ext)) == ARCHIVE_WARN)
	{
		printf("libarchive: %s\n", archive_error_string(a));
	}
	else if (ret < ARCHIVE_OK)
	{
		errmsg(archive_error_string(a), MB_OK | MB_ICONERROR);
		archive_write_free(a);
		return 0;
	}

	if (access(PackedFile, F_OK) != -1)
		result = archive_repack_existing(a, PackedFile, &tmpfn, ofd, AddList, SubPath, Flags, ext);
	else
	{

		if ((gOptions[0] != '\0') && archive_write_set_options(a, gOptions) < ARCHIVE_OK)
		{
			errmsg(archive_error_string(a), MB_OK | MB_ICONWARNING);
		}

		if (archive_filter_code(a, 0) == ARCHIVE_FILTER_UU)
		{
			asprintf(&msg, "name=%s", AddList);
			archive_write_set_options(a, msg);

			strcpy(infile, SrcPath);
			char* pos = strrchr(infile, '/');
			g_strlcpy(fname, AddList, PATH_MAX);

			if (pos != NULL)
				strcpy(pos + 1, fname);
			else
				strcpy(infile, fname);

			if (stat(infile, &st) == 0)
			{
				asprintf(&msg, "mode=%o", st.st_mode);
				archive_write_set_options(a, msg);
			}

			free(msg);
		}

		archive_write_set_passphrase_callback(a, NULL, archive_password_cb);

		if (Flags & PK_PACK_ENCRYPT)
		{
			if (archive_write_set_option(a, NULL, "encryption", gEncryption) < ARCHIVE_OK)
				errmsg(archive_error_string(a), MB_OK | MB_ICONERROR);
		}

		mode_t omode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

		if (strcasecmp(ext, ".shar") == 0 || strcasecmp(ext, ".run") == 0)
			omode |= S_IXUSR | S_IXGRP | S_IXOTH;

		ofd = open(PackedFile, O_WRONLY | O_CREAT | O_TRUNC, omode);

		if (ofd == -1)
			result = E_ECREATE;
		else if (archive_write_open_fd(a, ofd) < ARCHIVE_OK)
		{
			errmsg(archive_error_string(a), MB_OK | MB_ICONERROR);
			result = E_ECREATE;
		}
	}

	if (result == E_SUCCESS)
	{
		struct archive *disk = archive_read_disk_new();
		archive_read_disk_set_standard_lookup(disk);
		archive_read_disk_set_symlink_physical(disk);

		while (*AddList)
		{
			strcpy(infile, SrcPath);
			char* pos = strrchr(infile, '/');
			g_strlcpy(fname, AddList, PATH_MAX);

			if (pos != NULL)
				strcpy(pos + 1, fname);
			else
				strcpy(infile, fname);

			if (!(Flags & PK_PACK_SAVE_PATHS))
				g_strlcpy(fname, strdup(basename(fname)), PATH_MAX);

			if (!SubPath)
				strcpy(pkfile, fname);
			else
				snprintf(pkfile, PATH_MAX, "%s/%s", SubPath, fname);

			if (gProcessDataProc(infile, 0) == 0)
				result = E_EABORTED;

			while ((ret = lstat(infile, &st)) != 0)
			{
				int errsv = errno;
				asprintf(&msg, "%s: %s", infile, strerror(errsv));

				id = errmsg(msg, MB_ABORTRETRYIGNORE | MB_ICONERROR);

				if (id == ID_ABORT)
					result = E_EABORTED;

				free(msg);

				if (id != ID_RETRY)
					break;
			}

			if ((ret == 0) && !(S_ISDIR(st.st_mode) && !(Flags & PK_PACK_SAVE_PATHS)))
			{

				entry = archive_entry_new();

				if (strcmp(ext, ".mtree") == 0 && mtree_opts_nodata())
				{
					archive_entry_copy_stat(entry, &st);
					pw = getpwuid(st.st_uid);
					gr = getgrgid(st.st_gid);

					if (gr)
						archive_entry_set_gname(entry, gr->gr_name);

					if (pw)
						archive_entry_set_uname(entry, pw->pw_name);

					if (S_ISLNK(st.st_mode))
					{
						if ((len = readlink(pkfile, link, sizeof(link) - 1)) != -1)
						{
							link[len] = '\0';
							archive_entry_set_symlink(entry, link);
						}
						else
							archive_entry_set_symlink(entry, "");
					}

					if ((gOptions[0] == '\0') || (strstr(gOptions, "!flags") == NULL))
					{
						if (S_ISFIFO(st.st_mode))
						{
							asprintf(&msg, "%s: ignoring flags for named pipe.", infile);

							if (errmsg(msg, MB_OKCANCEL | MB_ICONWARNING) == ID_CANCEL)
								result = E_EABORTED;

							free(msg);
						}
						else
						{
							int stflags;

							if ((fd = open(infile, O_RDONLY)) == -1)
							{
								int errsv = errno;
								//printf("libarchive: %s: %s\n", infile, strerror(errsv));
								asprintf(&msg, "%s: %s", infile, strerror(errsv));

								if (errmsg(msg, MB_OKCANCEL | MB_ICONWARNING) == ID_CANCEL)
									result = E_EABORTED;

								free(msg);
							}

							if (fd != -1)
							{
								ret = ioctl(fd, FS_IOC_GETFLAGS, &stflags);

								if (ret == 0 && stflags != 0)
									archive_entry_set_fflags(entry, stflags, 0);
							}

							close(fd);
						}
					}

					archive_entry_set_pathname(entry, pkfile);
					archive_write_header(a, entry);

					if (st.st_size < INT_MAX && gProcessDataProc(infile, st.st_size) == 0)
						result = E_EABORTED;
				}
				else if (S_ISFIFO(st.st_mode))
				{
					asprintf(&msg, "%s: ignoring named pipe.", infile);

					if (errmsg(msg, MB_OKCANCEL | MB_ICONWARNING) == ID_CANCEL)
						result = E_EABORTED;

					free(msg);
				}
				else
				{

					while ((fd = open(infile, O_RDONLY)) == -1 && !S_ISLNK(st.st_mode))
					{
						int errsv = errno;
						asprintf(&msg, "%s: %s", infile, strerror(errsv));

						id = errmsg(msg, MB_ABORTRETRYIGNORE | MB_ICONERROR | MB_DEFBUTTON3);

						if (id == ID_ABORT)
							result = E_EABORTED;

						free(msg);

						if (id != ID_RETRY)
							break;
					}

					if (fd != -1)
					{
						archive_entry_set_pathname(entry, infile);
						archive_entry_copy_stat(entry, &st);
						archive_read_disk_entry_from_file(disk, entry, fd, &st);
						archive_entry_set_pathname(entry, pkfile);
						archive_write_header(a, entry);
						csize = 0;

						while ((len = read(fd, buff, sizeof(buff))) > 0)
						{
							if (archive_write_data(a, buff, len) < ARCHIVE_OK)
							{
								errmsg(archive_error_string(a), MB_OK | MB_ICONERROR);
								result = E_EWRITE;
								break;
							}

							if (gProcessDataProc(infile, len) == 0)
							{
								result = E_EABORTED;
								break;
							}

							if (st.st_size > 0)
							{
								csize += len;
								prcnt = csize * 100 / st.st_size;
							}
							else
								prcnt = 0;

							if (gProcessDataProc(infile, -(1000 + prcnt)) == 0)
							{
								result = E_EABORTED;
								break;
							}
						}

						close(fd);
					}
					else if (S_ISLNK(st.st_mode) && result != E_EABORTED)
					{
						archive_entry_copy_stat(entry, &st);
						archive_entry_set_pathname(entry, pkfile);
						pw = getpwuid(st.st_uid);
						gr = getgrgid(st.st_gid);

						if (gr)
							archive_entry_set_gname(entry, gr->gr_name);

						if (pw)
							archive_entry_set_uname(entry, pw->pw_name);

						if ((len = readlink(pkfile, link, sizeof(link) - 1)) != -1)
						{
							link[len] = '\0';
							archive_entry_set_symlink(entry, link);
						}
						else
							archive_entry_set_symlink(entry, "");

						archive_write_header(a, entry);

						if (st.st_size < INT_MAX)
							gProcessDataProc(infile, st.st_size);
					}
				}

				archive_entry_free(entry);
			}

			if (result != E_SUCCESS)
				break;

			while (*AddList++);
		}

		archive_read_free(disk);
		archive_write_finish_entry(a);
	}

	archive_write_close(a);
	archive_write_free(a);

	if (ofd > -1)
		close(ofd);

	if ((Flags & PK_PACK_MOVE_FILES && result == E_SUCCESS) &&
	                (!gNotabene || errmsg("Now WILL TRY to REMOVE ALL (including SKIPPED!) source files. Are you sure you want this?", MB_YESNO | MB_ICONWARNING) == ID_YES))
	{
		while (*rmlist)
		{
			strcpy(infile, SrcPath);
			char* pos = strrchr(infile, '/');

			if (pos != NULL)
				strcpy(pos + 1, rmlist);

			remove_target(infile);

			while (*rmlist++);
		}
	}

	if (tmpfn)
	{
		if (result == E_SUCCESS)
			rename(tmpfn, PackedFile);
		else
			remove_file(tmpfn);

		free(tmpfn);
	}

	return result;
}

int DCPCALL DeleteFiles(char *PackedFile, char *DeleteList)
{
	char *tmpfn = NULL;
	int ofd = -1, ret, result = E_SUCCESS;

	const char *ext = strrchr(PackedFile, '.');

	struct archive *a = archive_write_new();

	if ((ret = archive_set_format_filter(a, ext)) == ARCHIVE_WARN)
	{
		printf("libarchive: %s\n", archive_error_string(a));
	}
	else if (ret < ARCHIVE_OK)
	{
		errmsg(archive_error_string(a), MB_OK | MB_ICONERROR);
		archive_write_free(a);
		return 0;
	}

	result = archive_repack_existing(a, PackedFile, &tmpfn, ofd, DeleteList, NULL, PK_PACK_SAVE_PATHS, ext);
	gProcessDataProc(PackedFile, -100);
	archive_write_finish_entry(a);
	archive_write_close(a);
	archive_write_free(a);

	if (ofd > -1)
		close(ofd);

	if (result == E_SUCCESS)
		rename(tmpfn, PackedFile);

	if (tmpfn)
		free(tmpfn);

	return result;
}
