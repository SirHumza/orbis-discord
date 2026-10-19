/* msgview.c - message list layout. Pure C: no renderer, no orbis headers,
 * so tests/test_msgview.c compiles this file directly (same pattern as
 * ws_skip.c). */
#include "msgview.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *MONTHS[12] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

void mv_init(mv_layout *L){
    if(!L) return;
    memset(L, 0, sizeof *L);
}

void mv_free(mv_layout *L){
    if(!L) return;
    free(L->lines);
    memset(L, 0, sizeof *L);
}

uint64_t mv_day(uint64_t ms){
    return ms / 86400000ULL;
}

static void civil_from_days(int64_t z, int *yy, int *mm, int *dd){
    int64_t era;
    unsigned doe, yoe, doy, mp;
    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = (unsigned)(z - era * 146097);
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    {
        int64_t y = (int64_t)yoe + era * 400;
        doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        mp = (5 * doy + 2) / 153;
        *dd = (int)(doy - (153 * mp + 2) / 5 + 1);
        *mm = (int)(mp < 10 ? mp + 3 : mp - 9);
        *yy = (int)(y + (*mm <= 2));
    }
}

void mv_time_str(uint64_t ms, char *out, size_t cap){
    unsigned secs;
    if(!out || cap == 0) return;
    secs = (unsigned)((ms / 1000ULL) % 86400ULL);
    snprintf(out, cap, "%02u:%02u", secs / 3600u, (secs % 3600u) / 60u);
}

void mv_day_str(uint64_t ms, char *out, size_t cap){
    int y = 0, m = 1, d = 1;
    if(!out || cap == 0) return;
    civil_from_days((int64_t)mv_day(ms), &y, &m, &d);
    snprintf(out, cap, "%s %d, %d", MONTHS[m - 1], d, y);
}

static int push_line(mv_layout *L, const message *m, int is_sep, int author,
                     int off, int len, int y){
    mv_line *ln;
    if(L->n >= L->cap){
        int nc = L->cap ? L->cap * 2 : 256;
        mv_line *p;
        if(nc > MV_MAX_LINES) nc = MV_MAX_LINES;
        if(nc <= L->cap) return 0;
        p = realloc(L->lines, (size_t)nc * sizeof *p);
        if(!p) return 0;
        L->lines = p;
        L->cap = nc;
    }
    ln = &L->lines[L->n++];
    ln->m = m;
    ln->is_sep = is_sep;
    ln->author = author;
    ln->off = off;
    ln->len = len;
    ln->y = y;
    return 1;
}

static size_t utf8_step(const char *s, size_t len){
    unsigned char c;
    size_t n = 1;
    if(len == 0) return 0;
    c = (unsigned char)s[0];
    if((c & 0xE0) == 0xC0) n = 2;
    else if((c & 0xF0) == 0xE0) n = 3;
    else if((c & 0xF8) == 0xF0) n = 4;
    if(n > len) n = len;
    return n;
}

/* Wraps one paragraph (no newlines inside) into rows. */
static int wrap_paragraph(mv_layout *L, const message *m, const char *s,
                          size_t start, size_t end, int width,
                          mv_measure_fn fn, void *ud, int y){
    size_t off = start;
    if(width < 1) width = 1;
    if(start == end){
        push_line(L, m, 0, 0, (int)start, 0, y);
        return y + MV_LINE_H;
    }
    while(off < end){
        size_t i = off, brk = 0, fit;
        while(i < end){
            size_t step = utf8_step(s + i, end - i);
            if(fn(ud, s + off, (i + step) - off) > width) break;
            i += step;
            if(s[i - 1] == ' ') brk = i;
        }
        if(i >= end){
            fit = end;
        } else {
            fit = brk > off ? brk : i;
            if(fit == off) fit = off + utf8_step(s + off, end - off);
        }
        while(fit > off && s[fit - 1] == ' ') fit--;
        push_line(L, m, 0, 0, (int)off, (int)(fit - off), y);
        y += MV_LINE_H;
        off = fit;
        while(off < end && s[off] == ' ') off++;
    }
    return y;
}

static int wrap_content(mv_layout *L, const message *m, const char *s, size_t len,
                        int width, mv_measure_fn fn, void *ud, int y){
    size_t off = 0;
    while(off < len){
        size_t end = off;
        while(end < len && s[end] != '\n') end++;
        y = wrap_paragraph(L, m, s, off, end, width, fn, ud, y);
        if(end >= len) break;
        off = end + 1;
    }
    return y;
}

int mv_build(mv_layout *L, const message *head, int width_px,
             mv_measure_fn fn, void *ud){
    const message *m;
    uint64_t prev_day = 0;
    int have_day = 0;
    int y = 0;
    if(!L) return 0;
    L->n = 0;
    L->height = 0;
    for(m = head; m; m = m->next){
        uint64_t t = (uint64_t)((m->id >> 22) + 1420070400000ULL);
        uint64_t day = mv_day(t);
        int grouped = m->is_grouped;
        if(!have_day || day != prev_day){
            push_line(L, m, 1, 0, 0, 0, y);
            y += MV_SEP_H;
            prev_day = day;
            have_day = 1;
            grouped = 0;
        }
        if(!grouped){
            push_line(L, m, 0, 1, 0, 0, y);
            y += MV_LINE_H;
        }
        y = wrap_content(L, m, m->content, strlen(m->content),
                         width_px, fn, ud, y);
        y += MV_GROUP_GAP;
    }
    L->height = y;
    return L->n;
}
