#ifndef __PREF_BOP_H__
#define __PREF_BOP_H__

#include "pref_common.h"

typedef struct BOP_RR_Entry_struct {
  Addr tag;
  Flag valid;
} BOP_RR_Entry;

typedef struct BOP_Inflight_Entry_struct {
  Addr pref_line;
  Addr base_line;
  Flag valid;
} BOP_Inflight_Entry;

typedef struct Pref_BOP_struct {
  HWP_Info* hwp_info;
  CacheLevel type;

  BOP_RR_Entry* rr_table;
  BOP_Inflight_Entry* inflight_table;

  int* offsets;
  uns* scores;
  uns num_offsets;

  uns offset_index;
  uns rounds;
  int best_offset;
  uns best_score;
  int current_offset;
  Flag prefetch_enabled;
} Pref_BOP;

typedef struct BOP_Prefetchers_struct {
  Pref_BOP* bop_hwp_core_ul1;
  Pref_BOP* bop_hwp_core_umlc;
} BOP_Prefetchers;

void pref_bop_init(HWP* hwp);

void pref_bop_ul1_miss(uns8 proc_id, Addr lineAddr, Addr loadPC, uns32 global_hist);
void pref_bop_ul1_prefhit(uns8 proc_id, Addr lineAddr, Addr loadPC, uns32 global_hist);
void pref_bop_umlc_miss(uns8 proc_id, Addr lineAddr, Addr loadPC, uns32 global_hist);
void pref_bop_umlc_prefhit(uns8 proc_id, Addr lineAddr, Addr loadPC, uns32 global_hist);

void pref_bop_note_prefetch_fill(uns8 proc_id, Addr lineAddr, uns8 prefetcher_id, CacheLevel type);

#endif
