#include "rl/include/raylib.h"
#include "rl/include/raymath.h"

#define PLAYER_SPEED 40
#define PLAYER_ROTATION_SPEED 0.1f
#define PLAYER_RADIUS 5
#define CREATURE_SPEED 40

#define SQUID_AMOUNT 10

#define GRID_SIZE 16
#define CELL_SIZE 20
#define HALF_SIZE (CELL_SIZE/2)
#define DEFAULT_HEIGHT (CELL_SIZE*0.5f)
#define DAMAGE_TIME 0.1f

#define FIRE_START_OFFSET (HALF_SIZE)

#define ANIMATION_RATE (0.1f)

#define TEXTURE_SIZE 16

#if DEBUG
#define ASSERT(condition) do { if (condition) TraceLog(LOG_ERROR, "[%s] you are a horrible person", #condition); exit(1); } while(false)
#else
#define ASSERT(condition) (void)0;
#endif

enum { EAST, NORTH, WEST, SOUTH };

typedef struct Cell { int x; int z; } Cell;

typedef struct Plane {
    Texture2D texture;
    Model model;
} Plane;

typedef enum PlaneType {
    PLANE_FLOOR,
    PLANE_WALL,
    PLANE_CEILING,
} PlaneType;

#define ANIMATION_AMOUNT 5
typedef enum CreatureState {
    CREATURE_ALIVE,
    CREATURE_DAMAGED,
    CREATURE_DEAD,
} CreatureState;
typedef struct Creature {
    Matrix transform;
    CreatureState state;
    float hit_radius;
    int health;
    Plane plane;
    Cell cell;
    Vector3 position;
    Vector3 target;
    int direction;
    float angle;
    int animation_index;
    Texture textures[ANIMATION_AMOUNT];
    Texture death_textures[ANIMATION_AMOUNT];
    float animation_timer;
    float damage_timer;
} Creature;

float random_float(float min, float max) {
    return min + (float)GetRandomValue(0, 10000) / 10000.0f * (max - min);
}

static Vector3 cell_position_to_world_position(int x, int z) {
    Vector3 result;
    result.x = (x * CELL_SIZE) + HALF_SIZE;
    result.y = DEFAULT_HEIGHT;
    result.z = (z * CELL_SIZE) + HALF_SIZE;
    return result;
}

static Cell world_position_to_cell_position(Vector3 position) {
    Cell result;
    result.x = position.x / CELL_SIZE;
    result.z = position.z / CELL_SIZE;
    return result;
}

// TODO: not hardcoded map size
bool is_wall(const char map[16][16], int x, int y) {
    if (x < 0) return true;
    if (x >= 16) return true;
    if (y < 0) return true;
    if (x >= 16) return true;
    return map[x][y] == '#';
}

void update_creature_direction(const char map[16][16], Creature *creature) {
    int available_direction_count = 0;
    int available_directions[4];
    if (!is_wall(map, creature->cell.x + 1, creature->cell.z)) available_directions[available_direction_count++] = EAST;
    if (!is_wall(map, creature->cell.x, creature->cell.z - 1)) available_directions[available_direction_count++] = NORTH;
    if (!is_wall(map, creature->cell.x - 1, creature->cell.z)) available_directions[available_direction_count++] = WEST;
    if (!is_wall(map, creature->cell.x, creature->cell.z + 1)) available_directions[available_direction_count++] = SOUTH;
    ASSERT(available_direction_count > 0);
    int opposite_direction = -1;
    switch (creature->direction) {
        default:
            // this would be the case in the beginning,
            // at that point there is no "opposite" direction
            opposite_direction = (-1);
            break;
        case EAST: opposite_direction = WEST; break;
        case NORTH: opposite_direction = SOUTH; break;
        case WEST: opposite_direction = EAST; break;
        case SOUTH: opposite_direction = NORTH; break;
    }
    int random_index = GetRandomValue(0, available_direction_count - 1);
    int new_direction = available_directions[random_index];
    if ((new_direction == opposite_direction) && (available_direction_count > 1)) {
        // do not turn around if there are other options
        random_index = (random_index + 1) % available_direction_count;
        new_direction = available_directions[random_index];
    }
    creature->direction = new_direction;
}

void update_creature_animation(Creature *creature) {
    creature->animation_timer += GetFrameTime();
    if (creature->animation_timer > ANIMATION_RATE) {
        creature->animation_timer -= ANIMATION_RATE;
        creature->animation_index = (creature->animation_index + 1) % ANIMATION_AMOUNT;
        creature->plane.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = creature->textures[creature->animation_index];
    }
}

void update_creature_movement(const char map[16][16], Creature *creature) {
    Vector3 delta = Vector3Subtract(creature->target, creature->position);
    float distSqr = Vector3LengthSqr(delta);

    if (distSqr < (0.01f * 0.01f))
    {
        creature->position = creature->target;

        switch (creature->direction)
        {
            case EAST:  creature->cell.x++; break;
            case NORTH: creature->cell.z--; break;
            case WEST:  creature->cell.x--; break;
            case SOUTH: creature->cell.z++; break;
            default: ASSERT(false);
        }

        update_creature_direction(map, creature);

        Cell next_target_cell = creature->cell;

        switch (creature->direction)
        {
            case EAST:  next_target_cell.x++; break;
            case NORTH: next_target_cell.z--; break;
            case WEST:  next_target_cell.x--; break;
            case SOUTH: next_target_cell.z++; break;
            default: ASSERT(false);
        }

        Vector3 target_base = cell_position_to_world_position(
            next_target_cell.x, next_target_cell.z
        );

        Vector3 target_offset = {
            random_float(-1.0f, 1.0f) * HALF_SIZE,
            0.0f,
            random_float(-1.0f, 1.0f) * HALF_SIZE
        };

        creature->target = Vector3Add(target_base, target_offset);
    }
    else
    {
        Vector3 move_dir = Vector3Normalize(delta);

        float step = CREATURE_SPEED * GetFrameTime();

        // CLAMP STEP so we never overshoot target
        if (step * step > distSqr) {
            creature->position = creature->target;
        } else {
            creature->position = Vector3Add(
                creature->position,
                Vector3Scale(move_dir, step)
            );
        }
    }
}

void update_creature(const char map[16][16], Creature *creature, Vector3 player) {
    float dt = GetFrameTime();

    switch (creature->state) {
        case CREATURE_ALIVE:
        {
            update_creature_movement(map, creature);
            update_creature_animation(creature);
            break;
        }
        case CREATURE_DAMAGED:
        {
            creature->damage_timer += GetFrameTime();
            if (creature->damage_timer >= DAMAGE_TIME) {
                creature->damage_timer = 0;
                creature->state = CREATURE_ALIVE;
            }
            update_creature_movement(map, creature);
            update_creature_animation(creature);
            break;
        }
        case CREATURE_DEAD:
        {
            if (creature->animation_index < (ANIMATION_AMOUNT - 1)) {
                creature->animation_timer += GetFrameTime();
                if (creature->animation_timer > ANIMATION_RATE) {
                    creature->animation_timer -= ANIMATION_RATE;
                    creature->animation_index = (creature->animation_index + 1) % ANIMATION_AMOUNT;
                    creature->plane.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = creature->death_textures[creature->animation_index];
                }
            }
            break;
        }
    }

    creature->transform = (Matrix)MatrixMultiply(
        MatrixRotateY(creature->angle),
        MatrixTranslate(
            creature->position.x,
            creature->position.y,
            creature->position.z
        )
    );

    Vector3 player_dir = {
        player.x - creature->position.x,
        0.0f,
        player.z - creature->position.z
    };
    creature->angle = atan2f(player_dir.x, player_dir.z) * RAD2DEG;
}

void render_creature(const Creature *creature, Vector3 player) {
    Vector3 axis = { 0.0f, 1.0f, 0.0f };
    DrawModelEx(
        creature->plane.model,
        creature->position,
        axis,
        creature->angle,
        (Vector3){ 1.0f, 1.0f, 1.0f },
        WHITE
    );
}

static Color noise(int x, int y, float r_tint, float g_tint, float b_tint) {
    int random = GetRandomValue(32,128);
    Color result;
    result.r = r_tint * random;
    result.g = g_tint * random;
    result.b = b_tint * random;
    result.a = 255;
    return result;
}

static Color dirt(int x, int y) {
    return noise(x, y, 1.0f, 0.5f, 0.0f);
}

static Color dirt_dark(int x, int y) {
    return noise(x, y, 0.5f, 0.25f, 0.0f);
}

static Plane alloc_plane(PlaneType plane_type, Color (*get_plane_color)(int x, int y)) {
    Image image = GenImageColor(TEXTURE_SIZE, TEXTURE_SIZE, WHITE);
    Color *pixels = (Color *)image.data;
    for (int x = 0; x < TEXTURE_SIZE; x++) {
        for (int y = 0; y < TEXTURE_SIZE; y++) {
            int i = y * TEXTURE_SIZE + x;
            pixels[i] = get_plane_color(x, y);
        }
    }
    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);
    Mesh mesh = GenMeshPlane(CELL_SIZE, CELL_SIZE, 1, 1);
    Model model = LoadModelFromMesh(mesh);
    switch (plane_type) {
        default:
            ASSERT(false);
            break;
        case PLANE_WALL:
            model.transform = MatrixRotateZ(PI / 2.0f);
            break;
        case PLANE_FLOOR:
            // matches default model transform
            break;
        case PLANE_CEILING:
            model.transform = MatrixRotateZ(PI);
            break;
    }
    model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = texture;
    return (Plane) {
        .model = model,
        .texture = texture,
    };
}

void render_plane(Plane plane, Vector3 position, int direction) {
    Vector3 axis = { 0.0f, 1.0f, 0.0f };
    float angle = 0.0f;

    switch (direction) {
        case EAST:  angle = 0.0f;   break;
        case SOUTH: angle = 90.0f;  break;
        case WEST:  angle = 180.0f; break;
        case NORTH: angle = 270.0f; break;
    }

    DrawModelEx(
        plane.model,
        position,
        axis,
        angle,
        (Vector3){ 1.0f, 1.0f, 1.0f },
        WHITE
    );
}

void free_plane(Plane plane) {
    UnloadModel(plane.model);
    UnloadTexture(plane.texture);
}

void draw_map(const char map[16][16], Vector3 player_world_position, const Creature squid[SQUID_AMOUNT]) {
    float size = 10;
    for (int x = 0; x < 16; x++) {
        for (int z = 0; z < 16; z++) {
            if (map[x][z] == '#') {
                DrawRectangle(x * size, z * size, size, size, GREEN);
            } else {
                DrawRectangle(x * size, z * size, size, size, BLACK);
            }
        }
    }

    float half_size = (size / 2);

    Cell player_position = world_position_to_cell_position(player_world_position);
    DrawCircle((player_position.x * size) + half_size, (player_position.z * size) + half_size, half_size, RED);

    for (int i = 0; i < SQUID_AMOUNT; i++) {
        Cell squid_position = world_position_to_cell_position(squid[i].position);
        DrawCircle((squid_position.x * size) + half_size, (squid_position.z * size) + half_size, half_size, GREEN);
    }
}

bool is_wall_at_position(const char map[16][16], Vector3 pos) {
    Cell c = world_position_to_cell_position(pos);
    return map[c.x][c.z] == '#';
}

bool collides(const char map[16][16], float x, float z) {
    return (
        // cardinal directions
        is_wall_at_position(map, (Vector3){ x + PLAYER_RADIUS, 0, z }) ||
        is_wall_at_position(map, (Vector3){ x - PLAYER_RADIUS, 0, z }) ||
        is_wall_at_position(map, (Vector3){ x, 0, z + PLAYER_RADIUS }) ||
        is_wall_at_position(map, (Vector3){ x, 0, z - PLAYER_RADIUS }) ||

        // diagonals
        is_wall_at_position(map, (Vector3){ x + PLAYER_RADIUS, 0, z + PLAYER_RADIUS }) ||
        is_wall_at_position(map, (Vector3){ x + PLAYER_RADIUS, 0, z - PLAYER_RADIUS }) ||
        is_wall_at_position(map, (Vector3){ x - PLAYER_RADIUS, 0, z + PLAYER_RADIUS }) ||
        is_wall_at_position(map, (Vector3){ x - PLAYER_RADIUS, 0, z - PLAYER_RADIUS })
    );
}

void play_sound_random_pitch(Sound sound) {
    float pitch = random_float(1.0f, 2.0f);
    SetSoundPitch(sound, pitch);
    PlaySound(sound);
}

void fire(Camera camera, Creature squid[SQUID_AMOUNT]) {
    Vector3 forward = Vector3Normalize(
        Vector3Subtract(camera.target, camera.position)
    );

    Ray ray = (Ray){
        .position = Vector3Add(camera.position, Vector3Scale(forward, FIRE_START_OFFSET)),
        .direction = forward
    };

    int closest_index = -1;
    float closest_distance = 99999999.9;

    for (int i = 0; i < SQUID_AMOUNT; i++) {
        Creature *s = &squid[i];

        if (s->state == CREATURE_DEAD) {
            continue;
        }

        RayCollision collision = GetRayCollisionSphere(ray, s->position, s->hit_radius);

        if (collision.distance < closest_distance) {
            closest_index = i;
            closest_distance = collision.distance;
        }
    }

    if (closest_index >= 0) {
        Creature *s = &squid[closest_index];
        s->health--;
        if (s->health <= 0) {
            s->state = CREATURE_DEAD;
            s->animation_index = 0;
        } else {
            s->state = CREATURE_DAMAGED;
        }
    }
}

int main(void) {
    const char map[16][16] = {
        "################",
        "#              #",
        "# ### ##### ## #",
        "# #      #   # #",
        "# # #### # #   #",
        "# #      #   # #",
        "# ### ##### ## #",
        "# ### ### s  # #",
        "# #   ### ## # #",
        "# # #     ## # #",
        "# #   ####   # #",
        "# ### #### ### #",
        "# ##   ##   ## #",
        "#      ##      #",
        "#@##   ##   ## #",
        "################"
    };

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);

    InitWindow(1600, 1200, "Who is this");

    InitAudioDevice();

    Plane wall = alloc_plane(PLANE_WALL, dirt);
    Plane floor = alloc_plane(PLANE_FLOOR, dirt_dark);
    Plane ceiling = alloc_plane(PLANE_CEILING, dirt);

    Camera camera = { 0 };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;
    float yaw = 0.0f;

    DisableCursor();

    SetTargetFPS(60);

    Sound bullet_sound = LoadSound("sounds/gun/bullet.wav");
    float gun_animation_timer = 0;
    int gun_texture_index = 0;
    Texture gun_idle = LoadTexture("textures/gun/SpeedGun0.png");
    Texture gun_fire[4];
    gun_fire[0] = LoadTexture("textures/gun/SpeedGun1.png");
    gun_fire[1] = LoadTexture("textures/gun/SpeedGun2.png");
    gun_fire[2] = LoadTexture("textures/gun/SpeedGun3.png");
    gun_fire[3] = LoadTexture("textures/gun/SpeedGun4.png");

    Shader shader = LoadShader(
        "alpha_cutout_vs.glsl",
        "alpha_cutout_fs.glsl"
    );

    int shader_location_damage_flash = GetShaderLocation(shader, "damageFlash");

    Creature squid[SQUID_AMOUNT] = {0};

    for (int i = 0; i < SQUID_AMOUNT; i++) {
        squid[i].health = 5;
        squid[i].hit_radius = HALF_SIZE/2;
        if (i == 0) {
            squid[i].textures[0] = LoadTexture("textures/squid/squid0.png");
            squid[i].textures[1] = LoadTexture("textures/squid/squid1.png");
            squid[i].textures[2] = LoadTexture("textures/squid/squid2.png");
            squid[i].textures[3] = LoadTexture("textures/squid/squid3.png");
            squid[i].textures[4] = LoadTexture("textures/squid/squid4.png");
            squid[i].death_textures[0] = LoadTexture("textures/squid/squid-death0.png");
            squid[i].death_textures[1] = LoadTexture("textures/squid/squid-death1.png");
            squid[i].death_textures[2] = LoadTexture("textures/squid/squid-death2.png");
            squid[i].death_textures[3] = LoadTexture("textures/squid/squid-death3.png");
            squid[i].death_textures[4] = LoadTexture("textures/squid/squid-death4.png");
        } else {
            for (int j = 0; j < ANIMATION_AMOUNT; j++) {
                squid[i].textures[j] = squid[0].textures[j];
                squid[i].death_textures[j] = squid[0].death_textures[j];
            }
        }
        {
            // TODO add initialization creature function
            Mesh mesh = GenMeshPlane(10, 10, 1, 1);
            Model model = LoadModelFromMesh(mesh);
            model.transform = MatrixRotateX(PI / 2.0f);
            model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = squid[i].textures[0];
            model.materials[0].shader = shader;
            squid[i].plane.model = model;
        }
        update_creature_direction(map, &squid[i]);
    }

    for (int x = 0; x < 16; x++) {
        for (int z = 0; z < 16; z++) {
            switch (map[x][z]) {
                case '@':
                    camera.position = cell_position_to_world_position(x, z);
                    camera.target = (Vector3){ 0.0f, camera.position.y, 0.0f };
                    break;
                case 's':
                    for (int i = 0; i < SQUID_AMOUNT; i++) {
                        squid[i].cell.x = x;
                        squid[i].cell.z = z;
                        squid[i].position = cell_position_to_world_position(x, z);
                        squid[i].target = squid[i].position;
                    }
                    break;
            }
        }
    }

    while (!WindowShouldClose())
    {
        {
            float frameMovement = PLAYER_SPEED * GetFrameTime();
            Vector2 mouse = GetMouseDelta();
            yaw += mouse.x * PLAYER_ROTATION_SPEED;
            Vector3 forward = { cosf(yaw * DEG2RAD), 0.0f, sinf(yaw * DEG2RAD) };
            Vector3 up = { 0.0f, 1.0f, 0.0f };
            Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, up));

            Vector3 move = { 0 };

            if (IsKeyDown(KEY_W)) move = Vector3Add(move, forward);
            if (IsKeyDown(KEY_S)) move = Vector3Subtract(move, forward);
            if (IsKeyDown(KEY_D)) move = Vector3Add(move, right);
            if (IsKeyDown(KEY_A)) move = Vector3Subtract(move, right);

            if (Vector3Length(move) > 0.0f) move = Vector3Scale(Vector3Normalize(move), frameMovement);

            Vector3 old = camera.position;
            Vector3 newPos = old;

            newPos.x += move.x;
            newPos.z += move.z;

            if (!collides(map, newPos.x, old.z)) old.x = newPos.x;
            if (!collides(map, old.x, newPos.z)) old.z = newPos.z;

            camera.position = old;

            camera.target = Vector3Add(camera.position, forward);
        }

        for (int i = 0; i < SQUID_AMOUNT; i++) {
            update_creature(map, &squid[i], camera.position);
        }

        bool is_firing = IsMouseButtonDown(0);

        {
            if (IsMouseButtonPressed(0)) {
                gun_animation_timer = 0;
                gun_texture_index = 0;
                fire(camera, squid);
                play_sound_random_pitch(bullet_sound);
            } else if (is_firing) {
                gun_animation_timer += GetFrameTime();
                if (gun_animation_timer > 0.1f) {
                    gun_animation_timer -= 0.1f;
                    gun_texture_index = (gun_texture_index + 1) % 4;
                    fire(camera, squid);
                    play_sound_random_pitch(bullet_sound);
                }
            }
        }

        BeginDrawing();

        ClearBackground(BLACK);

        BeginMode3D(camera);

        for (int x = 0; x < 16; x++) {
            for (int z = 0; z < 16; z++) {
                if (map[x][z] == '#') {
                    continue;
                }

                Vector3 wp = cell_position_to_world_position(x, z);

                render_plane(floor, (Vector3){wp.x, 0.0f, wp.z}, EAST);
                render_plane(ceiling, (Vector3){wp.x, (1.0f*CELL_SIZE), wp.z}, EAST);

                if (is_wall(map, x + 1, z)) render_plane(wall, (Vector3){wp.x + HALF_SIZE, DEFAULT_HEIGHT, wp.z}, EAST);
                if (is_wall(map, x, z + 1)) render_plane(wall, (Vector3){wp.x, DEFAULT_HEIGHT, wp.z + HALF_SIZE}, NORTH);
                if (is_wall(map, x - 1, z)) render_plane(wall, (Vector3){wp.x - HALF_SIZE, DEFAULT_HEIGHT, wp.z}, WEST);
                if (is_wall(map, x, z - 1)) render_plane(wall, (Vector3){wp.x, DEFAULT_HEIGHT, wp.z - HALF_SIZE}, SOUTH);
            }
        }

        for (int i = 0; i < SQUID_AMOUNT; i++) {
            BeginShaderMode(shader);
            float value = 0;
            switch (squid[i].state) {
                default:
                {
                    value = 0.0f;
                    break;
                }
                case CREATURE_DAMAGED:
                {
                    value = (DAMAGE_TIME - squid[i].damage_timer) / DAMAGE_TIME;
                    break;
                }
            };
            SetShaderValue(shader, shader_location_damage_flash, &value, SHADER_UNIFORM_FLOAT);
            render_creature(&squid[i], camera.position);
            EndShaderMode();
        }

        EndMode3D();

        draw_map(map, camera.position, squid);

        {
            float scale = 8.0f;
            Vector2 position = {
                (GetScreenWidth() * .5f) - ((gun_idle.width * scale) / 2),
                GetScreenHeight() - (gun_idle.height * scale),
            };
            if (is_firing) {
                DrawTextureEx(gun_fire[gun_texture_index], position, 0, scale, WHITE);
            } else {
                DrawTextureEx(gun_idle, position, 0, scale, WHITE);
            }
        }

        {
            int center_x = GetScreenWidth() / 2;
            int center_y = GetScreenHeight() / 2;
            int length = 20;
            int thick = 3;

            Vector2 right = { center_x - length, center_y };
            Vector2 left = { center_x + length, center_y };

            Vector2 up = { center_x, center_y - length };
            Vector2 down = { center_x, center_y + length };

            DrawLineEx(right, left, thick, RED);
            DrawLineEx(up, down, thick, RED);
        }

        EndDrawing();
    }

    CloseWindow();

    CloseAudioDevice();

    return 0;
}

