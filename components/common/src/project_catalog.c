#include "common/project_catalog.h"

static const cat_species_profile_t k_species_profiles[] = {
    {
        .species = CAT_SPECIES_ABYSSINIAN,
        .display_name = "Abyssinian",
        .codename = "Dusk Runner",
        .lore = "A slender explorer with ticked fur and boundless curiosity.",
        .base_weight_grams = 3800,
    },
    {
        .species = CAT_SPECIES_BENGAL,
        .display_name = "Bengal",
        .codename = "Jungle Spark",
        .lore = "A wild-hearted sprinter whose rosette coat hides ancient instincts.",
        .base_weight_grams = 4600,
    },
    {
        .species = CAT_SPECIES_BIRMAN,
        .display_name = "Birman",
        .codename = "Sacred Paw",
        .lore = "A gentle temple guardian with silky fur and white-gloved paws.",
        .base_weight_grams = 4400,
    },
    {
        .species = CAT_SPECIES_BOMBAY,
        .display_name = "Bombay",
        .codename = "Night Panther",
        .lore = "A sleek black shadow that moves with panther-like confidence.",
        .base_weight_grams = 4000,
    },
    {
        .species = CAT_SPECIES_BRITISH_SHORTHAIR,
        .display_name = "British Shorthair",
        .codename = "Moon Biscuit",
        .lore = "A calm plush guardian that likes routine and gentle moonlight.",
        .base_weight_grams = 4800,
    },
    {
        .species = CAT_SPECIES_EGYPTIAN_MAU,
        .display_name = "Egyptian Mau",
        .codename = "Pharaoh Dash",
        .lore = "The fastest domestic cat, bearing sacred spots from ancient times.",
        .base_weight_grams = 3700,
    },
    {
        .species = CAT_SPECIES_MAINE_COON,
        .display_name = "Maine Coon",
        .codename = "Forest Giant",
        .lore = "A gentle giant with a lion-like mane and a dog-loyal heart.",
        .base_weight_grams = 6500,
    },
    {
        .species = CAT_SPECIES_PERSIAN,
        .display_name = "Persian",
        .codename = "Velvet Crown",
        .lore = "A regal dreamer draped in flowing fur and quiet dignity.",
        .base_weight_grams = 4500,
    },
    {
        .species = CAT_SPECIES_RAGDOLL,
        .display_name = "Ragdoll",
        .codename = "Cloud Ribbon",
        .lore = "A dreamy companion that drifts through adventures with grace.",
        .base_weight_grams = 5200,
    },
    {
        .species = CAT_SPECIES_RUSSIAN_BLUE,
        .display_name = "Russian Blue",
        .codename = "Silver Mist",
        .lore = "A reserved aristocrat whose silver coat shimmers like moonlit water.",
        .base_weight_grams = 4100,
    },
    {
        .species = CAT_SPECIES_SIAMESE,
        .display_name = "Siamese",
        .codename = "Echo Spark",
        .lore = "A sharp-eyed tactician known for voice, speed, and loyalty.",
        .base_weight_grams = 3900,
    },
    {
        .species = CAT_SPECIES_SPHYNX,
        .display_name = "Sphynx",
        .codename = "Bare Flame",
        .lore = "A hairless sun-seeker radiating warmth and mischievous energy.",
        .base_weight_grams = 3600,
    },
};

static const char *k_personality_names[] = {
    "Bold", "Gentle", "Curious", "Playful", "Lazy", "Proud",
};

static const char *k_rarity_names[] = {
    "Common", "Uncommon", "Rare", "Epic", "Legendary",
};

static const char *k_warehouse_sort_names[] = {
    "Time", "Rarity", "Level",
};

const cat_species_profile_t *project_catalog_get_species_profile(CatSpecies species)
{
    if ((size_t)species >= CAT_SPECIES_COUNT) {
        return NULL;
    }
    return &k_species_profiles[(size_t)species];
}

const cat_species_profile_t *project_catalog_get_all_species(size_t *count)
{
    if (count != NULL) {
        *count = CAT_SPECIES_COUNT;
    }
    return k_species_profiles;
}

const char *project_catalog_personality_name(Personality personality)
{
    if ((size_t)personality >= PERSONALITY_COUNT) return "Unknown";
    return k_personality_names[(size_t)personality];
}

const char *project_catalog_rarity_name(Rarity rarity)
{
    if ((size_t)rarity >= RARITY_COUNT) return "Unknown";
    return k_rarity_names[(size_t)rarity];
}

const char *project_catalog_warehouse_sort_name(WarehouseSortMode mode)
{
    if ((size_t)mode >= WAREHOUSE_SORT_COUNT) return "Unknown";
    return k_warehouse_sort_names[(size_t)mode];
}
