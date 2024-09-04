#include <glib.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include "wdxplugin.h"

#define imgtypes "jpeg|png|gif|svg|bmp|ico|xpm"

typedef struct _field
{
	char *name;
	int type;
	char *unit;
} FIELD;

#define fieldcount (sizeof(fields)/sizeof(FIELD))

FIELD fields[] =
{
	{"width",	ft_numeric_32,			""},
	{"height",	ft_numeric_32,			""},
	{"size",	ft_string,			""},
	{"type",	ft_multiplechoice,	  imgtypes},
	{"description",	ft_string,			""},
};

int DCPCALL ContentGetSupportedField(int FieldIndex, char* FieldName, char* Units, int maxlen)
{
	if (FieldIndex < 0 || FieldIndex >= fieldcount)
		return ft_nomorefields;

	g_strlcpy(FieldName, fields[FieldIndex].name, maxlen - 1);
	g_strlcpy(Units, fields[FieldIndex].unit, maxlen - 1);
	return fields[FieldIndex].type;
}

int DCPCALL ContentGetValue(char* FileName, int FieldIndex, int UnitIndex, void* FieldValue, int maxlen, int flags)
{
	int width;
	int height;
	gchar *string = NULL;

	if (!g_file_test(FileName, G_FILE_TEST_IS_REGULAR))
		return ft_fileerror;

	GdkPixbufFormat *fileinfo = gdk_pixbuf_get_file_info(FileName, &width, &height);

	if (!fileinfo)
		return ft_fileerror;

	switch (FieldIndex)
	{
	case 0:
		*(int*)FieldValue = width;
		break;

	case 1:
		*(int*)FieldValue = height;
		break;

	case 2:
		string = g_strdup_printf("%dx%d", width, height);

		if (string)
			g_strlcpy((char *)FieldValue, string, maxlen - 1);
		else
			return ft_fieldempty;

		break;

	case 3:
		string = gdk_pixbuf_format_get_name(fileinfo);

		if (string)
			g_strlcpy((char *)FieldValue, string, maxlen - 1);
		else
			return ft_fieldempty;

		break;

	case 4:
		string = gdk_pixbuf_format_get_description(fileinfo);

		if (string)
			g_strlcpy((char *)FieldValue, string, maxlen - 1);
		else
			return ft_fieldempty;

		break;

	default:
		return ft_nosuchfield;
	}

	g_free(string);

	return fields[FieldIndex].type;
}
