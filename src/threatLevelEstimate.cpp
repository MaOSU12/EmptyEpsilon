#include "gameGlobalInfo.h"
#include "threatLevelEstimate.h"
#include "spaceObjects/spaceship.h"
#include "spaceObjects/beamEffect.h"
#include "spaceObjects/missiles/missileWeapon.h"
#include "components/hull.h"
#include "systems/collision.h"


ThreatLevelEstimate::ThreatLevelEstimate()
{
    smoothed_threat_level = 0.0;
    threat_high = false;
    threat_high_func = nullptr;
    threat_low_func = nullptr;
}

void ThreatLevelEstimate::update(float delta)
{
    if (!gameGlobalInfo)
        return;

    float max_threat = 0.0f;
    for(int n=0; n<GameGlobalInfo::max_player_ships; n++)
    {
        max_threat = std::max(max_threat, getThreatFor(gameGlobalInfo->getPlayerShip(n)));
    }
    float f = delta / threat_drop_off_time;
    smoothed_threat_level = ((1.0f - f) * smoothed_threat_level) + (max_threat * f);

    if (!threat_high && smoothed_threat_level > threat_high_level)
    {
        threat_high = true;
        if (threat_high_func)
            threat_high_func();
    }

    if (threat_high && smoothed_threat_level < threat_low_level)
    {
        threat_high = false;
        if (threat_low_func)
            threat_low_func();
    }
}

float ThreatLevelEstimate::getThreatFor(P<SpaceShip> ship)
{
    if (!ship)
        return 0.0;

    float threat = 0.0;
    if (ship->getShieldsActive())
        threat += 200;

    for(int n=0; n<ship->shield_count; n++)
        threat += ship->shield_max[n] - ship->shield_level[n];
    auto hull = ship->entity.getComponent<Hull>();
    if (hull)
        threat += hull->max - hull->current;

    float radius = 7000.0;
    
    auto ship_position = ship->getPosition();
    for(auto entity : sp::CollisionSystem::queryArea(ship_position - glm::vec2(radius, radius), ship_position + glm::vec2(radius, radius)))
    {
        auto ptr = entity.getComponent<SpaceObject*>();
        if (!ptr) continue;
        P<SpaceObject> obj = *ptr;
        P<SpaceShip> other_ship = obj;
        if (!other_ship || !ship->isEnemy(other_ship))
        {
            if (P<MissileWeapon>(obj))
                threat += 5000.0f;
            if (P<BeamEffect>(obj))
                threat += 5000.0f;
            continue;
        }

        bool is_being_attacked = false;
        hull = entity.getComponent<Hull>();
        float score = 200.0f;
        if (hull)
            score += hull->max;
        for(int n=0; n<other_ship->shield_count; n++)
        {
            score += other_ship->shield_max[n] * 2.0f / float(other_ship->shield_count);
            if (other_ship->shield_hit_effect[n] > 0.0f)
                is_being_attacked = true;
        }
        if (is_being_attacked)
            score += 500.0f;

        threat += score;
    }

    return threat;
}

void ThreatLevelEstimate::setCallbacks(func_t low, func_t high)
{
    threat_low_func = low;
    threat_high_func = high;

    if (threat_high)
    {
        if (threat_high_func)
            threat_high_func();
    }else{
        if (threat_low_func)
            threat_low_func();
    }
}
