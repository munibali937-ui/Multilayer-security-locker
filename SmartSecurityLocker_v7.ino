/*
 * ════════════════════════════════════════════════════════════════
 *  MULTI LAYER SAFE  —  Competition Edition  v7  FINAL
 *                   Designed by : MUNIB ALI
 * ════════════════════════════════════════════════════════════════
 *  WHAT'S NEW IN v7:
 *    ▸ Boot screen  "MULTI LAYER SAFE" + "   MUNIB ALI   "
 *    ▸ Boss window  RESTRICTED — only ST_NORM_PIN + ST_HARD_LOCK
 *    ▸ Boss PIN change needs CONFIRM step (no accidental change)
 *    ▸ Emergency factory reset — press * five times in hard lock
 *      then # to confirm → all PINs reset to defaults
 *    ▸ Session timer — "Open: MM:SS" while door is open
 *    ▸ Auto-relock warning at 2 min + force-close at 3 min
 *    ▸ PIN strength — WEAK / OK / GOOD / STRG live while typing
 *    ▸ Intruder log — wrong attempts stored in EEPROM
 *      after boss clears hard lock → intruder summary shown
 *    ▸ Idle screen alternates door status / Wrong+Opens stats
 *    ▸ Equalizer screensaver — random bars, bass drop, peak dots
 *    ▸ No RTC, no history browser — removed cleanly
 *    ▸ All strings in F() — minimum SRAM waste
 * ════════════════════════════════════════════════════════════════
 *  INSTALL LIBRARIES (Sketch → Manage Libraries):
 *    ▸ MFRC522           by GithubCommunity (miguelbalboa)
 *    ▸ LiquidCrystal_I2C by Frank de Brabander
 *    ▸ Keypad            by Mark Stanley & Alexander Brevig
 *  BUILT-IN: SPI · Wire · Servo · EEPROM
 *
 *  ┌──────────────────┬──────────────────────────────────────┐
 *  │  RFID MFRC522    │  RST=9  SS=10  SCK=13  MOSI=11       │
 *  │                  │  MISO=12        VCC=3.3V  (!)        │
 *  │  Keypad 3×4      │  Rows=2,3,4,5   Cols=6,7,8           │
 *  │  LCD I2C 16×2    │  SDA=A4  SCL=A5  addr=0x27           │
 *  │  Servo SG90      │  Signal=A0                           │
 *  │  Limit Switch    │  A2 → Switch NO → GND (INPUT_PULLUP) │
 *  │  Buzzer          │  A3                                  │
 *  └──────────────────┴──────────────────────────────────────┘
 *
 *  SECURITY FLOW (3 layers):
 *    16-digit PIN → RFID Card → Secret PIN → Servo 90°
 *    → 10s to physically open door → Use freely
 *    → Close door → Servo locks INSTANTLY → Logged
 *
 *  BOSS OVERRIDES:
 *    Boss RFID  {86:70:03:29}  → instant unlock  (any state)
 *    Boss PIN   123456         → instant unlock  (idle or locked)
 *
 *  PIN CHANGE:
 *    Hold # 5s              → change Normal PIN
 *    # then * within 1s     → change Secret PIN
 *    Boss card in BossMode  → change Boss PIN (needs confirm)
 *
 *  SELF-TEST: hold * + 0 for 3s from idle
 *  EMERGENCY RESET: press * five times in ST_HARD_LOCK → # confirm
 *
 *  DEFAULTS (magic 0xAD — written on very first boot):
 *    Normal PIN : 0000000000000000   (16 digits)
 *    Secret PIN : 1234               ( 4 digits)
 *    Boss   PIN : 123456             ( 6 digits)
 * ════════════════════════════════════════════════════════════════
 */

#include <SPI.h>
#include <Wire.h>
#include <MFRC522.h>
#include <Servo.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <EEPROM.h>

// ════════════════════════ PINS ════════════════════════════════
#define RFID_RST_PIN   9
#define RFID_SS_PIN   10
#define SERVO_PIN     A0
#define LIMIT_SW_PIN  A2
#define BUZZER_PIN    A3

// ════════════════════════ EEPROM ══════════════════════════════
#define EE_MAGIC      0xAD   // new magic → forces fresh defaults
#define EE_FLAG        0     //  1 B
#define EE_NORM_PIN    1     // 16 B
#define EE_SCRT_PIN   17     //  4 B
#define EE_BOSS_PIN   21     //  6 B
#define EE_SESS_CNT   27     //  1 B  session counter
#define EE_WRNG_CNT   28     //  1 B  wrong-attempt total
#define EE_INTR_HD    29     //  1 B  intruder-log head
#define EE_INTR_CNT   30     //  1 B  intruder-log count
#define EE_INTR_BASE  31     // 20×3 = 60 B  (ends 90)
#define MAX_INTR      20
#define INTR_SZ        3
/*  Intruder entry 3 bytes:
 *    [0]=stage (0=PIN 1=RFID 2=SECRET)
 *    [1]=wrong count at event  [2]=session count at event
 */

// ════════════════════════ SIZES ═══════════════════════════════
#define NORM_LEN  16
#define SCRT_LEN   4
#define BOSS_LEN   6

// ════════════════════════ TIMING (ms) ════════════════════════
#define T_SECRET_WIN      10000UL
#define T_DOOR_WAIT       10000UL
#define T_ALARM_BUZZER    30000UL
#define T_HOLD_HASH        5000UL
#define T_HASH_COMBO       1000UL
#define T_RFID_COOL        1500UL
#define T_RFID_WATCH       5000UL
#define T_LOCKOUT_BASE     3000UL
#define T_IDLE_TIMEOUT    60000UL
#define T_SCREENSAVER    300000UL
#define T_SELFTEST_HOLD    3000UL
#define T_DOOR_OPEN_WARN 120000UL   // 2 min → flash warning
#define T_DOOR_OPEN_MAX  180000UL   // 3 min → force close
#define T_IDLE_ALT         3000UL   // idle line-2 alternation
#define MAX_WRONG              3
#define EMERGENCY_PRESSES      5    // * presses → emergency reset

// ════════════════════════ EQUALIZER DEFINES ══════════════════
#define EQ_COLS        16
#define EQ_MAX_H       16
#define EQ_FRAME_MS    45UL
#define EQ_PEAK_HOLD    8
#define EQ_DROP_INT    90

// ════════════════════════ KEYPAD ══════════════════════════════
const byte KP_ROWS = 4, KP_COLS = 3;
char keyMap[KP_ROWS][KP_COLS] = {
  {'1','2','3'},{'4','5','6'},{'7','8','9'},{'*','0','#'}
};
byte rowPins[KP_ROWS]   = {2,3,4,5};
byte colPins[KP_COLS]   = {6,7,8};
Keypad kpad = Keypad(makeKeymap(keyMap), rowPins, colPins, KP_ROWS, KP_COLS);

// ════════════════════════ HARDWARE ════════════════════════════
MFRC522           rfid(RFID_SS_PIN, RFID_RST_PIN);
Servo             lockServo;
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ════════════════════════ UIDs (Flash) ════════════════════════
const byte LOCKER_CARD_A[4] PROGMEM = {0xB1,0xDC,0x1D,0xAA};
const byte LOCKER_CARD_B[4] PROGMEM = {0xA3,0x11,0x42,0x16};
const byte BOSS_CARD_UID[4] PROGMEM = {0x86,0x70,0x03,0x29};

// ════════════════════════ BOOT STRINGS (Flash) ════════════════
const char BOOT_L1[] PROGMEM = "MULTI LAYER SAFE";
const char BOOT_L2[] PROGMEM = "   MUNIB ALI    ";

// ════════════════════════ STATES ═════════════════════════════
enum SysState : uint8_t {
  ST_NORM_PIN,     // waiting for 16-digit PIN
  ST_SCAN_RFID,    // waiting for RFID card
  ST_PROCESSING,   // 10-s secret-code window
  ST_DOOR_WAIT,    // servo open, 10 s to open door
  ST_DOOR_OPEN,    // door open, waiting for close
  ST_BOSS_MODE,    // boss card active
  ST_BOSS_NEW,     // entering new boss PIN
  ST_BOSS_CONFIRM, // confirming new boss PIN
  ST_HARD_LOCK,    // 3-strike lockout
  ST_LOCKOUT,      // timed cooldown
  ST_CN_OLD_PIN,   // change normal PIN — verify old normal
  ST_CN_OLD_SEC,   // change normal PIN — verify old secret
  ST_CN_NEW_PIN,   // change normal PIN — enter new
  ST_CS_NORM_PIN,  // change secret PIN — verify normal
  ST_CS_OLD_SEC,   // change secret PIN — verify old secret
  ST_CS_NEW_SEC,   // change secret PIN — enter new
  ST_CS_CONFIRM,   // change secret PIN — confirm new
  ST_SCREENSAVER   // equalizer screensaver
};
SysState sysState = ST_NORM_PIN;

// ════════════════════════ PIN STORAGE ═════════════════════════
char normPin[NORM_LEN + 1];
char scrtPin[SCRT_LEN + 1];
char bossPin[BOSS_LEN + 1];

// ════════════════════════ BUFFERS ════════════════════════════
char inputBuf[NORM_LEN + 2];
byte inputLen = 0;
char tempNew[BOSS_LEN + 2];   // large enough for boss too
char bossWindow[BOSS_LEN + 1];

// ════════════════════════ FLAGS ══════════════════════════════
volatile byte flags = 0;
#define FL_HASH         0x01
#define FL_STAR         0x02
#define FL_DOORPREV     0x04
#define FL_ZERO         0x08
#define FL_POSTSELFTEST 0x10
#define FL_EMERGENCY    0x20   // emergency confirm pending
#define getF(f)   (flags &   (f))
#define setF(f)   (flags |=  (f))
#define clrF(f)   (flags &= ~(byte)(f))

// ════════════════════════ COUNTERS ════════════════════════════
uint8_t wrongCount    = 0;   // consecutive wrongs in current attempt
uint8_t sessCount     = 0;   // session counter (opens since flash)
uint8_t wrongTotal    = 0;   // total wrongs since flash
uint8_t emergencyTaps = 0;   // * presses toward emergency reset

// ════════════════════════ TIMERS ══════════════════════════════
unsigned long t_secret       = 0;
unsigned long t_doorWait     = 0;
unsigned long t_doorOpenStart= 0;   // when door physically opened
unsigned long t_alarm        = 0;
unsigned long t_lockout      = 0;
unsigned long t_lockoutDur   = 0;
unsigned long t_rfidRead     = 0;
unsigned long t_rfidWatch    = 0;
unsigned long t_hashPress    = 0;
unsigned long t_starPress    = 0;
unsigned long t_zeroPress    = 0;
unsigned long t_lastKey      = 0;
unsigned long t_idleAlt      = 0;   // idle screen alternation

// ════════════════════════ EQUALIZER STATE ════════════════════
byte eqBarH[EQ_COLS];
byte eqPeakH[EQ_COLS];
byte eqPeakHold[EQ_COLS];
byte eqTargetH[EQ_COLS];
byte eqBeatTimer  = 0;
byte eqFrameCount = 0;
bool eqDropActive = false;
byte eqDropPhase  = 0;
byte eqDropHold   = 0;
unsigned long t_eqFrame = 0;

// EQ custom chars — bottom-up fill (row 7 = bottom of cell)
byte EQ_C0[8]={0b00000,0b00000,0b00000,0b00000,0b00000,0b00000,0b00000,0b00000};
byte EQ_C1[8]={0b00000,0b00000,0b00000,0b00000,0b00000,0b00000,0b11111,0b11111};
byte EQ_C2[8]={0b00000,0b00000,0b00000,0b00000,0b00000,0b11111,0b11111,0b11111};
byte EQ_C3[8]={0b00000,0b00000,0b00000,0b00000,0b11111,0b11111,0b11111,0b11111};
byte EQ_C4[8]={0b00000,0b00000,0b00000,0b11111,0b11111,0b11111,0b11111,0b11111};
byte EQ_C5[8]={0b00000,0b00000,0b11111,0b11111,0b11111,0b11111,0b11111,0b11111};
byte EQ_C6[8]={0b00000,0b11111,0b11111,0b11111,0b11111,0b11111,0b11111,0b11111};
byte EQ_C7[8]={0b11111,0b11111,0b11111,0b11111,0b11111,0b11111,0b11111,0b11111};

// ════════════════════════════════════════════════════════════════
//  EEPROM HELPERS
// ════════════════════════════════════════════════════════════════
void eeRead(int a, char* buf, byte len) {
  for (byte i=0;i<len;i++) buf[i]=EEPROM.read(a+i);
  buf[len]='\0';
}
void eeWrite(int a, const char* buf, byte len) {
  for (byte i=0;i<len;i++) EEPROM.update(a+i,buf[i]);
}

void loadPins() {
  if (EEPROM.read(EE_FLAG) != EE_MAGIC) {
    eeWrite(EE_NORM_PIN,"0000000000000000",NORM_LEN);
    eeWrite(EE_SCRT_PIN,"1234",SCRT_LEN);
    eeWrite(EE_BOSS_PIN,"123456",BOSS_LEN);
    EEPROM.write(EE_FLAG,    EE_MAGIC);
    EEPROM.write(EE_SESS_CNT,0);
    EEPROM.write(EE_WRNG_CNT,0);
    EEPROM.write(EE_INTR_HD, 0);
    EEPROM.write(EE_INTR_CNT,0);
  }
  eeRead(EE_NORM_PIN,normPin,NORM_LEN);
  eeRead(EE_SCRT_PIN,scrtPin,SCRT_LEN);
  eeRead(EE_BOSS_PIN,bossPin,BOSS_LEN);
  sessCount  = EEPROM.read(EE_SESS_CNT);
  wrongTotal = EEPROM.read(EE_WRNG_CNT);
}
void saveNormPin() { eeWrite(EE_NORM_PIN,normPin,NORM_LEN); }
void saveScrtPin() { eeWrite(EE_SCRT_PIN,scrtPin,SCRT_LEN); }
void saveBossPin() { eeWrite(EE_BOSS_PIN,bossPin,BOSS_LEN); }

void logIntruder(byte stage) {
  wrongTotal++;
  EEPROM.update(EE_WRNG_CNT, wrongTotal);
  byte head = EEPROM.read(EE_INTR_HD);
  byte cnt  = EEPROM.read(EE_INTR_CNT);
  int  addr = EE_INTR_BASE + head * INTR_SZ;
  EEPROM.update(addr+0, stage);
  EEPROM.update(addr+1, wrongCount);
  EEPROM.update(addr+2, sessCount);
  EEPROM.update(EE_INTR_HD, (head+1) % MAX_INTR);
  if (cnt < MAX_INTR) EEPROM.update(EE_INTR_CNT, cnt+1);
}

void logSession() {
  sessCount++;
  EEPROM.update(EE_SESS_CNT, sessCount);
}

void factoryReset() {
  eeWrite(EE_NORM_PIN,"0000000000000000",NORM_LEN);
  eeWrite(EE_SCRT_PIN,"1234",SCRT_LEN);
  eeWrite(EE_BOSS_PIN,"123456",BOSS_LEN);
  EEPROM.write(EE_SESS_CNT,0);
  EEPROM.write(EE_WRNG_CNT,0);
  EEPROM.write(EE_INTR_HD,0);
  EEPROM.write(EE_INTR_CNT,0);
  eeRead(EE_NORM_PIN,normPin,NORM_LEN);
  eeRead(EE_SCRT_PIN,scrtPin,SCRT_LEN);
  eeRead(EE_BOSS_PIN,bossPin,BOSS_LEN);
  sessCount=0; wrongTotal=0;
}

// ════════════════════════════════════════════════════════════════
//  LCD HELPERS
// ════════════════════════════════════════════════════════════════
void lcdShow(const __FlashStringHelper* l1,
             const __FlashStringHelper* l2) {
  lcd.clear();
  lcd.setCursor(0,0); lcd.print(l1);
  lcd.setCursor(0,1); lcd.print(l2);
}
void lcdPad2(byte v) { if(v<10) lcd.print('0'); lcd.print(v); }
void lcdAttemptLine(byte cur, byte mx) {
  lcd.setCursor(0,1);
  lcd.print(F("Attempt ")); lcd.print(cur);
  lcd.print(F(" of "));    lcd.print(mx);
  lcd.print(F("   "));
}

// PIN strength — returns 0=WEAK 1=OK 2=GOOD 3=STRG
byte pinStrength(byte len) {
  if (len < 2) return 0;
  bool seen[10];
  for (byte i=0;i<10;i++) seen[i]=false;
  byte unique=0;
  for (byte i=0;i<len;i++) {
    byte d = inputBuf[i] - '0';
    if (d<10 && !seen[d]) { seen[d]=true; unique++; }
  }
  if (unique <= 1) return 0;
  if (unique <= 3) return 1;
  if (unique <= 5) return 2;
  return 3;
}

void lcdMaskStrength(const __FlashStringHelper* prompt,
                     byte n, byte maxLen) {
  lcd.clear();
  lcd.setCursor(0,0); lcd.print(prompt);
  lcd.setCursor(0,1);
  byte stars = (n > 10) ? 10 : n;
  for (byte i=0;i<stars;i++) lcd.print('*');
  if (n > 0 && maxLen >= 4) {
    // right-align strength in positions 12-15
    lcd.setCursor(11,1);
    lcd.print(' ');
    byte s = pinStrength(n);
    if      (s==0) lcd.print(F("WEAK"));
    else if (s==1) lcd.print(F("OK  "));
    else if (s==2) lcd.print(F("GOOD"));
    else           lcd.print(F("STRG"));
  }
}

void lcdMask(const __FlashStringHelper* prompt, byte n) {
  lcd.clear();
  lcd.setCursor(0,0); lcd.print(prompt);
  lcd.setCursor(0,1);
  for (byte i=0;i<n&&i<16;i++) lcd.print('*');
}

// ════════════════════════════════════════════════════════════════
//  SERVO / MISC
// ════════════════════════════════════════════════════════════════
void clearInput()   { memset(inputBuf,0,sizeof(inputBuf)); inputLen=0; }
void clearTempNew() { memset(tempNew,0,sizeof(tempNew)); }
void servoOpen()    { lockServo.write(90); }
void servoClose()   { lockServo.write(0);  }
bool isLockedClosed() { return digitalRead(LIMIT_SW_PIN)==LOW; }  // INPUT_PULLUP: pressed=LOW=door CLOSED

// ════════════════════════════════════════════════════════════════
//  BUZZER — 13 distinct sounds, tuned melodies
// ════════════════════════════════════════════════════════════════
void bz_pinOK() {
  tone(BUZZER_PIN,1319,80);  delay(100);
  tone(BUZZER_PIN,1568,80);  delay(100);
  tone(BUZZER_PIN,2093,160); delay(180); noTone(BUZZER_PIN);
}
void bz_pinFail() {
  tone(BUZZER_PIN,494,180); delay(200);
  tone(BUZZER_PIN,330,400); delay(420); noTone(BUZZER_PIN);
}
void bz_rfidOK() {
  for(byte i=0;i<3;i++){tone(BUZZER_PIN,1047+i*262,70);delay(90);}
  noTone(BUZZER_PIN);
}
void bz_rfidFail() {
  tone(BUZZER_PIN,262,600); delay(630); noTone(BUZZER_PIN);
}
void bz_bossCard() {
  tone(BUZZER_PIN,2093,120); delay(140);
  tone(BUZZER_PIN,1568,120); delay(140);
  tone(BUZZER_PIN,1047,240); delay(260); noTone(BUZZER_PIN);
}
void bz_bossPin() {
  tone(BUZZER_PIN,784,80);  delay(100);
  tone(BUZZER_PIN,1047,80); delay(100);
  tone(BUZZER_PIN,1319,80); delay(100);
  tone(BUZZER_PIN,1568,160);delay(180); noTone(BUZZER_PIN);
}
void bz_unlock() {
  tone(BUZZER_PIN,1047,100); delay(120);
  tone(BUZZER_PIN,1319,100); delay(120);
  tone(BUZZER_PIN,1568,100); delay(120);
  tone(BUZZER_PIN,2093,350); delay(370); noTone(BUZZER_PIN);
}
void bz_lock() {
  tone(BUZZER_PIN,880,150); delay(170);
  tone(BUZZER_PIN,587,300); delay(320); noTone(BUZZER_PIN);
}
void bz_doorClosed() {
  tone(BUZZER_PIN,1319,90); delay(110); noTone(BUZZER_PIN);
}
void bz_doorMissed() {
  for(byte i=0;i<4;i++){
    tone(BUZZER_PIN,2093,100); delay(140);
    tone(BUZZER_PIN,1319,100); delay(140);
  } noTone(BUZZER_PIN);
}
void bz_timerExpired() {
  tone(BUZZER_PIN,784,600); delay(650);
  tone(BUZZER_PIN,523,800); delay(830); noTone(BUZZER_PIN);
}
void bz_processing() {
  tone(BUZZER_PIN,1760,50); delay(70); noTone(BUZZER_PIN);
}
void bz_warning() {
  for(byte i=0;i<3;i++){tone(BUZZER_PIN,2093,120);delay(200);}
  noTone(BUZZER_PIN);
}
void bz_startup() {
  tone(BUZZER_PIN,1047,110); delay(130);
  tone(BUZZER_PIN,1319,110); delay(130);
  tone(BUZZER_PIN,1568,110); delay(130);
  tone(BUZZER_PIN,1760,110); delay(130);
  tone(BUZZER_PIN,2093,300); delay(320); noTone(BUZZER_PIN);
}
void bz_factoryReset() {
  tone(BUZZER_PIN,784,100);  delay(120);
  tone(BUZZER_PIN,1047,100); delay(120);
  tone(BUZZER_PIN,784,100);  delay(120);
  tone(BUZZER_PIN,1047,200); delay(220); noTone(BUZZER_PIN);
}

// ════════════════════════════════════════════════════════════════
//  UID HELPERS
// ════════════════════════════════════════════════════════════════
bool uidMatch(const byte* a, const byte* pgm) {
  byte b[4]; memcpy_P(b,pgm,4); return memcmp(a,b,4)==0;
}
bool isLockerCard(const byte* uid, byte sz) {
  return sz==4&&(uidMatch(uid,LOCKER_CARD_A)||uidMatch(uid,LOCKER_CARD_B));
}
bool isBossCard(const byte* uid, byte sz) {
  return sz==4&&uidMatch(uid,BOSS_CARD_UID);
}

// ════════════════════════════════════════════════════════════════
//  BOSS PIN SLIDING WINDOW — RESTRICTED to safe states only
// ════════════════════════════════════════════════════════════════
bool bossWindowEnabled() {
  // Only allow from idle home screen and hard-lock
  return sysState==ST_NORM_PIN || sysState==ST_HARD_LOCK;
}
void pushBossWindow(char c) {
  memmove(bossWindow,bossWindow+1,BOSS_LEN-1);
  bossWindow[BOSS_LEN-1]=c; bossWindow[BOSS_LEN]='\0';
}
bool bossWindowMatches() {
  for(byte i=0;i<BOSS_LEN;i++) if(!bossWindow[i]) return false;
  return strcmp(bossWindow,bossPin)==0;
}

// ════════════════════════════════════════════════════════════════
//  FAST PSEUDO-RANDOM (for equalizer)
// ════════════════════════════════════════════════════════════════
static uint16_t _rA=0xA55A, _rB=0x1234;
uint8_t eqRnd() {
  _rA^=_rA<<7; _rA^=_rA>>9; _rA^=_rA<<8;
  _rB^=_rB<<5; _rB^=_rB>>3; _rB^=_rB<<6;
  return (uint8_t)(_rA^_rB);
}

// ════════════════════════════════════════════════════════════════
//  SYSTEM CONTROL
// ════════════════════════════════════════════════════════════════
void resetToIdle() {
  servoClose(); clearInput(); clearTempNew();
  wrongCount=0; emergencyTaps=0;
  clrF(FL_HASH|FL_STAR|FL_ZERO|FL_POSTSELFTEST|FL_EMERGENCY);
  memset(bossWindow,0,sizeof(bossWindow));
  t_rfidRead=0; t_lastKey=millis(); t_idleAlt=millis();
  sysState=ST_NORM_PIN;
  lcdShow(F("Enter 16-Digit"),F("PIN  (# = OK):"));
}

void hardLock() {
  servoClose(); noTone(BUZZER_PIN);
  tone(BUZZER_PIN,440);   // continuous alarm
  t_alarm=millis(); sysState=ST_HARD_LOCK;
  lcdShow(F("!! SYSTEM LOCKED"),F("Boss Override Only"));
}

// Show intruder summary after boss clears hard lock
void showIntruderSummary() {
  byte cnt = EEPROM.read(EE_INTR_CNT);
  if (cnt == 0) return;
  lcd.clear();
  lcd.setCursor(0,0); lcd.print(F("Intruders: "));  lcd.print(cnt);
  lcd.setCursor(0,1);
  // Show last intruder stage
  byte head = EEPROM.read(EE_INTR_HD);
  byte last = (head==0) ? MAX_INTR-1 : head-1;
  byte stage = EEPROM.read(EE_INTR_BASE + last*INTR_SZ + 0);
  byte wc    = EEPROM.read(EE_INTR_BASE + last*INTR_SZ + 1);
  lcd.print(F("Last:"));
  if      (stage==0) lcd.print(F("PIN"));
  else if (stage==1) lcd.print(F("RFID"));
  else               lcd.print(F("SEC"));
  lcd.print(F(" x")); lcd.print(wc);
  delay(2500);
}

void bossUnlock(bool fromCard) {
  wrongCount=0; emergencyTaps=0;
  clrF(FL_HASH|FL_STAR|FL_ZERO|FL_EMERGENCY);
  clearInput(); clearTempNew();
  memset(bossWindow,0,sizeof(bossWindow));
  bool wasHardLock = (sysState==ST_HARD_LOCK);
  if (!isLockedClosed()) {
    lcdShow(F("Boss: Door Open"),F("Proceeding...")); delay(700);
  }
  servoOpen();
  t_lastKey=millis(); t_doorWait=millis();
  if (fromCard) {
    bz_bossCard();
    lcdShow(F("Boss Card OK"),F("Open door (10s)"));
    sysState=ST_BOSS_MODE;
  } else {
    bz_bossPin(); bz_unlock();
    lcdShow(F("Boss PIN OK"),F("Open door (10s)"));
    sysState=ST_DOOR_WAIT;
  }
  if (wasHardLock) showIntruderSummary();
}

// ════════════════════════════════════════════════════════════════
//  RFID — hard-reset, watchdog, HALT-state-fix poll
// ════════════════════════════════════════════════════════════════
void rfidHardReset() {
  pinMode(RFID_RST_PIN,OUTPUT);
  digitalWrite(RFID_RST_PIN,LOW);  delay(50);
  digitalWrite(RFID_RST_PIN,HIGH); delay(50);
  rfid.PCD_Init(); delay(5);
  rfid.PCD_SetAntennaGain(rfid.RxGain_max);
  t_rfidRead=0; t_rfidWatch=millis();
}

void rfidWatchdog() {
  if (millis()-t_rfidWatch<T_RFID_WATCH) return;
  t_rfidWatch=millis();
  byte fw=rfid.PCD_ReadRegister(rfid.VersionReg);
  if (fw==0x00||fw==0xFF) rfidHardReset();
}

void pollRFID() {
  if (millis()-t_rfidRead<T_RFID_COOL) return;
  rfid.PCD_Init();
  if (!rfid.PICC_IsNewCardPresent()) return;
  delay(10);
  if (!rfid.PICC_ReadCardSerial()) {
    rfid.PICC_HaltA(); rfid.PCD_StopCrypto1(); rfid.PCD_Init(); return;
  }
  t_rfidRead=millis(); t_rfidWatch=millis(); t_lastKey=millis();
  byte uid[4]; byte sz=rfid.uid.size;
  if (sz==4) memcpy(uid,rfid.uid.uidByte,4);
  rfid.PICC_HaltA(); rfid.PCD_StopCrypto1(); rfid.PCD_Init();
  if (sz!=4) return;

  // ── Boss card: instant unlock from any state ─────────────────
  if (isBossCard(uid,sz)) {
    if (sysState==ST_BOSS_MODE||sysState==ST_DOOR_WAIT) {
      // Second tap in open mode → change boss PIN
      servoClose(); bz_bossCard(); clearInput(); clearTempNew();
      lcdShow(F("Boss PIN Change"),F("New PIN (6+#):"));
      sysState=ST_BOSS_NEW; return;
    }
    bossUnlock(true); return;
  }

  // ── Normal RFID ───────────────────────────────────────────────
  if (sysState==ST_SCAN_RFID) {
    if (isLockerCard(uid,sz)) {
      bz_rfidOK(); wrongCount=0; clearInput();
      bz_processing(); t_secret=millis();
      lcdShow(F("Processing..."),F(""));
      sysState=ST_PROCESSING;
    } else {
      bz_rfidFail(); wrongCount++;
      logIntruder(1);
      if (wrongCount>=MAX_WRONG) { hardLock(); return; }
      lcd.clear();
      lcd.setCursor(0,0); lcd.print(F("Invalid Card!"));
      lcdAttemptLine(wrongCount,MAX_WRONG);
      delay(1500); lcdShow(F("Scan RFID Card"),F("Now..."));
    }
    return;
  }

  if (sysState==ST_HARD_LOCK) {
    bz_rfidFail();
    lcdShow(F("LOCKED!"),F("Boss Card Only"));
    delay(1200);
    lcdShow(F("!! SYSTEM LOCKED"),F("Boss Override Only"));
  }
}

// ════════════════════════════════════════════════════════════════
//  EQUALIZER SCREENSAVER
// ════════════════════════════════════════════════════════════════
byte eqHeightToChar(byte px) {
  if      (px>=8) return 7;
  else if (px>=7) return 6;
  else if (px>=6) return 5;
  else if (px>=5) return 4;
  else if (px>=4) return 3;
  else if (px>=3) return 2;
  else if (px>=2) return 1;
  else            return 0;
}

void activateScreensaver() {
  sysState=ST_SCREENSAVER;
  // Load custom chars into LCD CGRAM
  lcd.createChar(0,EQ_C0); lcd.createChar(1,EQ_C1);
  lcd.createChar(2,EQ_C2); lcd.createChar(3,EQ_C3);
  lcd.createChar(4,EQ_C4); lcd.createChar(5,EQ_C5);
  lcd.createChar(6,EQ_C6); lcd.createChar(7,EQ_C7);
  lcd.clear();
  // Init bar state
  for (byte c=0;c<EQ_COLS;c++) {
    eqBarH[c]     = 3 + eqRnd()%8;
    eqPeakH[c]    = eqBarH[c];
    eqPeakHold[c] = 0;
    eqTargetH[c]  = 4 + eqRnd()%10;
  }
  eqBeatTimer=0; eqFrameCount=0;
  eqDropActive=false; eqDropPhase=0; eqDropHold=0;
  t_eqFrame=millis();
}

void eqUpdateBars() {
  eqBeatTimer++; eqFrameCount++;

  // Bass drop every EQ_DROP_INT frames
  if (!eqDropActive && eqBeatTimer>=EQ_DROP_INT) {
    eqBeatTimer=0; eqDropActive=true; eqDropPhase=0; eqDropHold=0;
    for (byte c=0;c<EQ_COLS;c++) eqTargetH[c]=EQ_MAX_H;
  }
  if (eqDropActive) {
    if (eqDropPhase==0) {
      bool allUp=true;
      for (byte c=0;c<EQ_COLS;c++) if(eqBarH[c]<EQ_MAX_H-2){allUp=false;break;}
      if (allUp) { eqDropPhase=1; eqDropHold=14; }
    } else if (eqDropPhase==1) {
      eqDropHold--;
      if (eqDropHold==0) {
        eqDropPhase=2; eqDropActive=false;
        for (byte c=0;c<EQ_COLS;c++) eqTargetH[c]=0;
      }
    }
  }

  // Normal random targeting
  if (!eqDropActive) {
    for (byte c=0;c<EQ_COLS;c++) {
      if (eqRnd()<65) {
        byte r=eqRnd()%20;
        if      (r<2)  eqTargetH[c]=1+eqRnd()%3;
        else if (r<7)  eqTargetH[c]=4+eqRnd()%4;
        else if (r<14) eqTargetH[c]=7+eqRnd()%5;
        else if (r<18) eqTargetH[c]=11+eqRnd()%4;
        else           eqTargetH[c]=EQ_MAX_H;
      }
    }
    // Random glitch spike
    if (eqRnd()<30) { byte c=eqRnd()%EQ_COLS; eqTargetH[c]=13+eqRnd()%4; }
  }

  for (byte c=0;c<EQ_COLS;c++) {
    // Rise fast
    if (eqBarH[c]<eqTargetH[c]) {
      byte step=(byte)min((int)(eqTargetH[c]-eqBarH[c]),3);
      eqBarH[c]+=step;
    } else if (eqBarH[c]>eqTargetH[c]) {
      // Fall — bass (left) slower, treble (right) faster
      byte fr=(c<6)?1:(c<11)?2:3;
      byte step=(byte)min((int)(eqBarH[c]-eqTargetH[c]),(int)fr);
      eqBarH[c]-=step;
      if (eqTargetH[c]>0) eqTargetH[c]--;
    }
    // Peak dot
    if (eqBarH[c]>=eqPeakH[c]) {
      eqPeakH[c]=eqBarH[c]; eqPeakHold[c]=EQ_PEAK_HOLD;
    } else if (eqPeakHold[c]>0) {
      eqPeakHold[c]--;
    } else if (eqPeakH[c]>0) {
      eqPeakH[c]--;
    }
  }
}

void eqDrawBars() {
  // Bottom row (line 1) — pixels 0-7
  lcd.setCursor(0,1);
  for (byte c=0;c<EQ_COLS;c++) {
    byte px=(eqBarH[c]>=8)?8:eqBarH[c];
    lcd.write(eqHeightToChar(px));
  }
  // Top row (line 0) — pixels 8-15
  lcd.setCursor(0,0);
  for (byte c=0;c<EQ_COLS;c++) {
    byte px=(eqBarH[c]>8)?(eqBarH[c]-8):0;
    if (eqPeakH[c]>8) {
      byte pp=eqPeakH[c]-8;
      if (pp>px) px=pp;
      if (px<2)  px=2;
    }
    if (px==0) lcd.print(' ');
    else       lcd.write(eqHeightToChar(px));
  }
}

void drawScreensaver() {
  unsigned long now=millis();
  if (now-t_eqFrame<EQ_FRAME_MS) return;
  t_eqFrame=now;
  eqUpdateBars();
  eqDrawBars();
}

void wakeFromScreensaver() {
  t_lastKey=millis(); t_idleAlt=millis();
  sysState=ST_NORM_PIN;
  lcdShow(F("Enter 16-Digit"),F("PIN  (# = OK):"));
}

// ════════════════════════════════════════════════════════════════
//  IDLE TIMEOUT SCOPE
// ════════════════════════════════════════════════════════════════
bool idleTimeoutApplies() {
  switch(sysState) {
    case ST_SCAN_RFID: case ST_PROCESSING:
    case ST_CN_OLD_PIN: case ST_CN_OLD_SEC: case ST_CN_NEW_PIN:
    case ST_CS_NORM_PIN: case ST_CS_OLD_SEC:
    case ST_CS_NEW_SEC: case ST_CS_CONFIRM: return true;
    case ST_NORM_PIN: return inputLen>0;
    default: return false;
  }
}

// ════════════════════════════════════════════════════════════════
//  SUBMIT HANDLERS
// ════════════════════════════════════════════════════════════════
void submitNormPin() {
  inputBuf[inputLen]='\0';
  if (strcmp(inputBuf,normPin)==0) {
    bz_pinOK(); wrongCount=0; clearInput();
    lcdShow(F("Scan RFID Card"),F("Now...")); sysState=ST_SCAN_RFID;
  } else {
    bz_pinFail(); wrongCount++;
    logIntruder(0); clearInput();
    if (wrongCount>=MAX_WRONG) { hardLock(); return; }
    t_lockoutDur=(unsigned long)wrongCount*T_LOCKOUT_BASE;
    t_lockout=millis(); sysState=ST_LOCKOUT;
  }
}

void submitSecret() {
  inputBuf[inputLen]='\0';
  if (strcmp(inputBuf,scrtPin)==0) {
    if (!isLockedClosed()) {
      bz_pinFail(); clearInput();
      lcdShow(F("CLOSE DOOR FIRST"),F("Then try again"));
      delay(2000); resetToIdle(); return;
    }
    bz_pinOK(); bz_unlock();
    servoOpen(); wrongCount=0; clearInput();
    lcdShow(F("Unlocked!"),F("Open door (10s)"));
    t_doorWait=millis(); sysState=ST_DOOR_WAIT;
  } else {
    bz_pinFail(); wrongCount++;
    logIntruder(2); clearInput();
    if (wrongCount>=MAX_WRONG) { hardLock(); return; }
    lcdShow(F("Secret Wrong!"),F("Restarting...")); delay(1000); resetToIdle();
  }
}

void submitBossNew() {
  if (inputLen!=BOSS_LEN) return;
  inputBuf[inputLen]='\0';
  memcpy(tempNew,inputBuf,BOSS_LEN+1);
  clearInput();
  lcdShow(F("Confirm Boss PIN"),F("Re-enter (6+#):")); sysState=ST_BOSS_CONFIRM;
}

void submitBossConfirm() {
  if (inputLen!=BOSS_LEN) return;
  inputBuf[inputLen]='\0';
  if (strcmp(inputBuf,tempNew)==0) {
    memcpy(bossPin,tempNew,BOSS_LEN+1);
    saveBossPin(); bz_pinOK(); clearInput(); clearTempNew();
    lcdShow(F("Boss PIN"),F("Updated!")); delay(1600); resetToIdle();
  } else {
    bz_pinFail(); clearInput(); clearTempNew();
    lcdShow(F("Mismatch!"),F("Cancelled")); delay(1300); resetToIdle();
  }
}

void submitCnOldPin() {
  inputBuf[inputLen]='\0';
  if (strcmp(inputBuf,normPin)==0) {
    clearInput(); lcdShow(F("Old Secret PIN:"),F("")); sysState=ST_CN_OLD_SEC;
  } else {
    bz_pinFail(); clearInput();
    lcdShow(F("Wrong PIN!"),F("Cancelled")); delay(1300); resetToIdle();
  }
}
void submitCnOldSec() {
  inputBuf[inputLen]='\0';
  if (strcmp(inputBuf,scrtPin)==0) {
    clearInput(); lcdShow(F("New 16-Digit PIN"),F("(# when done)")); sysState=ST_CN_NEW_PIN;
  } else {
    bz_pinFail(); clearInput();
    lcdShow(F("Wrong Secret!"),F("Cancelled")); delay(1300); resetToIdle();
  }
}
void submitCnNewPin() {
  if (inputLen!=NORM_LEN) return;
  inputBuf[inputLen]='\0';
  memcpy(normPin,inputBuf,NORM_LEN+1);
  saveNormPin(); bz_pinOK(); clearInput();
  lcdShow(F("Normal PIN"),F("Updated!")); delay(1600); resetToIdle();
}
void submitCsNormPin() {
  inputBuf[inputLen]='\0';
  if (strcmp(inputBuf,normPin)==0) {
    clearInput(); lcdShow(F("Old Secret PIN:"),F("")); sysState=ST_CS_OLD_SEC;
  } else {
    bz_pinFail(); clearInput();
    lcdShow(F("Wrong PIN!"),F("Cancelled")); delay(1300); resetToIdle();
  }
}
void submitCsOldSec() {
  inputBuf[inputLen]='\0';
  if (strcmp(inputBuf,scrtPin)==0) {
    clearInput(); lcdShow(F("New 4-Digit"),F("Secret PIN:")); sysState=ST_CS_NEW_SEC;
  } else {
    bz_pinFail(); clearInput();
    lcdShow(F("Wrong Secret!"),F("Cancelled")); delay(1300); resetToIdle();
  }
}
void submitCsNewSec() {
  if (inputLen!=SCRT_LEN) return;
  inputBuf[inputLen]='\0';
  memcpy(tempNew,inputBuf,SCRT_LEN+1);
  clearInput(); lcdShow(F("Confirm Secret"),F("PIN:")); sysState=ST_CS_CONFIRM;
}
void submitCsConfirm() {
  inputBuf[inputLen]='\0';
  if (strcmp(inputBuf,tempNew)==0) {
    memcpy(scrtPin,tempNew,SCRT_LEN+1);
    saveScrtPin(); bz_pinOK(); clearInput(); clearTempNew();
    lcdShow(F("Secret PIN"),F("Updated!")); delay(1600); resetToIdle();
  } else {
    bz_pinFail(); clearInput();
    lcdShow(F("Mismatch!"),F("Re-enter New:")); delay(1000);
    sysState=ST_CS_NEW_SEC; lcdShow(F("New 4-Digit"),F("Secret PIN:"));
  }
}

void doHashSubmit() {
  switch(sysState) {
    case ST_NORM_PIN:    submitNormPin();    break;
    case ST_PROCESSING:  submitSecret();     break;
    case ST_BOSS_NEW:    submitBossNew();    break;
    case ST_BOSS_CONFIRM:submitBossConfirm();break;
    case ST_CN_OLD_PIN:  submitCnOldPin();   break;
    case ST_CN_OLD_SEC:  submitCnOldSec();   break;
    case ST_CN_NEW_PIN:  submitCnNewPin();   break;
    case ST_CS_NORM_PIN: submitCsNormPin();  break;
    case ST_CS_OLD_SEC:  submitCsOldSec();   break;
    case ST_CS_NEW_SEC:  submitCsNewSec();   break;
    case ST_CS_CONFIRM:  submitCsConfirm();  break;
    default: break;
  }
}

// ════════════════════════════════════════════════════════════════
//  HANDLE DIGIT + PROCESS DIGIT
// ════════════════════════════════════════════════════════════════
void handleDigit(char k) {
  byte maxLen=0;
  const __FlashStringHelper* prompt=nullptr;
  bool showStrength=false;
  switch(sysState) {
    case ST_NORM_PIN:    maxLen=NORM_LEN; prompt=F("Enter PIN:");      break;  // existing PIN - no strength
    case ST_PROCESSING:  maxLen=SCRT_LEN;                              break;
    case ST_BOSS_NEW:    maxLen=BOSS_LEN; prompt=F("New Boss PIN:");   showStrength=true;  break;
    case ST_BOSS_CONFIRM:maxLen=BOSS_LEN; prompt=F("Confirm Boss PIN:");break;
    case ST_CN_OLD_PIN:  maxLen=NORM_LEN; prompt=F("Old Normal PIN:"); break;
    case ST_CN_OLD_SEC:  maxLen=SCRT_LEN; prompt=F("Old Secret:");     break;
    case ST_CN_NEW_PIN:  maxLen=NORM_LEN; prompt=F("New PIN (16):");   showStrength=true;  break;
    case ST_CS_NORM_PIN: maxLen=NORM_LEN; prompt=F("Normal PIN:");     break;
    case ST_CS_OLD_SEC:  maxLen=SCRT_LEN; prompt=F("Old Secret:");     break;
    case ST_CS_NEW_SEC:  maxLen=SCRT_LEN; prompt=F("New Secret:");     showStrength=true;  break;
    case ST_CS_CONFIRM:  maxLen=SCRT_LEN; prompt=F("Confirm:");        break;
    default: return;
  }
  if (inputLen<maxLen) {
    inputBuf[inputLen++]=k;
    if (sysState!=ST_PROCESSING && prompt!=nullptr) {
      if (showStrength) lcdMaskStrength(prompt,inputLen,maxLen);
      else              lcdMask(prompt,inputLen);
    }
  }
}

void processDigit(char k) {
  if (bossWindowEnabled()) {
    pushBossWindow(k);
    if (bossWindowMatches()) {
      memset(bossWindow,0,sizeof(bossWindow));
      if (sysState==ST_BOSS_MODE||sysState==ST_DOOR_WAIT) {
        servoClose(); bz_bossPin(); clearInput(); clearTempNew();
        lcdShow(F("Boss PIN Change"),F("New PIN (6+#):")); sysState=ST_BOSS_NEW; return;
      }
      bossUnlock(false); return;
    }
  }
  handleDigit(k);
}

// ════════════════════════════════════════════════════════════════
//  SELF-TEST
// ════════════════════════════════════════════════════════════════
void runSelfTest() {
  lcdShow(F("  SELF TEST MODE"),F("  Starting...   "));
  bz_processing(); delay(1200);

  // 1: LCD
  lcdShow(F("1. LCD Test"),F("  [OK] Working! ")); delay(1000);

  // 2: Servo
  if (isLockedClosed()) {
    lcdShow(F("2. Servo Test"),F("  Sweeping...   "));
    servoOpen(); delay(700); servoClose(); delay(700);
    lcdShow(F("2. Servo Test"),F("  [OK] Swept 90 "));
  } else {
    lcdShow(F("2. Servo Test"),F("  [SKIP] Door Op"));
  }
  delay(900);

  // 3: Buzzer
  lcdShow(F("3. Buzzer Test"),F("  Playing...    "));
  for (int f=1000;f<=2000;f+=250){tone(BUZZER_PIN,f,110);delay(140);}
  noTone(BUZZER_PIN);
  lcdShow(F("3. Buzzer Test"),F("  [OK] 5 Tones  ")); delay(900);

  // 4: RFID
  lcdShow(F("4. RFID Module"),F("  Reading FW... "));
  byte fw=rfid.PCD_ReadRegister(rfid.VersionReg);
  lcd.setCursor(0,1);
  if (fw==0x00||fw==0xFF) { lcd.print(F("  [FAIL] No Resp")); }
  else { lcd.print(F("  [OK] FW: 0x")); if(fw<0x10)lcd.print('0'); lcd.print(fw,HEX); }
  delay(1200);

  // 5: EEPROM
  lcdShow(F("5. EEPROM"),F("  Checking...   "));
  lcd.setCursor(0,1);
  lcd.print(EEPROM.read(EE_FLAG)==EE_MAGIC ? F("  [OK] Magic OK ") : F("  [FAIL] Bad Mag"));
  delay(1200);

  // 6: Door switch
  lcdShow(F("6. Door Switch"),F("  Reading...    "));
  lcd.setCursor(0,1);
  lcd.print(isLockedClosed() ? F("  [OK] CLOSED   ") : F("  [OK] OPEN     "));
  delay(1200);

  bz_pinOK(); bz_pinOK();
  lcdShow(F(" ALL TESTS DONE!"),F("  # or * = exit "));
  unsigned long tExit=millis(); bool done=false;
  while (!done&&millis()-tExit<12000UL) {
    if (kpad.getKeys()) {
      for (byte i=0;i<LIST_MAX;i++) {
        if (kpad.key[i].stateChanged&&kpad.key[i].kstate==PRESSED) {
          if (kpad.key[i].kchar=='#'||kpad.key[i].kchar=='*') { done=true; break; }
        }
      }
    }
  }
  setF(FL_POSTSELFTEST);
  resetToIdle();
}

// ════════════════════════════════════════════════════════════════
//  BOOT ANIMATION
// ════════════════════════════════════════════════════════════════
void bootAnimation() {
  lcd.clear();
  // Line 0: MULTI LAYER SAFE — left to right
  for (byte i=0;i<16;i++) {
    lcd.setCursor(i,0);
    lcd.print((char)pgm_read_byte(BOOT_L1+i));
    delay(60);
  }
  // Line 1: MUNIB ALI — right to left
  for (byte i=0;i<16;i++) {
    byte pos=15-i;
    lcd.setCursor(pos,1);
    lcd.print((char)pgm_read_byte(BOOT_L2+pos));
    delay(60);
  }
  delay(200);
  // Double backlight flash
  lcd.noBacklight(); delay(100); lcd.backlight(); delay(100);
  lcd.noBacklight(); delay(100); lcd.backlight();
  // Startup jingle
  bz_startup();
  delay(300);
}

// ════════════════════════════════════════════════════════════════
//  ASYNC TIMERS — non-blocking, called every loop()
// ════════════════════════════════════════════════════════════════
void asyncTimers() {
  unsigned long now=millis();

  // ── Screensaver update ──────────────────────────────────────
  if (sysState==ST_SCREENSAVER) {
    drawScreensaver(); return;
  }

  // ── Tamper detection: door forced open while locked ──────────
  // If limit switch shows OPEN but we never commanded servo open
  // (i.e. we are in a state where door must be closed) → hard lock
  if (!isLockedClosed()) {
    bool shouldBeClosed = (sysState==ST_NORM_PIN   ||
                           sysState==ST_SCAN_RFID  ||
                           sysState==ST_PROCESSING ||
                           sysState==ST_LOCKOUT);
    if (shouldBeClosed) {
      noTone(BUZZER_PIN);
      lcdShow(F("!! TAMPER !!"),F("Door Forced Open"));
      delay(500);
      hardLock(); return;
    }
  }

  // ── Self-test combo: * + 0 held 3s from idle ───────────────
  if (getF(FL_STAR)&&getF(FL_ZERO)&&sysState==ST_NORM_PIN&&inputLen==0) {
    unsigned long both=(t_starPress>t_zeroPress)?t_starPress:t_zeroPress;
    if (now-both>=T_SELFTEST_HOLD) {
      clrF(FL_STAR|FL_ZERO); runSelfTest(); return;
    }
  }

  // ── Emergency confirm: # pressed with FL_EMERGENCY ─────────
  // (handled in routeKey)

  // ── Flush deferred # ────────────────────────────────────────
  if (getF(FL_HASH)&&now-t_hashPress>T_HASH_COMBO) {
    clrF(FL_HASH); doHashSubmit(); return;
  }

  // ── Idle timeout ────────────────────────────────────────────
  if (idleTimeoutApplies()&&now-t_lastKey>=T_IDLE_TIMEOUT) {
    lcdShow(F("Idle Timeout!"),F("Auto Reset...")); delay(800); resetToIdle(); return;
  }

  // ── Screensaver activate ─────────────────────────────────────
  if (sysState==ST_NORM_PIN&&inputLen==0&&now-t_lastKey>=T_SCREENSAVER) {
    activateScreensaver(); return;
  }

  // ── Secret window ────────────────────────────────────────────
  if (sysState==ST_PROCESSING&&now-t_secret>=T_SECRET_WIN) {
    bz_timerExpired();
    lcdShow(F("Time Expired!"),F("Restarting...")); delay(1000); resetToIdle(); return;
  }

  // ── Door-wait ────────────────────────────────────────────────
  if (sysState==ST_DOOR_WAIT||sysState==ST_BOSS_MODE) {
    if (!isLockedClosed()) {
      logSession();
      lcdShow(F("Door Open!"),F(""));
      t_doorOpenStart=now;
      sysState=ST_DOOR_OPEN; return;
    }
    unsigned long elapsed=now-t_doorWait;
    if (elapsed>=T_DOOR_WAIT) {
      servoClose(); bz_doorMissed();
      lcdShow(F("Door Not Opened"),F("Auto Locked")); delay(1500); resetToIdle(); return;
    }
    static unsigned long lastDSec=0;
    if (now-lastDSec>=1000UL) {
      lastDSec=now;
      lcd.setCursor(0,1);
      lcd.print(F("Open door: "));
      lcd.print((T_DOOR_WAIT-elapsed)/1000UL+1);
      lcd.print(F("s  "));
    }
    return;
  }

  // ── Door-open: session timer + auto-close ───────────────────
  if (sysState==ST_DOOR_OPEN) {
    // Lock instantly if door closes
    if (isLockedClosed()) {
      servoClose(); bz_lock(); bz_doorClosed();
      lcdShow(F("Locked!"),F("Have a safe day!")); delay(1200); resetToIdle(); return;
    }
    unsigned long openMs=now-t_doorOpenStart;

    // Force close at 3 minutes
    if (openMs>=T_DOOR_OPEN_MAX) {
      servoClose(); bz_doorMissed();
      lcdShow(F("AUTO LOCKED!"),F("Time Limit Hit")); delay(2000); resetToIdle(); return;
    }

    // Warning at 2 minutes — flash + beep
    static bool warnedAlready=false;
    if (openMs>=T_DOOR_OPEN_WARN && !warnedAlready) {
      warnedAlready=true; bz_warning();
    }

    // Update session timer every second
    static unsigned long lastST=0;
    if (now-lastST>=1000UL) {
      lastST=now;
      unsigned long secs=openMs/1000UL;
      byte om=(byte)(secs/60); byte os=(byte)(secs%60);
      if (openMs<T_DOOR_OPEN_WARN) {
        lcd.setCursor(0,0); lcd.print(F("Door Open!      "));
        lcd.setCursor(0,1);
        lcd.print(F("Open: ")); lcdPad2(om); lcd.print(':'); lcdPad2(os);
        lcd.print(F("       "));
      } else {
        // Warning state — alternate flash
        static bool flashState=false; flashState=!flashState;
        lcd.setCursor(0,0);
        lcd.print(flashState ? F("!! CLOSE DOOR !!") : F("  Door Open...  "));
        lcd.setCursor(0,1);
        byte remSecs=(byte)((T_DOOR_OPEN_MAX-openMs)/1000UL);
        lcd.print(F("Force close: ")); lcd.print(remSecs); lcd.print(F("s "));
      }
    }
    // Reset warnedAlready when door goes back to wait state
    if (!isLockedClosed()) warnedAlready = (openMs>=T_DOOR_OPEN_WARN);
    return;
  }

  // ── Hard-lock alarm silence ──────────────────────────────────
  if (sysState==ST_HARD_LOCK&&now-t_alarm>=T_ALARM_BUZZER) noTone(BUZZER_PIN);

  // ── Lockout countdown ────────────────────────────────────────
  if (sysState==ST_LOCKOUT) {
    if (now-t_lockout>=t_lockoutDur) {
      sysState=ST_NORM_PIN; lcdShow(F("Enter 16-Digit"),F("PIN  (# = OK):"));
    } else {
      static unsigned long lastLS=0;
      if (now-lastLS>=1000UL) {
        lastLS=now;
        lcd.setCursor(0,0); lcd.print(F("Cool Down...    "));
        lcd.setCursor(0,1);
        lcd.print(F("Wait: "));
        lcd.print((t_lockoutDur-(now-t_lockout))/1000UL+1);
        lcd.print(F("s     "));
      }
    }
    return;
  }

  // ── Idle screen: door status only — stats via * hold ────────
  if (sysState==ST_NORM_PIN&&inputLen==0) {
    static unsigned long lastDL=0;
    if (now-lastDL>=400UL) {
      lastDL=now;
      lcd.setCursor(0,1);
      lcd.print(isLockedClosed() ? F("CLOSED  Ready   ") : F("OPEN - Close 1st"));
    }
  }
}

// ════════════════════════════════════════════════════════════════
//  MAIN KEY ROUTER
// ════════════════════════════════════════════════════════════════
void routeKey(char k, KeyState ks) {
  t_lastKey=millis();

  // Screensaver: any PRESS wakes
  if (sysState==ST_SCREENSAVER) {
    if (ks==PRESSED) wakeFromScreensaver();
    return;
  }

  // ── # HOLD 5s → change normal PIN ───────────────────────────
  if (k=='#'&&ks==HOLD) {
    clrF(FL_HASH);
    if (sysState==ST_NORM_PIN||sysState==ST_SCAN_RFID||sysState==ST_LOCKOUT) {
      clearInput(); clearTempNew();
      lcdShow(F("Change Norm PIN"),F("Old 16-Digit:")); sysState=ST_CN_OLD_PIN;
    }
    return;
  }

  // ── * HOLD 5s — show stats from idle ────────────────────────
  if (k=='*'&&ks==HOLD) {
    clrF(FL_STAR);
    if (getF(FL_POSTSELFTEST)) { clrF(FL_POSTSELFTEST); return; }
    // Show wrong + opens stats for 3 seconds then return to idle
    if (sysState==ST_NORM_PIN&&inputLen==0) {
      lcd.clear();
      lcd.setCursor(0,0); lcd.print(F("Wrong:"));  lcd.print(wrongTotal);
      lcd.print(F("  Opens:")); lcd.print(sessCount);
      lcd.setCursor(0,1); lcd.print(F("* or wait to exit"));
      unsigned long tStats=millis(); bool exitStats=false;
      while (!exitStats&&millis()-tStats<3000UL) {
        if (kpad.getKeys()) {
          for (byte i=0;i<LIST_MAX;i++) {
            if (kpad.key[i].stateChanged&&kpad.key[i].kstate==PRESSED) {
              exitStats=true; break;
            }
          }
        }
      }
      t_lastKey=millis();
      lcdShow(F("Enter 16-Digit"),F("PIN  (# = OK):"));
    }
    return;
  }

  // ── # PRESSED ───────────────────────────────────────────────
  if (k=='#'&&ks==PRESSED) {
    // Emergency reset confirm
    if (getF(FL_EMERGENCY)) {
      clrF(FL_EMERGENCY); emergencyTaps=0;
      noTone(BUZZER_PIN);
      lcdShow(F("Factory Reset!"),F("Resetting..."));
      delay(500); bz_factoryReset();
      factoryReset();
      lcdShow(F("Reset Complete!"),F("Defaults loaded")); delay(2000);
      resetToIdle(); return;
    }
    t_hashPress=millis(); setF(FL_HASH); return;
  }
  if (k=='#'&&ks==RELEASED) return;

  // ── * PRESSED ───────────────────────────────────────────────
  if (k=='*'&&ks==PRESSED) {
    t_starPress=millis(); setF(FL_STAR);
    // Emergency tap counter in hard lock
    if (sysState==ST_HARD_LOCK) {
      if (getF(FL_EMERGENCY)) { clrF(FL_EMERGENCY); emergencyTaps=0; resetToIdle(); }
      emergencyTaps++;
      if (emergencyTaps>=EMERGENCY_PRESSES) {
        setF(FL_EMERGENCY);
        lcdShow(F("!! RESET !!"),F("# confirm * cancel"));
      }
    }
    return;
  }

  // ── * RELEASED ──────────────────────────────────────────────
  if (k=='*'&&ks==RELEASED) {
    if (!getF(FL_STAR)) return;
    clrF(FL_STAR);
    // Cancel emergency confirm
    if (getF(FL_EMERGENCY)) { clrF(FL_EMERGENCY); emergencyTaps=0;
      lcdShow(F("!! SYSTEM LOCKED"),F("Boss Override Only")); return; }
    // # + * combo → change secret PIN
    if (getF(FL_HASH)&&millis()-t_hashPress<=T_HASH_COMBO) {
      clrF(FL_HASH);
      if (sysState==ST_NORM_PIN||sysState==ST_SCAN_RFID||sysState==ST_LOCKOUT) {
        clearInput(); clearTempNew();
        lcdShow(F("Change Scrt PIN"),F("Normal PIN:")); sysState=ST_CS_NORM_PIN; return;
      }
    }
    clrF(FL_HASH);
    // Plain cancel
    clearInput();
    if (sysState>=ST_CN_OLD_PIN) { resetToIdle(); }
    else if (sysState==ST_SCAN_RFID) { wrongCount=0; resetToIdle(); }
    return;
  }

  // ── 0 PRESSED: arm self-test combo ──────────────────────────
  if (k=='0'&&ks==PRESSED) { setF(FL_ZERO); t_zeroPress=millis(); return; }

  // ── 0 RELEASED: commit digit ────────────────────────────────
  if (k=='0'&&ks==RELEASED) {
    if (!getF(FL_ZERO)) return;
    clrF(FL_ZERO);
    if (getF(FL_POSTSELFTEST)) { clrF(FL_POSTSELFTEST); return; }
    if (getF(FL_HASH)) { clrF(FL_HASH); doHashSubmit(); }
    processDigit('0'); return;
  }
  if (k=='0') return;

  if (ks!=PRESSED) return;

  // Flush deferred #
  if (getF(FL_HASH)) { clrF(FL_HASH); doHashSubmit(); }

  // Digits 1-9
  if (k>='1'&&k<='9') processDigit(k);
}

// ════════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════════
void setup() {
  pinMode(BUZZER_PIN,  OUTPUT); noTone(BUZZER_PIN);
  pinMode(LIMIT_SW_PIN,INPUT_PULLUP);

  Wire.begin();
  lcd.init();
  lcd.backlight();

  // Boot animation + jingle
  bootAnimation();

  // RFID hard-reset + init
  pinMode(RFID_RST_PIN,OUTPUT);
  digitalWrite(RFID_RST_PIN,LOW);  delay(50);
  digitalWrite(RFID_RST_PIN,HIGH); delay(50);
  SPI.begin(); delay(50);
  rfid.PCD_Init(); delay(10);
  rfid.PCD_SetAntennaGain(rfid.RxGain_max);
  t_rfidWatch=millis();

  lcdShow(F("MULTI LAYER SAFE"),F("RFID Ready...   "));
  delay(600);

  lockServo.attach(SERVO_PIN);
  servoClose();

  kpad.setHoldTime((uint16_t)T_HOLD_HASH);
  kpad.setDebounceTime(50);

  loadPins();

  if (isLockedClosed()) setF(FL_DOORPREV); else clrF(FL_DOORPREV);

  t_lastKey=millis(); t_idleAlt=millis();
  lcdShow(F("Enter 16-Digit"),F("PIN  (# = OK):"));
}

// ════════════════════════════════════════════════════════════════
//  LOOP
// ════════════════════════════════════════════════════════════════
void loop() {
  asyncTimers();
  rfidWatchdog();

  if (sysState!=ST_LOCKOUT&&sysState!=ST_SCREENSAVER) {
    pollRFID();
  }

  if (!kpad.getKeys()) return;
  for (byte i=0;i<LIST_MAX;i++) {
    if (!kpad.key[i].stateChanged) continue;
    routeKey(kpad.key[i].kchar, kpad.key[i].kstate);
  }
}
