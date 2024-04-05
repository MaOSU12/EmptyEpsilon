#include <i18n.h>
#include "gameGlobalInfo.h"
#include "preferenceManager.h"
#include "scenarioInfo.h"
#include "multiplayer_client.h"
#include "soundManager.h"
#include "random.h"
#include "config.h"
#include "components/collision.h"
#include "systems/collision.h"
#include "ecs/query.h"
#include "menus/luaConsole.h"
#include "spaceObjects/spaceObject.h"
#include "playerInfo.h"
#include <SDL_assert.h>

P<GameGlobalInfo> gameGlobalInfo;

REGISTER_MULTIPLAYER_CLASS(GameGlobalInfo, "GameGlobalInfo")
GameGlobalInfo::GameGlobalInfo()
: MultiplayerObject("GameGlobalInfo")
{
    SDL_assert(!gameGlobalInfo);

    callsign_counter = 0;
    gameGlobalInfo = this;

    global_message_timeout = 0.0;
    scanning_complexity = SC_Normal;
    hacking_difficulty = 2;
    hacking_games = HG_All;
    use_beam_shield_frequencies = true;
    use_system_damage = true;
    allow_main_screen_tactical_radar = true;
    allow_main_screen_long_range_radar = true;
    gm_control_code = "";
    elapsed_time = 0.0f;

    intercept_all_comms_to_gm = false;

    registerMemberReplication(&scanning_complexity);
    registerMemberReplication(&hacking_difficulty);
    registerMemberReplication(&hacking_games);
    registerMemberReplication(&global_message);
    registerMemberReplication(&global_message_timeout, 1.0);
    registerMemberReplication(&banner_string);
    registerMemberReplication(&victory_faction);
    registerMemberReplication(&use_beam_shield_frequencies);
    registerMemberReplication(&use_system_damage);
    registerMemberReplication(&allow_main_screen_tactical_radar);
    registerMemberReplication(&allow_main_screen_long_range_radar);
    registerMemberReplication(&gm_control_code);
    registerMemberReplication(&elapsed_time, 0.1);
}

//due to a suspected compiler bug this deconstructor needs to be explicitly defined
GameGlobalInfo::~GameGlobalInfo()
{
}

void GameGlobalInfo::update(float delta)
{
    foreach(SpaceObject, obj, space_object_list) {
        if (obj->entity) {
            //Add a back reference to the SpaceObject from the ECS, we need this until we fully phased out the OOP style code (if ever...)
            // We do this here so this is done for client and server.
            obj->entity.addComponent<SpaceObject*>(*obj);
        }
    }

    if (global_message_timeout > 0.0f)
    {
        global_message_timeout -= delta;
    }
    if (my_player_info)
    {
        //Set the my_spaceship variable based on the my_player_info->ship_id
        if (my_spaceship != my_player_info->ship)
            my_spaceship = my_player_info->ship;
    }
    elapsed_time += delta;

    if (main_script && main_script_error_count < 5) {
        auto res = main_script->call<void>("update", delta);
        if (res.isErr() && res.error() != "Not a function") {
            LuaConsole::checkResult(res);
            main_script_error_count += 1;
            if (main_script_error_count == 5) {
                LuaConsole::addLog("5 repeated script update errors, stopping updates.");
            }
        } else {
            main_script_error_count = 0;
        }
    }
    for(auto& as : additional_scripts) {
        auto res = as->call<void>("update", delta);
        if (res.isErr() && res.error() != "Not a function")
            LuaConsole::checkResult(res);
    }
}

string GameGlobalInfo::getNextShipCallsign()
{
    callsign_counter += 1;
    switch(irandom(0, 9))
    {
    case 0: return "S" + string(callsign_counter);
    case 1: return "NC" + string(callsign_counter);
    case 2: return "CV" + string(callsign_counter);
    case 3: return "SS" + string(callsign_counter);
    case 4: return "VS" + string(callsign_counter);
    case 5: return "BR" + string(callsign_counter);
    case 6: return "CSS" + string(callsign_counter);
    case 7: return "UTI" + string(callsign_counter);
    case 8: return "VK" + string(callsign_counter);
    case 9: return "CCN" + string(callsign_counter);
    }
    return "SS" + string(callsign_counter);
}

void GameGlobalInfo::execScriptCode(const string& code)
{
    if (main_script) {
        auto res = main_script->run<void>(code);
        LuaConsole::checkResult(res);
    }
}

bool GameGlobalInfo::allowNewPlayerShips()
{
    auto res = main_script->call<bool>("allowNewPlayerShips");
    LuaConsole::checkResult(res);
    return res.value();
}

namespace sp::script {
    template<> struct Convert<std::vector<GameGlobalInfo::ShipSpawnInfo>> {
        static std::vector<GameGlobalInfo::ShipSpawnInfo> fromLua(lua_State* L, int idx) {
            std::vector<GameGlobalInfo::ShipSpawnInfo> result{};
            if (lua_istable(L, idx)) {
                for(int index=1; lua_geti(L, idx, index) == LUA_TTABLE; index++) {
                    lua_geti(L, -1, 1); auto callback = Convert<sp::script::Callback>::fromLua(L, -1); lua_pop(L, 1);
                    lua_geti(L, -1, 2); auto label = lua_tostring(L, -1); lua_pop(L, 1);
                    lua_geti(L, -1, 3); auto description = lua_tostring(L, -1); lua_pop(L, 1);
                    lua_pop(L, 1);
                    result.push_back({callback, label ? label : "", description ? description : ""});
                }
                lua_pop(L, 1);
            }
            return result;
        }
    };
}
std::vector<GameGlobalInfo::ShipSpawnInfo> GameGlobalInfo::getSpawnablePlayerShips()
{
    std::vector<GameGlobalInfo::ShipSpawnInfo> info;
    if (main_script) {
        auto res = main_script->call<std::vector<GameGlobalInfo::ShipSpawnInfo>>("getSpawnablePlayerShips");
        LuaConsole::checkResult(res);
        if (res.isOk())
            info = res.value();
    }
    return info;
}
namespace sp::script {
    template<> struct Convert<std::vector<GameGlobalInfo::ObjectSpawnInfo>> {
        static std::vector<GameGlobalInfo::ObjectSpawnInfo> fromLua(lua_State* L, int idx) {
            std::vector<GameGlobalInfo::ObjectSpawnInfo> result{};
            if (lua_istable(L, idx)) {
                for(int index=1; lua_geti(L, idx, index) == LUA_TTABLE; index++) {
                    lua_geti(L, -1, 1); auto callback = Convert<sp::script::Callback>::fromLua(L, -1); lua_pop(L, 1);
                    lua_geti(L, -1, 2); auto label = lua_tostring(L, -1); lua_pop(L, 1);
                    lua_geti(L, -1, 3); auto category = lua_tostring(L, -1); lua_pop(L, 1);
                    lua_pop(L, 1);
                    result.push_back({callback, label ? label : "", category ? category : ""});
                }
                lua_pop(L, 1);
            }
            return result;
        }
    };
}
std::vector<GameGlobalInfo::ObjectSpawnInfo> GameGlobalInfo::getGMSpawnableObjects()
{
    std::vector<GameGlobalInfo::ObjectSpawnInfo> info;
    if (main_script) {
        auto res = main_script->call<std::vector<GameGlobalInfo::ObjectSpawnInfo>>("getSpawnableGMObjects");
        LuaConsole::checkResult(res);
        if (res.isOk())
            info = res.value();
    }
    return info;
}


void GameGlobalInfo::reset()
{
    if (state_logger)
        state_logger->destroy();

    gm_callback_functions.clear();
    gm_messages.clear();
    on_gm_click = nullptr;

    FactionInfoLegacy::reset();

    sp::ecs::Entity::destroyAllEntities();
    foreach(SpaceObject, o, space_object_list)
        o->destroy();
    main_script = nullptr;
    additional_scripts.clear();

    elapsed_time = 0.0f;
    callsign_counter = 0;
    global_message = "";
    global_message_timeout = 0.0f;
    banner_string = "";

    //Pause the game
    engine->setGameSpeed(0.0);

    foreach(PlayerInfo, p, player_info_list)
    {
        p->reset();
    }
}

void GameGlobalInfo::setScenarioSettings(const string filename, std::unordered_map<string, string> new_settings)
{
    // Use the parsed scenario metadata.
    ScenarioInfo info(filename);

    // Set the scenario name.
    gameGlobalInfo->scenario = info.name;
    LOG(INFO) << "Configuring settings for scenario " << gameGlobalInfo->scenario;

    // Set each scenario setting to either a matching passed new value, or the
    // default if there's no match (or no new value).
    for(auto& setting : info.settings)
    {
        // Initialize with defaults.
        gameGlobalInfo->scenario_settings[setting.key] = setting.default_option;

        // If new settings were passed ...
        if (!new_settings.empty())
        {
            // ... confirm that this setting key exists in the new settings.
            if (new_settings.find(setting.key) != new_settings.end())
            {
                if (new_settings[setting.key] != "")
                {
                    // If so, override the default with the new value.
                    gameGlobalInfo->scenario_settings[setting.key] = new_settings[setting.key];
                }
            }
        }

        // Log scenario setting confirmation.
        LOG(INFO) << setting.key << " scenario setting set to " << gameGlobalInfo->scenario_settings[setting.key];
    }
}

void GameGlobalInfo::startScenario(string filename, std::unordered_map<string, string> new_settings)
{
    reset();

    i18n::reset();
    i18n::load("locale/main." + PreferencesManager::get("language", "en") + ".po");
    i18n::load("locale/comms_ship." + PreferencesManager::get("language", "en") + ".po");
    i18n::load("locale/comms_station." + PreferencesManager::get("language", "en") + ".po");
    i18n::load("locale/factionInfo." + PreferencesManager::get("language", "en") + ".po");
    i18n::load("locale/science_db." + PreferencesManager::get("language", "en") + ".po");
    i18n::load("locale/" + filename.replace(".lua", "." + PreferencesManager::get("language", "en") + ".po"));

    main_script = std::make_unique<sp::script::Environment>();
    main_script_error_count = 0;
    if (setupScriptEnvironment(*main_script.get())) {
        auto res = main_script->runFile<void>("model_data.lua");
        LuaConsole::checkResult(res);
        if (!res.isErr()) {
            res = main_script->runFile<void>("factionInfo.lua");
            LuaConsole::checkResult(res);
        }
        if (!res.isErr()) {
            res = main_script->runFile<void>("shipTemplates.lua");
            LuaConsole::checkResult(res);
        }
        if (!res.isErr()) {
            res = main_script->runFile<void>("science_db.lua");
            LuaConsole::checkResult(res);
        }
    }
    //TODO: int max_cycles = PreferencesManager::get("script_cycle_limit", "0").toInt();
    //TODO: if (max_cycles > 0)
    //TODO:     script->setMaxRunCycles(max_cycles);

    // Initialize scenario settings.
    setScenarioSettings(filename, new_settings);

    auto res = main_script->runFile<void>(filename);
    LuaConsole::checkResult(res);
    if (res.isOk()) {
        res = main_script->call<void>("init");
        LuaConsole::checkResult(res);
    }

    if (PreferencesManager::get("game_logs", "1").toInt())
    {
        state_logger = new GameStateLogger();
        state_logger->start();
    }
}

void GameGlobalInfo::destroy()
{
    reset();
    MultiplayerObject::destroy();
}

string GameGlobalInfo::getMissionTime() {
    unsigned int seconds = gameGlobalInfo->elapsed_time;
    unsigned int minutes = (seconds / 60) % 60;
    unsigned int hours = (seconds / 60 / 60) % 24;
    seconds = seconds % 60;
    char buf[9];
    std::snprintf(buf, 9, "%02d:%02d:%02d", hours, minutes, seconds);
    return string(buf);
}

string getSectorName(glm::vec2 position)
{
    constexpr float sector_size = 20000;
    int sector_x = floorf(position.x / sector_size) + 5;
    int sector_y = floorf(position.y / sector_size) + 5;
    string y;
    string x;
    if (sector_y >= 0)
        if (sector_y < 26)
            y = string(char('A' + (sector_y)));
        else
            y = string(char('A' - 1 + (sector_y / 26))) + string(char('A' + (sector_y % 26)));
    else
        y = string(char('z' + ((sector_y + 1) / 26))) + ((sector_y  % 26) == 0 ? "a" : string(char('z' + 1 + (sector_y  % 26))));
    x = string(sector_x);
    return y + x;
}

static int getPlayerShip(lua_State* L)
{
    int index = luaL_checkinteger(L, 1);
    if (index == -1) {
        for(auto [entity, pc] : sp::ecs::Query<PlayerControl>())
            return convert<sp::ecs::Entity>::returnType(L, entity);
        return 0;
    }
    for(auto [entity, pc] : sp::ecs::Query<PlayerControl>())
        if (--index == 0)
            return convert<sp::ecs::Entity>::returnType(L, entity);
    return 0;
}
/// P<PlayerSpaceship> getPlayerShip(int index)
/// Returns the PlayerSpaceship with the given index.
/// PlayerSpaceships are 1-indexed.
/// A new ship is assigned the lowest available index, and a destroyed ship leaves its index vacant.
/// Pass -1 to return the first active player ship.
/// Example: getPlayerShip(2) -- returns the second-indexed ship, if it exists
REGISTER_SCRIPT_FUNCTION(getPlayerShip);

static int getActivePlayerShips(lua_State* L)
{
    std::vector<sp::ecs::Entity> ships;
    for(auto [entity, pc] : sp::ecs::Query<PlayerControl>())
        ships.emplace_back(entity);

    return convert<std::vector<sp::ecs::Entity>>::returnType(L, ships);
}
/// PVector<PlayerSpaceship> getActivePlayerShips()
/// Returns a 1-indexed list of active PlayerSpaceships.
/// Unlike getPlayerShip()'s index, destroyed ships don't leave gaps.
/// Example: getActivePlayerShips()[2] -- returns the second-indexed active ship
REGISTER_SCRIPT_FUNCTION(getActivePlayerShips);

static int getObjectsInRadius(lua_State* L)
{
    float x = luaL_checknumber(L, 1);
    float y = luaL_checknumber(L, 2);
    float r = luaL_checknumber(L, 3);

    glm::vec2 position(x, y);

    PVector<SpaceObject> objects;
    for(auto entity : sp::CollisionSystem::queryArea(position - glm::vec2(r, r), position + glm::vec2(r, r))) {
        auto entity_transform = entity.getComponent<sp::Transform>();
        if (entity_transform) {
            if (glm::length2(entity_transform->getPosition() - position) < r*r) {
                auto obj = entity.getComponent<SpaceObject*>();
                if (obj)
                    objects.push_back(*obj);
            }
        }
    }
    return convert<PVector<SpaceObject> >::returnType(L, objects);
}
/// PVector<SpaceObject> getObjectsInRadius(float x, float y, float radius)
/// Returns a list of all SpaceObjects within the given radius of the given x/y coordinates.
/// Example: getObjectsInRadius(0,0,5000) -- returns all objects within 5U of 0,0
REGISTER_SCRIPT_FUNCTION(getObjectsInRadius);

static int getAllObjects(lua_State* L)
{
    return convert<PVector<SpaceObject> >::returnType(L, space_object_list);
}
/// PVector<SpaceObject> getAllObjects()
/// Returns a list of all SpaceObjects.
/// This can return a very long list and could slow down the game if called every tick.
/// Example: getAllObjects()
REGISTER_SCRIPT_FUNCTION(getAllObjects);

static int getGameLanguage(lua_State* L)
{
    lua_pushstring(L, PreferencesManager::get("language", "en").c_str());
    return 1;
}
/// string getGameLanguage()
/// Returns the language as the string value of the language key in game preferences.
/// Example: getGameLanguage() -- returns "en" if the game language is set to English
REGISTER_SCRIPT_FUNCTION(getGameLanguage);

/** Short lived object to do a scenario change on the update loop. See "setScenario" for details */
class ScenarioChanger : public Updatable
{
public:
    ScenarioChanger(string script_name, const std::unordered_map<string, string>& settings)
    : script_name(script_name), settings(settings)
    {
    }

    virtual void update(float delta) override
    {
        gameGlobalInfo->startScenario(script_name, settings);
        destroy();
    }
private:
    string script_name;
    std::unordered_map<string, string> settings;
};

static int setScenario(lua_State* L)
{
    string script_name = luaL_checkstring(L, 1);
    string variation = luaL_optstring(L, 2, "");

    // Script filename must not be an empty string.
    if (script_name == "")
    {
        LOG(ERROR) << "setScenario() requires a non-empty value.";
        return 1;
    }

    if (variation != "")
    {
        LOG(WARNING) << "LUA: DEPRECATED setScenario() called with scenario variation. Passing the value as the \"variation\" scenario setting instead.";
        // Start the scenario, passing the "variation" scenario setting.
        new ScenarioChanger(script_name, {{"variation", variation}});
    }
    else
    {
        // Start the scenario with defaults.
        new ScenarioChanger(script_name, {{}});
    }

    // This could be called from a currently active scenario script.
    // Calling GameGlobalInfo::startScenario is unsafe at this point,
    // as this will destroy the lua state that this function is running in.
    // So use the ScenarioChanger object which will do the change in the update loop. Which is safe.
    return 0;
}
/// void setScenario(string script_name, std::optional<string> variation_name)
/// Launches the given scenario, even if another scenario is running.
/// Paths are relative to the scripts/ directory.
/// Example: setScenario("scenario_03_waves.lua") -- launches the scenario at scripts/scenario_03_waves.lua
REGISTER_SCRIPT_FUNCTION(setScenario);

static int shutdownGame(lua_State* L)
{
    engine->shutdown();
    return 0;
}
/// void shutdownGame()
/// Shuts down the server.
/// Use to gracefully shut down a headless server.
/// Example: shutdownGame()
REGISTER_SCRIPT_FUNCTION(shutdownGame);

static int pauseGame(lua_State* L)
{
    engine->setGameSpeed(0.0);
    return 0;
}
/// void pauseGame()
/// Pauses the game.
/// Use to pause a headless server, which doesn't have access to the GM screen.
/// Example: pauseGame()
REGISTER_SCRIPT_FUNCTION(pauseGame);

static int unpauseGame(lua_State* L)
{
    engine->setGameSpeed(1.0);
    return 0;
}
/// void unpauseGame()
/// Unpauses the game.
/// Use to unpause a headless server, which doesn't have access to the GM screen.
/// Example: unpauseGame()
REGISTER_SCRIPT_FUNCTION(unpauseGame);

static int playSoundFile(lua_State* L)
{
    string filename = luaL_checkstring(L, 1);
    int n = filename.rfind(".");
    if (n > -1)
    {
        string filename_with_locale = filename.substr(0, n) + "." + PreferencesManager::get("language", "en") + filename.substr(n);
        if (getResourceStream(filename_with_locale)) {
            soundManager->playSound(filename_with_locale);
            return 0;
        }
    }
    soundManager->playSound(filename);
    return 0;
}
/// void playSoundFile(string filename)
/// Plays the given audio file on the server.
/// Paths are relative to the resources/ directory.
/// Works with any file format supported by SDL, including .wav, .ogg, .flac.
/// The sound is played only on the server, and not on any clients.
/// Example: playSoundFile("sfx/laser.wav")
REGISTER_SCRIPT_FUNCTION(playSoundFile);

template<> int convert<EScanningComplexity>::returnType(lua_State* L, EScanningComplexity complexity)
{
    switch(complexity)
    {
    case SC_None:
        lua_pushstring(L, "none");
        return 1;
    case SC_Simple:
        lua_pushstring(L, "simple");
        return 1;
    case SC_Normal:
        lua_pushstring(L, "normal");
        return 1;
    case SC_Advanced:
        lua_pushstring(L, "advanced");
        return 1;
    default:
        return 0;
    }
}

static int getScanningComplexity(lua_State* L)
{
    return convert<EScanningComplexity>::returnType(L, gameGlobalInfo->scanning_complexity);
}
/// EScanningComplexity getScanningComplexity()
/// Returns the running scenario's scanning complexity setting.
/// Example: getScanningComplexity() -- returns "normal" by default
REGISTER_SCRIPT_FUNCTION(getScanningComplexity);

static int getHackingDifficulty(lua_State* L)
{
    lua_pushinteger(L, gameGlobalInfo->hacking_difficulty);
    return 1;
}
/// int getHackingDifficulty()
/// Returns the running scenario's hacking difficulty setting.
/// The returned value is an integer between 0 and 3:
/// 0 = Simple
/// 1 = Normal
/// 2 = Difficult (default)
/// 3 = Fiendish 
/// Example: getHackingDifficulty() -- returns 2 by default
REGISTER_SCRIPT_FUNCTION(getHackingDifficulty);

template<> int convert<EHackingGames>::returnType(lua_State* L, EHackingGames game)
{
    switch(game)
    {
    case HG_Mine:
        lua_pushstring(L, "mines");
        return 1;
    case HG_Lights:
        lua_pushstring(L, "lights");
        return 1;
    case HG_All:
        lua_pushstring(L, "all");
        return 1;
    default:
        return 0;
    }
}

static int getHackingGames(lua_State* L)
{
    return convert<EHackingGames>::returnType(L, gameGlobalInfo->hacking_games);
}
/// EHackingGames getHackingGames()
/// Returns the running scenario's hacking difficulty setting.
/// Example: getHackingGames() -- returns "all" by default
REGISTER_SCRIPT_FUNCTION(getHackingGames);

static int areBeamShieldFrequenciesUsed(lua_State* L)
{
    lua_pushboolean(L, gameGlobalInfo->use_beam_shield_frequencies);
    return 1;
}
/// bool areBeamShieldFrequenciesUsed()
/// Returns whether the "Beam/Shield Frequencies" setting is enabled in the running scenario.
/// Example: areBeamShieldFrequenciesUsed() -- returns true by default
REGISTER_SCRIPT_FUNCTION(areBeamShieldFrequenciesUsed);

static int isPerSystemDamageUsed(lua_State* L)
{
    lua_pushboolean(L, gameGlobalInfo->use_system_damage);
    return 1;
}
/// bool isPerSystemDamageUsed()
/// Returns whether the "Per-System Damage" setting is enabled in the running scenario.
/// Example: isPerSystemDamageUsed() -- returns true by default
REGISTER_SCRIPT_FUNCTION(isPerSystemDamageUsed);

static int isTacticalRadarAllowed(lua_State* L)
{
    lua_pushboolean(L, gameGlobalInfo->allow_main_screen_tactical_radar);
    return 1;
}
/// bool isTacticalRadarAllowed()
/// Returns whether the "Tactical Radar" setting for main screens is enabled in the running scenario.
/// Example: isTacticalRadarAllowed() -- returns true by default
REGISTER_SCRIPT_FUNCTION(isTacticalRadarAllowed);

static int isLongRangeRadarAllowed(lua_State* L)
{
    lua_pushboolean(L, gameGlobalInfo->allow_main_screen_long_range_radar);
    return 1;
}
/// bool isLongRangeRadarAllowed()
/// Returns whether the "Long Range Radar" setting for main screens is enabled in the running scenario.
/// Example: isLongRangeRadarAllowed() -- returns true by default
REGISTER_SCRIPT_FUNCTION(isLongRangeRadarAllowed);

static int onNewPlayerShip(lua_State* L)
{
    //TODO
    return 0;
}
/// void onNewPlayerShip(ScriptSimpleCallback callback)
/// Defines a function to call when a new PlayerSpaceship is created, whether on the ship selection screen or with the constructor in a Lua script.
/// Passes the newly created PlayerSpaceship.
/// Example: onNewPlayerShip(function(player) print(player:getCallSign()) end) -- prints the callsign of new PlayerSpaceships to the console
REGISTER_SCRIPT_FUNCTION(onNewPlayerShip);
