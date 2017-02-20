#include "RWGame.hpp"
#include "DrawUI.hpp"
#include "GameInput.hpp"
#include "State.hpp"
#include "states/BenchmarkState.hpp"
#include "states/IngameState.hpp"
#include "states/LoadingState.hpp"
#include "states/MenuState.hpp"
#include "states/MovieState.hpp"

#include <core/Profiler.hpp>

#include <engine/SaveGame.hpp>
#include <objects/GameObject.hpp>

#include <script/SCMFile.hpp>

#include <ai/PlayerController.hpp>
#include <objects/CharacterObject.hpp>
#include <objects/VehicleObject.hpp>

#include <boost/algorithm/string/predicate.hpp>
#include <functional>

std::map<GameRenderer::SpecialModel, std::string> kSpecialModels = {
    {GameRenderer::ZoneCylinderA, "zonecyla.dff"},
    {GameRenderer::ZoneCylinderB, "zonecylb.dff"},
    {GameRenderer::Arrow, "arrow.dff"}};

#define MOUSE_SENSITIVITY_SCALE 2.5f

RWGame::RWGame(Logger& log, int argc, char* argv[])
    : GameBase(log, argc, argv)
    , data(&log, config.getGameDataPath())
    , renderer(&log, &data) {
    bool newgame = options.count("newgame");
    bool test = options.count("test");
    bool movies = options.count("movies");
    std::string startSave(
        options.count("load") ? options["load"].as<std::string>() : "");
    std::string benchFile(options.count("benchmark")
                              ? options["benchmark"].as<std::string>()
                              : "");

    log.info("Game", "Game directory: " + config.getGameDataPath());

    if (!GameData::isValidGameDirectory(config.getGameDataPath())) {
        throw std::runtime_error("Invalid game directory path: " +
                                 config.getGameDataPath());
    }

    data.load();

    for (const auto& p : kSpecialModels) {
        auto model = data.loadClump(p.second);
        renderer.setSpecialModel(p.first, model);
    }

    // Set up text renderer
    renderer.text.setFontTexture(0, "pager");
    renderer.text.setFontTexture(1, "font1");
    renderer.text.setFontTexture(2, "font2");

    debug.setDebugMode(btIDebugDraw::DBG_DrawWireframe |
                       btIDebugDraw::DBG_DrawConstraints |
                       btIDebugDraw::DBG_DrawConstraintLimits);
    debug.setShaderProgram(renderer.worldProg);

    data.loadDynamicObjects(config.getGameDataPath() + "/data/object.dat");

    data.loadGXT("text/" + config.getGameLanguage() + ".gxt");

    getRenderer().water.setWaterTable(data.waterHeights, 48, data.realWater,
                                      128 * 128);

    for (int m = 0; m < MAP_BLOCK_SIZE; ++m) {
        std::string num = (m < 10 ? "0" : "");
        std::string name = "radar" + num + std::to_string(m);
        data.loadTXD(name + ".txd");
    }

    if (movies) {
        StateManager::get().enter<MovieState>(this, config.getGameDataPath() + "/movies/Logo.mpg");
        //StateManager::get().enter<MovieState>(this, config.getGameDataPath() + "/movies/GTAtitles.mpg");
    }

    StateManager::get().enter<LoadingState>(this, [=]() {
        if (!benchFile.empty()) {
            StateManager::get().enter<BenchmarkState>(this, benchFile);
        } else if (test) {
            StateManager::get().enter<IngameState>(this, true, "test");
        } else if (newgame) {
            StateManager::get().enter<IngameState>(this, true);
        } else if (!startSave.empty()) {
            StateManager::get().enter<IngameState>(this, true, startSave);
        } else {
            StateManager::get().enter<MenuState>(this);
        }
    });

    log.info("Game", "Started");
}

RWGame::~RWGame() {
    log.info("Game", "Beginning cleanup");
}

void RWGame::newGame() {
    // Get a fresh state
    state = GameState();

    // Destroy the current world and start over
    world = std::make_unique<GameWorld>(&log, &data);
    world->dynamicsWorld->setDebugDrawer(&debug);

    // Associate the new world with the new state and vice versa
    state.world = world.get();
    world->state = &state;

    for (auto ipl : world->data->iplLocations) {
        world->data->loadZone(ipl.second);
        world->placeItems(ipl.second);
    }
}

void RWGame::saveGame(const std::string& savename) {
    RW_UNUSED(savename);
}

void RWGame::loadGame(const std::string& savename) {
    delete state.script;

    log.info("Game", "Loading game " + savename);

    newGame();

    startScript("data/main.scm");

    if (!SaveGame::loadGame(state, savename)) {
        log.error("Game", "Failed to load game");
    }
}

void RWGame::startScript(const std::string& name) {
    script.reset(data.loadSCM(name));
    if (script) {
        vm = std::make_unique<ScriptMachine>(&state, script.get(), &opcodes);

        state.script = vm.get();
    } else {
        log.error("Game", "Failed to load SCM: " + name);
    }
}

PlayerController* RWGame::getPlayer() {
    auto object = world->pedestrianPool.find(state.playerObject);
    if (object) {
        auto controller = static_cast<CharacterObject*>(object)->controller;
        return static_cast<PlayerController*>(controller);
    }
    return nullptr;
}

// Modifiers for GTA3 we try to recreate
#define RW_GAME_VERSION 1100
#define RW_GAME_GTA3_GERMAN 0
#define RW_GAME_GTA3_ANNIVERSARY 0

void RWGame::handleCheatInput(char symbol) {
    cheatInputWindow = cheatInputWindow.substr(1) + symbol;

    // Helper to check for cheats
    auto checkForCheat = [this](std::string cheat,
                                std::function<void()> action) {
        RW_CHECK(cheatInputWindow.length() >= cheat.length(), "Cheat too long");
        size_t offset = cheatInputWindow.length() - cheat.length();
        if (cheat == cheatInputWindow.substr(offset)) {
            log.info("Game", "Cheat triggered: '" + cheat + "'");
            if (action) {
                action();
            }
        }
    };

    // Player related cheats
    {
        auto player = getPlayer()->getCharacter();

#ifdef RW_GAME_GTA3_GERMAN  // Germans got their own cheat
        std::string health_cheat = "GESUNDHEIT";
#else
        std::string health_cheat = "HEALTH";
#endif
        checkForCheat(health_cheat, [&] {
            player->getCurrentState().health = 100.f;
            // @todo ShowHelpMessage("CHEAT3"); // III / VC: Inputting health
            // cheat.
        });

#if RW_GAME_VERSION >= 1100  // Changed cheat name in version 1.1
        std::string armor_cheat = "TORTOISE";
#else
        std::string armor_cheat = "TURTOISE";
#endif
        checkForCheat(armor_cheat, [&] {
            player->getCurrentState().armour = 100.f;
            // @todo ShowHelpMessage("CHEAT4"); // III / VC: Inputting armor
            // cheat.
        });

        checkForCheat("GUNSGUNSGUNS", [&] {
            // @todo give player weapons
            // @todo ShowHelpMessage("CHEAT2"); // III / VC: Inputting weapon
            // cheats.
        });

        checkForCheat("IFIWEREARICHMAN", [&] {
            state.playerInfo.money += 250000;
            // @todo ShowHelpMessage("CHEAT6"); // III: Inputting money cheat.
        });

        checkForCheat("MOREPOLICEPLEASE", [&] {
            // @todo raise to next wanted level
            // @todo ShowHelpMessage("CHEAT5"); // III / VC: Inputting wanted
            // level cheats.
        });

        checkForCheat("NOPOLICEPLEASE", [&] {
            // @todo lower to next lower wanted level
            // @todo ShowHelpMessage("CHEAT5"); // III / VC: Inputting wanted
            // level cheats.
        });
    }

    // Misc cheats.
    {
        checkForCheat("BANGBANGBANG", [&] {
            // @todo Explode nearby vehicles
            // @todo What radius?
            // @todo ShowHelpMessage("CHEAT1"); // III / VC: Inputting most
            // cheats.
        });

        checkForCheat("GIVEUSATANK", [&] {
// The iPod / Android version of the game (10th year anniversary) spawns random
// (?) vehicles instead of always rhino
#ifdef RW_GAME_GTA3_ANNIVERSARY
            uint16_t vehicleModel = 110;  // @todo Which cars are spawned?!
#else
			uint16_t vehicleModel = 122;
#endif
            // @todo Spawn rhino
            // @todo ShowHelpMessage("CHEAT1"); // III / VC: Inputting most
            // cheats.
        });

        checkForCheat("CORNERSLIKEMAD", [&] {
            // @todo Weird car handling
            // @todo ShowHelpMessage("CHEAT1"); // III / VC: Inputting most
            // cheats.
        });

        checkForCheat("ANICESETOFWHEELS", [&] {
            // @todo Hide car bodies
            // @todo ShowHelpMessage("CHEAT1"); // III / VC: Inputting most
            // cheats.
        });

        checkForCheat("CHITTYCHITTYBB", [&] {
            // @todo Cars can fly
            // @todo ShowHelpMessage("CHEAT1"); // III / VC: Inputting most
            // cheats.
        });

        checkForCheat("NASTYLIMBCHEAT", [&] {
            // @todo Makes it possible to shoot limbs off, iirc?
            // @todo ShowHelpMessage("CHEAT1"); // III / VC: Inputting most
            // cheats.
        });

        checkForCheat("ILIKEDRESSINGUP", [&] {
            // @todo Which skins will be chosen?
            // @todo ShowHelpMessage("CHEAT1"); // III / VC: Inputting most
            // cheats.
        });
    }

    // Pedestrian cheats
    {
        checkForCheat("WEAPONSFORALL", [&] {
            // @todo Give all pedestrians weapons.. this is also saved in the
            // savegame?!
            // @todo Which weapons do they get?
            // @todo ShowHelpMessage("CHEAT1"); // III / VC: Inputting most
            // cheats.
        });

        checkForCheat("NOBODYLIKESME", [&] {
            // @todo Set all pedestrians hostile towards player.. this is also
            // saved in the savegame?!
            // @todo ShowHelpMessage("CHEAT1"); // III / VC: Inputting most
            // cheats.
        });

        checkForCheat("ITSALLGOINGMAAAD", [&] {
            // @todo Set all pedestrians to fighting each other.. this is also
            // saved in the savegame?!
            // @todo ShowHelpMessage("CHEAT1"); // III / VC: Inputting most
            // cheats.
        });

        // Game speed cheats

        checkForCheat("TIMEFLIESWHENYOU", [&] {
            // @todo Set fast gamespeed
            // @todo ShowHelpMessage("CHEAT1"); // III / VC: Inputting most
            // cheats.
        });

        checkForCheat("BOOOOORING", [&] {
            // @todo Set slow gamespeed
            // @todo ShowHelpMessage("CHEAT1"); // III / VC: Inputting most
            // cheats.
        });
    }

    // Weather cheats
    {
        checkForCheat("ILIKESCOTLAND", [&] {
            // @todo Set weather to cloudy
            // @todo ShowHelpMessage("CHEAT7"); // III / VC: Inputting weather
            // cheats.
        });

        checkForCheat("SKINCANCERFORME", [&] {
            // @todo Set sunny / normal weather
            // @todo ShowHelpMessage("CHEAT7"); // III / VC: Inputting weather
            // cheats.
        });

        checkForCheat("MADWEATHER", [&] {
            // @todo Set bad weather
            // @todo ShowHelpMessage("CHEAT7"); // III / VC: Inputting weather
            // cheats.
        });

        checkForCheat("ILOVESCOTLAND", [&] {
            // @todo Set weather to rainy
            // @todo ShowHelpMessage("CHEAT7"); // III / VC: Inputting weather
            // cheats.
        });

        checkForCheat("PEASOUP", [&] {
            // @todo Set weather to foggy
            // @todo ShowHelpMessage("CHEAT7"); // III / VC: Inputting weather
            // cheats.
        });
    }
}

int RWGame::run() {
    last_clock_time = clock.now();

    // Loop until we run out of states.
    bool running = true;
    while (!StateManager::get().states.empty() && running) {
        State* state = StateManager::get().states.back().get();

        RW_PROFILE_FRAME_BOUNDARY();

        RW_PROFILE_BEGIN("Input");
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;

                case SDL_WINDOWEVENT:
                    switch (event.window.event) {
                        case SDL_WINDOWEVENT_FOCUS_GAINED:
                            inFocus = true;
                            break;

                        case SDL_WINDOWEVENT_FOCUS_LOST:
                            inFocus = false;
                            break;
                    }
                    break;

                case SDL_KEYDOWN:
                    globalKeyEvent(event);
                    break;

                case SDL_MOUSEMOTION:
                    event.motion.xrel *= MOUSE_SENSITIVITY_SCALE;
                    event.motion.yrel *= MOUSE_SENSITIVITY_SCALE;
                    break;
            }

            GameInput::updateGameInputState(&getState()->input[0], event);

            RW_PROFILE_BEGIN("State");
            state->handleEvent(event);
            RW_PROFILE_END()
        }
        RW_PROFILE_END();

        auto now = clock.now();
        float timer =
            std::chrono::duration<float>(now - last_clock_time).count();
        last_clock_time = now;
        accum += timer * timescale;

        RW_PROFILE_BEGIN("Update");
        if (accum >= GAME_TIMESTEP) {
            if (StateManager::get().states.size() == 0) {
                break;
            }

            RW_PROFILE_BEGIN("state");
            StateManager::get().tick(GAME_TIMESTEP);
            RW_PROFILE_END();

            RW_PROFILE_BEGIN("engine");
            tick(GAME_TIMESTEP);
            RW_PROFILE_END();

            getState()->swapInputState();

            accum -= GAME_TIMESTEP;

            // Throw away time if the accumulator reaches too high.
            if (accum > GAME_TIMESTEP * 5.f) {
                accum = 0.f;
            }
        }
        RW_PROFILE_END();

        float alpha = fmod(accum, GAME_TIMESTEP) / GAME_TIMESTEP;
        if (!state->shouldWorldUpdate()) {
            alpha = 1.f;
        }

        RW_PROFILE_BEGIN("Render");
        RW_PROFILE_BEGIN("engine");
        render(alpha, timer);
        RW_PROFILE_END();

        RW_PROFILE_BEGIN("state");
        if (StateManager::get().states.size() > 0) {
            StateManager::get().draw(&renderer);
        }
        RW_PROFILE_END();
        RW_PROFILE_END();

        renderProfile();

        getWindow().swap();

        // Make sure the topmost state is the correct state
        StateManager::get().updateStack();
    }

    StateManager::get().clear();

    return 0;
}

void RWGame::tick(float dt) {
    State* currState = StateManager::get().states.back().get();

    world->chase.update(dt);

    static float clockAccumulator = 0.f;
    if (currState->shouldWorldUpdate()) {
        // Clear out any per-tick state.
        world->clearTickData();

        state.gameTime += dt;

        clockAccumulator += dt;
        while (clockAccumulator >= 1.f) {
            state.basic.gameMinute++;
            while (state.basic.gameMinute >= 60) {
                state.basic.gameMinute = 0;
                state.basic.gameHour++;
                while (state.basic.gameHour >= 24) {
                    state.basic.gameHour = 0;
                }
            }
            clockAccumulator -= 1.f;
        }

        // Clean up old VisualFX
        for (int i = 0; i < static_cast<int>(world->effects.size()); ++i) {
            VisualFX* effect = world->effects[i];
            if (effect->getType() == VisualFX::Particle) {
                auto& part = effect->particle;
                if (part.lifetime < 0.f) continue;
                if (world->getGameTime() >= part.starttime + part.lifetime) {
                    world->destroyEffect(effect);
                    --i;
                }
            }
        }

        for (auto& object : world->allObjects) {
            object->_updateLastTransform();
            object->tick(dt);
        }

        world->destroyQueuedObjects();

        state.text.tick(dt);

        world->dynamicsWorld->stepSimulation(dt, 2, dt);

        if (vm) {
            try {
                vm->execute(dt);
            } catch (SCMException& ex) {
                std::cerr << ex.what() << std::endl;
                log.error("Script", ex.what());
                throw;
            }
        }

        /// @todo this doesn't make sense as the condition
        if (state.playerObject) {
            currentCam.frustum.update(currentCam.frustum.projection() *
                                   currentCam.getView());
            // Use the current camera position to spawn pedestrians.
            world->cleanupTraffic(currentCam);
            // Only create new traffic outside cutscenes
            if (!state.currentCutscene) {
                world->createTraffic(currentCam);
            }
        }
    }
}

void RWGame::render(float alpha, float time) {
    lastDraws = getRenderer().getRenderer()->getDrawCount();

    getRenderer().getRenderer()->swap();

    // Update the camera
    if (!StateManager::get().states.empty()) {
        currentCam = StateManager::get().states.back()->getCamera(alpha);
    }

    glm::ivec2 windowSize = getWindow().getSize();
    renderer.setViewport(windowSize.x, windowSize.y);

    ViewCamera viewCam = currentCam;

    viewCam.frustum.aspectRatio =
        windowSize.x / static_cast<float>(windowSize.y);

    if (state.isCinematic) {
        viewCam.frustum.fov *= viewCam.frustum.aspectRatio;
    }

    glEnable(GL_DEPTH_TEST);
    glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

    renderer.getRenderer()->pushDebugGroup("World");

    RW_PROFILE_BEGIN("world");
    renderer.renderWorld(world.get(), viewCam, alpha);
    RW_PROFILE_END();

    renderer.getRenderer()->popDebugGroup();

    RW_PROFILE_BEGIN("debug");
    switch (debugview_) {
        case DebugViewMode::General:
            renderDebugStats(time);
            break;
        case DebugViewMode::Physics:
            world->dynamicsWorld->debugDrawWorld();
            debug.flush(&renderer);
            break;
        case DebugViewMode::Navigation:
            renderDebugPaths(time);
            break;
        case DebugViewMode::Objects:
            renderDebugObjects(time, viewCam);
            break;
        default:
            break;
    }
    RW_PROFILE_END();

    drawOnScreenText(world.get(), &renderer);
}

void RWGame::renderDebugStats(float time) {
    // Turn time into milliseconds
    float time_ms = time * 1000.f;
    constexpr size_t average_every_frame = 15;
    static float times[average_every_frame];
    static size_t times_index = 0;
    static float time_average = 0;
    times[times_index++] = time_ms;
    if (times_index >= average_every_frame) {
        times_index = 0;
        time_average = 0;

        for (size_t i = 0; i < average_every_frame; ++i) {
            time_average += times[i];
        }
        time_average /= average_every_frame;
    }

    std::stringstream ss;
    ss << "FPS: " << (1000.f / time_average) << " (" << time_average << "ms)\n"
       << "Frame: " << time_ms << "ms\n"
       << "Draws/Culls/Textures/Buffers: " << lastDraws << "/"
       << renderer.getCulledCount() << "/"
       << renderer.getRenderer()->getTextureCount() << "/"
       << renderer.getRenderer()->getBufferCount() << "\n";

    TextRenderer::TextInfo ti;
    ti.text = GameStringUtil::fromString(ss.str());
    ti.font = 2;
    ti.screenPosition = glm::vec2(10.f, 10.f);
    ti.size = 15.f;
    ti.baseColour = glm::u8vec3(255);
    renderer.text.renderText(ti);

    /*while( engine->log.size() > 0 && engine->log.front().time + 10.f <
    engine->gameTime ) {
        engine->log.pop_front();
    }

    ti.screenPosition = glm::vec2( 10.f, 500.f );
    ti.size = 15.f;
    for(auto it = engine->log.begin(); it != engine->log.end(); ++it) {
        ti.text = it->message;
        switch(it->type) {
        case GameWorld::LogEntry::Error:
            ti.baseColour = glm::vec3(1.f, 0.f, 0.f);
            break;
        case GameWorld::LogEntry::Warning:
            ti.baseColour = glm::vec3(1.f, 1.f, 0.f);
            break;
        default:
            ti.baseColour = glm::vec3(1.f, 1.f, 1.f);
            break;
        }

        // Interpolate the color
        // c.a = (engine->gameTime - it->time > 5.f) ? 255 - (((engine->gameTime
    - it->time) - 5.f)/5.f) * 255 : 255;
        // text.setColor(c);

        engine->renderer.text.renderText(ti);
        ti.screenPosition.y -= ti.size;
    }*/
}

void RWGame::renderDebugPaths(float time) {
    RW_UNUSED(time);

    btVector3 roadColour(1.f, 0.f, 0.f);
    btVector3 pedColour(0.f, 0.f, 1.f);

    for (AIGraphNode* n : world->aigraph.nodes) {
        btVector3 p(n->position.x, n->position.y, n->position.z);
        auto& col = n->type == AIGraphNode::Pedestrian ? pedColour : roadColour;
        debug.drawLine(p - btVector3(0.f, 0.f, 1.f),
                       p + btVector3(0.f, 0.f, 1.f), col);
        debug.drawLine(p - btVector3(1.f, 0.f, 0.f),
                       p + btVector3(1.f, 0.f, 0.f), col);
        debug.drawLine(p - btVector3(0.f, 1.f, 0.f),
                       p + btVector3(0.f, 1.f, 0.f), col);

        for (AIGraphNode* c : n->connections) {
            btVector3 f(c->position.x, c->position.y, c->position.z);
            debug.drawLine(p, f, col);
        }
    }

    // Draw Garage bounds
    for (size_t g = 0; g < state.garages.size(); ++g) {
        auto& garage = state.garages[g];
        btVector3 minColor(1.f, 0.f, 0.f);
        btVector3 maxColor(0.f, 1.f, 0.f);
        btVector3 min(garage.min.x, garage.min.y, garage.min.z);
        btVector3 max(garage.max.x, garage.max.y, garage.max.z);
        debug.drawLine(min, min + btVector3(0.5f, 0.f, 0.f), minColor);
        debug.drawLine(min, min + btVector3(0.f, 0.5f, 0.f), minColor);
        debug.drawLine(min, min + btVector3(0.f, 0.f, 0.5f), minColor);

        debug.drawLine(max, max - btVector3(0.5f, 0.f, 0.f), maxColor);
        debug.drawLine(max, max - btVector3(0.f, 0.5f, 0.f), maxColor);
        debug.drawLine(max, max - btVector3(0.f, 0.f, 0.5f), maxColor);
    }

    // Draw vehicle generators
    for (size_t v = 0; v < state.vehicleGenerators.size(); ++v) {
        auto& generator = state.vehicleGenerators[v];
        btVector3 color(1.f, 0.f, 0.f);
        btVector3 position(generator.position.x, generator.position.y,
                           generator.position.z);
        float heading = glm::radians(generator.heading);
        auto back =
            btVector3(0.f, -1.f, 0.f).rotate(btVector3(0.f, 0.f, 1.f), heading);
        auto right = btVector3(0.15f, -0.15f, 0.f)
                         .rotate(btVector3(0.f, 0.f, 1.f), heading);
        auto left = btVector3(-0.15f, -0.15f, 0.f)
                        .rotate(btVector3(0.f, 0.f, 1.f), heading);
        debug.drawLine(position, position + back, color);
        debug.drawLine(position, position + right, color);
        debug.drawLine(position, position + left, color);
    }

    debug.flush(&renderer);
}

void RWGame::renderDebugObjects(float time, ViewCamera& camera) {
    RW_UNUSED(time);

    std::stringstream ss;

    ss << "Models: " << data.modelinfo.size() << "\n"
       << "Dynamic Objects:\n"
       << " Vehicles: " << world->vehiclePool.objects.size() << "\n"
       << " Peds: " << world->pedestrianPool.objects.size() << "\n";

    TextRenderer::TextInfo ti;
    ti.text = GameStringUtil::fromString(ss.str());
    ti.font = 2;
    ti.screenPosition = glm::vec2(10.f, 10.f);
    ti.size = 15.f;
    ti.baseColour = glm::u8vec3(255);
    renderer.text.renderText(ti);

    // Render worldspace overlay for nearby objects
    constexpr float kNearbyDistance = 25.f;
    const auto& view = camera.position;
    const auto& model = camera.getView();
    const auto& proj = camera.frustum.projection();
    const auto& size = getWindow().getSize();
    glm::vec4 viewport(0.f, 0.f, size.x, size.y);
    auto isnearby = [&](GameObject* o) {
        return glm::distance2(o->getPosition(), view) <
               kNearbyDistance * kNearbyDistance;
    };
    auto showdata = [&](GameObject* o, std::stringstream& ss) {
        auto screen = glm::project(o->getPosition(), model, proj, viewport);
        if (screen.z >= 1.f) {
            return;
        }
        ti.text = GameStringUtil::fromString(ss.str());
        screen.y = viewport.w - screen.y;
        ti.screenPosition = glm::vec2(screen);
        ti.size = 10.f;
        renderer.text.renderText(ti);
    };

    for (auto& p : world->vehiclePool.objects) {
        if (!isnearby(p.second)) continue;
        auto v = static_cast<VehicleObject*>(p.second);

        std::stringstream ss;
        ss << v->getVehicle()->vehiclename_ << "\n"
           << (v->isFlipped() ? "Flipped" : "Upright") << "\n"
           << (v->isStopped() ? "Stopped" : "Moving") << "\n"
           << v->getVelocity() << "m/s\n";

        showdata(v, ss);
    }
    for (auto& p : world->pedestrianPool.objects) {
        if (!isnearby(p.second)) continue;
        auto c = static_cast<CharacterObject*>(p.second);
        const auto& state = c->getCurrentState();
        auto act = c->controller->getCurrentActivity();

        std::stringstream ss;
        ss << "Health: " << state.health << " (" << state.armour << ")\n"
           << (c->isAlive() ? "Alive" : "Dead") << "\n"
           << "Activity: " << (act ? act->name() : "Idle") << "\n";

        showdata(c, ss);
    }
}

void RWGame::renderProfile() {
#if RW_PROFILER
    auto& frame = perf::Profiler::get().getFrame();
    constexpr float upperlimit = 30000.f;
    constexpr float lineHeight = 15.f;
    static std::vector<glm::vec4> perf_colours;
    if (perf_colours.size() == 0) {
        float c = 8.f;
        for (int r = 0; r < c; ++r) {
            for (int g = 0; g < c; ++g) {
                for (int b = 0; b < c; ++b) {
                    perf_colours.push_back({r / c, g / c, b / c, 1.f});
                }
            }
        }
    }

    float xscale = renderer.getRenderer()->getViewport().x / upperlimit;
    TextRenderer::TextInfo ti;
    ti.align = TextRenderer::TextInfo::Left;
    ti.font = 2;
    ti.size = lineHeight - 2.f;
    ti.baseColour = glm::u8vec3(255);
    std::function<void(const perf::ProfileEntry&, int)> renderEntry = [&](
        const perf::ProfileEntry& entry, int depth) {
        int g = 0;
        for (auto& event : entry.childProfiles) {
            auto duration = event.end - event.start;
            float y = 60.f + (depth * (lineHeight + 5.f));
            renderer.drawColour(
                perf_colours[(std::hash<std::string>()(entry.label) * (g++)) %
                             perf_colours.size()],
                {xscale * event.start, y, xscale * duration, lineHeight});
            ti.screenPosition.x = xscale * (event.start);
            ti.screenPosition.y = y + 2.f;
            ti.text = GameStringUtil::fromString(event.label + " " + std::to_string(duration) + " us ");
            renderer.text.renderText(ti);
            renderEntry(event, depth + 1);
        }
    };
    renderEntry(frame, 0);
    ti.screenPosition = glm::vec2(xscale * (16000), 40.f);
    ti.text = GameStringUtil::fromString(".16 ms");
    renderer.text.renderText(ti);
#endif
}

void RWGame::globalKeyEvent(const SDL_Event& event) {
    const auto toggle_debug = [&](DebugViewMode m) {
        debugview_ = debugview_ == m ? DebugViewMode::Disabled : m;
    };

    switch (event.key.keysym.sym) {
        case SDLK_LEFTBRACKET:
            world->offsetGameTime(-30);
            break;
        case SDLK_RIGHTBRACKET:
            world->offsetGameTime(30);
            break;
        case SDLK_9:
            timescale *= 0.5f;
            break;
        case SDLK_0:
            timescale *= 2.0f;
            break;
        case SDLK_F1:
            toggle_debug(DebugViewMode::General);
            break;
        case SDLK_F2:
            toggle_debug(DebugViewMode::Navigation);
            break;
        case SDLK_F3:
            toggle_debug(DebugViewMode::Physics);
            break;
        case SDLK_F4:
            toggle_debug(DebugViewMode::Objects);
            break;
        default:
            break;
    }

    std::string keyName = SDL_GetKeyName(event.key.keysym.sym);
    if (keyName.length() == 1) {
        char symbol = keyName[0];
        handleCheatInput(symbol);
    }
}
