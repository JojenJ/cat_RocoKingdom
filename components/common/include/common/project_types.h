#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/app_config.h"

typedef enum {
    CAT_SPECIES_ABYSSINIAN = 0,
    CAT_SPECIES_BENGAL,
    CAT_SPECIES_BIRMAN,
    CAT_SPECIES_BOMBAY,
    CAT_SPECIES_BRITISH_SHORTHAIR,
    CAT_SPECIES_EGYPTIAN_MAU,
    CAT_SPECIES_MAINE_COON,
    CAT_SPECIES_PERSIAN,
    CAT_SPECIES_RAGDOLL,
    CAT_SPECIES_RUSSIAN_BLUE,
    CAT_SPECIES_SIAMESE,
    CAT_SPECIES_SPHYNX,
    CAT_SPECIES_COUNT,
    CAT_SPECIES_UNKNOWN = 0xFF
} CatSpecies;

typedef enum {
    PERSONALITY_BOLD = 0,
    PERSONALITY_GENTLE,
    PERSONALITY_CURIOUS,
    PERSONALITY_PLAYFUL,
    PERSONALITY_LAZY,
    PERSONALITY_PROUD,
    PERSONALITY_COUNT
} Personality;

typedef enum {
    RARITY_COMMON = 0,
    RARITY_UNCOMMON,
    RARITY_RARE,
    RARITY_EPIC,
    RARITY_LEGENDARY,
    RARITY_COUNT
} Rarity;

typedef enum {
    WAREHOUSE_SORT_TIME = 0,
    WAREHOUSE_SORT_RARITY,
    WAREHOUSE_SORT_LEVEL,
    WAREHOUSE_SORT_COUNT
} WarehouseSortMode;

typedef struct {
    char name[CATDEX_NAME_LEN];
    uint8_t power;
    uint8_t mastery;
} Skill;

typedef struct {
    uint8_t hp;
    uint8_t attack;
    uint8_t defense;
    uint8_t agility;
    uint8_t affinity;
} Talent;

typedef struct {
    uint32_t unique_id;
    char nickname[CATDEX_NAME_LEN];
    CatSpecies species;
    uint8_t level;
    uint16_t weight_grams;
    Personality personality;
    Talent talent;
    Skill skills[CATDEX_MAX_SKILLS];
    uint8_t skill_count;
    Rarity rarity;
    uint64_t captured_at;
    uint8_t classifier_confidence;
    bool is_favorite;
    bool is_first_discovery;
} CapturedCat;

typedef struct {
    CatSpecies species;
    bool discovered;
    uint16_t encounter_count;
    uint16_t capture_count;
    uint64_t first_captured_at;
    uint64_t last_captured_at;
    char lore[CATDEX_LORE_LEN];
} DexEntry;

typedef struct {
    uint32_t schema_version;
    uint32_t next_unique_id;
    uint16_t discovered_species_count;
    uint16_t captured_count;
    DexEntry dex_entries[CATDEX_MAX_SPECIES];
    CapturedCat captured[CATDEX_MAX_CAPTURED];
} GameSaveData;

void project_types_init_save_data(GameSaveData *save_data);
void project_types_refresh_discovery_count(GameSaveData *save_data);
