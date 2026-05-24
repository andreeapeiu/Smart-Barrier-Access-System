#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <Servo.h>

// LCD I2C
#define LCD_I2C_ADDR 0x27
LiquidCrystal_I2C lcd(LCD_I2C_ADDR, 16, 2);

// Configurare tastatura 4x4
const byte ROWS = 4;
const byte COLS = 4;

char keys[ROWS][COLS] = {
  {'D','C','B','A'},
  {'#','9','6','3'},
  {'0','8','5','2'},
  {'*','7','4','1'}
};

byte rowPins[ROWS] = {6, 7, 8, 9};
byte colPins[COLS] = {10, 11, 12, 13};

Keypad keypad = Keypad(
  makeKeymap(keys),
  rowPins,
  colPins,
  ROWS,
  COLS
);

// Pini hardware
#define PIN_SERVO 5
#define PIN_IR_FATA   A0
#define PIN_IR_SPATE  A1
#define PIN_LED_GREEN 2
#define PIN_LED_RED   3
#define MOTOR_EN   4
#define MOTOR_IN1  A2
#define MOTOR_IN2  A3

// Configurari sistem
const char* SECRET_PIN = "4567";

const int SERVO_INCHIS  = 20;
const int SERVO_DESCHIS = 110;

const unsigned long TIMEOUT_PIN_MS   = 15000UL;
const unsigned long TIMEOUT_TRECE_MS = 45000UL;

// Starile sistemului
enum Stare {
  ST_IDLE,
  WAIT_FOR_CONFIRM,
  OPENING,
  WAIT_FOR_PASS,
  CLOSING,
  EROARE
};

Stare stare = ST_IDLE;

Servo servo;

String pinIntroduced = "";

unsigned long timerStart = 0;

int pragFata = 0;
int pragSpate = 0;

void tranzitie(Stare noua);

void lcdAfis(const char* r1,
             const char* r2 = "");

bool irFata();
bool irSpate();

void servoMisca(int target);

void bandaPorneste();
void bandaOpreste();

void setup() {

  Serial.begin(9600);

  lcd.init();
  lcd.backlight();

  servo.attach(PIN_SERVO);
  servo.write(SERVO_INCHIS);

  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);

  pinMode(MOTOR_EN, OUTPUT);
  pinMode(MOTOR_IN1, OUTPUT);
  pinMode(MOTOR_IN2, OUTPUT);

  digitalWrite(PIN_LED_GREEN, HIGH);
  digitalWrite(PIN_LED_RED, HIGH);

  bandaOpreste();

  lcdAfis(" Sistem acces ",
           " Initializare ");

  delay(1500);

  // Calibrare senzori IR
  lcdAfis("Calibrare...",
          "Nu blocati IR");

  long sumaFata = 0;
  long sumaSpate = 0;

  for (int i = 0; i < 100; i++) {

    sumaFata += analogRead(PIN_IR_FATA);
    sumaSpate += analogRead(PIN_IR_SPATE);

    delay(10);
  }

  int bazaFata = sumaFata / 100;
  int bazaSpate = sumaSpate / 100;

  pragFata = bazaFata + 80;
  pragSpate = bazaSpate + 80;

  Serial.print("Prag fata: ");
  Serial.println(pragFata);

  Serial.print("Prag spate: ");
  Serial.println(pragSpate);

  delay(1000);

  tranzitie(ST_IDLE);

  Serial.println("Sistem pornit");
}

void loop() {

  switch (stare) {

    case ST_IDLE: {

      digitalWrite(PIN_LED_GREEN, LOW);
      digitalWrite(PIN_LED_RED, HIGH);

      if (irFata()) {

        Serial.println("Persoana detectata");

        digitalWrite(PIN_LED_GREEN, LOW);
        digitalWrite(PIN_LED_RED, LOW);

        tranzitie(WAIT_FOR_CONFIRM);
      }

      break;
    }

    case WAIT_FOR_CONFIRM: {

      digitalWrite(PIN_LED_GREEN, HIGH);

      unsigned long scurs =
        millis() - timerStart;

      // Timeout introducere PIN
      if (scurs >= TIMEOUT_PIN_MS) {

        lcdAfis(" Timp expirat ", "");

        delay(1500);

        tranzitie(ST_IDLE);

        break;
      }

      unsigned long ramase =
        (TIMEOUT_PIN_MS - scurs) / 1000;

      char buf[17];

      lcd.setCursor(0, 1);

      snprintf(buf,
               sizeof(buf),
               "Cod:%-6s%2lus",
               pinIntroduced.c_str(),
               ramase);

      lcd.print(buf);

      char tasta = keypad.getKey();

      if (!tasta)
        break;

      Serial.print("Tasta: ");
      Serial.println(tasta);

      // Confirmare PIN
      if (tasta == '#') {

        if (pinIntroduced == SECRET_PIN) {

          Serial.println("PIN corect");

          tranzitie(OPENING);

        } else {

          Serial.println("PIN gresit");

          lcdAfis("PIN gresit",
                  "Banda inapoi");

          bandaInapoi();


          delay(15000);

          bandaOpreste();

          tranzitie(ST_IDLE);
        }
      }

      // Stergere ultima cifra
      else if (tasta == '*') {

        if (pinIntroduced.length() > 0) {

          pinIntroduced.remove(
            pinIntroduced.length() - 1
          );
        }
      }

      // Adaugare cifra in PIN
      else if (tasta >= '0' &&
               tasta <= '9' &&
               pinIntroduced.length() < 8) {

        pinIntroduced += tasta;
      }

      break;
    }

    case OPENING: {

      lcdAfis("Acces permis",
              "Banda pornita");

      digitalWrite(PIN_LED_GREEN, LOW);

      servoMisca(SERVO_DESCHIS);


      digitalWrite(PIN_LED_GREEN, LOW);
      digitalWrite(PIN_LED_RED, HIGH); // merge banda

      tranzitie(WAIT_FOR_PASS);

      break;
    }

    case WAIT_FOR_PASS: {

      digitalWrite(PIN_LED_GREEN, HIGH);

      unsigned long scurs =
        millis() - timerStart;

      // Detectare trecere prin al doilea senzor
      if (irSpate()) {

        Serial.println("Trecere OK");

        tranzitie(CLOSING);

        break;
      }

      // Timeout trecere
      if (scurs >= TIMEOUT_TRECE_MS) {

        tranzitie(EROARE);

        break;
      }

      unsigned long ramase =
        (TIMEOUT_TRECE_MS - scurs) / 1000;

      char buf[17];

      lcd.setCursor(0, 1);

      snprintf(buf,
               sizeof(buf),
               "Treceti %2lus ",
               ramase);

      lcd.print(buf);

      break;
    }

    case CLOSING: {

      lcdAfis("Trecere OK",
              "Se inchide");

      bandaOpreste();

      servoMisca(SERVO_INCHIS);

      digitalWrite(PIN_LED_GREEN, HIGH);
      digitalWrite(PIN_LED_RED, HIGH);

      delay(800);

      tranzitie(ST_IDLE);

      break;
    }

    case EROARE: {

      static unsigned long ultimBlink = 0;
      static bool ledState = false;

      bandaOpreste();

      digitalWrite(PIN_LED_GREEN, LOW);

      // Blink LED rosu in stare de eroare
      if (millis() - ultimBlink > 400) {

        ledState = !ledState;

        digitalWrite(PIN_LED_RED, ledState);

        ultimBlink = millis();
      }

      char tasta = keypad.getKey();

      // Reset eroare
      if (tasta == '*' || tasta == 'D') {

        servoMisca(SERVO_INCHIS);

        tranzitie(ST_IDLE);
      }

      break;
    }
  }
}

void tranzitie(Stare noua) {

  stare = noua;

  timerStart = millis();

  pinIntroduced = "";

  switch (noua) {

    case ST_IDLE:

      lcdAfis(" Sistem acces ",
              "Astept persoana");

      break;

    case WAIT_FOR_CONFIRM:

      lcdAfis("Introdu PIN:", "");

      break;

    case OPENING:
      break;

    case WAIT_FOR_PASS:

      lcdAfis("Puteti trece",
              "Banda activa");

      break;

    case CLOSING:
      break;

    case EROARE:

      lcdAfis(" EROARE ",
              "* sau D reset");

      break;
  }
}

bool irFata() {

  int val = analogRead(PIN_IR_FATA);

  Serial.print("Fata: ");
  Serial.println(val);

  return val > pragFata;
}

bool irSpate() {

  int val = analogRead(PIN_IR_SPATE);

  Serial.print("Spate: ");
  Serial.println(val);

  return val > pragSpate;
}

// Miscare lenta a servomotorului
void servoMisca(int target) {

  int current = servo.read();

  int pas = (target > current) ? 1 : -1;

  while (current != target) {

    current += pas;

    servo.write(current);

    delay(12);
  }
}

// Pornire banda transportoare
void bandaPorneste() {

  digitalWrite(MOTOR_IN1, HIGH);
  digitalWrite(MOTOR_IN2, LOW);

  digitalWrite(MOTOR_EN, HIGH);

  Serial.println("Banda PORNITA");
}

// Pornire banda transportoare inapoi
void bandaInapoi() {

  digitalWrite(MOTOR_IN1, LOW);
  digitalWrite(MOTOR_IN2, HIGH);

  digitalWrite(MOTOR_EN, HIGH);

  Serial.println("Banda INAPOI");
}

// Oprire banda transportoare
void bandaOpreste() {

  digitalWrite(MOTOR_EN, LOW);

  digitalWrite(MOTOR_IN1, LOW);
  digitalWrite(MOTOR_IN2, LOW);
}

// Afisare mesaj pe LCD
void lcdAfis(const char* r1,
             const char* r2) {

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print(r1);

  if (r2 && r2[0] != '\0') {

    lcd.setCursor(0, 1);
    lcd.print(r2);
  }
}