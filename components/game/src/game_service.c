#include "game/game_service.h"

#include <stdio.h>
#include <string.h>

#include "common/project_catalog.h"
#include "common/time_utils.h"
#include "esp_log.h"
#include "esp_random.h"

static const char *TAG = "game_service";

static const char *k_skill_pool[][CATDEX_MAX_SKILLS] = {
    {"Dusk Sprint", "Tick Swipe", "Agile Leap", "Wild Sense"},    /* Abyssinian */
    {"Jungle Claw", "Rosette Dash", "Ambush Pounce", "Feral Roar"}, /* Bengal */
    {"Sacred Guard", "Silk Veil", "Temple Step", "Gentle Press"}, /* Birman */
    {"Shadow Curl", "Night Blink", "Silent Paw", "Dark Wink"},    /* Bombay */
    {"Moon Swipe", "Nap Guard", "Round Pounce", "Silver Fur"},    /* British Shorthair */
    {"Pharaoh Dash", "Spot Flash", "Sand Sprint", "Ancient Mark"},/* Egyptian Mau */
    {"Forest Roar", "Giant Swipe", "Mane Guard", "Loyal Howl"},   /* Maine Coon */
    {"Velvet Press", "Crown Guard", "Silk Step", "Regal Gaze"},   /* Persian */
    {"Ribbon Glide", "Dream Guard", "Soft Press", "Star Veil"},   /* Ragdoll */
    {"Silver Mist", "Frost Step", "Quiet Fang", "Blue Veil"},     /* Russian Blue */
    {"Echo Cry", "Focus Step", "Light Fang", "Twin Spark"},       /* Siamese */
    {"Bare Flame", "Heat Pulse", "Sun Curl", "Warm Aura"},        /* Sphynx */
};

static uint8_t random_range_u8(uint8_t min_value, uint8_t max_value)
{
    if (max_value <= min_value) {
        return min_value;
    }
    return (uint8_t)(min_value + (esp_random() % (max_value - min_value + 1)));
}

static uint16_t random_range_u16(uint16_t min_value, uint16_t max_value)
{
    if (max_value <= min_value) {
        return min_value;
    }
    return (uint16_t)(min_value + (esp_random() % (max_value - min_value + 1)));
}

static Rarity decide_rarity(CatSpecies species, uint8_t confidence)
{
    uint8_t roll = (uint8_t)(esp_random() % 100);
    uint8_t rare_boost = confidence / 20;

    if (species == CAT_SPECIES_BOMBAY || species == CAT_SPECIES_RAGDOLL || species == CAT_SPECIES_SIAMESE) {
        rare_boost += 8;
    }

    if (roll + rare_boost > 96) {
        return RARITY_LEGENDARY;
    }
    if (roll + rare_boost > 84) {
        return RARITY_EPIC;
    }
    if (roll + rare_boost > 64) {
        return RARITY_RARE;
    }
    if (roll + rare_boost > 35) {
        return RARITY_UNCOMMON;
    }
    return RARITY_COMMON;
}

static void fill_talent_for_species(CatSpecies species, Talent *talent)
{
    if (talent == NULL) {
        return;
    }

    talent->hp = random_range_u8(45, 75);
    talent->attack = random_range_u8(45, 75);
    talent->defense = random_range_u8(45, 75);
    talent->agility = random_range_u8(45, 75);
    talent->affinity = random_range_u8(45, 75);

    switch (species) {
        case CAT_SPECIES_BRITISH_SHORTHAIR:
            talent->defense += 12;
            break;
        case CAT_SPECIES_ABYSSINIAN:
            talent->attack += 10;
            break;
        case CAT_SPECIES_RAGDOLL:
            talent->hp += 14;
            break;
        case CAT_SPECIES_SIAMESE:
            talent->agility += 15;
            talent->affinity += 8;
            break;
        case CAT_SPECIES_BENGAL:
            talent->attack += 8;
            talent->hp += 6;
            break;
        case CAT_SPECIES_EGYPTIAN_MAU:
            talent->agility += 12;
            break;
        case CAT_SPECIES_BOMBAY:
            talent->affinity += 14;
            break;
        case CAT_SPECIES_BIRMAN:
            talent->defense += 7;
            talent->affinity += 10;
            break;
        default:
            break;
    }
}

static void fill_skills(CapturedCat *cat)
{
    if (cat == NULL || (size_t)cat->species >= CAT_SPECIES_COUNT) {
        return;
    }

    cat->skill_count = 2;
    for (size_t i = 0; i < cat->skill_count; ++i) {
        snprintf(cat->skills[i].name, sizeof(cat->skills[i].name), "%s", k_skill_pool[cat->species][i]);
        cat->skills[i].power = random_range_u8(35, 85);
        cat->skills[i].mastery = random_range_u8(1, 5);
    }
}

static void build_nickname(CatSpecies species, uint32_t unique_id, char *buffer, size_t buffer_len)
{
    const cat_species_profile_t *profile = project_catalog_get_species_profile(species);
    if (profile == NULL || buffer == NULL || buffer_len == 0) {
        return;
    }
    snprintf(buffer, buffer_len, "%s-%03lu", profile->codename, (unsigned long)(unique_id % 1000));
}

static esp_err_t persist_save(game_service_t *service)
{
    if (service == NULL || service->storage == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    project_types_refresh_discovery_count(&service->save_data);
    return save_storage_save(service->storage, &service->save_data);
}

static int compare_capture_priority(const CapturedCat *lhs,
                                    const CapturedCat *rhs,
                                    WarehouseSortMode sort_mode)
{
    if (lhs == NULL || rhs == NULL) {
        return 0;
    }

    switch (sort_mode) {
        case WAREHOUSE_SORT_RARITY:
            if (lhs->rarity != rhs->rarity) {
                return (lhs->rarity > rhs->rarity) ? -1 : 1;
            }
            if (lhs->level != rhs->level) {
                return (lhs->level > rhs->level) ? -1 : 1;
            }
            break;
        case WAREHOUSE_SORT_LEVEL:
            if (lhs->level != rhs->level) {
                return (lhs->level > rhs->level) ? -1 : 1;
            }
            if (lhs->rarity != rhs->rarity) {
                return (lhs->rarity > rhs->rarity) ? -1 : 1;
            }
            break;
        case WAREHOUSE_SORT_TIME:
        default:
            break;
    }

    if (lhs->captured_at != rhs->captured_at) {
        return (lhs->captured_at > rhs->captured_at) ? -1 : 1;
    }

    if (lhs->unique_id != rhs->unique_id) {
        return (lhs->unique_id > rhs->unique_id) ? -1 : 1;
    }

    return 0;
}

esp_err_t game_service_init(game_service_t *service, save_storage_t *storage)
{
    if (service == NULL || storage == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(service, 0, sizeof(*service));
    service->storage = storage;

    bool was_created = false;
    esp_err_t err = save_storage_load(storage, &service->save_data, &was_created);
    if (err != ESP_OK) {
        return err;
    }

    if (was_created) {
        ESP_LOGI(TAG, "Initialized fresh save data");
    } else {
        ESP_LOGI(TAG, "Loaded save data with %u captures", service->save_data.captured_count);
    }

    return ESP_OK;
}

esp_err_t game_service_seed_demo_if_needed(game_service_t *service)
{
    if (service == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

#if CATDEX_DEMO_SEED_ON_EMPTY
    if (service->save_data.captured_count == 0) {
        classifier_result_t seed_prediction = {
            .has_result = true,
            .species = CAT_SPECIES_BENGAL,
            .confidence = 77,
        };
        CapturedCat captured = {0};
        return game_service_capture_prediction(service, &seed_prediction, &captured);
    }
#endif
    return ESP_OK;
}

esp_err_t game_service_capture_prediction(game_service_t *service,
                                          const classifier_result_t *prediction,
                                          CapturedCat *out_captured)
{
    if (service == NULL || prediction == NULL || !prediction->has_result) {
        return ESP_ERR_INVALID_ARG;
    }

    if (prediction->species == CAT_SPECIES_UNKNOWN || prediction->confidence < CATDEX_MIN_CONFIDENCE_TO_CAPTURE) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (service->save_data.captured_count >= CATDEX_MAX_CAPTURED) {
        return ESP_ERR_NO_MEM;
    }

    CapturedCat cat = {0};
    const cat_species_profile_t *profile = project_catalog_get_species_profile(prediction->species);
    if (profile == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    cat.unique_id = service->save_data.next_unique_id++;
    cat.species = prediction->species;
    cat.level = random_range_u8(3, 20) + (prediction->confidence / 12);
    cat.weight_grams = random_range_u16((uint16_t)(profile->base_weight_grams * 8 / 10),
                                        (uint16_t)(profile->base_weight_grams * 12 / 10));
    cat.personality = (Personality)(esp_random() % PERSONALITY_COUNT);
    cat.rarity = decide_rarity(prediction->species, prediction->confidence);
    cat.captured_at = time_utils_now_seconds();
    cat.classifier_confidence = prediction->confidence;
    cat.is_favorite = false;

    DexEntry *entry = &service->save_data.dex_entries[prediction->species];
    cat.is_first_discovery = !entry->discovered;

    build_nickname(prediction->species, cat.unique_id, cat.nickname, sizeof(cat.nickname));
    fill_talent_for_species(prediction->species, &cat.talent);
    fill_skills(&cat);

    entry->encounter_count++;
    entry->capture_count++;
    entry->last_captured_at = cat.captured_at;
    if (!entry->discovered) {
        entry->discovered = true;
        entry->first_captured_at = cat.captured_at;
    }

    service->save_data.captured[service->save_data.captured_count++] = cat;

    esp_err_t err = persist_save(service);
    if (err != ESP_OK) {
        return err;
    }

    if (out_captured != NULL) {
        *out_captured = cat;
    }

    ESP_LOGI(TAG, "Captured %s with confidence=%u", profile->display_name, prediction->confidence);
    return ESP_OK;
}

const GameSaveData *game_service_get_save_data(const game_service_t *service)
{
    if (service == NULL) {
        return NULL;
    }
    return &service->save_data;
}

esp_err_t game_service_set_favorite(game_service_t *service, uint32_t unique_id, bool is_favorite)
{
    if (service == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < service->save_data.captured_count; ++i) {
        CapturedCat *cat = &service->save_data.captured[i];
        if (cat->unique_id == unique_id) {
            cat->is_favorite = is_favorite;
            return persist_save(service);
        }
    }

    return ESP_ERR_NOT_FOUND;
}

const DexEntry *game_service_get_dex_entry(const game_service_t *service, CatSpecies species)
{
    if (service == NULL || (size_t)species >= CAT_SPECIES_COUNT) {
        return NULL;
    }
    return &service->save_data.dex_entries[(size_t)species];
}

const CapturedCat *game_service_get_capture_by_rank(const game_service_t *service,
                                                    size_t rank,
                                                    WarehouseSortMode sort_mode)
{
    bool used[CATDEX_MAX_CAPTURED] = {0};

    if (service == NULL || rank >= service->save_data.captured_count) {
        return NULL;
    }

    for (size_t pick = 0; pick <= rank; ++pick) {
        const CapturedCat *best = NULL;
        size_t best_index = 0;

        for (size_t i = 0; i < service->save_data.captured_count; ++i) {
            if (used[i]) {
                continue;
            }

            const CapturedCat *candidate = &service->save_data.captured[i];
            if (best == NULL || compare_capture_priority(candidate, best, sort_mode) < 0) {
                best = candidate;
                best_index = i;
            }
        }

        if (best == NULL) {
            return NULL;
        }

        used[best_index] = true;
        if (pick == rank) {
            return best;
        }
    }

    return NULL;
}

const CapturedCat *game_service_get_latest_capture(const game_service_t *service)
{
    if (service == NULL || service->save_data.captured_count == 0) {
        return NULL;
    }
    return &service->save_data.captured[service->save_data.captured_count - 1];
}
