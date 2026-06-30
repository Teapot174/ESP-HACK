#include "display.h"
#include <GyverButton.h>
#include <math.h>
#include <string.h>
#include "menu/games.h"

extern DisplayType display;
extern GButton buttonUp;
extern GButton buttonDown;
extern GButton buttonOK;
extern GButton buttonBack;
extern byte gamesMenuIndex;
extern byte colorSelectionIndex;

enum GamesState : byte {
  GAMES_MENU_STATE,
  GAMES_PLACEHOLDER_STATE,
  GAMES_SNAKE_STATE,
  GAMES_BIRD_STATE,
  GAMES_TETRIS_STATE,
  GAMES_PONG_STATE,
  GAMES_DOOM_STATE
};

static GamesState gamesState = GAMES_MENU_STATE;

namespace {
namespace Doom {
#define _constants_h
#define LEVEL_WIDTH_BASE 6
#define LEVEL_WIDTH (1 << LEVEL_WIDTH_BASE)
#define LEVEL_HEIGHT 57
#define LEVEL_SIZE (LEVEL_WIDTH / 2 * LEVEL_HEIGHT)
#define FRAME_TIME 66.666666
#define RES_DIVIDER 2
#define Z_RES_DIVIDER 2
#define DISTANCE_MULTIPLIER 20
#define MAX_RENDER_DEPTH 12
#define MAX_SPRITE_DEPTH 8
#define ZBUFFER_SIZE (SCREEN_WIDTH / Z_RES_DIVIDER)
#define RENDER_HEIGHT 56
#define HALF_WIDTH (SCREEN_WIDTH / 2)
#define GUN_TARGET_POS 18
#define GUN_SHOT_POS (GUN_TARGET_POS + 4)
#define ROT_SPEED .12
#define MOV_SPEED .2
#define MOV_SPEED_INV 5
#define JOGGING_SPEED .005
#define ENEMY_SPEED .02
#define FIREBALL_SPEED .2
#define FIREBALL_ANGLES 45
#define MAX_ENTITIES 10
#define MAX_STATIC_ENTITIES 28
#define MAX_ENTITY_DISTANCE 200
#define MAX_ENEMY_VIEW 80
#define ITEM_COLLIDER_DIST 6
#define ENEMY_COLLIDER_DIST 4
#define FIREBALL_COLLIDER_DIST 2
#define ENEMY_MELEE_DIST 6
#define FIRE_COOLDOWN_MS 500
#define ENEMY_MELEE_DAMAGE 8
#define ENEMY_FIREBALL_DAMAGE 20
#define GUN_MAX_DAMAGE 15
#define INTRO 0
#define GAME_PLAY 1
#include "doom/types.h"
#include "doom/entities.h"
#include "doom/sprites.h"
#include "doom/level.h"

#define doomSwap(a, b) do { auto temp = a; a = b; b = temp; } while (0)
#define doomSign(a, b) ((double)((a) > (b) ? 1 : ((b) > (a) ? -1 : 0)))

uint8_t scene = INTRO;
bool exitScene = false;
bool invertScreen = false;
uint8_t flashScreen = 0;
Player player;
Entity entity[MAX_ENTITIES];
StaticEntity staticEntity[MAX_STATIC_ENTITIES];
uint8_t numEntities = 0;
uint8_t numStaticEntities = 0;
double delta = 1;
uint32_t lastFrameTime = 0;
uint8_t zbuffer[ZBUFFER_SIZE];
bool introDrawn = false;
bool hudDrawn = false;
uint8_t gunPos = 0;
uint8_t fade = 0;
double viewHeight = 0;
double jogging = 0;
bool fireWasPressed = false;
bool okWasPressed = false;
bool backWasPressed = false;
unsigned long lastFireTime = 0;

const static uint8_t PROGMEM bitMask[8] = {128, 64, 32, 16, 8, 4, 2, 1};

inline bool readBit(uint8_t value, uint8_t bit) {
  return (value & pgm_read_byte(bitMask + bit)) != 0;
}

Entity create_entity(uint8_t type, uint8_t x, uint8_t y, uint8_t initialState, uint8_t initialHealth) {
  UID uid = create_uid(type, x, y);
  Coords pos = create_coords((double)x + .5, (double)y + .5);
  return {uid, pos, initialState, initialHealth, 0, 0};
}

StaticEntity create_static_entity(UID uid, uint8_t x, uint8_t y, bool active) {
  return {uid, x, y, active};
}

Coords create_coords(double x, double y) {
  return {x, y};
}

uint8_t coords_distance(Coords* a, Coords* b) {
  const double dx = a->x - b->x;
  const double dy = a->y - b->y;
  return sqrt(dx * dx + dy * dy) * DISTANCE_MULTIPLIER;
}

UID create_uid(uint8_t type, uint8_t x, uint8_t y) {
  return ((y << LEVEL_WIDTH_BASE) | x) << 4 | type;
}

uint8_t uid_get_type(UID uid) {
  return uid & 0x0F;
}

void playSound(const uint8_t*, uint8_t) {
}

bool inputForward() { return digitalRead(BUTTON_UP) == LOW; }
bool inputLeft() { return digitalRead(BUTTON_OK) == LOW; }
bool inputRight() { return digitalRead(BUTTON_BACK) == LOW; }
bool inputFire() { return digitalRead(BUTTON_DOWN) == LOW; }

bool buttonPressedOnce(uint8_t pin, bool &wasPressed) {
  const bool pressed = digitalRead(pin) == LOW;
  if (pressed && !wasPressed) {
    wasPressed = true;
    return true;
  }
  if (!pressed) {
    wasPressed = false;
  }
  return false;
}

bool inputOkPress() { return buttonPressedOnce(BUTTON_OK, okWasPressed); }
bool inputBackPress() { return buttonPressedOnce(BUTTON_BACK, backWasPressed); }

void jumpTo(uint8_t targetScene) {
  scene = targetScene;
  exitScene = true;
}

void fps() {
  while (millis() - lastFrameTime < FRAME_TIME) {
    delay(1);
  }
  delta = (double)(millis() - lastFrameTime) / FRAME_TIME;
  lastFrameTime = millis();
}

double getActualFps() {
  return 1000 / (FRAME_TIME * delta);
}

bool getGradientPixel(uint8_t x, uint8_t y, uint8_t i) {
  if (i == 0) return false;
  if (i >= GRADIENT_COUNT - 1) return true;
  const uint8_t intensity = constrain(i, 0, GRADIENT_COUNT - 1);
  const uint8_t index = intensity * GRADIENT_WIDTH * GRADIENT_HEIGHT
    + y * GRADIENT_WIDTH % (GRADIENT_WIDTH * GRADIENT_HEIGHT)
    + x / GRADIENT_HEIGHT % GRADIENT_WIDTH;
  return readBit(pgm_read_byte(gradient + index), x % 8);
}

void drawPixel(int16_t x, int16_t y, bool color, bool raycasterViewport = false) {
  if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= (raycasterViewport ? RENDER_HEIGHT : SCREEN_HEIGHT)) {
    return;
  }
  display.drawPixel(x, y, color ? SH110X_WHITE : SH110X_BLACK);
}

void fadeScreen(uint8_t intensity, bool color = false) {
  for (uint8_t x = 0; x < SCREEN_WIDTH; x++) {
    for (uint8_t y = 0; y < SCREEN_HEIGHT; y++) {
      if (getGradientPixel(x, y, intensity)) {
        drawPixel(x, y, color, false);
      }
    }
  }
}

void drawVLine(uint8_t x, int16_t startY, int16_t endY, uint8_t intensity) {
  const int16_t lowerY = max<int16_t>(min<int16_t>(startY, endY), 0);
  const int16_t higherY = min<int16_t>(max<int16_t>(startY, endY), RENDER_HEIGHT - 1);
  for (int16_t y = lowerY; y <= higherY; y++) {
    for (uint8_t c = 0; c < RES_DIVIDER; c++) {
      if (getGradientPixel(x + c, y, intensity)) {
        drawPixel(x + c, y, true, true);
      }
    }
  }
}

void drawSprite(int16_t x, int16_t y, const uint8_t bitmap[], const uint8_t mask[],
                int16_t w, int16_t h, uint8_t sprite, double distance) {
  const uint8_t tw = max<int>(1, (double)w / distance);
  const uint8_t th = max<int>(1, (double)h / distance);
  const uint8_t byteWidth = w / 8;
  const uint8_t pixelSize = max<int>(1, 1.0 / distance);
  const uint16_t spriteOffset = byteWidth * h * sprite;

  if (zbuffer[min(max<int>(x, 0), ZBUFFER_SIZE - 1) / Z_RES_DIVIDER] < distance * DISTANCE_MULTIPLIER) {
    return;
  }

  for (uint8_t ty = 0; ty < th; ty += pixelSize) {
    if (y + ty < 0 || y + ty >= RENDER_HEIGHT) continue;
    const uint8_t sy = ty * distance;
    for (uint8_t tx = 0; tx < tw; tx += pixelSize) {
      if (x + tx < 0 || x + tx >= SCREEN_WIDTH) continue;
      const uint8_t sx = tx * distance;
      const uint16_t byteOffset = spriteOffset + sy * byteWidth + sx / 8;
      if (!readBit(pgm_read_byte(mask + byteOffset), sx % 8)) continue;
      const bool pixel = readBit(pgm_read_byte(bitmap + byteOffset), sx % 8);
      for (uint8_t ox = 0; ox < pixelSize; ox++) {
        for (uint8_t oy = 0; oy < pixelSize; oy++) {
          drawPixel(x + tx + ox, y + ty + oy, pixel, true);
        }
      }
    }
  }
}

void drawChar(int16_t x, int16_t y, char ch) {
  uint8_t c = 0;
  while (CHAR_MAP[c] != ch && CHAR_MAP[c] != '\0') c++;
  const uint8_t bOffset = c / 2;
  for (uint8_t line = 0; line < CHAR_HEIGHT; line++) {
    const uint8_t b = pgm_read_byte(bmp_font + (line * bmp_font_width + bOffset));
    for (uint8_t n = 0; n < CHAR_WIDTH; n++) {
      if (readBit(b, (c % 2 == 0 ? 0 : 4) + n)) {
        drawPixel(x + n, y + line, true, false);
      }
    }
  }
}

void drawText(int16_t x, int16_t y, const char* text, uint8_t space = 1) {
  int16_t pos = x;
  for (uint8_t i = 0; text[i] != '\0' && pos < SCREEN_WIDTH; i++) {
    drawChar(pos, y, text[i]);
    pos += CHAR_WIDTH + space;
  }
}

void drawText(uint8_t x, uint8_t y, uint8_t num) {
  char buf[4];
  itoa(num, buf, 10);
  drawText(x, y, buf);
}

uint8_t getBlockAt(const uint8_t level[], uint8_t x, uint8_t y) {
  if (x >= LEVEL_WIDTH || y >= LEVEL_HEIGHT) {
    return E_FLOOR;
  }
  return pgm_read_byte(level + (((LEVEL_HEIGHT - 1 - y) * LEVEL_WIDTH + x) / 2))
    >> (!(x % 2) * 4) & 0b1111;
}

void initializeLevel(const uint8_t level[]) {
  for (int y = LEVEL_HEIGHT - 1; y >= 0; y--) {
    for (uint8_t x = 0; x < LEVEL_WIDTH; x++) {
      if (getBlockAt(level, x, y) == E_PLAYER) {
        player = create_player(x, y);
        return;
      }
    }
  }
}

bool isSpawned(UID uid) {
  for (uint8_t i = 0; i < numEntities; i++) {
    if (entity[i].uid == uid) return true;
  }
  return false;
}

void spawnEntity(uint8_t type, uint8_t x, uint8_t y) {
  if (numEntities >= MAX_ENTITIES) return;
  if (type == E_ENEMY) entity[numEntities++] = create_enemy(x, y);
  else if (type == E_KEY) entity[numEntities++] = create_key(x, y);
  else if (type == E_MEDIKIT) entity[numEntities++] = create_medikit(x, y);
}

void spawnFireball(double x, double y) {
  if (numEntities >= MAX_ENTITIES) return;
  const UID uid = create_uid(E_FIREBALL, x, y);
  if (isSpawned(uid)) return;
  int16_t dir = FIREBALL_ANGLES + atan2(y - player.pos.y, x - player.pos.x) / PI * FIREBALL_ANGLES;
  if (dir < 0) dir += FIREBALL_ANGLES * 2;
  entity[numEntities++] = create_fireball(x, y, dir);
}

void removeEntity(UID uid) {
  uint8_t i = 0;
  bool found = false;
  while (i < numEntities) {
    if (!found && entity[i].uid == uid) {
      found = true;
      numEntities--;
    }
    if (found) entity[i] = entity[i + 1];
    i++;
  }
}

UID detectCollision(const uint8_t level[], Coords *pos, double relativeX, double relativeY, bool onlyWalls = false) {
  const uint8_t roundX = int(pos->x + relativeX);
  const uint8_t roundY = int(pos->y + relativeY);
  const uint8_t block = getBlockAt(level, roundX, roundY);
  if (block == E_WALL) {
    return create_uid(block, roundX, roundY);
  }
  if (onlyWalls) return UID_null;

  for (uint8_t i = 0; i < numEntities; i++) {
    if (&(entity[i].pos) == pos) continue;
    const uint8_t type = uid_get_type(entity[i].uid);
    if (type != E_ENEMY || entity[i].state == S_DEAD || entity[i].state == S_HIDDEN) continue;
    Coords newCoords = {entity[i].pos.x - relativeX, entity[i].pos.y - relativeY};
    const uint8_t distance = coords_distance(pos, &newCoords);
    if (distance < ENEMY_COLLIDER_DIST && distance < entity[i].distance) {
      return entity[i].uid;
    }
  }
  return UID_null;
}

UID updatePosition(const uint8_t level[], Coords *pos, double relativeX, double relativeY, bool onlyWalls = false) {
  const UID collideX = detectCollision(level, pos, relativeX, 0, onlyWalls);
  const UID collideY = detectCollision(level, pos, 0, relativeY, onlyWalls);
  if (!collideX) pos->x += relativeX;
  if (!collideY) pos->y += relativeY;
  return collideX || collideY || UID_null;
}

Coords translateIntoView(Coords *pos) {
  const double spriteX = pos->x - player.pos.x;
  const double spriteY = pos->y - player.pos.y;
  const double invDet = 1.0 / (player.plane.x * player.dir.y - player.dir.x * player.plane.y);
  return {
    invDet * (player.dir.y * spriteX - player.dir.x * spriteY),
    invDet * (-player.plane.y * spriteX + player.plane.x * spriteY)
  };
}

void fire() {
  int8_t targetIndex = -1;
  int16_t targetOffset = SCREEN_WIDTH;
  uint8_t targetDistance = 255;

  for (uint8_t i = 0; i < numEntities; i++) {
    if (uid_get_type(entity[i].uid) != E_ENEMY || entity[i].state == S_DEAD || entity[i].state == S_HIDDEN) continue;
    const Coords transform = translateIntoView(&(entity[i].pos));
    if (transform.y <= 0.1 || transform.y > MAX_SPRITE_DEPTH) continue;

    const int16_t spriteScreenX = HALF_WIDTH * (1.0 + transform.x / transform.y);
    const int16_t crosshairOffset = abs(spriteScreenX - HALF_WIDTH);
    const int16_t hitRadius = max<int16_t>(5, (BMP_IMP_WIDTH * .5) / transform.y);

    if (crosshairOffset <= hitRadius &&
        (targetIndex < 0 || crosshairOffset < targetOffset ||
         (crosshairOffset == targetOffset && entity[i].distance < targetDistance))) {
      targetIndex = i;
      targetOffset = crosshairOffset;
      targetDistance = entity[i].distance;
    }
  }

  if (targetIndex < 0) {
    return;
  }

  Entity &target = entity[targetIndex];
  const uint8_t damage = constrain(GUN_MAX_DAMAGE - (target.distance / 10), 5, GUN_MAX_DAMAGE);
  target.health = target.health > damage ? target.health - damage : 0;
  target.state = target.health == 0 ? S_DEAD : S_HIT;
  target.timer = target.health == 0 ? 6 : 4;
}

void shootGun() {
  gunPos = GUN_SHOT_POS;
  fire();
}

void updateHud() {
  display.fillRect(12, 57, 15, 6, SH110X_BLACK);
  display.fillRect(50, 57, 8, 6, SH110X_BLACK);
  drawText(12, 57, player.health);
  drawText(50, 57, player.keys);
}

void updateEntities(const uint8_t level[]) {
  uint8_t i = 0;
  while (i < numEntities) {
    entity[i].distance = coords_distance(&(player.pos), &(entity[i].pos));
    if (entity[i].timer > 0) entity[i].timer--;
    if (entity[i].distance > MAX_ENTITY_DISTANCE) {
      removeEntity(entity[i].uid);
      continue;
    }
    if (entity[i].state == S_HIDDEN) {
      i++;
      continue;
    }

    switch (uid_get_type(entity[i].uid)) {
      case E_ENEMY:
        if (entity[i].health == 0) {
          if (entity[i].state != S_DEAD) {
            entity[i].state = S_DEAD;
            entity[i].timer = 6;
          }
        } else if (entity[i].state == S_HIT || entity[i].state == S_FIRING) {
          if (entity[i].timer == 0) {
            entity[i].state = S_ALERT;
            entity[i].timer = 40;
          }
        } else if (entity[i].distance > ENEMY_MELEE_DIST && entity[i].distance < MAX_ENEMY_VIEW) {
          if (entity[i].state != S_ALERT) {
            entity[i].state = S_ALERT;
            entity[i].timer = 20;
          } else if (entity[i].timer == 0) {
            spawnFireball(entity[i].pos.x, entity[i].pos.y);
            entity[i].state = S_FIRING;
            entity[i].timer = 6;
          } else {
            updatePosition(level, &(entity[i].pos),
                           doomSign(player.pos.x, entity[i].pos.x) * ENEMY_SPEED * delta,
                           doomSign(player.pos.y, entity[i].pos.y) * ENEMY_SPEED * delta,
                           true);
          }
        } else if (entity[i].distance <= ENEMY_MELEE_DIST) {
          if (entity[i].state != S_MELEE) {
            entity[i].state = S_MELEE;
            entity[i].timer = 10;
          } else if (entity[i].timer == 0) {
            player.health = max<int>(0, player.health - ENEMY_MELEE_DAMAGE);
            entity[i].timer = 14;
            flashScreen = 1;
            updateHud();
          }
        } else {
          entity[i].state = S_STAND;
        }
        break;
      case E_FIREBALL:
        if (entity[i].distance < FIREBALL_COLLIDER_DIST) {
          player.health = max<int>(0, player.health - ENEMY_FIREBALL_DAMAGE);
          flashScreen = 1;
          updateHud();
          removeEntity(entity[i].uid);
          continue;
        } else if (updatePosition(level, &(entity[i].pos),
                                  cos((double)entity[i].health / FIREBALL_ANGLES * PI) * FIREBALL_SPEED,
                                  sin((double)entity[i].health / FIREBALL_ANGLES * PI) * FIREBALL_SPEED,
                                  true)) {
          removeEntity(entity[i].uid);
          continue;
        }
        break;
      case E_MEDIKIT:
        if (entity[i].distance < ITEM_COLLIDER_DIST) {
          entity[i].state = S_HIDDEN;
          player.health = min<int>(100, player.health + 50);
          updateHud();
          flashScreen = 1;
        }
        break;
      case E_KEY:
        if (entity[i].distance < ITEM_COLLIDER_DIST) {
          entity[i].state = S_HIDDEN;
          player.keys++;
          updateHud();
          flashScreen = 1;
        }
        break;
    }
    i++;
  }
}

void renderMap(const uint8_t level[], double currentViewHeight) {
  UID lastUid = UID_null;
  for (uint8_t x = 0; x < SCREEN_WIDTH; x += RES_DIVIDER) {
    const double cameraX = 2 * (double)x / SCREEN_WIDTH - 1;
    const double rayX = player.dir.x + player.plane.x * cameraX;
    const double rayY = player.dir.y + player.plane.y * cameraX;
    uint8_t mapX = uint8_t(player.pos.x);
    uint8_t mapY = uint8_t(player.pos.y);
    Coords mapCoords = {player.pos.x, player.pos.y};
    const double deltaX = abs(1 / rayX);
    const double deltaY = abs(1 / rayY);
    int8_t stepX, stepY;
    double sideX, sideY;

    if (rayX < 0) {
      stepX = -1;
      sideX = (player.pos.x - mapX) * deltaX;
    } else {
      stepX = 1;
      sideX = (mapX + 1.0 - player.pos.x) * deltaX;
    }
    if (rayY < 0) {
      stepY = -1;
      sideY = (player.pos.y - mapY) * deltaY;
    } else {
      stepY = 1;
      sideY = (mapY + 1.0 - player.pos.y) * deltaY;
    }

    uint8_t depth = 0;
    bool hit = false;
    bool side = false;
    while (!hit && depth < MAX_RENDER_DEPTH) {
      if (sideX < sideY) {
        sideX += deltaX;
        mapX += stepX;
        side = false;
      } else {
        sideY += deltaY;
        mapY += stepY;
        side = true;
      }
      const uint8_t block = getBlockAt(level, mapX, mapY);
      if (block == E_WALL) {
        hit = true;
      } else if (block == E_ENEMY || (block & 0b00001000)) {
        if (coords_distance(&(player.pos), &mapCoords) < MAX_ENTITY_DISTANCE) {
          const UID uid = create_uid(block, mapX, mapY);
          if (lastUid != uid && !isSpawned(uid)) {
            spawnEntity(block, mapX, mapY);
            lastUid = uid;
          }
        }
      }
      depth++;
    }

    if (hit) {
      const double distance = side == 0
        ? max<double>(1, (mapX - player.pos.x + (1 - stepX) / 2) / rayX)
        : max<double>(1, (mapY - player.pos.y + (1 - stepY) / 2) / rayY);
      zbuffer[x / Z_RES_DIVIDER] = min<int>(distance * DISTANCE_MULTIPLIER, 255);
      const uint8_t lineHeight = RENDER_HEIGHT / distance;
      drawVLine(x,
                currentViewHeight / distance - lineHeight / 2 + RENDER_HEIGHT / 2,
                currentViewHeight / distance + lineHeight / 2 + RENDER_HEIGHT / 2,
                GRADIENT_COUNT - int(distance / MAX_RENDER_DEPTH * GRADIENT_COUNT) - side * 2);
    }
  }
}

void sortEntities() {
  uint8_t gap = numEntities;
  bool swapped = false;
  while (gap > 1 || swapped) {
    gap = (gap * 10) / 13;
    if (gap == 9 || gap == 10) gap = 11;
    if (gap < 1) gap = 1;
    swapped = false;
    for (uint8_t i = 0; i < numEntities - gap; i++) {
      const uint8_t j = i + gap;
      if (entity[i].distance < entity[j].distance) {
        doomSwap(entity[i], entity[j]);
        swapped = true;
      }
    }
  }
}

void renderEntities(double currentViewHeight) {
  sortEntities();
  for (uint8_t i = 0; i < numEntities; i++) {
    if (entity[i].state == S_HIDDEN) continue;
    const Coords transform = translateIntoView(&(entity[i].pos));
    if (transform.y <= 0.1 || transform.y > MAX_SPRITE_DEPTH) continue;
    const int16_t spriteScreenX = HALF_WIDTH * (1.0 + transform.x / transform.y);
    const int8_t spriteScreenY = RENDER_HEIGHT / 2 + currentViewHeight / transform.y;
    if (spriteScreenX < -HALF_WIDTH || spriteScreenX > SCREEN_WIDTH + HALF_WIDTH) continue;

    switch (uid_get_type(entity[i].uid)) {
      case E_ENEMY: {
        uint8_t sprite = 0;
        if (entity[i].state == S_ALERT) sprite = int(millis() / 500) % 2;
        else if (entity[i].state == S_FIRING) sprite = 2;
        else if (entity[i].state == S_HIT) sprite = 3;
        else if (entity[i].state == S_MELEE) sprite = entity[i].timer > 10 ? 2 : 1;
        else if (entity[i].state == S_DEAD) sprite = entity[i].timer > 0 ? 3 : 4;
        drawSprite(spriteScreenX - BMP_IMP_WIDTH * .5 / transform.y,
                   spriteScreenY - 8 / transform.y,
                   bmp_imp_bits, bmp_imp_mask, BMP_IMP_WIDTH, BMP_IMP_HEIGHT, sprite, transform.y);
        break;
      }
      case E_FIREBALL:
        drawSprite(spriteScreenX - BMP_FIREBALL_WIDTH / 2 / transform.y,
                   spriteScreenY - BMP_FIREBALL_HEIGHT / 2 / transform.y,
                   bmp_fireball_bits, bmp_fireball_mask, BMP_FIREBALL_WIDTH, BMP_FIREBALL_HEIGHT, 0, transform.y);
        break;
      case E_MEDIKIT:
        drawSprite(spriteScreenX - BMP_ITEMS_WIDTH / 2 / transform.y,
                   spriteScreenY + 5 / transform.y,
                   bmp_items_bits, bmp_items_mask, BMP_ITEMS_WIDTH, BMP_ITEMS_HEIGHT, 0, transform.y);
        break;
      case E_KEY:
        drawSprite(spriteScreenX - BMP_ITEMS_WIDTH / 2 / transform.y,
                   spriteScreenY + 5 / transform.y,
                   bmp_items_bits, bmp_items_mask, BMP_ITEMS_WIDTH, BMP_ITEMS_HEIGHT, 1, transform.y);
        break;
    }
  }
}

void renderGun(uint8_t currentGunPos, double amountJogging) {
  const int16_t x = 48 + sin((double)millis() * JOGGING_SPEED) * 10 * amountJogging;
  const int16_t y = RENDER_HEIGHT - currentGunPos + abs(cos((double)millis() * JOGGING_SPEED)) * 8 * amountJogging;
  if (currentGunPos > GUN_SHOT_POS - 2) {
    display.drawBitmap(x + 6, y - 11, bmp_fire_bits, BMP_FIRE_WIDTH, BMP_FIRE_HEIGHT, SH110X_WHITE);
  }
  const uint8_t clipHeight = max<int>(0, min<int>(y + BMP_GUN_HEIGHT, RENDER_HEIGHT) - y);
  display.drawBitmap(x, y, bmp_gun_mask, BMP_GUN_WIDTH, clipHeight, SH110X_BLACK);
  display.drawBitmap(x, y, bmp_gun_bits, BMP_GUN_WIDTH, clipHeight, SH110X_WHITE);
}

void renderHud() {
  drawText(2, 57, "{}", 0);
  drawText(40, 57, "[]", 0);
  updateHud();
}

void renderStats() {
  display.fillRect(58, 57, 70, 6, SH110X_BLACK);
  drawText(114, 57, (uint8_t)getActualFps());
  drawText(82, 57, numEntities);
}

void resetGame() {
  scene = INTRO;
  exitScene = false;
  invertScreen = false;
  flashScreen = 0;
  numEntities = 0;
  numStaticEntities = 0;
  delta = 1;
  lastFrameTime = millis();
  memset(zbuffer, 0xFF, sizeof(zbuffer));
  introDrawn = false;
  hudDrawn = false;
  gunPos = 0;
  fade = GRADIENT_COUNT - 1;
  viewHeight = 0;
  jogging = 0;
  fireWasPressed = false;
  okWasPressed = digitalRead(BUTTON_OK) == LOW;
  backWasPressed = digitalRead(BUTTON_BACK) == LOW;
  lastFireTime = 0;
}

void start() {
  resetGame();
  gamesState = GAMES_DOOM_STATE;
}

void drawIntro() {
  display.clearDisplay();
  display.drawBitmap((SCREEN_WIDTH - BMP_LOGO_WIDTH) / 2,
                     (SCREEN_HEIGHT - BMP_LOGO_HEIGHT) / 3,
                     bmp_logo_bits, BMP_LOGO_WIDTH, BMP_LOGO_HEIGHT, SH110X_WHITE);
  drawText((SCREEN_WIDTH - (8 * CHAR_WIDTH + 7)) / 2, SCREEN_HEIGHT * .8, "PRESS OK");
  display.display();
  introDrawn = true;
}

void beginPlay() {
  numEntities = 0;
  numStaticEntities = 0;
  memset(zbuffer, 0xFF, sizeof(zbuffer));
  initializeLevel(sto_level_1);
  gunPos = 0;
  fade = GRADIENT_COUNT - 1;
  hudDrawn = false;
  viewHeight = 0;
  jogging = 0;
  fireWasPressed = false;
  okWasPressed = digitalRead(BUTTON_OK) == LOW;
  backWasPressed = digitalRead(BUTTON_BACK) == LOW;
  lastFireTime = 0;
  lastFrameTime = millis();
  scene = GAME_PLAY;
  exitScene = false;
}

void stop();

void tickIntro() {
  if (!introDrawn) {
    drawIntro();
  }
  if (inputBackPress()) {
    stop();
  } else if (inputOkPress()) {
    beginPlay();
  }
}

void rotatePlayer(double rotSpeed) {
  const double oldDirX = player.dir.x;
  player.dir.x = player.dir.x * cos(rotSpeed) - player.dir.y * sin(rotSpeed);
  player.dir.y = oldDirX * sin(rotSpeed) + player.dir.y * cos(rotSpeed);
  const double oldPlaneX = player.plane.x;
  player.plane.x = player.plane.x * cos(rotSpeed) - player.plane.y * sin(rotSpeed);
  player.plane.y = oldPlaneX * sin(rotSpeed) + player.plane.y * cos(rotSpeed);
}

void tickPlay() {
  fps();
  display.fillRect(0, 0, SCREEN_WIDTH, RENDER_HEIGHT, SH110X_BLACK);

  if (player.health > 0) {
    if (inputForward()) {
      player.velocity += (MOV_SPEED - player.velocity) * .4;
      jogging = abs(player.velocity) * MOV_SPEED_INV;
    } else {
      player.velocity *= .5;
      jogging = abs(player.velocity) * MOV_SPEED_INV;
    }

    if (inputRight()) {
      rotatePlayer(-ROT_SPEED * delta);
    } else if (inputLeft()) {
      rotatePlayer(ROT_SPEED * delta);
    }
    okWasPressed = digitalRead(BUTTON_OK) == LOW;
    backWasPressed = digitalRead(BUTTON_BACK) == LOW;

    viewHeight = abs(sin((double)millis() * JOGGING_SPEED)) * 6 * jogging;
    const bool firePressed = inputFire();
    const unsigned long now = millis();
    if (gunPos > GUN_TARGET_POS) {
      gunPos--;
    } else if (gunPos < GUN_TARGET_POS) {
      gunPos += 2;
    }

    if (firePressed && !fireWasPressed) {
      fireWasPressed = true;
      if (lastFireTime == 0 || now - lastFireTime >= FIRE_COOLDOWN_MS) {
        lastFireTime = now;
        shootGun();
      }
    } else if (!firePressed) {
      fireWasPressed = false;
    }
  } else {
    if (inputBackPress()) {
      stop();
      return;
    } else if (inputOkPress()) {
      beginPlay();
      return;
    }
    if (viewHeight > -10) viewHeight--;
    if (gunPos > 1) gunPos -= 2;
  }

  if (abs(player.velocity) > 0.003) {
    updatePosition(sto_level_1, &(player.pos),
                   player.dir.x * player.velocity * delta,
                   player.dir.y * player.velocity * delta);
  } else {
    player.velocity = 0;
  }

  updateEntities(sto_level_1);
  renderMap(sto_level_1, viewHeight);
  renderEntities(viewHeight);
  renderGun(gunPos, jogging);

  if (fade > 0) {
    fadeScreen(fade);
    fade--;
    if (fade == 0) {
      renderHud();
      hudDrawn = true;
    }
  } else {
    if (!hudDrawn) {
      renderHud();
      hudDrawn = true;
    }
    renderStats();
  }

  if (flashScreen > 0) {
    invertScreen = !invertScreen;
    flashScreen--;
  } else if (invertScreen) {
    invertScreen = false;
  }

  display.invertDisplay((::colorSelectionIndex == 0) != invertScreen);
  display.display();
}

void stop() {
  display.invertDisplay(::colorSelectionIndex == 0);
  gamesState = GAMES_MENU_STATE;
  displayGamesMenu(display, gamesMenuIndex);
}

void tick() {
  if (scene == INTRO) {
    tickIntro();
  } else {
    tickPlay();
  }
}
}

constexpr byte SNAKE_TILE_SIZE = 4;
constexpr byte SNAKE_GRID_WIDTH = SCREEN_WIDTH / SNAKE_TILE_SIZE;
constexpr byte SNAKE_GRID_HEIGHT = SCREEN_HEIGHT / SNAKE_TILE_SIZE;
constexpr byte SNAKE_SCORE_SAFE_X = 12 / SNAKE_TILE_SIZE;
constexpr byte SNAKE_SCORE_SAFE_Y = 8 / SNAKE_TILE_SIZE;
constexpr byte SNAKE_WALL_MIN_X = 1;
constexpr byte SNAKE_WALL_MAX_X = SNAKE_GRID_WIDTH - 2;
constexpr byte SNAKE_WALL_MIN_Y = 1;
constexpr byte SNAKE_WALL_MAX_Y = SNAKE_GRID_HEIGHT - 2;
constexpr byte SNAKE_INITIAL_X = 16;
constexpr byte SNAKE_INITIAL_Y = 8;
constexpr uint16_t SNAKE_MAX_LENGTH = 128;

byte snakeAppleX = 0;
byte snakeAppleY = 0;
int8_t snakeDirX = 1;
int8_t snakeDirY = 0;
byte snakeHeadX = SNAKE_INITIAL_X;
byte snakeHeadY = SNAKE_INITIAL_Y;
uint16_t snakeLength = 2;
uint16_t snakeScore = 0;
byte snakePosX[SNAKE_MAX_LENGTH];
byte snakePosY[SNAKE_MAX_LENGTH];
unsigned long snakeFrameTime = 0;
float snakeTilesPerSecond = 7.0f;
bool snakeAwaitRestart = false;
bool snakeUpWasPressed = false;
bool snakeDownWasPressed = false;
bool snakeOkWasPressed = false;
bool snakeBackWasPressed = false;

constexpr int BIRD_SCALE_FACTOR = 10;
constexpr int BIRD_GRAVITY = 5;
constexpr int BIRD_JUMP_STRENGTH = -25;
constexpr int BIRD_PIPE_WIDTH = 15 * BIRD_SCALE_FACTOR;
constexpr int BIRD_PIPE_GAP = 30 * BIRD_SCALE_FACTOR;
constexpr int BIRD_PIPE_SPEED = 10;
constexpr int BIRD_PIPE_SPACING = 65 * BIRD_SCALE_FACTOR;
constexpr int BIRD_WIDTH_PX = 10;
constexpr int BIRD_HEIGHT_PX = 8;
constexpr int BIRD_MAX_PIPES = 3;
constexpr unsigned long BIRD_FRAME_TIME = 40;

struct BirdPipe {
  int x;
  int height;
};

const unsigned char birdBitmap1[] PROGMEM = {
  0x0e, 0x00, 0x15, 0x00, 0x60, 0x80, 0x81, 0x80, 0xfc, 0xc0, 0xfb, 0x80, 0x71, 0x00, 0x1e, 0x00
};

const unsigned char birdBitmap2[] PROGMEM = {
  0x0e, 0x00, 0x15, 0x00, 0x70, 0x80, 0xf9, 0x80, 0xfc, 0xc0, 0x83, 0x80, 0x71, 0x00, 0x1e, 0x00
};

const unsigned char* const birdBitmaps[2] = {
  birdBitmap1,
  birdBitmap2
};

BirdPipe birdPipes[BIRD_MAX_PIPES];
int birdY = 0;
int birdVelocity = 0;
int birdScore = 0;
unsigned long birdFrameCounter = 0;
unsigned long birdFrameTime = 0;
bool birdAwaitRestart = false;
bool birdBackReadyForExit = true;

constexpr byte TETRIS_BOARD_WIDTH = 10;
constexpr byte TETRIS_BOARD_HEIGHT = 18;
constexpr byte TETRIS_TILE_SIZE = 5;
constexpr byte TETRIS_TILE_GAP = 1;
constexpr byte TETRIS_TILE_STEP = TETRIS_TILE_SIZE + TETRIS_TILE_GAP;
constexpr byte TETRIS_BOARD_PIXEL_WIDTH = TETRIS_BOARD_WIDTH * TETRIS_TILE_STEP - 1;
constexpr byte TETRIS_BOARD_PIXEL_HEIGHT = TETRIS_BOARD_HEIGHT * TETRIS_TILE_STEP - 1;
constexpr byte TETRIS_MARGIN_LEFT = 3;
constexpr byte TETRIS_MARGIN_TOP = 19;
constexpr byte TETRIS_HEADER_LINE_Y = 15;
constexpr byte TETRIS_SCORE_X = 7;
constexpr byte TETRIS_SCORE_Y = 4;
constexpr byte TETRIS_NEXT_LABEL_X = 40;
constexpr byte TETRIS_NEXT_LABEL_Y = 4;
constexpr byte TETRIS_NEXT_PIECE_X = 45;
constexpr byte TETRIS_NEXT_PIECE_Y = 4;
constexpr byte TETRIS_TYPES = 6;
constexpr uint16_t TETRIS_INITIAL_DROP_MS = 420;
constexpr uint16_t TETRIS_FAST_DROP_MS = 60;
constexpr uint16_t TETRIS_MIN_DROP_MS = 120;

const int8_t tetrisPiecesSL[2][2][4] = {
  {{0, 0, 1, 1}, {0, 1, 1, 2}},
  {{0, 1, 1, 2}, {1, 1, 0, 0}}
};

const int8_t tetrisPiecesSR[2][2][4] = {
  {{1, 1, 0, 0}, {0, 1, 1, 2}},
  {{0, 1, 1, 2}, {0, 0, 1, 1}}
};

const int8_t tetrisPiecesLL[4][2][4] = {
  {{0, 0, 0, 1}, {0, 1, 2, 2}},
  {{0, 1, 2, 2}, {1, 1, 1, 0}},
  {{0, 1, 1, 1}, {0, 0, 1, 2}},
  {{0, 0, 1, 2}, {1, 0, 0, 0}}
};

const int8_t tetrisPiecesSquare[1][2][4] = {
  {{0, 1, 0, 1}, {0, 0, 1, 1}}
};

const int8_t tetrisPiecesT[4][2][4] = {
  {{0, 0, 1, 0}, {0, 1, 1, 2}},
  {{0, 1, 1, 2}, {1, 0, 1, 1}},
  {{1, 0, 1, 1}, {0, 1, 1, 2}},
  {{0, 1, 1, 2}, {0, 0, 1, 0}}
};

const int8_t tetrisPiecesI[2][2][4] = {
  {{0, 1, 2, 3}, {0, 0, 0, 0}},
  {{0, 0, 0, 0}, {0, 1, 2, 3}}
};

bool tetrisGrid[TETRIS_BOARD_WIDTH][TETRIS_BOARD_HEIGHT];
int8_t tetrisPiece[2][4];
int8_t tetrisCurrentType = 0;
int8_t tetrisNextType = 0;
int8_t tetrisRotation = 0;
int8_t tetrisPieceX = 0;
int8_t tetrisPieceY = 0;
uint16_t tetrisScore = 0;
uint16_t tetrisDropInterval = TETRIS_INITIAL_DROP_MS;
unsigned long tetrisDropTime = 0;
bool tetrisAwaitRestart = false;
bool tetrisBackReadyForExit = true;

constexpr int PONG_PADDLE_HEIGHT = 16;
constexpr int PONG_PADDLE_WIDTH = 3;
constexpr int PONG_PLAYER_PADDLE_SPEED = 4;
constexpr int PONG_CPU_PADDLE_SPEED = 3;
constexpr int PONG_BALL_SIZE = 3;
constexpr float PONG_BALL_SPEED_X = 2.0f;
constexpr float PONG_BALL_SPEED_Y = 1.5f;
constexpr uint16_t PONG_MAX_CPU_SCORE = 3;
constexpr float PONG_MAX_BALL_SPEED_Y = 3.2f;

int pongPlayerY = SCREEN_HEIGHT / 2 - PONG_PADDLE_HEIGHT / 2;
int pongCpuY = SCREEN_HEIGHT / 2 - PONG_PADDLE_HEIGHT / 2;
float pongBallX = SCREEN_WIDTH / 2.0f;
float pongBallY = SCREEN_HEIGHT / 2.0f;
float pongVelocityX = PONG_BALL_SPEED_X;
float pongVelocityY = PONG_BALL_SPEED_Y;
unsigned long pongFrameTime = 0;
bool pongAwaitRestart = false;
uint16_t pongPlayerScore = 0;
uint16_t pongCpuScore = 0;

void tetrisSetDisplayRotation(bool enabled) {
  display.setRotation(enabled ? 3 : 0);
}

void renderGameOverScreen(const char *restartText = "OK to RESTART") {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);

  int16_t x1, y1;
  uint16_t w, h;

  display.setTextSize(2);
  display.getTextBounds("Game Over", 0, 0, &x1, &y1, &w, &h);
  display.setCursor((display.width() - w) / 2, max<int>(0, (display.height() / 2) - 16));
  display.print("Game Over");

  display.setTextSize(1);
  display.getTextBounds(restartText, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((display.width() - w) / 2, min<int>(display.height() - 10, (display.height() / 2) + 10));
  display.print(restartText);
  display.display();
}

void snakeInitializeBody() {
  for (uint16_t i = 0; i < snakeLength; i++) {
    snakePosX[i] = snakeHeadX - i;
    snakePosY[i] = snakeHeadY;
  }
}

void snakeSpawnApple() {
  bool valid = false;
  while (!valid) {
    valid = true;
    snakeAppleX = random(max<int>(SNAKE_SCORE_SAFE_X, SNAKE_WALL_MIN_X), SNAKE_WALL_MAX_X + 1);
    snakeAppleY = random(max<int>(SNAKE_SCORE_SAFE_Y, SNAKE_WALL_MIN_Y), SNAKE_WALL_MAX_Y + 1);

    if (snakeAppleX == snakeHeadX && snakeAppleY == snakeHeadY) {
      valid = false;
      continue;
    }

    for (uint16_t i = 0; i < snakeLength; i++) {
      if (snakeAppleX == snakePosX[i] && snakeAppleY == snakePosY[i]) {
        valid = false;
        break;
      }
    }
  }
}

void snakeReset() {
  snakeHeadX = SNAKE_INITIAL_X;
  snakeHeadY = SNAKE_INITIAL_Y;
  snakeDirX = 1;
  snakeDirY = 0;
  snakeLength = 2;
  snakeScore = 0;
  snakeTilesPerSecond = 8.0f;
  snakeFrameTime = millis();
  snakeAwaitRestart = false;
  snakeInitializeBody();
  snakeSpawnApple();
}

void snakeResetInputStates() {
  snakeUpWasPressed = false;
  snakeDownWasPressed = false;
  snakeOkWasPressed = false;
  snakeBackWasPressed = false;
}

bool snakeButtonPressOnce(uint8_t pin, bool &wasPressed) {
  const bool pressed = digitalRead(pin) == LOW;
  if (!pressed) {
    wasPressed = false;
    return false;
  }

  if (wasPressed) {
    return false;
  }

  wasPressed = true;
  return true;
}

void snakeReadInput(bool &upPress, bool &downPress, bool &okPress, bool &backPress) {
  upPress = snakeButtonPressOnce(BUTTON_UP, snakeUpWasPressed);
  downPress = snakeButtonPressOnce(BUTTON_DOWN, snakeDownWasPressed);
  okPress = snakeButtonPressOnce(BUTTON_OK, snakeOkWasPressed);
  backPress = snakeButtonPressOnce(BUTTON_BACK, snakeBackWasPressed);
}

void snakeRenderScore() {
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(4, 4);
  display.print(snakeScore);
}

void snakeRenderFrame() {
  display.clearDisplay();
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SH110X_BLACK);
  display.drawRect(1, 1, SCREEN_WIDTH - 2, SCREEN_HEIGHT - 2, SH110X_WHITE);

  for (uint16_t i = 0; i < snakeLength; i++) {
    display.drawRect(snakePosX[i] * SNAKE_TILE_SIZE, snakePosY[i] * SNAKE_TILE_SIZE,
                     SNAKE_TILE_SIZE, SNAKE_TILE_SIZE, SH110X_WHITE);
  }

  display.fillRect(snakeAppleX * SNAKE_TILE_SIZE, snakeAppleY * SNAKE_TILE_SIZE,
                   SNAKE_TILE_SIZE, SNAKE_TILE_SIZE, SH110X_WHITE);
  snakeRenderScore();
  display.display();
}

void snakeGameOver() {
  for (byte blink = 0; blink < 3; blink++) {
    snakeRenderFrame();
    delay(180);

    display.clearDisplay();
    display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SH110X_BLACK);
    display.drawRect(1, 1, SCREEN_WIDTH - 2, SCREEN_HEIGHT - 2, SH110X_WHITE);
    display.fillRect(snakeAppleX * SNAKE_TILE_SIZE, snakeAppleY * SNAKE_TILE_SIZE,
                     SNAKE_TILE_SIZE, SNAKE_TILE_SIZE, SH110X_WHITE);
    snakeRenderScore();
    display.display();
    delay(180);
  }

  snakeAwaitRestart = true;
  renderGameOverScreen();
}

void snakeStart() {
  randomSeed(millis());
  snakeResetInputStates();
  snakeReset();
  gamesState = GAMES_SNAKE_STATE;
  snakeRenderFrame();
}

void snakeHandleDirection(bool upClick, bool downClick, bool okClick, bool backClick) {
  if (upClick && snakeDirY != 1) {
    snakeDirX = 0;
    snakeDirY = -1;
  } else if (downClick && snakeDirY != -1) {
    snakeDirX = 0;
    snakeDirY = 1;
  } else if (backClick && snakeDirX != -1) {
    snakeDirX = 1;
    snakeDirY = 0;
  } else if (okClick && snakeDirX != 1) {
    snakeDirX = -1;
    snakeDirY = 0;
  }
}

void snakeHandleTick(bool upClick, bool downClick, bool okClick, bool backClick) {
  if (snakeAwaitRestart) {
    if (okClick) {
      snakeReset();
      snakeRenderFrame();
    }
    return;
  }

  snakeHandleDirection(upClick, downClick, okClick, backClick);

  const unsigned long frameInterval = static_cast<unsigned long>(1000.0f / snakeTilesPerSecond);
  const unsigned long now = millis();
  if (now - snakeFrameTime < frameInterval) {
    return;
  }

  snakeFrameTime = now;
  int nextX = static_cast<int>(snakeHeadX) + snakeDirX;
  int nextY = static_cast<int>(snakeHeadY) + snakeDirY;

  if (nextX < SNAKE_WALL_MIN_X || nextX > SNAKE_WALL_MAX_X || nextY < SNAKE_WALL_MIN_Y || nextY > SNAKE_WALL_MAX_Y) {
    snakeGameOver();
    return;
  }

  const bool ateApple = nextX == snakeAppleX && nextY == snakeAppleY;
  if (ateApple && snakeLength < SNAKE_MAX_LENGTH - 1) {
    snakeLength++;
  }

  for (uint16_t i = snakeLength - 1; i > 0; i--) {
    snakePosX[i] = snakePosX[i - 1];
    snakePosY[i] = snakePosY[i - 1];
  }

  snakeHeadX = static_cast<byte>(nextX);
  snakeHeadY = static_cast<byte>(nextY);
  snakePosX[0] = snakeHeadX;
  snakePosY[0] = snakeHeadY;

  if (ateApple) {
    snakeScore++;
    snakeTilesPerSecond += 0.4f;
    snakeSpawnApple();
  }

  for (uint16_t i = 1; i < snakeLength; i++) {
    if (snakeHeadX == snakePosX[i] && snakeHeadY == snakePosY[i]) {
      snakeGameOver();
      return;
    }
  }

  snakeRenderFrame();
}

int birdGeneratePipeHeight() {
  return random(10 * BIRD_SCALE_FACTOR,
                SCREEN_HEIGHT * BIRD_SCALE_FACTOR - BIRD_PIPE_GAP - 10 * BIRD_SCALE_FACTOR);
}

int birdFindFurthestPipe() {
  int maxX = 0;
  for (int i = 0; i < BIRD_MAX_PIPES; i++) {
    if (birdPipes[i].x > maxX) {
      maxX = birdPipes[i].x;
    }
  }
  return maxX;
}

void birdReset() {
  birdY = SCREEN_HEIGHT * BIRD_SCALE_FACTOR / 5;
  birdVelocity = 0;
  birdScore = 0;
  birdFrameCounter = 0;
  birdFrameTime = millis();
  birdAwaitRestart = false;
  birdBackReadyForExit = true;

  birdPipes[0].x = SCREEN_WIDTH * BIRD_SCALE_FACTOR;
  birdPipes[0].height = birdGeneratePipeHeight();

  for (int i = 1; i < BIRD_MAX_PIPES; i++) {
    birdPipes[i].x = SCREEN_WIDTH * BIRD_SCALE_FACTOR +
                     i * BIRD_PIPE_SPACING +
                     random(-20, 21) * BIRD_SCALE_FACTOR;
    birdPipes[i].height = birdGeneratePipeHeight();
  }
}

void birdDrawBird() {
  const int birdFrame = (birdFrameCounter / 5) % 2;
  display.drawBitmap(15 - BIRD_WIDTH_PX / 2,
                     birdY / BIRD_SCALE_FACTOR - BIRD_HEIGHT_PX / 2,
                     birdBitmaps[birdFrame], BIRD_WIDTH_PX, BIRD_HEIGHT_PX, SH110X_WHITE);
}

void birdDrawPipes() {
  for (int i = 0; i < BIRD_MAX_PIPES; i++) {
    const int pipeX = birdPipes[i].x / BIRD_SCALE_FACTOR;
    const int upperPipeHeight = birdPipes[i].height / BIRD_SCALE_FACTOR;
    const int gapHeight = BIRD_PIPE_GAP / BIRD_SCALE_FACTOR;
    const int lowerPipeY = upperPipeHeight + gapHeight;
    const int lowerPipeHeight = SCREEN_HEIGHT - lowerPipeY;
    const int pipeWidth = BIRD_PIPE_WIDTH / BIRD_SCALE_FACTOR;

    display.fillRect(pipeX, 0, pipeWidth, upperPipeHeight - 6, SH110X_WHITE);
    display.fillRect(pipeX - 2, upperPipeHeight - 6, pipeWidth + 4, 6, SH110X_WHITE);
    display.fillRect(pipeX - 2, lowerPipeY, pipeWidth + 4, 6, SH110X_WHITE);
    display.fillRect(pipeX, lowerPipeY + 6, pipeWidth, lowerPipeHeight - 6, SH110X_WHITE);
  }
}

void birdDrawScore() {
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);
  display.setCursor(3, 3);
  display.print(birdScore);
}

void birdRenderFrame(bool showBird = true) {
  display.clearDisplay();
  if (showBird) {
    birdDrawBird();
  }
  birdDrawPipes();
  birdDrawScore();
  display.display();
}

void birdGameOver() {
  for (byte blink = 0; blink < 3; blink++) {
    birdRenderFrame(true);
    delay(180);
    birdRenderFrame(false);
    delay(180);
  }
  birdAwaitRestart = true;
  birdBackReadyForExit = false;
  buttonBack.resetStates();
  renderGameOverScreen();
}

void birdMovePipes() {
  const int currentSpeed = BIRD_PIPE_SPEED + birdScore;

  for (int i = 0; i < BIRD_MAX_PIPES; i++) {
    birdPipes[i].x -= currentSpeed;

    if (birdPipes[i].x < -BIRD_PIPE_WIDTH) {
      birdPipes[i].x = birdFindFurthestPipe() + BIRD_PIPE_SPACING + random(-20, 21) * BIRD_SCALE_FACTOR;
      birdPipes[i].height = birdGeneratePipeHeight();
      birdScore++;
    }
  }
}

void birdCheckCollision() {
  for (int i = 0; i < BIRD_MAX_PIPES; i++) {
    if (birdPipes[i].x < 17 * BIRD_SCALE_FACTOR &&
        birdPipes[i].x + BIRD_PIPE_WIDTH > 13 * BIRD_SCALE_FACTOR) {
      if (birdY - 2 * BIRD_SCALE_FACTOR < birdPipes[i].height ||
          birdY + 2 * BIRD_SCALE_FACTOR > birdPipes[i].height + BIRD_PIPE_GAP) {
        birdGameOver();
        return;
      }
    }
  }

  if (birdY >= SCREEN_HEIGHT * BIRD_SCALE_FACTOR) {
    birdGameOver();
  }
}

void birdStart() {
  randomSeed(millis());
  birdReset();
  gamesState = GAMES_BIRD_STATE;
  birdRenderFrame();
}

void birdHandleTick(bool okClick) {
  if (birdAwaitRestart) {
    if (okClick) {
      birdReset();
      birdRenderFrame();
    }
    return;
  }

  const unsigned long now = millis();
  if (now - birdFrameTime < BIRD_FRAME_TIME) {
    return;
  }
  birdFrameTime = now;
  birdFrameCounter++;

  if (digitalRead(BUTTON_UP) == LOW || digitalRead(BUTTON_DOWN) == LOW) {
    birdVelocity = BIRD_JUMP_STRENGTH;
  }

  birdVelocity += BIRD_GRAVITY;
  birdVelocity = constrain(birdVelocity, -50, 50);
  birdY += birdVelocity;

  if (birdY < 0) {
    birdY = 0;
  }

  birdMovePipes();
  birdCheckCollision();

  if (birdAwaitRestart) {
    return;
  }

  birdRenderFrame();
}

void tetrisCopyPiece(int8_t target[2][4], int8_t type, int8_t rotation) {
  switch (type) {
    case 0:
      for (byte i = 0; i < 4; i++) {
        target[0][i] = tetrisPiecesLL[rotation][0][i];
        target[1][i] = tetrisPiecesLL[rotation][1][i];
      }
      break;
    case 1:
      for (byte i = 0; i < 4; i++) {
        target[0][i] = tetrisPiecesSL[rotation][0][i];
        target[1][i] = tetrisPiecesSL[rotation][1][i];
      }
      break;
    case 2:
      for (byte i = 0; i < 4; i++) {
        target[0][i] = tetrisPiecesSR[rotation][0][i];
        target[1][i] = tetrisPiecesSR[rotation][1][i];
      }
      break;
    case 3:
      for (byte i = 0; i < 4; i++) {
        target[0][i] = tetrisPiecesSquare[0][0][i];
        target[1][i] = tetrisPiecesSquare[0][1][i];
      }
      break;
    case 4:
      for (byte i = 0; i < 4; i++) {
        target[0][i] = tetrisPiecesT[rotation][0][i];
        target[1][i] = tetrisPiecesT[rotation][1][i];
      }
      break;
    case 5:
      for (byte i = 0; i < 4; i++) {
        target[0][i] = tetrisPiecesI[rotation][0][i];
        target[1][i] = tetrisPiecesI[rotation][1][i];
      }
      break;
  }
}

int8_t tetrisGetMaxRotation(int8_t type) {
  if (type == 1 || type == 2 || type == 5) {
    return 2;
  }
  if (type == 0 || type == 4) {
    return 4;
  }
  if (type == 3) {
    return 1;
  }
  return 0;
}

bool tetrisPieceCollides(const int8_t target[2][4], int8_t offsetX, int8_t offsetY) {
  for (byte i = 0; i < 4; i++) {
    const int boardX = offsetX + target[0][i];
    const int boardY = offsetY + target[1][i];
    if (boardX < 0 || boardX >= TETRIS_BOARD_WIDTH || boardY < 0 || boardY >= TETRIS_BOARD_HEIGHT) {
      return true;
    }
    if (tetrisGrid[boardX][boardY]) {
      return true;
    }
  }
  return false;
}

void tetrisDrawCell(int boardX, int boardY, bool filled) {
  if (!filled) {
    return;
  }
  const int pixelX = TETRIS_MARGIN_LEFT + boardX * TETRIS_TILE_STEP;
  const int pixelY = TETRIS_MARGIN_TOP + boardY * TETRIS_TILE_STEP;
  display.fillRect(pixelX, pixelY, TETRIS_TILE_SIZE, TETRIS_TILE_SIZE, SH110X_WHITE);
}

void tetrisDrawActivePiece() {
  for (byte i = 0; i < 4; i++) {
    tetrisDrawCell(tetrisPieceX + tetrisPiece[0][i], tetrisPieceY + tetrisPiece[1][i], true);
  }
}

void tetrisDrawNextPiece() {
  int8_t preview[2][4];
  tetrisCopyPiece(preview, tetrisNextType, 0);

  for (byte i = 0; i < 4; i++) {
    const int pixelX = TETRIS_NEXT_PIECE_X + preview[0][i] * 3;
    const int pixelY = TETRIS_NEXT_PIECE_Y + preview[1][i] * 3;
    display.fillRect(pixelX, pixelY, 3, 3, SH110X_WHITE);
  }
}

void tetrisRenderScore() {
  display.setTextSize(1);
  display.setCursor(TETRIS_SCORE_X, TETRIS_SCORE_Y);
  display.print(tetrisScore);
}

void tetrisRenderFrame() {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);
  display.drawLine(0, TETRIS_HEADER_LINE_Y, display.width(), TETRIS_HEADER_LINE_Y, SH110X_WHITE);
  display.drawRect(0, 0, display.width(), display.height(), SH110X_WHITE);
  display.drawRect(TETRIS_MARGIN_LEFT - 1, TETRIS_MARGIN_TOP - 1,
                   TETRIS_BOARD_PIXEL_WIDTH + 1, TETRIS_BOARD_PIXEL_HEIGHT + 2, SH110X_WHITE);

  for (byte x = 0; x < TETRIS_BOARD_WIDTH; x++) {
    for (byte y = 0; y < TETRIS_BOARD_HEIGHT; y++) {
      tetrisDrawCell(x, y, tetrisGrid[x][y]);
    }
  }

  tetrisDrawActivePiece();
  tetrisDrawNextPiece();
  tetrisRenderScore();
  display.display();
}

void tetrisResetGrid() {
  for (byte x = 0; x < TETRIS_BOARD_WIDTH; x++) {
    for (byte y = 0; y < TETRIS_BOARD_HEIGHT; y++) {
      tetrisGrid[x][y] = false;
    }
  }
}

void tetrisGameOver() {
  tetrisAwaitRestart = true;
  tetrisBackReadyForExit = false;
  buttonBack.resetStates();
  tetrisSetDisplayRotation(false);
  renderGameOverScreen("OK to RESTART");
}

bool tetrisSpawnPiece() {
  tetrisCurrentType = tetrisNextType;
  tetrisNextType = random(TETRIS_TYPES);
  tetrisRotation = 0;
  tetrisPieceY = 0;
  tetrisPieceX = tetrisCurrentType == 5 ? 3 : 4;
  tetrisCopyPiece(tetrisPiece, tetrisCurrentType, tetrisRotation);

  if (tetrisPieceCollides(tetrisPiece, tetrisPieceX, tetrisPieceY)) {
    tetrisGameOver();
    return false;
  }
  return true;
}

void tetrisReset() {
  tetrisResetGrid();
  tetrisScore = 0;
  tetrisDropInterval = TETRIS_INITIAL_DROP_MS;
  tetrisDropTime = millis();
  tetrisAwaitRestart = false;
  tetrisBackReadyForExit = true;
  tetrisNextType = random(TETRIS_TYPES);
  tetrisSpawnPiece();
}

void tetrisStart() {
  randomSeed(millis());
  tetrisSetDisplayRotation(true);
  tetrisReset();
  gamesState = GAMES_TETRIS_STATE;
  tetrisRenderFrame();
}

void tetrisLockPiece() {
  for (byte i = 0; i < 4; i++) {
    const int boardX = tetrisPieceX + tetrisPiece[0][i];
    const int boardY = tetrisPieceY + tetrisPiece[1][i];
    if (boardX >= 0 && boardX < TETRIS_BOARD_WIDTH && boardY >= 0 && boardY < TETRIS_BOARD_HEIGHT) {
      tetrisGrid[boardX][boardY] = true;
    }
  }
}

void tetrisClearLine(byte line) {
  for (int y = line; y > 0; y--) {
    for (byte x = 0; x < TETRIS_BOARD_WIDTH; x++) {
      tetrisGrid[x][y] = tetrisGrid[x][y - 1];
    }
  }
  for (byte x = 0; x < TETRIS_BOARD_WIDTH; x++) {
    tetrisGrid[x][0] = false;
  }
}

void tetrisCheckLines() {
  byte cleared = 0;
  for (int y = TETRIS_BOARD_HEIGHT - 1; y >= 0; y--) {
    bool full = true;
    for (byte x = 0; x < TETRIS_BOARD_WIDTH; x++) {
      full = full && tetrisGrid[x][y];
    }

    if (full) {
      tetrisClearLine(static_cast<byte>(y));
      cleared++;
      y++;
    }
  }

  if (cleared == 0) {
    return;
  }

  tetrisScore += cleared * 10;
  if (tetrisDropInterval > TETRIS_MIN_DROP_MS) {
    const uint16_t reduction = cleared * 12;
    tetrisDropInterval = reduction >= (tetrisDropInterval - TETRIS_MIN_DROP_MS)
      ? TETRIS_MIN_DROP_MS
      : static_cast<uint16_t>(tetrisDropInterval - reduction);
  }

  display.invertDisplay(true);
  delay(45);
  display.invertDisplay(false);
}

void tetrisTryRotate() {
  const int8_t maxRotation = tetrisGetMaxRotation(tetrisCurrentType);
  if (maxRotation <= 1) {
    return;
  }

  const int8_t nextRotation = (tetrisRotation + 1) % maxRotation;
  int8_t rotated[2][4];
  tetrisCopyPiece(rotated, tetrisCurrentType, nextRotation);

  if (!tetrisPieceCollides(rotated, tetrisPieceX, tetrisPieceY)) {
    tetrisRotation = nextRotation;
    tetrisCopyPiece(tetrisPiece, tetrisCurrentType, tetrisRotation);
    return;
  }

  if (!tetrisPieceCollides(rotated, tetrisPieceX - 1, tetrisPieceY)) {
    tetrisPieceX--;
    tetrisRotation = nextRotation;
    tetrisCopyPiece(tetrisPiece, tetrisCurrentType, tetrisRotation);
    return;
  }

  if (!tetrisPieceCollides(rotated, tetrisPieceX + 1, tetrisPieceY)) {
    tetrisPieceX++;
    tetrisRotation = nextRotation;
    tetrisCopyPiece(tetrisPiece, tetrisCurrentType, tetrisRotation);
  }
}

void tetrisMoveHorizontal(int8_t delta) {
  if (!tetrisPieceCollides(tetrisPiece, tetrisPieceX + delta, tetrisPieceY)) {
    tetrisPieceX += delta;
  }
}

void tetrisStepDown() {
  if (!tetrisPieceCollides(tetrisPiece, tetrisPieceX, tetrisPieceY + 1)) {
    tetrisPieceY++;
    return;
  }

  tetrisLockPiece();
  tetrisCheckLines();
  tetrisSpawnPiece();
}

void tetrisHandleTick(bool leftPress, bool rightPress, bool rotateClick, bool softDropPress, bool restartClick) {
  if (tetrisAwaitRestart) {
    if (restartClick) {
      tetrisSetDisplayRotation(true);
      tetrisReset();
      tetrisRenderFrame();
    }
    return;
  }

  if (leftPress) {
    tetrisMoveHorizontal(-1);
  }
  if (rightPress) {
    tetrisMoveHorizontal(1);
  }
  if (rotateClick) {
    tetrisTryRotate();
  }

  const unsigned long now = millis();
  const unsigned long dropInterval = softDropPress ? TETRIS_FAST_DROP_MS : tetrisDropInterval;
  if (now - tetrisDropTime >= dropInterval) {
    tetrisDropTime = now;
    tetrisStepDown();
  }

  if (!tetrisAwaitRestart) {
    tetrisRenderFrame();
  }
}

void pongResetBall(int direction) {
  pongBallX = SCREEN_WIDTH / 2.0f;
  pongBallY = SCREEN_HEIGHT / 2.0f;
  pongVelocityX = direction >= 0 ? PONG_BALL_SPEED_X : -PONG_BALL_SPEED_X;
  pongVelocityY = random(0, 2) == 0 ? PONG_BALL_SPEED_Y : -PONG_BALL_SPEED_Y;
}

void pongReset() {
  pongPlayerY = SCREEN_HEIGHT / 2 - PONG_PADDLE_HEIGHT / 2;
  pongCpuY = SCREEN_HEIGHT / 2 - PONG_PADDLE_HEIGHT / 2;
  pongResetBall(random(0, 2) == 0 ? -1 : 1);
  pongFrameTime = millis();
  pongAwaitRestart = false;
  pongPlayerScore = 0;
  pongCpuScore = 0;
}

void pongRenderScore() {
  display.setTextColor(1);
  display.setTextSize(1);
  display.setTextWrap(false);
  display.setCursor(44, 2);
  display.print(pongPlayerScore);
  display.setCursor(76, 2);
  display.print(pongCpuScore);
}

void pongRenderFrame() {
  display.clearDisplay();
  display.drawFastVLine(SCREEN_WIDTH / 2, 0, SCREEN_HEIGHT, SH110X_WHITE);
  display.fillRect(0, pongPlayerY, PONG_PADDLE_WIDTH, PONG_PADDLE_HEIGHT, SH110X_WHITE);
  display.fillRect(SCREEN_WIDTH - PONG_PADDLE_WIDTH, pongCpuY, PONG_PADDLE_WIDTH, PONG_PADDLE_HEIGHT, SH110X_WHITE);
  display.fillRect(static_cast<int>(pongBallX), static_cast<int>(pongBallY), PONG_BALL_SIZE, PONG_BALL_SIZE, SH110X_WHITE);
  pongRenderScore();
  display.display();
}

bool pongRectsIntersect(float leftA, float topA, float widthA, float heightA,
                        float leftB, float topB, float widthB, float heightB) {
  return leftA < leftB + widthB && leftA + widthA > leftB &&
         topA < topB + heightB && topA + heightA > topB;
}

void pongBounceFromPaddle(int paddleY, bool fromPlayer) {
  const float paddleCenter = paddleY + (PONG_PADDLE_HEIGHT / 2.0f);
  const float ballCenter = pongBallY + (PONG_BALL_SIZE / 2.0f);
  const float normalizedImpact = constrain((ballCenter - paddleCenter) / (PONG_PADDLE_HEIGHT / 2.0f), -1.0f, 1.0f);

  if (pongVelocityX < 0.0f) {
    pongVelocityX = -pongVelocityX;
  }
  if (!fromPlayer) {
    pongVelocityX = -pongVelocityX;
  }
  pongVelocityY = constrain(normalizedImpact * PONG_MAX_BALL_SPEED_Y, -PONG_MAX_BALL_SPEED_Y, PONG_MAX_BALL_SPEED_Y);

  if (pongVelocityY > -0.6f && pongVelocityY < 0.6f) {
    pongVelocityY = normalizedImpact >= 0.0f ? 0.6f : -0.6f;
  }

}

void pongGameOver() {
  renderGameOverScreen();
}

void pongStart() {
  randomSeed(millis());
  pongReset();
  gamesState = GAMES_PONG_STATE;
  pongRenderFrame();
}

void pongMoveCpu() {
  const int cpuCenter = pongCpuY + (PONG_PADDLE_HEIGHT / 2);
  const float ballCenter = pongBallY + (PONG_BALL_SIZE / 2.0f);

  if (ballCenter < cpuCenter && pongCpuY > 0) {
    pongCpuY--;
  } else if (ballCenter > cpuCenter && pongCpuY + PONG_PADDLE_HEIGHT < SCREEN_HEIGHT) {
    pongCpuY++;
  }

  if (random(2) == 0) {
    if (ballCenter > cpuCenter && pongCpuY + PONG_PADDLE_HEIGHT < SCREEN_HEIGHT) {
      pongCpuY++;
    } else if (ballCenter < cpuCenter && pongCpuY > 0) {
      pongCpuY--;
    }
  }

  pongCpuY = constrain(pongCpuY, 0, SCREEN_HEIGHT - PONG_PADDLE_HEIGHT);
}

void pongHandleTick(bool upPress, bool downPress, bool okClick) {
  if (pongAwaitRestart) {
    if (okClick) {
      pongReset();
      pongRenderFrame();
    }
    return;
  }

  if (upPress && pongPlayerY > 0) pongPlayerY -= PONG_PLAYER_PADDLE_SPEED;
  if (downPress && pongPlayerY + PONG_PADDLE_HEIGHT < SCREEN_HEIGHT) pongPlayerY += PONG_PLAYER_PADDLE_SPEED;
  pongPlayerY = constrain(pongPlayerY, 0, SCREEN_HEIGHT - PONG_PADDLE_HEIGHT);

  pongMoveCpu();

  const unsigned long now = millis();
  if (now - pongFrameTime < 10) {
    return;
  }
  pongFrameTime = now;

  pongBallX += pongVelocityX;
  pongBallY += pongVelocityY;

  if (pongBallY <= 0 || pongBallY + PONG_BALL_SIZE >= SCREEN_HEIGHT) {
    pongVelocityY *= -1;
    pongBallY = constrain(pongBallY, 0.0f, static_cast<float>(SCREEN_HEIGHT - PONG_BALL_SIZE));
  }

  if (pongVelocityX < 0.0f &&
      pongRectsIntersect(pongBallX, pongBallY, PONG_BALL_SIZE, PONG_BALL_SIZE,
                         0.0f, pongPlayerY, PONG_PADDLE_WIDTH, PONG_PADDLE_HEIGHT)) {
    pongBallX = PONG_PADDLE_WIDTH;
    pongBounceFromPaddle(pongPlayerY, true);
  }

  if (pongVelocityX > 0.0f &&
      pongRectsIntersect(pongBallX, pongBallY, PONG_BALL_SIZE, PONG_BALL_SIZE,
                         SCREEN_WIDTH - PONG_PADDLE_WIDTH, pongCpuY, PONG_PADDLE_WIDTH, PONG_PADDLE_HEIGHT)) {
    pongBallX = SCREEN_WIDTH - PONG_PADDLE_WIDTH - PONG_BALL_SIZE;
    pongBounceFromPaddle(pongCpuY, false);
  }

  if (pongBallX + PONG_BALL_SIZE < 0) {
    pongCpuScore++;
    if (pongCpuScore >= PONG_MAX_CPU_SCORE) {
      pongAwaitRestart = true;
      pongGameOver();
    } else {
      pongResetBall(1);
      pongRenderFrame();
    }
    return;
  }

  if (pongBallX > SCREEN_WIDTH) {
    pongPlayerScore++;
    pongResetBall(-1);
    pongRenderFrame();
    return;
  }

  pongRenderFrame();
}
}

static void renderSelectedGame() {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);
  display.setTextSize(2);
  display.setCursor(10, 8);
  display.print(gamesMenuItems[gamesMenuIndex]);
  display.setTextSize(1);
  display.setCursor(10, 34);
  display.print("Coming soon");
  display.setCursor(10, 50);
  display.print("Back to return");
  display.display();
}

void handleGamesSubmenu() {
  buttonUp.tick();
  buttonDown.tick();
  buttonOK.tick();
  buttonBack.tick();

  static MenuButtonState upHeld;
  static MenuButtonState downHeld;
  static bool doomExitAwaitRelease = false;
  const bool upPress = isMenuButtonPress(BUTTON_UP, upHeld);
  const bool downPress = isMenuButtonPress(BUTTON_DOWN, downHeld);
  const bool okClick = buttonOK.isClick();
  const bool backClick = buttonBack.isClick();
  const bool okHold = buttonOK.isHold();
  const bool backHold = buttonBack.isHold();

  if (doomExitAwaitRelease) {
    if (digitalRead(BUTTON_OK) == LOW || digitalRead(BUTTON_BACK) == LOW) {
      return;
    }
    buttonOK.resetStates();
    buttonBack.resetStates();
    doomExitAwaitRelease = false;
    displayGamesMenu(display, gamesMenuIndex);
    return;
  }

  if (gamesState == GAMES_SNAKE_STATE) {
    if (snakeAwaitRestart && backClick) {
      snakeResetInputStates();
      gamesState = GAMES_MENU_STATE;
      displayGamesMenu(display, gamesMenuIndex);
      return;
    }

    bool snakeUpPress = false;
    bool snakeDownPress = false;
    bool snakeOkPress = false;
    bool snakeBackPress = false;
    snakeReadInput(snakeUpPress, snakeDownPress, snakeOkPress, snakeBackPress);
    snakeHandleTick(snakeUpPress, snakeDownPress, snakeOkPress, snakeBackPress);
    return;
  }

  if (gamesState == GAMES_BIRD_STATE) {
    if (birdAwaitRestart && !birdBackReadyForExit) {
      if (digitalRead(BUTTON_BACK) != LOW) {
        buttonBack.resetStates();
        birdBackReadyForExit = true;
      }
      birdHandleTick(okClick);
      return;
    }

    if (birdAwaitRestart && backClick) {
      gamesState = GAMES_MENU_STATE;
      displayGamesMenu(display, gamesMenuIndex);
      return;
    }

    birdHandleTick(okClick);
    return;
  }

  if (gamesState == GAMES_TETRIS_STATE) {
    static bool exitHoldLatch = false;
    if (tetrisAwaitRestart && backClick) {
      tetrisSetDisplayRotation(false);
      gamesState = GAMES_MENU_STATE;
      displayGamesMenu(display, gamesMenuIndex);
      return;
    }

    if (tetrisAwaitRestart && !tetrisBackReadyForExit) {
      if (digitalRead(BUTTON_BACK) != LOW) {
        buttonBack.resetStates();
        tetrisBackReadyForExit = true;
      }
    }

    const bool exitHold = tetrisAwaitRestart
      ? false
      : (backHold && okHold);

    if (exitHold && !exitHoldLatch) {
      exitHoldLatch = true;
      tetrisSetDisplayRotation(false);
      gamesState = GAMES_MENU_STATE;
      displayGamesMenu(display, gamesMenuIndex);
      return;
    }
    if (!exitHold) {
      exitHoldLatch = false;
    }

    tetrisHandleTick(downPress, upPress, okClick, digitalRead(BUTTON_BACK) == LOW, okClick);
    return;
  }

  if (gamesState == GAMES_PONG_STATE) {
    static bool backHoldLatch = false;

    if (pongAwaitRestart && backClick) {
      gamesState = GAMES_MENU_STATE;
      displayGamesMenu(display, gamesMenuIndex);
      return;
    }

    if (!pongAwaitRestart && backHold && !backHoldLatch) {
      backHoldLatch = true;
      gamesState = GAMES_MENU_STATE;
      displayGamesMenu(display, gamesMenuIndex);
      return;
    }
    if (!backHold || pongAwaitRestart) {
      backHoldLatch = false;
    }

    pongHandleTick(upPress, downPress, okClick);
    return;
  }

  if (gamesState == GAMES_DOOM_STATE) {
    static bool exitHoldLatch = false;
    const bool exitHold = okHold && backHold;
    if (exitHold && !exitHoldLatch) {
      exitHoldLatch = true;
      Doom::stop();
      doomExitAwaitRelease = true;
      return;
    }
    if (!exitHold) {
      exitHoldLatch = false;
    }

    Doom::tick();
    return;
  }

  if (gamesState == GAMES_PLACEHOLDER_STATE) {
    if (okClick || backClick) {
      gamesState = GAMES_MENU_STATE;
      displayGamesMenu(display, gamesMenuIndex);
    }
    return;
  }

  if (upPress) {
    const byte previousIndex = gamesMenuIndex;
    gamesMenuIndex = (gamesMenuIndex + GAMES_MENU_ITEM_COUNT - 1) % GAMES_MENU_ITEM_COUNT;
    displayGamesMenu(display, gamesMenuIndex, previousIndex);
  }
  if (downPress) {
    const byte previousIndex = gamesMenuIndex;
    gamesMenuIndex = (gamesMenuIndex + 1) % GAMES_MENU_ITEM_COUNT;
    displayGamesMenu(display, gamesMenuIndex, previousIndex);
  }
  if (okClick) {
    if (gamesMenuIndex == 0) {
      snakeStart();
    } else if (gamesMenuIndex == 1) {
      birdStart();
    } else if (gamesMenuIndex == 2) {
      tetrisStart();
    } else if (gamesMenuIndex == 3) {
      pongStart();
    } else if (gamesMenuIndex == 4) {
      Doom::start();
    } else {
      gamesState = GAMES_PLACEHOLDER_STATE;
      renderSelectedGame();
    }
  }
  if (backClick) {
    gamesState = GAMES_MENU_STATE;
    returnToMainMenu();
  }
}
