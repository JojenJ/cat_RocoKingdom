#pragma once

#include "common/project_types.h"

typedef struct {
    CatSpecies species;
    const char *display_name;
    const char *codename;
    const char *lore;
    uint16_t base_weight_grams;
} cat_species_profile_t;

const cat_species_profile_t *project_catalog_get_species_profile(CatSpecies species);
const cat_species_profile_t *project_catalog_get_all_species(size_t *count);
const char *project_catalog_personality_name(Personality personality);
const char *project_catalog_rarity_name(Rarity rarity);
const char *project_catalog_warehouse_sort_name(WarehouseSortMode mode);
