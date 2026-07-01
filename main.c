#include <stdlib.h>
#include <stdint.h>

#include "rl/include/raylib.h"
#include "rl/include/raymath.h"

#define DEBUG 1
#define DRAW_MAP 1

#define BIG_FLOAT 1e30f

#define PLAYER_SPEED 100
#define PLAYER_ROTATION_SPEED 0.1f
#define PLAYER_RADIUS 5
#define CREATURE_SPEED 100
#define CHASE_TRY_MAX 10

#define KEY_ARROW_ROTATION_SENSITIVITY 200.0f

#define SQUID_CAPACITY 200

#define CELL_SIZE 20
#define HALF_SIZE (CELL_SIZE/2)
#define DEFAULT_HEIGHT (CELL_SIZE*0.5f)
#define DAMAGE_TIME 0.1f

#define ROOM_SIZE 5
#define ROOMS_PER_MAP_SIDE 8
#define ROOM_PADDING_SIZE 1

#define MAP_SIDE (ROOMS_PER_MAP_SIDE * ROOM_SIZE) + ((ROOMS_PER_MAP_SIDE + 1) * ROOM_PADDING_SIZE)
typedef uint32_t Flags;
typedef Flags Map[MAP_SIDE][MAP_SIDE];
#define FLAG_NONE (0)
#define FLAG_WALL (1 << 0)
#define FLAG_PLAYER_START (1 << 1)
#define FLAG_SQUID_SPAWNER (1 << 2)
#define FLAG_ROOM_CONNECTOR (1 << 3)
#define FLAG_PORTAL_WALL (1 << 0)
#define IS_VALID_CELL(cell) ((cell.x>=0) && (cell.x<MAP_SIDE) && (cell.z>=0) && (cell.z<MAP_SIDE))

#define FIRE_RATE (0.07f)
#define FIRE_OFFSET (0)

#define ANIMATION_RATE (0.1f)

#define TEXTURE_SIZE 16

#if DEBUG
#define ASSERT(condition) do { if (!(condition)) { TraceLog(LOG_ERROR, "%s:%i(%s) you are a horrible person", __FUNCTION__, __LINE__, #condition); exit(1); } } while(false)
#else
#define ASSERT(condition) (void)0;
#endif

enum { EAST, NORTH, WEST, SOUTH };
static const char *direction_to_string(int direction) {
    switch (direction) {
        case EAST: return "EAST";
        case NORTH: return "NORTH";
        case WEST: return "WEST";
        case SOUTH: return "SOUTH";
    }
    return "UNKNOWN";
};

enum {
    ROOM_UNSET,
    ROOM_1X1,
    ROOM_3X3,
    ROOM_3X3_PILLAR,
    ROOM_5X5,
    ROOM_5X5_PILLAR,
    ROOM_5X5_PILLAR_4,
    ROOM_TYPE_COUNT,
};

typedef struct XZ { int x; int z; } XZ;
typedef struct Room {
    int x;
    int z;
    int type;
    int size;
} Room;

static bool is_cell_out_of_bounds(int x, int z) {
    return (x<0) || (x>=MAP_SIDE) || (z<0) || (z>=MAP_SIDE);
}

static bool is_room_out_of_bounds(XZ room) {
    return (room.x<0) || (room.x>=ROOMS_PER_MAP_SIDE) || (room.z<0) || (room.z>=ROOMS_PER_MAP_SIDE);
}

static bool has_flag(Flags flags, Flags flag) {
    return (flags & flag) == flag;
}

static bool has_map_flag(Map map, int x, int z, Flags flag) {
    if (is_cell_out_of_bounds(x, z)) {
        return flag == FLAG_NONE;
    }
    return (map[z][x] & flag) == flag;
}

static Room create_room(XZ location, int type) {
    int size = -1;
    switch (type) {
        default:
        case ROOM_UNSET:
            ASSERT(false);
            break;

        case ROOM_1X1:
            size = 1;
            break;

        case ROOM_3X3:
        case ROOM_3X3_PILLAR:
            size = 3;
            break;

        case ROOM_5X5:
        case ROOM_5X5_PILLAR:
        case ROOM_5X5_PILLAR_4:
            size = 5;
            break;
    }

    Room room;
    room.x = location.x;
    room.z = location.z;
    room.type = type;
    room.size = size;

    return room;
}

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
    CREATURE_ANGRY,
    CREATURE_DEAD,
} CreatureState;
typedef struct Creature {
    Matrix transform;
    CreatureState state;
    float hit_radius;
    int health;
    Plane plane;
    XZ cell;
    XZ chase_cell;
    int chase_tries;
    Vector3 position;
    float height;
    Vector3 target;
    int direction;
    float angle;
    int animation_index;
    Texture textures[ANIMATION_AMOUNT];
    Texture death_textures[ANIMATION_AMOUNT];
    float animation_timer;
    float damage_timer;
} Creature;

typedef enum CellType {
    CELL_TYPE_NONE,
    CELL_TYPE_FLOOR,
    CELL_TYPE_WALL,
    CELL_TYPE_PORTAL_WALL,
} CellType;

typedef struct State {
    Camera camera;
    float yaw;
    Plane wall;
    Plane portal_wall;
    Plane floor;
    Plane ceiling;
    Flags map[MAP_SIDE][MAP_SIDE];
    Room rooms[ROOMS_PER_MAP_SIDE][ROOMS_PER_MAP_SIDE];
    Creature squid[SQUID_CAPACITY];
    XZ squid_spawners[8];
    int squid_count;
    int squid_spawner_count;
    float gun_animation_timer;
    int gun_texture_index;
    Sound small_slimes[8];
    Sound bullet_sound;
    Shader shader;
    int shader_location_damage_flash;
    Texture gun_idle;
    Texture gun_fire[4];
    bool is_firing;
    XZ player_cell;
} State;

static Vector3 cell_position_to_world_position(int x, int z, float height) {
    Vector3 result;
    result.x = (x * CELL_SIZE) + HALF_SIZE;
    result.y = height;
    result.z = (z * CELL_SIZE) + HALF_SIZE;
    return result;
}

static XZ world_position_to_cell_position(Vector3 position) {
    XZ result;
    result.x = position.x / CELL_SIZE;
    result.z = position.z / CELL_SIZE;
    return result;
}

static XZ debug_cell_1 = {-1};
static XZ debug_cell_2 = {-1};
static XZ debug_cell_3 = {-1};
static XZ debug_cell_4 = {-1};
static void draw_debug_cell(int index, float cell_size) {
    XZ cell = {0};
    Color color = {0};
    switch (index) {
        default:
            TraceLog(LOG_ERROR, "invalid debug cell index %i", index);
            return;
        case 1:
            cell = debug_cell_1;
            color = (Color){255,0,0,255};
            break;
        case 2:
            cell = debug_cell_2;
            color = (Color){0,255,0,255};
            break;
        case 3:
            cell = debug_cell_3;
            color = (Color){0,0,255,255};
            break;
        case 4:
            cell = debug_cell_4;
            color = (Color){255,255,255,255};
            break;
    }

    float half_size = (cell_size / 2);
    DrawRectangle(cell.x * cell_size, cell.z * cell_size, cell_size, cell_size, BLACK);
    DrawCircle((cell.x * cell_size) + half_size, (cell.z * cell_size) + half_size, half_size, color);
}

static void get_surroundings(int x, int z, XZ outputSurroundings[4]) {
    outputSurroundings[0] = (XZ){ x + 1, z };
    outputSurroundings[1] = (XZ){ x, z - 1 };
    outputSurroundings[2] = (XZ){ x - 1, z };
    outputSurroundings[3] = (XZ){ x, z + 1 };
}

static bool bresenham(State *state, XZ start, XZ end) {
    int dx =  abs(end.x - start.x), sx = start.x < end.x ? 1 : -1;
    int dz = -abs(end.z - start.z), sz = start.z < end.z ? 1 : -1;
    int err = dx + dz, e2;
    while (true) {
        XZ ray_cell = { start.x, start.z };
        // TODO: check out of bounds
        if (has_flag(state->map[ray_cell.z][ray_cell.x], FLAG_WALL)) {
            return false;
        }
        if (start.x == end.x && start.z == end.z) {
            return true;
        }
        e2 = 2*err;
        if (e2 >= dz) { err += dz; start.x += sx; }
        if (e2 <= dx) { err += dx; start.z += sz; }
    }
}

static bool dda(const Map map, Vector3 start_world, Vector3 end_world) {
    // convert world to grid (float space)
    float start_x = start_world.x / CELL_SIZE;
    float start_z = start_world.z / CELL_SIZE;
    float end_x = end_world.x / CELL_SIZE;
    float end_z = end_world.z / CELL_SIZE;

    // direction in grid space
    float dir_x = end_x - start_x;
    float dir_z = end_z - start_z;

    // current cell
    int map_x = (int)start_x;
    int map_z = (int)start_z;

    int end_cell_x = (int)end_x;
    int end_cell_z = (int)end_z;

    // step direction
    int step_x = (dir_x < 0) ? -1 : 1;
    int step_z = (dir_z < 0) ? -1 : 1;

    // avoid division by zero
    float delta_x = (dir_x == 0.0f) ? 1e30f : fabsf(CELL_SIZE / dir_x);
    float delta_z = (dir_z == 0.0f) ? 1e30f : fabsf(CELL_SIZE / dir_z);

    float side_x;
    float side_z;

    // initial distance to first grid boundary
    if (dir_x < 0) {
        side_x = (start_x - map_x) * delta_x;
    } else {
        side_x = (map_x + 1.0f - start_x) * delta_x;
    }

    if (dir_z < 0) {
        side_z = (start_z - map_z) * delta_z;
    } else {
        side_z = (map_z + 1.0f - start_z) * delta_z;
    }

    while (true) {
        if (side_x < side_z) {
            side_x += delta_x;
            map_x += step_x;
        } else {
            side_z += delta_z;
            map_z += step_z;
        }

        if (map_x < 0 || map_x >= MAP_SIDE || map_z < 0 || map_z >= MAP_SIDE) {
            return false;
        }

        if (has_flag(map[map_z][map_x], FLAG_WALL)) {
            return false;
        }

        if (map_x == end_cell_x && map_z == end_cell_z) {
            return true;
        }
    }
}

static float random_float(float min, float max) {
    return min + (float)GetRandomValue(0, 10000) / 10000.0f * (max - min);
}

static bool cell_eq(XZ a, XZ b) {
    return (a.x == b.x) && (a.z == b.z);
}

static int get_direction_from_to(XZ from, XZ to) {
    if (cell_eq(from, to)) {
        return -1;
    }
    int diff_x = to.x - from.x;
    int diff_z = to.z - from.z;
    if (abs(diff_x) > abs(diff_z)) {
        return diff_x > 0 ? EAST : WEST;
    } else {
        return diff_z > 0 ? SOUTH : NORTH;
    }
}

static void update_creature_direction(State *state, Creature *creature, Vector3 player_position, XZ player_cell) {
    int available_direction_count = 0;
    int available_directions[4];
    if (!has_map_flag(state->map, creature->cell.x + 1, creature->cell.z, FLAG_WALL)) available_directions[available_direction_count++] = EAST;
    if (!has_map_flag(state->map, creature->cell.x, creature->cell.z - 1, FLAG_WALL)) available_directions[available_direction_count++] = NORTH;
    if (!has_map_flag(state->map, creature->cell.x - 1, creature->cell.z, FLAG_WALL)) available_directions[available_direction_count++] = WEST;
    if (!has_map_flag(state->map, creature->cell.x, creature->cell.z + 1, FLAG_WALL)) available_directions[available_direction_count++] = SOUTH;
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

    if (dda(state->map, creature->position, player_position)) {
        creature->chase_tries = CHASE_TRY_MAX;
        creature->chase_cell = player_cell;
    }

    if (creature->chase_tries > 0) {
        int new_direction = get_direction_from_to(creature->cell, creature->chase_cell);
        for (int i = 0; i < available_direction_count; i++) {
            if (new_direction == available_directions[i]) {
                creature->direction = new_direction;
                return;
            }
        }
        // if direction is not available fall back to random available direction
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

static void update_creature_animation(Creature *creature) {
    creature->animation_timer += GetFrameTime();
    if (creature->animation_timer > ANIMATION_RATE) {
        creature->animation_timer -= ANIMATION_RATE;
        creature->animation_index = (creature->animation_index + 1) % ANIMATION_AMOUNT;
        creature->plane.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = creature->textures[creature->animation_index];
    }
}

static void update_creature_movement(State *state, Creature *creature, Vector3 player_position, XZ player_cell) {
    Vector3 delta = Vector3Subtract(creature->target, creature->position);
    float distSqr = Vector3LengthSqr(delta);

    if (distSqr < (0.01f * 0.01f)) {
        creature->position = creature->target;

        switch (creature->direction)
        {
            case EAST:  creature->cell.x++; break;
            case NORTH: creature->cell.z--; break;
            case WEST:  creature->cell.x--; break;
            case SOUTH: creature->cell.z++; break;
            default: ASSERT(false);
        }

        if (cell_eq(creature->cell, creature->chase_cell)) {
            creature->chase_tries = 0;
        }

        update_creature_direction(state, creature, player_position, player_cell);

        XZ next_target_cell = creature->cell;

        switch (creature->direction)
        {
            case EAST:  next_target_cell.x++; break;
            case NORTH: next_target_cell.z--; break;
            case WEST:  next_target_cell.x--; break;
            case SOUTH: next_target_cell.z++; break;
            default: ASSERT(false);
        }

        Vector3 target_base = cell_position_to_world_position(
            next_target_cell.x,
            next_target_cell.z,
            creature->height
        );

        float offset_max = HALF_SIZE / 2;

        Vector3 target_offset = {
            random_float(-1.0f, 1.0f) * offset_max,
            0.0f,
            random_float(-1.0f, 1.0f) * offset_max
        };

        creature->target = Vector3Add(target_base, target_offset);
    } else {
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

static void damage_creature(Creature *creature, const Sound small_slimes[8]) {
    creature->health--;
    if (creature->health <= 0) {
        creature->state = CREATURE_DEAD;
        creature->animation_index = 0;
        creature->damage_timer = 0;
        PlaySound(small_slimes[GetRandomValue(0, 7)]);
        return;
    }

    creature->damage_timer = DAMAGE_TIME;
}

static void update_creature(State *state, Creature *creature, Vector3 player_position, XZ player_cell) {
    switch (creature->state) {
        default: ASSERT(false); break;
        case CREATURE_ALIVE:
        {
            update_creature_movement(state, creature, player_position, player_cell);
            update_creature_animation(creature);

            creature->damage_timer -= GetFrameTime();
            if (creature->damage_timer < 0) {
                creature->damage_timer = 0;
            }

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
        player_position.x - creature->position.x,
        0.0f,
        player_position.z - creature->position.z
    };
    creature->angle = atan2f(player_dir.x, player_dir.z) * RAD2DEG;
}

static void render_creature(const Creature *creature, Vector3 player) {
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

static Color wo(int x, int y) {
    if (x == 0 || x == (TEXTURE_SIZE - 1) || y == 0 || y == (TEXTURE_SIZE - 1)) {
        return (Color){64,32,0,255};
    }
    return (Color){128,64,0,255};
}

static Color chess(int x, int y) {
    bool a = (int)floor(x / 4) % 2 == 0;
    bool b = (int)floor(y / 4) % 2 == 0;
    if ((a && !b) || (b && !a)) {
        return (Color){64,64,64,255};
    }
    return (Color){192,192,192,255};
}

static Color portal(int x, int y) {
    int center = 8;
    int x_distance = fabsf(center - x);
    int y_distance = fabsf(center - y);
    int lead = (x_distance > y_distance) ? x_distance : y_distance;
    int multiplier = 255 / 16;
    Color result;
    result.r = 0;
    result.g = 255 - (lead * multiplier);
    result.b = 0;
    result.a = 255;
    return result;
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

static void render_plane(Plane plane, Vector3 position, int direction) {
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

static void maybe_render_wall(State *state, int x, int z, int direction, Vector3 world_position) {
    Plane plane;
    Flags flags = state->map[z][x];
    if (has_flag(flags, FLAG_WALL)) {
        plane = state->wall;
    } else if (has_flag(flags, FLAG_PORTAL_WALL)) {
        plane = state->portal_wall;
    } else {
        return;
    }
    render_plane(plane, (Vector3){
        world_position.x,
        DEFAULT_HEIGHT,
        world_position.z
    }, direction);
}

static void draw_map(const Map map, Vector3 player_world_position, const Creature squid[SQUID_CAPACITY]) {
    float size = 10;
    for (int z = 0; z < MAP_SIDE; z++) {
        for (int x = 0; x < MAP_SIDE; x++) {
            Flags flags = map[z][x];
            Color color = BLACK;
            if (has_flag(flags, FLAG_WALL)) {
                color.g = 255;
            } else if (has_flag(flags, FLAG_ROOM_CONNECTOR)) {
                color.r = 255;
            }
            DrawRectangle(x * size, z * size, size, size, color);
        }
    }

    float half_size = (size / 2);

    XZ player_position = world_position_to_cell_position(player_world_position);
    DrawCircle((player_position.x * size) + half_size, (player_position.z * size) + half_size, half_size, PURPLE);

    for (int i = 0; i < SQUID_CAPACITY; i++) {
        XZ squid_position = world_position_to_cell_position(squid[i].position);
        DrawCircle((squid_position.x * size) + half_size, (squid_position.z * size) + half_size, half_size, BLUE);
    }

    for (int i = 1; i < 5; i++) {
        draw_debug_cell(i, size);
    }
}

static bool is_wall_at_position(const Map map, Vector3 pos) {
    XZ c = world_position_to_cell_position(pos);
    return has_flag(map[c.z][c.x], FLAG_WALL);
}

static bool collides(const Map map, float x, float z) {
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

static void play_sound_random_pitch(Sound sound) {
    float pitch = random_float(1.0f, 2.0f);
    SetSoundPitch(sound, pitch);
    PlaySound(sound);
}

static void fire(Camera camera, Creature squid[SQUID_CAPACITY], const Sound small_slimes[8]) {
    Vector3 forward = Vector3Normalize(
        Vector3Subtract(camera.target, camera.position)
    );

    Ray ray = (Ray){
        .position = Vector3Add(camera.position, Vector3Scale(forward, FIRE_OFFSET)),
        .direction =  forward
    };

    int closest_index = -1;
    float closest_distance = 99999999.9;

    for (int i = 0; i < SQUID_CAPACITY; i++) {
        Creature *s = &squid[i];

        if (s->state == CREATURE_DEAD) {
            continue;
        }

        RayCollision collision = GetRayCollisionSphere(ray, s->position, s->hit_radius);

        if (!collision.hit) {
            continue;
        }

        if (collision.distance < 0.0f) {
            // backwards direction of the ray
            continue;
        }

        if (collision.distance < closest_distance) {
            closest_index = i;
            closest_distance = collision.distance;
        }
    }

    if (closest_index >= 0) {
        Creature *s = &squid[closest_index];
        damage_creature(s, small_slimes);
    }
}

static void spawn_squid(State *state, Creature *squid, Vector3 player_position, XZ player_cell) {
    squid->cell = state->squid_spawners[GetRandomValue(0, state->squid_spawner_count - 1)];
    squid->position = cell_position_to_world_position(squid->cell.x, squid->cell.z, squid->height);
    squid->target = squid->position;
    squid->health = 5;
    squid->state = CREATURE_ALIVE;
    squid->chase_tries = 0;
    update_creature_direction(state, squid, player_position, player_cell);
    // TODO: update target, now it is weird
}

static void main_update(State *state) {
    state->player_cell = world_position_to_cell_position(state->camera.position);

    {
        float frameMovement = PLAYER_SPEED * GetFrameTime();
        Vector2 mouse = GetMouseDelta();
        state->yaw += mouse.x * PLAYER_ROTATION_SPEED;
        if (IsKeyDown(KEY_RIGHT)) {
            state->yaw += KEY_ARROW_ROTATION_SENSITIVITY * GetFrameTime();
        } else if (IsKeyDown(KEY_LEFT)) {
            state->yaw -= KEY_ARROW_ROTATION_SENSITIVITY * GetFrameTime();
        }
        Vector3 forward = { cosf(state->yaw * DEG2RAD), 0.0f, sinf(state->yaw * DEG2RAD) };
        Vector3 up = { 0.0f, 1.0f, 0.0f };
        Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, up));

        Vector3 move = { 0 };

        if (IsKeyDown(KEY_W)) move = Vector3Add(move, forward);
        if (IsKeyDown(KEY_S)) move = Vector3Subtract(move, forward);
        if (IsKeyDown(KEY_D)) move = Vector3Add(move, right);
        if (IsKeyDown(KEY_A)) move = Vector3Subtract(move, right);

        if (Vector3Length(move) > 0.0f) move = Vector3Scale(Vector3Normalize(move), frameMovement);

        Vector3 old = state->camera.position;
        Vector3 newPos = old;

        newPos.x += move.x;
        newPos.z += move.z;

        if (!collides(state->map, newPos.x, old.z)) old.x = newPos.x;
        if (!collides(state->map, old.x, newPos.z)) old.z = newPos.z;

        state->camera.position = old;

        state->camera.target = Vector3Add(state->camera.position, forward);
    }

    for (int i = 0; i < SQUID_CAPACITY; i++) {
        update_creature(state, &state->squid[i], state->camera.position, state->player_cell);
    }

    {
        state->is_firing = IsMouseButtonDown(0) || IsKeyDown(KEY_UP);
        bool is_initial_fire_frame = IsMouseButtonPressed(0) || IsKeyPressed(KEY_UP);

        if (is_initial_fire_frame) {
            state->gun_animation_timer = 0;
            state->gun_texture_index = 0;
            fire(state->camera, state->squid, state->small_slimes);
            play_sound_random_pitch(state->bullet_sound);
        } else if (state->is_firing) {
            state->gun_animation_timer += GetFrameTime();
            if (state->gun_animation_timer > FIRE_RATE) {
                state->gun_animation_timer -= FIRE_RATE;
                state->gun_texture_index = (state->gun_texture_index + 1) % 4;
                fire(state->camera, state->squid, state->small_slimes);
                play_sound_random_pitch(state->bullet_sound);
            }
        }
    }

    bool all_dead = true;
    for (int i = 0; i < SQUID_CAPACITY; i++) {
        if (state->squid[i].state != CREATURE_DEAD) {
            all_dead = false;
            break;
        }
    }
    if (all_dead || IsKeyPressed(KEY_O)) {
        for (int i = 0; i < state->squid_count; i++) {
            spawn_squid(state, &state->squid[i], state->camera.position, state->player_cell);
        }
        state->squid_count += 10;
        if (state->squid_count > SQUID_CAPACITY) {
            state->squid_count = SQUID_CAPACITY;
        }
    }
}

static void main_render(State *state) {
    BeginDrawing();

    ClearBackground(BLACK);

    BeginMode3D(state->camera);

    for (int z = 0; z < MAP_SIDE; z++) {
        for (int x = 0; x < MAP_SIDE; x++) {
            Vector3 wp = cell_position_to_world_position(x, z, DEFAULT_HEIGHT);

            Flags flags = state->map[z][x];
            if ((flags & (FLAG_WALL|FLAG_SQUID_SPAWNER)) != FLAG_NONE) {
                continue;
            }
            render_plane(state->floor, (Vector3){wp.x, 0.0f, wp.z}, EAST);
            render_plane(state->ceiling, (Vector3){wp.x, (1.0f*CELL_SIZE), wp.z}, EAST);

            maybe_render_wall(state, x + 1, z, EAST, (Vector3){wp.x + HALF_SIZE, DEFAULT_HEIGHT, wp.z});
            maybe_render_wall(state, x, z + 1, NORTH, (Vector3){wp.x, DEFAULT_HEIGHT, wp.z + HALF_SIZE});
            maybe_render_wall(state, x - 1, z, WEST, (Vector3){wp.x - HALF_SIZE, DEFAULT_HEIGHT, wp.z});
            maybe_render_wall(state, x, z - 1, SOUTH, (Vector3){wp.x, DEFAULT_HEIGHT, wp.z - HALF_SIZE});
        }
    }

    for (int i = 0; i < SQUID_CAPACITY; i++) {
        BeginShaderMode(state->shader);
        float damage_flash = (state->squid[i].damage_timer) / DAMAGE_TIME;
        SetShaderValue(state->shader, state->shader_location_damage_flash, &damage_flash, SHADER_UNIFORM_FLOAT);
        render_creature(&state->squid[i], state->camera.position);
        EndShaderMode();
    }

    EndMode3D();

    {
        float scale = 8.0f;
        Vector2 position = {
            (GetScreenWidth() * .5f) - ((state->gun_idle.width * scale) / 2),
            GetScreenHeight() - (state->gun_idle.height * scale),
        };
        if (state->is_firing) {
            DrawTextureEx(state->gun_fire[state->gun_texture_index], position, 0, scale, WHITE);
        } else {
            DrawTextureEx(state->gun_idle, position, 0, scale, WHITE);
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

#if DRAW_MAP
    draw_map(state->map, state->camera.position, state->squid);
#endif

    EndDrawing();
}

bool generate_room_1x1(int x, int z) { return (x==(ROOM_SIZE/2) && z==(ROOM_SIZE/2)); }
bool generate_room_3x3(int x, int z) { return (x>0) && (x<(ROOM_SIZE-1)) && (z>0) && (z<(ROOM_SIZE-1)); }
bool generate_room_3x3_pillar(int x, int z) { return generate_room_3x3(x, z) && !generate_room_1x1(x, z); }
bool generate_room_5x5(int x, int z) { return true; }
bool generate_room_5x5_pillar(int x, int z) { return !generate_room_1x1(x, z); }
bool generate_room_5x5_pillar_4(int x, int z) { return !(((x==1) || (x==3)) && ((z==1) || (z==3))); }

static void add_room(State *state, Room room) {
    ASSERT(state->rooms[room.z][room.x].type == ROOM_UNSET);
    state->rooms[room.z][room.x] = room;

    XZ room_start;
    room_start.x = ROOM_PADDING_SIZE + (room.x * ROOM_PADDING_SIZE) + (room.x * ROOM_SIZE);
    room_start.z = ROOM_PADDING_SIZE + (room.z * ROOM_PADDING_SIZE) + (room.z * ROOM_SIZE);

    for (int z = 0; z < ROOM_SIZE; z++) {
        for (int x = 0; x < ROOM_SIZE; x++) {
            bool floor;
            switch (room.type) {
                default:
                case ROOM_UNSET:
                    ASSERT(false);
                    break;
                case ROOM_1X1: floor = generate_room_1x1(x, z); break;
                case ROOM_3X3: floor = generate_room_3x3(x, z); break;
                case ROOM_3X3_PILLAR: floor = generate_room_3x3_pillar(x, z); break;
                case ROOM_5X5: floor = generate_room_5x5(x, z); break;
                case ROOM_5X5_PILLAR: floor = generate_room_5x5_pillar(x, z); break;
                case ROOM_5X5_PILLAR_4: floor = generate_room_5x5_pillar_4(x, z); break;
            }
            if (!floor) {
                continue;
            }
            state->map[room_start.z + z][room_start.x + x] = FLAG_NONE;
        }
    }
}

static int room_space_to_cell_space(int room_space) {
    return ROOM_PADDING_SIZE + (ROOM_SIZE * room_space) + (ROOM_PADDING_SIZE * room_space) + (ROOM_SIZE / 2);
}

static XZ room_to_cells(Room room) {
    return (XZ) {
        .x = room_space_to_cell_space(room.x),
        .z = room_space_to_cell_space(room.z),
    };
}

static void add_room_connection(State *state, Room a, Room b) {
    int east_west_diff = (b.x - a.x);
    int north_south_diff = (b.z - a.z);

    // one has to be 0
    ASSERT((east_west_diff==0) || (north_south_diff==0));

    // one has to be NOT 0
    ASSERT((east_west_diff!=0) || (north_south_diff!=0));

    bool is_horizontal;
    int increment = 0;
    if (east_west_diff==0) {
        is_horizontal = false;
        increment = (north_south_diff > 0) ? 1 : -1;
    } else if (north_south_diff==0) {
        is_horizontal = true;
        increment = (east_west_diff > 0) ? 1 : -1;
    }

    XZ current = room_to_cells(a);
    {
        int border = (a.size / 2) + 1;
        if (is_horizontal) {
            current.x += increment * border;
        } else {
            current.z += increment * border;
        }
    }

    ASSERT(IS_VALID_CELL(current));

    XZ target = room_to_cells(b);
    {
        int border = (b.size / 2) + 1;
        if (is_horizontal) {
            target.x -= increment * border;
        } else {
            target.z -= increment * border;
        }
    }

    ASSERT(IS_VALID_CELL(target));

    int connection_width = 1;
    {
        int smallest_size = (a.size < b.size) ? a.size : b.size;
        connection_width = GetRandomValue(1, smallest_size);
        if ((connection_width%2)==0) {
            connection_width += GetRandomValue(0, 1) ? 1 : -1;
        }
    }
    int half_connection_width = connection_width / 2;

    bool done = false;
    while (true) {
        if (done) {
            break;
        }
        if (cell_eq(current, target)) {
            done = true;
        }
        if (is_horizontal) {
            for (int i = -half_connection_width; i <= half_connection_width; i++) {
                state->map[current.z + i][current.x] = FLAG_ROOM_CONNECTOR;
            }
            current.x += increment;
            ASSERT(IS_VALID_CELL(current));
        } else {
            for (int i = -half_connection_width; i <= half_connection_width; i++) {
                state->map[current.z][current.x + i] = FLAG_ROOM_CONNECTOR;
            }
            current.z += increment;
            ASSERT(IS_VALID_CELL(current));
        }
    }
}

static bool is_room_available(Room rooms[ROOMS_PER_MAP_SIDE][ROOMS_PER_MAP_SIDE], XZ room) {
    if ((room.x<0) || (room.z<0) || (room.x>=ROOMS_PER_MAP_SIDE) || (room.z>=ROOMS_PER_MAP_SIDE)) {
        return false;
    }
    return rooms[room.z][room.x].type == ROOM_UNSET;
}

static int get_random_room_type() {
    return GetRandomValue(ROOM_UNSET+1, ROOM_TYPE_COUNT-1);
}

static void generate_rooms_recursive(State *state, Room room) {
#if DEBUG
    static int depth = 0;
    depth++;
    ASSERT(depth < 1000);
#endif

    XZ surrounding_rooms[4];
    get_surroundings(room.x, room.z, surrounding_rooms);

    // this is to prevent the generation from randomly deciding to stop
    int guaranteed_index = GetRandomValue(0, 3);

    for (int i = 0; i < 4; i++) {
        if (!is_room_available(state->rooms, surrounding_rooms[i])) {
            continue;
        }
        if (guaranteed_index != i) {
            // randomness for fun
            if (GetRandomValue(0, 1) > 0) {
                continue;
            }
        }

        int room_type = get_random_room_type();
        Room next_room = create_room(surrounding_rooms[i], room_type);
        add_room(state, next_room);
        add_room_connection(state, room, next_room);
        generate_rooms_recursive(state, next_room);
    }

#if DEBUG
    depth--;
#endif
}

static void generate_map(State *state) {
    // TODO: out of bounds could be a wall, then you would not have to put walls around the thing manually
    for (int z = 0; z < MAP_SIDE; z++) {
        for (int x = 0; x < MAP_SIDE; x++) {
            state->map[z][x] |= FLAG_WALL;
        }
    }

    XZ room_location;
    room_location.x = GetRandomValue(0, ROOMS_PER_MAP_SIDE - 1);
    room_location.z = GetRandomValue(0, ROOMS_PER_MAP_SIDE - 1);

    Room room = create_room(room_location, ROOM_5X5);
    add_room(state, room);

    Room player_room = room;

    XZ player_cell = room_to_cells(room);
    state->map[player_cell.z][player_cell.x] = FLAG_PLAYER_START;

    while (true) {
        generate_rooms_recursive(state, room);
        bool generation_completed = true;
        for (int z = 0; z < ROOMS_PER_MAP_SIDE; z++) {
            for (int x = 0; x < ROOMS_PER_MAP_SIDE; x++) {
                Room a = state->rooms[z][x];
                if (a.type == ROOM_UNSET) {
                    continue;
                }
                XZ surroundings[4];
                get_surroundings(a.x, a.z, surroundings);
                for (int i = 0; i < 4; i++) {
                    XZ s = surroundings[i];
                    if (is_room_out_of_bounds(s)) {
                        continue;
                    }
                    if (state->rooms[s.z][s.x].type != ROOM_UNSET) {
                        continue;
                    }
                    Room b = create_room((XZ){ s.x, s.z }, get_random_room_type());
                    add_room(state, b);
                    add_room_connection(state, a, b);

                    room = b;
                    generation_completed = false;
                    break;
                }
                if (!generation_completed) {
                    break;
                }
            }
            if (!generation_completed) {
                break;
            }
        }
        if (generation_completed) {
            break;
        }
    }

    for (int i = 0; i < 8; i++) {
        int x = GetRandomValue(0, ROOMS_PER_MAP_SIDE - 1);
        int z = GetRandomValue(0, ROOMS_PER_MAP_SIDE - 1);
        if ((player_room.x == x) && (player_room.z == z)) {
            i--;
            continue;
        }
        XZ cell = room_to_cells(state->rooms[z][x]);
        state->map[cell.z][cell.x] = FLAG_SQUID_SPAWNER;
    }
}

int main(void) {

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);

    InitWindow(1600, 1200, "Who is this");

    InitAudioDevice();

    State *state = (State *)calloc(sizeof(State), 1);

    state->wall = alloc_plane(PLANE_WALL, wo);
    state->portal_wall = alloc_plane(PLANE_WALL, portal);
    state->floor = alloc_plane(PLANE_FLOOR, chess);
    state->ceiling = alloc_plane(PLANE_CEILING, dirt);

    generate_map(state);

    state->camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    state->camera.fovy = 60.0f;
    state->camera.projection = CAMERA_PERSPECTIVE;

    DisableCursor();

    SetTargetFPS(60);

    state->bullet_sound = LoadSound("sounds/gun/bullet.wav");
    state->gun_idle = LoadTexture("textures/gun/SpeedGun0.png");
    state->gun_fire[0] = LoadTexture("textures/gun/SpeedGun1.png");
    state->gun_fire[1] = LoadTexture("textures/gun/SpeedGun2.png");
    state->gun_fire[2] = LoadTexture("textures/gun/SpeedGun3.png");
    state->gun_fire[3] = LoadTexture("textures/gun/SpeedGun4.png");

    state->shader = LoadShader(
        "alpha_cutout_vs.glsl",
        "alpha_cutout_fs.glsl"
    );

    state->shader_location_damage_flash = GetShaderLocation(state->shader, "damageFlash");

    state->small_slimes[0] = LoadSound("sounds/SmallSlime1.wav");
    state->small_slimes[1] = LoadSound("sounds/SmallSlime2.wav");
    state->small_slimes[2] = LoadSound("sounds/SmallSlime3.wav");
    state->small_slimes[3] = LoadSound("sounds/SmallSlime4.wav");
    state->small_slimes[4] = LoadSound("sounds/SmallSlime5.wav");
    state->small_slimes[5] = LoadSound("sounds/SmallSlime6.wav");
    state->small_slimes[6] = LoadSound("sounds/SmallSlime7.wav");
    state->small_slimes[7] = LoadSound("sounds/SmallSlime8.wav");

    state->player_cell = world_position_to_cell_position(state->camera.position);

    state->squid_count = 50;

    for (int i = 0; i < SQUID_CAPACITY; i++) {
        state->squid[i].state = CREATURE_DEAD;
        state->squid[i].hit_radius = HALF_SIZE/2;
        state->squid[i].height = HALF_SIZE/2;
        if (i == 0) {
            state->squid[i].textures[0] = LoadTexture("textures/squid/squid0.png");
            state->squid[i].textures[1] = LoadTexture("textures/squid/squid1.png");
            state->squid[i].textures[2] = LoadTexture("textures/squid/squid2.png");
            state->squid[i].textures[3] = LoadTexture("textures/squid/squid3.png");
            state->squid[i].textures[4] = LoadTexture("textures/squid/squid4.png");
            state->squid[i].death_textures[0] = LoadTexture("textures/squid/squid-death0.png");
            state->squid[i].death_textures[1] = LoadTexture("textures/squid/squid-death1.png");
            state->squid[i].death_textures[2] = LoadTexture("textures/squid/squid-death2.png");
            state->squid[i].death_textures[3] = LoadTexture("textures/squid/squid-death3.png");
            state->squid[i].death_textures[4] = LoadTexture("textures/squid/squid-death4.png");
        } else {
            for (int j = 0; j < ANIMATION_AMOUNT; j++) {
                state->squid[i].textures[j] = state->squid[0].textures[j];
                state->squid[i].death_textures[j] = state->squid[0].death_textures[j];
            }
        }
        {
            // TODO add initialization creature function
            Mesh mesh = GenMeshPlane(10, 10, 1, 1);
            Model model = LoadModelFromMesh(mesh);
            model.transform = MatrixRotateX(PI / 2.0f);
            model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = state->squid[i].textures[0];
            model.materials[0].shader = state->shader;
            state->squid[i].plane.model = model;
        }
        update_creature_direction(state, &state->squid[i], state->camera.position, state->player_cell);
    }

    for (int z = 0; z < MAP_SIDE; z++) {
        for (int x = 0; x < MAP_SIDE; x++) {
            Flags flags = state->map[z][x];
            if (has_flag(flags, FLAG_PLAYER_START)) {
                state->camera.position = cell_position_to_world_position(x, z, HALF_SIZE/2);
                state->camera.target = (Vector3){ 0.0f, state->camera.position.y, 0.0f };
            } else if (has_flag(flags, FLAG_SQUID_SPAWNER)) {
                state->squid_spawners[state->squid_spawner_count++] = (XZ){x,z};
            }
        }
    }

    while (!WindowShouldClose())
    {
        main_update(state);

        main_render(state);
    }

    CloseWindow();

    CloseAudioDevice();

    return 0;
}

