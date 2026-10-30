/* OPENMPK3 — pattern slots: 4 KiB each at 0x08015000 + slot * 0x1000; slot 8 is the hidden undo buffer. */
#include "patterns.h"
#include "flash.h"
#include <string.h>

#define PAT_BASE  0x08015000u
#define PAT_SIZE  0x1000u
#define PAT_MAGIC 0x5450504Eu   /* "NPPT": v2 header with voicing */

typedef struct {
    uint32_t magic;
    uint16_t n, length, bpm, sum;
    uint8_t  sound, root, loop_len, layer;
    uint8_t  voicing, spare[3];
} pat_hdr_t;   /* 20 bytes, followed by n loop_ev_t */

_Static_assert(sizeof(pat_hdr_t) + LOOP_MAX_EVENTS * sizeof(loop_ev_t) <= PAT_SIZE, "pattern slot too small");

static const pat_hdr_t *hdr(int slot) { return (const pat_hdr_t *)(PAT_BASE + (uint32_t)slot * PAT_SIZE); }
static uint16_t sum16(const void *p, uint32_t n) { const uint8_t *b = p; uint16_t s = 0; while (n--) s = (uint16_t)(s + *b++); return s; }

bool pattern_exists(int slot)
{
    const pat_hdr_t *h = hdr(slot);
    return slot >= 0 && slot < PATTERN_SLOTS && h->magic == PAT_MAGIC && h->n <= LOOP_MAX_EVENTS &&
           h->sum == sum16((const uint8_t *)h + sizeof *h, h->n * sizeof(loop_ev_t));
}

bool pattern_save(int slot, const looper_t *l, const pattern_ctx_t *ctx)
{
    if (slot < 0 || slot >= PATTERN_SLOTS) return false;
    uint32_t a = PAT_BASE + (uint32_t)slot * PAT_SIZE;
    uint8_t maxl = 0;
    for (int i = 0; i < l->n; i++) if (l->ev[i].layer > maxl) maxl = l->ev[i].layer;
    pat_hdr_t h = { PAT_MAGIC, l->n, l->length, ctx->bpm, sum16(l->ev, l->n * sizeof(loop_ev_t)),
                    ctx->sound, ctx->root, ctx->loop_len, maxl, ctx->voicing, { 0, 0, 0 } };
    flash_erase_page(a); flash_erase_page(a + FLASH_PAGE);
    flash_program(a + sizeof h, l->ev, l->n * sizeof(loop_ev_t));
    flash_program(a, &h, sizeof h);                       /* header last: a torn write reads as empty */
    return pattern_exists(slot);
}

void pattern_erase(int slot)
{
    if (slot < 0 || slot >= PATTERN_SLOTS || !pattern_exists(slot)) return;
    uint32_t a = PAT_BASE + (uint32_t)slot * PAT_SIZE;
    flash_erase_page(a); flash_erase_page(a + FLASH_PAGE);
}

bool pattern_load(int slot, looper_t *l, pattern_ctx_t *ctx)
{
    if (!pattern_exists(slot)) return false;
    const pat_hdr_t *h = hdr(slot);
    looper_clear(l);
    memcpy(l->ev, (const uint8_t *)h + sizeof *h, h->n * sizeof(loop_ev_t));
    l->n = h->n;
    l->layer = h->layer;
    ctx->bpm = h->bpm; ctx->sound = h->sound; ctx->root = h->root; ctx->loop_len = h->loop_len; ctx->voicing = h->voicing;
    return true;
}

void pattern_copy(int dst, int src)
{
    if (dst < 0 || dst >= PATTERN_SLOTS || src < 0 || src >= PATTERN_SLOTS || dst == src) return;
    uint32_t d = PAT_BASE + (uint32_t)dst * PAT_SIZE;
    flash_erase_page(d); flash_erase_page(d + FLASH_PAGE);
    if (!pattern_exists(src)) return;                 /* empty source -> empty destination */
    const pat_hdr_t *h = hdr(src);
    flash_program(d + sizeof *h, (const uint8_t *)h + sizeof *h, h->n * sizeof(loop_ev_t));   /* flash -> flash */
    flash_program(d, h, sizeof *h);
}
