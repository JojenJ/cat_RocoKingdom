#include <string.h>

#include "common/project_catalog.h"
#include "common/project_types.h"

void project_types_init_save_data(GameSaveData *save_data)
{
    if (save_data == NULL) {
        return;
    }

    memset(save_data, 0, sizeof(*save_data));
    save_data->schema_version = CATDEX_SCHEMA_VERSION;
    save_data->next_unique_id = 1;

    for (size_t i = 0; i < CAT_SPECIES_COUNT; ++i) {
        const cat_species_profile_t *profile = project_catalog_get_species_profile((CatSpecies)i);
        save_data->dex_entries[i].species = (CatSpecies)i;
        save_data->dex_entries[i].discovered = false;
        save_data->dex_entries[i].encounter_count = 0;
        save_data->dex_entries[i].capture_count = 0;
        save_data->dex_entries[i].first_captured_at = 0;
        save_data->dex_entries[i].last_captured_at = 0;
        if (profile != NULL) {
            strncpy(save_data->dex_entries[i].lore, profile->lore, sizeof(save_data->dex_entries[i].lore) - 1);
        }
    }
}

void project_types_refresh_discovery_count(GameSaveData *save_data)
{
    if (save_data == NULL) {
        return;
    }

    uint16_t discovered_count = 0;
    for (size_t i = 0; i < CAT_SPECIES_COUNT; ++i) {
        if (save_data->dex_entries[i].discovered) {
            discovered_count++;
        }
    }
    save_data->discovered_species_count = discovered_count;
}
