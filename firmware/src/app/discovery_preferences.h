#pragma once

#include "personal_tags.h"

struct DiscoveryScreen;

bool discovery_preferences_load(struct DiscoveryScreen *screen, const PersonalTags *tags);
bool discovery_preferences_save(const struct DiscoveryScreen *screen, const PersonalTags *tags);
void discovery_preferences_validate_tags(struct DiscoveryScreen *screen, const PersonalTags *tags);
