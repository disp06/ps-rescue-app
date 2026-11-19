#ifndef CONFIG_H
#define CONFIG_H

#define CFG_PATH_MAX    256
#define CFG_SECTION_MAX 32

typedef enum { CL_BLANK, CL_COMMENT, CL_SECTION, CL_ENTRY } CfgLineType;

typedef enum {
	CS_NONE = 0,
	CS_OK,
	CS_MISSING,        /* device mounted, file not there           */
	CS_NODEV,          /* device (uma0:, imc0: ...) not mounted now */
	CS_WRONG_SECTION,  /* .skprx outside *KERNEL / .suprx inside it */
	CS_ODD_EXT,        /* neither .skprx nor .suprx                 */
	CS_NO_SECTION,     /* entry before any *SECTION header          */
	CS_BAD_PATH        /* no device prefix                          */
} CfgStatus;

typedef struct {
	char        *text;                     /* raw line, no EOL      */
	CfgLineType  type;
	char         section[CFG_SECTION_MAX]; /* "KERNEL", "main", ... */
	char         path[CFG_PATH_MAX];       /* trimmed entry path    */
	CfgStatus    status;
	int          warn_early_dev;           /* *KERNEL plugin not on ur0: */
} CfgLine;

typedef struct {
	CfgLine *lines;
	int      count, cap;
	int      crlf, trailing_eol, loaded;
	char     src[64];
} Config;

int   cfg_load(Config *c, const char *path);
void  cfg_free(Config *c);
void  cfg_analyze(Config *c);
char *cfg_serialize(const Config *c, int *out_len);
int   cfg_save(Config *c, char *backup_out, int bsz);
void  cfg_comment_out(Config *c, int i, const char *why);
void  cfg_insert(Config *c, int at, const char *text);
void  cfg_delete(Config *c, int i);
int   cfg_find_section(const Config *c, const char *name);
int   cfg_section_end(const Config *c, int sec);
int   cfg_entry_matches(const CfgLine *l, const char *needle);
const char *cfg_status_name(CfgStatus s);
int   cfg_is_broken(const CfgLine *l);

#endif
