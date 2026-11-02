/* SPDX-License-Identifier: GPL-3.0-only
 * MOLECULAR: the patches. PATCH_SLOTS slots (0 .. 99), each the whole molecule as it sounds now (mol_d: the shape, the
 * atoms' values and LFOs, the generator's patch, chord and LFOs) with a check. A slot that does not check (never
 * written, or by a later version of the record) is empty.
 *
 * Build 75: kept in 64 KB of NOR flash (the device's from HAL_PATCH_BASE; the simulator's a file, the tests' memory),
 * through the platform's plat_flash_read, plat_flash_erase (a 4 KB sector) and plat_flash_program (bits to 0 only),
 * at offsets in those 64 KB. Seven records a sector, PATCH_REC_MAX bytes each: slot s in sector s % PF_SECTORS at
 * (s / PF_SECTORS) x PATCH_REC_MAX. Slots 0 .. 11 are where builds up to 74 kept their twelve (slot n at sector n's
 * start), so those patches stay. Sector 15 is the spare: a save writes the slot's whole sector anew there first (its
 * six other records and the new one) and marks it (PF_MARK + the sector, and the check of what it holds), then
 * erases the sector, copies the spare back and erases the spare. A save cut short (the power going) is finished at
 * the next start (patch_recover): the sector is copied back from the spare if the spare's mark and check hold. */

#define PATCH_SLOTS 100u
#define PATCH_MAGIC 0x504C4F4Du  /* "MOLP" */
#define PATCH_VER 2u             /* the record's layout: a new one whenever mol_design_t grows (fields only added at
                                  * its end, so older records still load: patch_used) */
#define PATCH_REC_MAX 584u       /* a record's room: seven to a sector, the mark after them */
#define PF_SECTORS 15u
#define PF_SPARE (PF_SECTORS * 4096u)
#define PF_MARK_AT (7u * PATCH_REC_MAX)
#define PF_MARK 0x4D4F4C53u      /* "SLOM": the spare holds sector (mark - PF_MARK), whole */

typedef struct {
    uint32_t magic;
    uint16_t ver, len;
    uint32_t check;              /* FNV-1a over d */
    mol_design_t d;
} patch_rec_t;
typedef char patch_rec_fits[sizeof(patch_rec_t) <= PATCH_REC_MAX ? 1 : -1];
typedef char patch_slots_fit[PATCH_SLOTS <= 7u * PF_SECTORS ? 1 : -1];

/* the platform's, at offsets in the patches' 64 KB: 0 when done */
static int plat_flash_read(uint32_t off, void *buf, uint32_t n);
static int plat_flash_erase(uint32_t off);                           /* the 4 KB sector at off, to 0xFF */
static int plat_flash_program(uint32_t off, const void *buf, uint32_t n);

static uint32_t patch_check_add(uint32_t h, const void *p, uint32_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    while (n--)
        h = (h ^ *b++) * 16777619u;
    return h;
}
static uint32_t patch_check(const void *p, uint32_t n) { return patch_check_add(2166136261u, p, n); }

static patch_rec_t patch_buf;    /* (one record, not on the stack) */

static uint32_t patch_off(uint32_t slot) { return slot % PF_SECTORS * 4096u + slot / PF_SECTORS * PATCH_REC_MAX; }

/* n bytes from src to dst (an erased place), 64 at a time; those all 0xFF left as erased. The check of what was
 * copied added to *h (h: 0, none) */
static int pf_copy(uint32_t dst, uint32_t src, uint32_t n, uint32_t *h)
{
    uint8_t b[64];
    while (n) {
        uint32_t k = n < sizeof b ? n : sizeof b, i, blank = 1;
        if (plat_flash_read(src, b, k))
            return -1;
        for (i = 0; i < k; i++)
            blank &= b[i] == 0xFFu;
        if (!blank && plat_flash_program(dst, b, k))
            return -1;
        if (h)
            *h = patch_check_add(*h, b, k);
        src += k;
        dst += k;
        n -= k;
    }
    return 0;
}
/* the check of the spare's records, as they read */
static int pf_spare_check(uint32_t *h)
{
    uint8_t b[64];
    uint32_t o;
    *h = 2166136261u;
    for (o = 0; o < PF_MARK_AT; o += sizeof b) {
        uint32_t k = PF_MARK_AT - o < sizeof b ? PF_MARK_AT - o : sizeof b;
        if (plat_flash_read(PF_SPARE + o, b, k))
            return -1;
        *h = patch_check_add(*h, b, k);
    }
    return 0;
}

/* one record into its slot through the spare (above); 0 when done */
static int patch_write(uint32_t slot, const void *rec, uint32_t n)
{
    uint32_t sec = slot % PF_SECTORS * 4096u, idx = slot / PF_SECTORS, i, mark[2];
    if (slot >= PATCH_SLOTS || n > PATCH_REC_MAX)
        return -1;
    if (plat_flash_erase(PF_SPARE))
        return -1;
    for (i = 0; i < 7u; i++)
        if (i != idx && pf_copy(PF_SPARE + i * PATCH_REC_MAX, sec + i * PATCH_REC_MAX, PATCH_REC_MAX, 0))
            return -1;
    if (plat_flash_program(PF_SPARE + idx * PATCH_REC_MAX, rec, n) || pf_spare_check(&mark[1]))
        return -1;
    mark[0] = PF_MARK + slot % PF_SECTORS;
    if (plat_flash_program(PF_SPARE + PF_MARK_AT, mark, sizeof mark))
        return -1;
    if (plat_flash_erase(sec) || pf_copy(sec, PF_SPARE, PF_MARK_AT, 0))
        return -1;
    return plat_flash_erase(PF_SPARE);
}
/* at the start: a save cut short finished (above). 1 if one was */
static int patch_recover(void)
{
    uint32_t mark[2], h, sec;
    if (plat_flash_read(PF_SPARE + PF_MARK_AT, mark, sizeof mark) || mark[0] - PF_MARK >= PF_SECTORS)
        return 0;
    if (pf_spare_check(&h) || h != mark[1])
        return 0;                                    /* (a spare half erased: its sector was copied back already) */
    sec = (mark[0] - PF_MARK) * 4096u;
    if (plat_flash_erase(sec) || pf_copy(sec, PF_SPARE, PF_MARK_AT, 0))
        return 0;
    plat_flash_erase(PF_SPARE);
    return 1;
}

/* 1 if the slot holds a patch (then in patch_buf.d). An older record, shorter (a later version only adds to the end
 * of mol_design_t), reads with the rest zero: as the older version made it */
static int patch_used(uint32_t slot)
{
    if (slot >= PATCH_SLOTS || plat_flash_read(patch_off(slot), &patch_buf, sizeof patch_buf))
        return 0;
    if (patch_buf.magic != PATCH_MAGIC || patch_buf.ver < 1u || patch_buf.ver > PATCH_VER ||
        patch_buf.len > sizeof patch_buf.d || patch_buf.check != patch_check(&patch_buf.d, patch_buf.len))
        return 0;
    memset((uint8_t *)&patch_buf.d + patch_buf.len, 0, sizeof patch_buf.d - patch_buf.len);
    return 1;
}
/* the slots that hold a patch, a bit each (m: PATCH_SLOTS bits) */
#define PATCH_MAP_BYTES ((PATCH_SLOTS + 7u) / 8u)
static void patch_used_map(uint8_t *m)
{
    uint32_t i;
    memset(m, 0, PATCH_MAP_BYTES);
    for (i = 0; i < PATCH_SLOTS; i++)
        if (patch_used(i))
            m[i >> 3] |= (uint8_t)(1u << (i & 7u));
}
/* the molecule as it sounds now into a slot (main loop); 1 when written and read back whole */
static int patch_save(uint32_t slot)
{
    if (slot >= PATCH_SLOTS)
        return 0;
    memset(&patch_buf, 0, sizeof patch_buf);
    patch_buf.magic = PATCH_MAGIC;
    patch_buf.ver = PATCH_VER;
    patch_buf.len = (uint16_t)sizeof patch_buf.d;
    memcpy(&patch_buf.d, &mol_d, sizeof mol_d);
    patch_buf.check = patch_check(&patch_buf.d, sizeof patch_buf.d);
    if (patch_write(slot, &patch_buf, sizeof patch_buf))
        return 0;
    return patch_used(slot) && !memcmp(&patch_buf.d, &mol_d, sizeof mol_d);
}
/* the slot's patch, sounding (through a fade); 1 if it held one */
static int patch_load(uint32_t slot)
{
    return patch_used(slot) && mol_load(&patch_buf.d);
}
