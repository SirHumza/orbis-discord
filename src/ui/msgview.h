/* msgview.h - message list layout (wrapping, grouping, day separators).
 * Pure: the caller passes a measure callback, so the host tests can pin the
 * math with a fake font. Drawing lives in screen_main.c. */
#ifndef UI_MSGVIEW_H
#define UI_MSGVIEW_H
#include <stdint.h>
#include <stddef.h>
#include "../model/store.h"

#define MV_LINE_H 22
#define MV_SEP_H 34
#define MV_GROUP_GAP 8
#define MV_MAX_LINES 2048
#define MV_MEASURE_MAX 2048
#define MV_ATT_MAX_W 400
#define MV_ATT_MAX_H 260
#define MV_REACT_H 26
#define MV_REPLY_H 20
#define MV_IMG_MAX 32

/* width in px of s[0..len) */
typedef int (*mv_measure_fn)(void *ud, const char *s, size_t len);

typedef struct {
    const message *m;   /* separator rows point at the day's first message */
    int is_sep;
    int author;         /* draw the author header on this row */
    int off;            /* byte offset into m->content */
    int len;            /* byte length of this content segment */
    int y;              /* absolute top y inside the content */
    int att;            /* attachment index, -1 for text rows */
    int att_w, att_h;   /* thumbnail box for image attachments */
    int emb;            /* embed index, -1 for non-embed rows */
    int emb_kind;       /* 1 title, 2 description, 3 image, 4 site, 5 footer,
                           6 field name, 7 field value */
    int fld;            /* embed field index, -1 for non-field rows */
    int react;          /* -1 for non-reaction rows */
    int reply;          /* -1 for non-reply rows */
} mv_line;

typedef struct {
    mv_line *lines;
    int n, cap;
    int height;
} mv_layout;

void mv_init(mv_layout *L);
void mv_free(mv_layout *L);
/* Rebuilds from the store list (oldest head first). Returns line count. */
int mv_build(mv_layout *L, const message *head, int width_px,
             mv_measure_fn fn, void *ud);
int mv_images(const message *head, const char **out, int max);
uint64_t mv_day(uint64_t ms);
void mv_time_str(uint64_t ms, char *out, size_t cap);
void mv_day_str(uint64_t ms, char *out, size_t cap);

#endif
