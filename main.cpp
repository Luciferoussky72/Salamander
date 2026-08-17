#include <cmath>

#include "../Aseprite_Parsing/aseprite_parser.hpp"
#include <SDL3/SDL_init.h>
#include <loam/Loader.hpp>
#include <loam/Random.hpp>
#include <loam/Vec2.hpp>
#include <loam/KeyboardMouse.hpp>
#include <loam/Collisions.hpp>
#include <loam/Math.hpp>

constexpr u_char rocks_data[] = {
    #embed "Rock.aseprite"
};
constexpr auto rocks_parsed = parsed_file<sizeof(rocks_data), rocks_data, read_frame_count(rocks_data)>{};
constexpr u_char salamander_data[] = {
    #embed "Salamander.aseprite"
};
constexpr auto salamander_parsed = parsed_file<sizeof(salamander_data), salamander_data, read_frame_count(salamander_data)>{};
constexpr u_char interface_data[] = {
    #embed "Interface.aseprite"
};
constexpr auto interface_parsed = parsed_file<sizeof(interface_data), interface_data, read_frame_count(interface_data)>{};
constexpr u_char hand_data[] = {
    #embed "Hand.aseprite"
};
constexpr auto hand_parsed = parsed_file<sizeof(hand_data), hand_data, read_frame_count(hand_data)>{};
constexpr u_char text_data[] = {
    #embed "Text.aseprite"
};
constexpr auto text_parsed = parsed_file<sizeof(text_data), text_data, read_frame_count(text_data)>{};
constexpr u_char gameover_data[] = {
    #embed "Gameover.aseprite"
};
constexpr auto gameover_parsed = parsed_file<sizeof(gameover_data), gameover_data, read_frame_count(gameover_data)>{};

enum class textures : Uint8 {
    salamander,
    rocks,
    rock_shadows,
    interface,
    hand_grab,
    hand_grab_shadow,
    text,
    gameover,
};

struct Salamander {
    loam::Spritesheet sprites{};
    loam::Vec2 position{};
    loam::Vec2 velocity{};
    double angle = 0.0;
    bool moving_this_frame = false;
    constexpr static double ANGLE_STEP = 2.0;
    size_t frame = 0;
    Uint8 temperature = 25;
    bool caught = false;
    bool cooling_down = false;
};

struct Hand {
    loam::Spritesheet grab_sprites{};
    loam::Spritesheet grab_shadow_sprites{};
    loam::Vec2 position{};
    float z = 50.0f;
    size_t frame = 0;
    size_t grab_interval = 360;
    size_t ticks_since_last_grab = 0;
    bool on_ground = false;
    bool hit_rock = false;
    loam::Vec2* grabbed_thing_pos = nullptr;
};

struct Camera {
    loam::Vec2 top_left_pos{};
    [[nodiscard]] loam::Vec2 world_to_screen(loam::Vec2 input) const {
        return input - top_left_pos;
    }
    [[maybe_unused]] [[nodiscard]] loam::Vec2 screen_to_world(loam::Vec2 input) const {
        return top_left_pos + input;
    }
    void center_around(SDL_Window* window, loam::Vec2 center) {
        int w = 0;
        int h = 0;
        SDL_GetWindowSize(window, &w, &h);
        if (!w or !h) return;

        top_left_pos.x = center.x - static_cast<float>(w) / 2.0f;
        top_left_pos.y = center.y - static_cast<float>(h) / 2.0f;
    }
};

constexpr size_t ROCK_COUNT = 1024;
constexpr float ROCK_SPREAD_AREA = 25000.0f;
constinit loam::Vec2 rock_positions[ROCK_COUNT] = {};
constinit size_t rock_sprites[ROCK_COUNT] = {};

constexpr float SCALE = 6.0f;
constexpr float UPDATES_PER_SECOND = 60;
constexpr Uint64 UPDATE_LOOP_TIME_MS = static_cast<Uint64>(1000.0f / UPDATES_PER_SECOND);
constinit Uint64 start_time = 0ul;
constinit Uint64 time_accumulator = 0ul;

int main() {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD);
    SDL_Window* window = SDL_CreateWindow("Salamander", 1920, 1080, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_BORDERLESS);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    loam::Loader<textures> textures{renderer};

    Salamander salamander{};
    textures.load_texture(textures::salamander, salamander_parsed.fetch_animation("Walk", renderer));
    salamander.sprites = loam::Spritesheet{textures[textures::salamander], *salamander_parsed.fetch_animation_direction("Walk"),
        salamander_parsed.fetch_canvas_width(), salamander_parsed.fetch_canvas_height()};

    Hand hand{};
    hand.position = loam::Vec2{50000.0f, 50000.0f};
    textures.load_texture(textures::hand_grab, hand_parsed.fetch_animation("Grab", renderer));
    hand.grab_sprites = loam::Spritesheet{
        textures[textures::hand_grab],
        *hand_parsed.fetch_animation_direction("Grab"),
        hand_parsed.fetch_canvas_width(),
        hand_parsed.fetch_canvas_height()
    };
    textures.load_texture(textures::hand_grab_shadow, hand_parsed.fetch_animation("Grab Shadow", renderer));
    hand.grab_shadow_sprites = loam::Spritesheet{
        textures[textures::hand_grab_shadow],
        *hand_parsed.fetch_animation_direction("Grab Shadow"),
        hand_parsed.fetch_canvas_width(),
        hand_parsed.fetch_canvas_height()
    };

    textures.load_texture(textures::text, text_parsed.fetch_spritesheet(renderer));
    textures.load_texture(textures::gameover, gameover_parsed.fetch_spritesheet(renderer));

    Camera camera{};
    camera.center_around(window, loam::Vec2 {
        salamander.position.x + static_cast<float>(salamander.sprites.sprite_width) * SCALE / 2.0f,
        salamander.position.y + static_cast<float>(salamander.sprites.sprite_height) * SCALE / 2.0f
    });

    textures.load_texture(textures::rocks, rocks_parsed.fetch_animation("Rocks", renderer));
    loam::Spritesheet rock_spritesheet = loam::Spritesheet{textures[textures::rocks], *rocks_parsed.fetch_animation_direction("Rocks"),
        rocks_parsed.fetch_canvas_width(), rocks_parsed.fetch_canvas_height()};
    textures.load_texture(textures::rock_shadows, rocks_parsed.fetch_animation("Shadows", renderer));
    loam::Spritesheet rock_shadow_spritesheet = loam::Spritesheet{textures[textures::rock_shadows], *rocks_parsed.fetch_animation_direction("Shadows"),
        rocks_parsed.fetch_canvas_width(), rocks_parsed.fetch_canvas_height()};
    for (loam::Vec2& pos : rock_positions) {
        pos.x = loam::rand_float(-ROCK_SPREAD_AREA, ROCK_SPREAD_AREA);
        pos.y = loam::rand_float(-ROCK_SPREAD_AREA, ROCK_SPREAD_AREA);
    }
    for (size_t& spr : rock_sprites) {
        spr = loam::rand_int(0, rocks_parsed.header_data.frame_count);
    }

    textures.load_texture(textures::interface, interface_parsed.fetch_spritesheet(renderer));
    loam::Spritesheet interface_spritesheet = loam::Spritesheet{textures[textures::interface], loam::animation_direction::forward,
        interface_parsed.fetch_canvas_width(), interface_parsed.fetch_canvas_height()};

    loam::KeyboardMouse input{};

    bool running = true;
    volatile bool on_main_menu = true;
    volatile bool on_game_over_screen = false;
    SDL_Event event;
    Uint64 ticks = 0;

    while (running) {
        start_time = SDL_GetTicks();
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    running = false;
                default:
                    break;
            }
        }
        input.update();

        //end update calls
        if (on_main_menu) {
            if (input.key_down(SDL_SCANCODE_SPACE)) {
                salamander.position = loam::Vec2{};
                salamander.angle = 0.0;
                salamander.frame = 0;
                salamander.moving_this_frame = false;
                salamander.velocity = loam::Vec2{};
                ticks = 0;
                on_main_menu = false;
                continue;
            }
            while (time_accumulator >= UPDATE_LOOP_TIME_MS) {
                if (time_accumulator > 250) {
                    time_accumulator = 250;
                }
                //start main menu updating logic

                salamander.moving_this_frame = false;
                if (input.key_down(SDL_SCANCODE_UP) or input.key_down(SDL_SCANCODE_W)) {
                    salamander.velocity.x += static_cast<float>(std::cos(salamander.angle * (std::numbers::pi / 180.0)));
                    salamander.velocity.y += static_cast<float>(std::sin(salamander.angle * (std::numbers::pi / 180.0)));
                }
                if (input.key_down(SDL_SCANCODE_LEFT) or input.key_down(SDL_SCANCODE_A)) {
                    salamander.angle -= Salamander::ANGLE_STEP;
                    salamander.angle = std::fmod(salamander.angle, 360.0);
                    salamander.moving_this_frame = true;
                } else if (input.key_down(SDL_SCANCODE_RIGHT) or input.key_down(SDL_SCANCODE_D)) {
                    salamander.angle += Salamander::ANGLE_STEP;
                    salamander.angle = std::fmod(salamander.angle, 360.0);
                    salamander.moving_this_frame = true;
                }
                salamander.velocity *= 0.95f;
                if (salamander.velocity.magnitude_squared() >= 50.0f) {
                    salamander.moving_this_frame = true;
                }
                if (salamander.moving_this_frame) {
                    if (ticks % 6 == 0) {
                        salamander.frame += 1;
                    }
                }
                salamander.position += salamander.velocity;

                ++ticks;
                if (!hand.on_ground) {
                    ++hand.ticks_since_last_grab;
                }
                time_accumulator -= UPDATE_LOOP_TIME_MS;
                //end main menu updating logic
            }
            //start main menu rendering code
            SDL_SetRenderDrawColor(renderer, 217, 160, 102, 255);
            SDL_RenderClear(renderer);

            loam::Vec2 salamander_screen_pos = camera.world_to_screen(salamander.position + loam::Vec2{200.0f, 0.0f});
            salamander.sprites.render(renderer, salamander.frame, {
                .x = salamander_screen_pos.x,
                .y = salamander_screen_pos.y,
                .w = static_cast<float>(salamander.sprites.sprite_width) * SCALE,
                .h = static_cast<float>(salamander.sprites.sprite_height) * SCALE
            }, false, false, salamander.angle);


            SDL_FRect src = {.x = 0.0f, .y = 0.0f, .w = 128.0f, .h = 32.0f};
            SDL_FRect dest = {.x = 300.0f, .y = 200.0f, .w = 128.0f * SCALE, .h = 32.0f * SCALE};

            SDL_RenderTexture(renderer, textures[textures::text], &src, &dest);
            src.y += 32.0f;
            dest.y += 32.0f * SCALE;
            SDL_RenderTexture(renderer, textures[textures::text], &src, &dest);
            src.y += 32.0f;
            dest.y += 32.0f * SCALE;
            SDL_RenderTexture(renderer, textures[textures::text], &src, &dest);
            src.y += 32.0f;
            dest.y += 32.0f * SCALE;
            SDL_RenderTexture(renderer, textures[textures::text], &src, &dest);

            SDL_RenderPresent(renderer);

            //end main menu rendering code

            Uint64 elapsed_time = SDL_GetTicks() - start_time;
            time_accumulator += elapsed_time;
        } else if (on_game_over_screen) {
            if (input.key_down(SDL_SCANCODE_R)) {
                salamander = Salamander{};
                salamander.sprites = loam::Spritesheet{textures[textures::salamander], *salamander_parsed.fetch_animation_direction("Walk"),
                    salamander_parsed.fetch_canvas_width(), salamander_parsed.fetch_canvas_height()};
                for (loam::Vec2& pos : rock_positions) {
                    pos.x = loam::rand_float(-ROCK_SPREAD_AREA, ROCK_SPREAD_AREA);
                    pos.y = loam::rand_float(-ROCK_SPREAD_AREA, ROCK_SPREAD_AREA);
                }
                for (size_t& spr : rock_sprites) {
                    spr = loam::rand_int(0, rocks_parsed.header_data.frame_count);
                }
                hand = Hand{};
                hand.grab_sprites = loam::Spritesheet{
                    textures[textures::hand_grab],
                    *hand_parsed.fetch_animation_direction("Grab"),
                    hand_parsed.fetch_canvas_width(),
                    hand_parsed.fetch_canvas_height()
                };
                hand.grab_shadow_sprites = loam::Spritesheet{
                    textures[textures::hand_grab_shadow],
                    *hand_parsed.fetch_animation_direction("Grab Shadow"),
                    hand_parsed.fetch_canvas_width(),
                    hand_parsed.fetch_canvas_height()
                };
                hand.position = loam::Vec2{50000.0f, 50000.0f};
                ticks = 0;
                on_game_over_screen = false;
                continue;
            }
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);

            SDL_FRect dest = {
                .x = 400.0f,
                .y = 300.0f,
                .w = static_cast<float>(gameover_parsed.fetch_canvas_width()) * SCALE,
                .h = static_cast<float>(gameover_parsed.fetch_canvas_height()) * SCALE
            };
            SDL_RenderTexture(renderer, textures[textures::gameover], nullptr, &dest);
            SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
            SDL_SetRenderScale(renderer, 3.0f, 3.0f);
            SDL_RenderDebugText(renderer, 100.0f, 250.0f, std::format("You evaded the hand for {} seconds.", ticks / 60).c_str());
            SDL_SetRenderScale(renderer, 1.0f, 1.0f);

            SDL_RenderPresent(renderer);
        } else {
            while (time_accumulator >= UPDATE_LOOP_TIME_MS) {
                if (time_accumulator > 250) {
                    time_accumulator = 250;
                }

                //start game logic
                if (!salamander.caught) {
                    salamander.moving_this_frame = false;
                    if (input.key_down(SDL_SCANCODE_UP) or input.key_down(SDL_SCANCODE_W)) {
                        salamander.velocity.x += static_cast<float>(std::cos(salamander.angle * (std::numbers::pi / 180.0)));
                        salamander.velocity.y += static_cast<float>(std::sin(salamander.angle * (std::numbers::pi / 180.0)));
                    }
                    if (input.key_down(SDL_SCANCODE_LEFT) or input.key_down(SDL_SCANCODE_A)) {
                        salamander.angle -= Salamander::ANGLE_STEP;
                        salamander.angle = std::fmod(salamander.angle, 360.0);
                        salamander.moving_this_frame = true;
                    } else if (input.key_down(SDL_SCANCODE_RIGHT) or input.key_down(SDL_SCANCODE_D)) {
                        salamander.angle += Salamander::ANGLE_STEP;
                        salamander.angle = std::fmod(salamander.angle, 360.0);
                        salamander.moving_this_frame = true;
                    }
                    salamander.velocity *= 0.95f;
                    if (salamander.velocity.magnitude_squared() >= 50.0f) {
                        salamander.moving_this_frame = true;
                    }
                    if (salamander.moving_this_frame) {
                        if (ticks % 6 == 0) {
                            salamander.frame += 1;
                        }
                    }
                    salamander.position += salamander.velocity;
                } else {
                    if (hand.z >= 50.0f and hand.grabbed_thing_pos == &salamander.position) {
                        on_game_over_screen = true;
                    }
                }

                camera.center_around(window, loam::Vec2 {
                    salamander.position.x + static_cast<float>(salamander.sprites.sprite_width) * SCALE / 2.0f,
                    salamander.position.y + static_cast<float>(salamander.sprites.sprite_height) * SCALE / 2.0f
                });

                if (hand.ticks_since_last_grab == hand.grab_interval) {
                    hand.frame = 0;
                    hand.position = salamander.position + loam::Vec2{
                        (loam::rand_bool() ? 1.0f : -1.0f) * loam::rand_float(400.0f, 800.0f),
                        (loam::rand_bool() ? 1.0f : -1.0f) * loam::rand_float(400.0f, 800.0f)};
                }
                if (!hand.on_ground and hand.ticks_since_last_grab > hand.grab_interval) {
                    if (not((salamander.position - hand.position).magnitude_squared() < 100.0f)) {
                        hand.position.x += loam::signof(salamander.position.x - hand.position.x) * SCALE * 2.0f;
                        hand.position.y += loam::signof(salamander.position.y - hand.position.y) * SCALE * 2.0f;
                    }
                    hand.z -= 1.0f;
                    if (hand.z <= 25.0f) {
                        hand.frame = 1;
                    }
                    if (hand.z <= 10.0f) {
                        hand.frame = 2;
                        hand.on_ground = true;
                    }
                }
                if (hand.on_ground) {
                    for (loam::Vec2& pos : rock_positions) {
                        if (loam::colliding(
                            SDL_FPoint(pos),
                            static_cast<float>(rock_spritesheet.sprite_width) * SCALE,
                            static_cast<float>(rock_spritesheet.sprite_height) * SCALE,
                            SDL_FPoint(salamander.position + loam::Vec2{16.0f, 16.0f}),
                            (static_cast<float>(salamander.sprites.sprite_width) - 16.0f) * SCALE,
                            (static_cast<float>(salamander.sprites.sprite_height) - 16.0f) * SCALE)
                            and
                            loam::colliding(
                            SDL_FPoint(pos),
                            static_cast<float>(rock_spritesheet.sprite_width) * SCALE,
                            static_cast<float>(rock_spritesheet.sprite_height) * SCALE,
                            SDL_FPoint(hand.position + loam::Vec2{16.0f, 16.0f}),
                            (static_cast<float>(hand.grab_sprites.sprite_width) - 16.0f) * SCALE,
                            (static_cast<float>(hand.grab_sprites.sprite_height) - 16.0f) * SCALE)

                        ) {
                            hand.frame = 3;
                            hand.hit_rock = true;
                            hand.grabbed_thing_pos = &pos;
                            goto grabbed_a_rock;
                        }
                    }
                    if (loam::colliding(
                        SDL_FPoint(salamander.position + loam::Vec2{16.0f, 16.0f}),
                        (static_cast<float>(salamander.sprites.sprite_width) - 16.0f) * SCALE,
                        (static_cast<float>(salamander.sprites.sprite_height) - 16.0f) * SCALE,
                        SDL_FPoint(hand.position + loam::Vec2{16.0f, 16.0f}),
                        (static_cast<float>(hand.grab_sprites.sprite_width) - 16.0f) * SCALE,
                        (static_cast<float>(hand.grab_sprites.sprite_height) - 16.0f) * SCALE)
                        and !hand.grabbed_thing_pos
                    ) {
                        hand.grabbed_thing_pos = &salamander.position;
                        salamander.caught = true;
                    }


                    grabbed_a_rock:
                    hand.z += 2.0f;
                    if (hand.grabbed_thing_pos) {
                        *hand.grabbed_thing_pos = loam::Vec2{hand.position.x, hand.position.y - hand.z * SCALE};
                    }
                    if (hand.z >= 100.0f) {
                        hand.grabbed_thing_pos = nullptr;
                        if (hand.grab_interval > 60) {
                            hand.grab_interval -= 20;
                        }
                        hand.on_ground = false;
                        hand.ticks_since_last_grab = 0;
                    }
                }

                if (ticks % 60 == 0) {
                    salamander.cooling_down = false;
                    Uint8 temperature_change = 1 + (ticks / 3600 < 3 ? ticks / 3600 : 3);
                    for (loam::Vec2 pos : rock_positions) {
                        if (loam::colliding(
                            SDL_FPoint(pos),
                            static_cast<float>(rock_spritesheet.sprite_width) * SCALE,
                            static_cast<float>(rock_spritesheet.sprite_height) * SCALE,
                            SDL_FPoint(salamander.position + loam::Vec2{16.0f, 16.0f}),
                            (static_cast<float>(salamander.sprites.sprite_width) - 16.0f) * SCALE,
                            (static_cast<float>(salamander.sprites.sprite_height) - 16.0f) * SCALE)
                        ) {
                            salamander.temperature -= temperature_change;
                            salamander.cooling_down = true;
                            goto cooled_down;
                        }
                    }
                    salamander.temperature += temperature_change;
                    cooled_down:

                    if (salamander.temperature == 0 or salamander.temperature >= 50) {
                        salamander.caught = true;
                        on_game_over_screen = true;
                    }
                }

                ++ticks;
                if (!hand.on_ground) {
                    ++hand.ticks_since_last_grab;
                }
                time_accumulator -= UPDATE_LOOP_TIME_MS;
                //end game logic
            }
            //start rendering code

            SDL_SetRenderDrawColor(renderer, 217, 160, 102, 255);
            SDL_RenderClear(renderer);

            for (size_t i = 0; i < ROCK_COUNT; ++i) {
                SDL_SetTextureAlphaModFloat(rock_shadow_spritesheet.texture, 0.8f);
                loam::Vec2 screen_pos = camera.world_to_screen(rock_positions[i]);
                rock_shadow_spritesheet.render(renderer, rock_sprites[i], {
                    .x = screen_pos.x,
                    .y = screen_pos.y + 2.0f * SCALE,
                    .w = SCALE * static_cast<float>(rock_shadow_spritesheet.sprite_width),
                    .h = SCALE * static_cast<float>(rock_shadow_spritesheet.sprite_height),
                });
            }

            loam::Vec2 salamander_screen_pos = camera.world_to_screen(salamander.position);
            salamander.sprites.render(renderer, salamander.frame, {
                .x = std::floor(salamander_screen_pos.x),
                .y = std::floor(salamander_screen_pos.y),
                .w = SCALE * static_cast<float>(salamander.sprites.sprite_width),
                .h = SCALE * static_cast<float>(salamander.sprites.sprite_height)
            }, false, false, salamander.angle);

            for (size_t i = 0; i < ROCK_COUNT; ++i) {
                loam::Vec2 screen_pos = camera.world_to_screen(rock_positions[i]);
                if (loam::colliding(
                    SDL_FPoint(screen_pos),
                    static_cast<float>(rock_spritesheet.sprite_width) * SCALE,
                    static_cast<float>(rock_spritesheet.sprite_height) * SCALE,
                    SDL_FPoint(salamander_screen_pos + loam::Vec2{16.0f, 16.0f}),
                    (static_cast<float>(salamander.sprites.sprite_width) - 16.0f) * SCALE,
                    (static_cast<float>(salamander.sprites.sprite_height) - 16.0f) * SCALE)
                ) {
                    constexpr float ALPHA_MOD = 0.75f;
                    SDL_SetTextureAlphaModFloat(rock_spritesheet.texture, ALPHA_MOD);
                    rock_spritesheet.render(renderer, rock_sprites[i], {
                        .x = screen_pos.x,
                        .y = screen_pos.y,
                        .w = SCALE * static_cast<float>(rock_spritesheet.sprite_width),
                        .h = SCALE * static_cast<float>(rock_spritesheet.sprite_height),
                    });
                    SDL_SetTextureAlphaModFloat(rock_spritesheet.texture, 1.0f);
                    salamander.cooling_down = true;
                    continue;
                }
                rock_spritesheet.render(renderer, rock_sprites[i], {
                    .x = screen_pos.x,
                    .y = screen_pos.y,
                    .w = SCALE * static_cast<float>(rock_spritesheet.sprite_width),
                    .h = SCALE * static_cast<float>(rock_spritesheet.sprite_height),
                });
            }

            loam::Vec2 real_hand_pos = camera.world_to_screen(hand.position);
            SDL_SetTextureAlphaModFloat(hand.grab_shadow_sprites.texture, 0.8f);
            hand.grab_shadow_sprites.render(renderer, hand.frame, {
                .x = real_hand_pos.x,
                .y = real_hand_pos.y,
                .w = static_cast<float>(hand.grab_shadow_sprites.sprite_width) * SCALE,
                .h = static_cast<float>(hand.grab_shadow_sprites.sprite_height) * SCALE
            });
            SDL_SetTextureAlphaModFloat(hand.grab_shadow_sprites.texture, 1.0f);
            hand.grab_sprites.render(renderer, hand.frame, {
                .x = real_hand_pos.x,
                .y = real_hand_pos.y - hand.z * SCALE,
                .w = static_cast<float>(hand.grab_sprites.sprite_width) * SCALE,
                .h = static_cast<float>(hand.grab_sprites.sprite_height) * SCALE
            });

            interface_spritesheet.stretch_render(renderer,
                {.x = 0.0f, .y = 0.0f, .w = 16.0f, .h = 32.0f},
                {.x = 1800.0f, .y = 800.0f, .w = 16.0f * SCALE, .h = 32.0f * SCALE}
            );
            //should range from -1.0f to 1.0f
            float normalized_temperature = (static_cast<float>(salamander.temperature) - 25.0f) / 25.0f;
            SDL_FRect heatbar = {.x = 1800.0f + 2.0f * SCALE, .y = 800.0f + 16.0f * SCALE, .w = 12.0f * SCALE, .h = -normalized_temperature * 16.0f * SCALE};
            if (normalized_temperature > 0.0f) {
                SDL_SetRenderDrawColor(renderer,
                    100 + static_cast<Uint8>(std::fabs(normalized_temperature) * 155),
                    0,
                    0,
                    255
                );
            } else {
                SDL_SetRenderDrawColor(renderer,
                    0,
                    0,
                    100 + static_cast<Uint8>(std::fabs(normalized_temperature) * 155),
                    255
                );
            }
            SDL_RenderFillRect(renderer, &heatbar);
            interface_spritesheet.stretch_render(renderer,
                {.x = 16.0f, .y = 0.0f, .w = 16.0f, .h = 32.0f},
                {.x = 1800.0f, .y = 800.0f, .w = 16.0f * SCALE, .h = 32.0f * SCALE}
            );
            if (salamander.cooling_down) {
                constexpr float ARROW_SPRITE_OFFSET = 8.0f;
                interface_spritesheet.stretch_render(renderer,
                    {.x = 32.0f, .y = 0.0f + ARROW_SPRITE_OFFSET, .w = 8.0f, .h = 8.0f}, {
                        .x = 1700.0f,
                        .y = 800.0f + std::ceil(std::sinf(static_cast<float>(ticks) / 10.0f)) * 2.0f * SCALE + ARROW_SPRITE_OFFSET * SCALE * 2.0f,
                        .w = 8.0f * SCALE,
                        .h = 8.0f * SCALE
                    }
                );
            } else {
                interface_spritesheet.stretch_render(renderer,
                    {.x = 32.0f, .y = 0.0f, .w = 8.0f, .h = 8.0f}, {
                        .x = 1700.0f,
                        .y = 800.0f + std::ceil(std::sinf(static_cast<float>(ticks) / 10.0f)) * 2.0f * SCALE,
                        .w = 8.0f * SCALE,
                        .h = 8.0f * SCALE
                    }
                );
            }
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_SetRenderScale(renderer, 4.0f, 3.0f);
            SDL_RenderDebugText(renderer, 450.0f, 10.0f, std::format("{}", ticks / 60).c_str());
            SDL_SetRenderScale(renderer, 1.0f, 1.0f);


#ifndef NDEBUG
            SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
            SDL_SetRenderScale(renderer, 2.0f, 2.0f);
            SDL_RenderDebugText(renderer, 0, 0, std::format("Salamander position: {}", salamander.position).c_str());
            SDL_RenderDebugText(renderer, 0, 10, std::format("Tick: {}", ticks).c_str());
            SDL_RenderDebugText(renderer, 0, 20, std::format("Salamander temperature: {}", salamander.temperature).c_str());
            SDL_SetRenderScale(renderer, 1.0f, 1.0f);
#endif


            SDL_RenderPresent(renderer);

            //end rendering logic
            //start delay logic

            Uint64 elapsed_time = SDL_GetTicks() - start_time;
            time_accumulator += elapsed_time;
        }
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
