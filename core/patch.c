/* SPDX-License-Identifier: GPL-3.0-only
 * MOLECULAR: the patches. PATCH_SLOTS slots, each the whole molecule as it sounds now (mol_d: the shape, the
 * atoms' values and LFOs, the generator's patch, chord and LFOs) with a check. The platform keeps them
 * (plat_patch_read, plat_patch_write: the device in flash, a sector a slot; the simulator in a file; the tests in
 * memory). A slot that does not check (never written, or by a later version of the record) is empty. */

#define PATCH_SLOTS 12u
#define PATCH_MAGIC 0x504C4F4Du  /* "MOLP" */
#define PATCH_VER 2u             /* the record's layout: a new one whenever mol_design_t grows (fields only added at
                                  * its end, so older records still load: patch_used) */

typedef struct {
    uint32_t magic;
    uint16_t ver, len;
    uint32_t check;              /* FNV-1a over d */
    mol_design_t d;
} patch_rec_t;

/* the platform's: 0 when done */
static int plat_patch_read(uint32_t slot, void *buf, uint32_t n);
static int plat_patch_write(uint32_t slot, const void *buf, uint32_t n);

static uint32_t patch_check(const void *p, uint32_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t h = 2166136261u;
    while (n--)
        h = (h ^ *b++) * 16777619u;
    return h;
}

static patch_rec_t patch_buf;    /* (one record, not on the stack) */

/* 1 if the slot holds a patch (then in patch_buf.d). An older record, shorter (a later version only adds to the end
 * of mol_design_t), reads with the rest zero: as the older version made it */
static int patch_used(uint32_t slot)
{
    if (slot >= PATCH_SLOTS || plat_patch_read(slot, &patch_buf, sizeof patch_buf))
        return 0;
    if (patch_buf.magic != PATCH_MAGIC || patch_buf.ver < 1u || patch_buf.ver > PATCH_VER ||
        patch_buf.len > sizeof patch_buf.d || patch_buf.check != patch_check(&patch_buf.d, patch_buf.len))
        return 0;
    memset((uint8_t *)&patch_buf.d + patch_buf.len, 0, sizeof patch_buf.d - patch_buf.len);
    return 1;
}
/* the slots that hold a patch, a bit each */
static uint32_t patch_used_mask(void)
{
    uint32_t i, m = 0;
    for (i = 0; i < PATCH_SLOTS; i++)
        if (patch_used(i))
            m |= 1u << i;
    return m;
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
    if (plat_patch_write(slot, &patch_buf, sizeof patch_buf))
        return 0;
    return patch_used(slot) && !memcmp(&patch_buf.d, &mol_d, sizeof mol_d);
}
/* the slot's patch, sounding (through a fade); 1 if it held one */
static int patch_load(uint32_t slot)
{
    return patch_used(slot) && mol_load(&patch_buf.d);
}
