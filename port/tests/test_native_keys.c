/* SPDX-License-Identifier: GPL-2.0-or-later */
/* iss_native_keys.h: the split of actions that share a PS4 button, and the state they press. */
#undef NDEBUG
#include "iss_native_keys.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    /* A made-up scheme: kinds 3 and 4 (melee, execute) share 0x40; kind 36 is that plain button;
     * kind 0 (jump) is 0x20 alone; kind 35 is the plain 0x20 button. */
    uint32_t table[ISS_NATIVE_KINDS]={0};
    table[0]=0x20; table[3]=0x40; table[4]=0x40; table[35]=0x20; table[36]=0x40;
    uint64_t sig[ISS_NA_COUNT]={0};
    uint32_t vbit[ISS_NATIVE_KINDS], bit[ISS_NA_COUNT];
    /* Melee and execute on one key: nothing to split; each presses the shared button. */
    sig[ISS_NA_JUMP]=1; sig[ISS_NA_MELEE]=2; sig[ISS_NA_EXECUTE]=2;
    assert(iss_native_split(table,sig,vbit,bit)==0);
    assert(vbit[3]==0 && vbit[4]==0 && vbit[36]==0);
    assert(iss_native_state(table,bit,1u<<ISS_NA_MELEE)==0x40);
    /* Different keys: one bit each, the plain button sees both, jump is left alone. */
    sig[ISS_NA_EXECUTE]=3;
    assert(iss_native_split(table,sig,vbit,bit)==2);
    assert(bit[ISS_NA_MELEE] && bit[ISS_NA_EXECUTE] && bit[ISS_NA_MELEE]!=bit[ISS_NA_EXECUTE]);
    assert(((bit[ISS_NA_MELEE]|bit[ISS_NA_EXECUTE]) & ~ISS_NATIVE_VBITS)==0);
    assert(vbit[3]==bit[ISS_NA_MELEE] && vbit[4]==bit[ISS_NA_EXECUTE]);
    assert(vbit[36]==(bit[ISS_NA_MELEE]|bit[ISS_NA_EXECUTE]) && vbit[0]==0 && vbit[35]==0);
    /* The melee key presses melee's bit only: the game's lookup for execute (0x40 | its bit) misses it. */
    const uint32_t melee=iss_native_state(table,bit,1u<<ISS_NA_MELEE);
    assert(melee==bit[ISS_NA_MELEE]);
    assert((melee & (table[3]|vbit[3])) && !(melee & (table[4]|vbit[4])) && (melee & (table[36]|vbit[36])));
    assert(iss_native_state(table,bit,1u<<ISS_NA_JUMP)==0x20);
    /* An unbound action sharing the button: the bound one still gets its own bit. */
    sig[ISS_NA_EXECUTE]=0;
    assert(iss_native_split(table,sig,vbit,bit)==1 && bit[ISS_NA_MELEE] && !bit[ISS_NA_EXECUTE]);
    /* Bits run out after 7 split actions; the rest press their masks. */
    uint32_t all[ISS_NATIVE_KINDS]; uint64_t each[ISS_NA_COUNT];
    for (unsigned k=0;k<ISS_NATIVE_KINDS;++k) all[k]=0x1;
    for (int a=0;a<ISS_NA_COUNT;++a) each[a]=(uint64_t)a+1;
    assert(iss_native_split(all,each,vbit,bit)==7);
    puts("native keys tests passed");
    return 0;
}
