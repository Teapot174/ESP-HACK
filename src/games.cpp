#include "display.h"
#include <GyverButton.h>
#include "menu/games.h"

extern DisplayType display;
extern GButton buttonUp;
extern GButton buttonDown;
extern GButton buttonOK;
extern GButton buttonBack;
extern byte gamesMenuIndex;

enum GamesState : byte {
  GAMES_MENU_STATE,
  GAMES_PLACEHOLDER_STATE,
  GAMES_SNAKE_STATE,
  GAMES_BIRD_STATE,
  GAMES_TETRIS_STATE,
  GAMES_PONG_STATE
};

static GamesState gamesState = GAMES_MENU_STATE;

namespace {
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
  const bool upPress = isMenuButtonPress(BUTTON_UP, upHeld);
  const bool downPress = isMenuButtonPress(BUTTON_DOWN, downHeld);
  const bool upClick = buttonUp.isClick();
  const bool downClick = buttonDown.isClick();
  const bool okClick = buttonOK.isClick();
  const bool backClick = buttonBack.isClick();
  const bool okHold = buttonOK.isHold();
  const bool backHold = buttonBack.isHold();
  if (gamesState == GAMES_SNAKE_STATE) {
    if (backHold) {
      gamesState = GAMES_MENU_STATE;
      displayGamesMenu(display, gamesMenuIndex);
      return;
    }

    snakeHandleTick(upClick, downClick, okClick, backClick);
    return;
  }

  if (gamesState == GAMES_BIRD_STATE) {
    static bool backHoldLatch = false;

    if (backHold && !backHoldLatch) {
      backHoldLatch = true;
      gamesState = GAMES_MENU_STATE;
      displayGamesMenu(display, gamesMenuIndex);
      return;
    }
    if (!backHold) {
      backHoldLatch = false;
    }

    birdHandleTick(okClick);
    return;
  }

  if (gamesState == GAMES_TETRIS_STATE) {
    static bool exitHoldLatch = false;
    const bool exitHold = tetrisAwaitRestart ? backHold : (backHold && okHold);

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

    if (backHold && !backHoldLatch) {
      backHoldLatch = true;
      gamesState = GAMES_MENU_STATE;
      displayGamesMenu(display, gamesMenuIndex);
      return;
    }
    if (!backHold) {
      backHoldLatch = false;
    }

    pongHandleTick(upPress, downPress, okClick);
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
