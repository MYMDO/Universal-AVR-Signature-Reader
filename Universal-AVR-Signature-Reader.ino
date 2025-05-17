#include <SPI.h>

// Піни підключення до цільового мікроконтролера (наприклад, ATtiny13A)
const int RESET_PIN = 10; // SS пін Arduino, підключений до RESET цілі
const int MOSI_PIN = 11;  // MOSI
const int MISO_PIN = 12;  // MISO
const int SCK_PIN = 13;   // SCK

// ISP команди
const byte PROG_ENABLE_CMD[] = {0xAC, 0x53, 0x00, 0x00}; // Команда входу в режим програмування
const byte READ_SIGNATURE_CMD = 0x30;                   // Команда читання сигнатурного байта

// Структура для зберігання сигнатури та імені пристрою
struct DeviceSignature {
  byte sig0;          // Signature Byte 0 (Vendor Code)
  byte sig1;          // Signature Byte 1 (Part Family and Flash Size)
  byte sig2;          // Signature Byte 2 (Part Number)
  const char* name;   // Ім'я мікроконтролера
};

// Масив відомих мікроконтролерів та їх сигнатур
// !!! ДОДАВАЙТЕ НОВІ МІКРОКОНТРОЛЕРИ СЮДИ !!!
const DeviceSignature knownDevices[] = {
  {0x1E, 0x90, 0x07, "ATtiny13A / ATtiny13"},
  {0x1E, 0x91, 0x0A, "ATtiny25"}, // (Також може бути ATtiny25V)
  {0x1E, 0x92, 0x0A, "ATtiny45"}, // (Також може бути ATtiny45V)
  {0x1E, 0x93, 0x0B, "ATtiny85"}, // (Також може бути ATtiny85V)
  {0x1E, 0x92, 0x06, "ATtiny44A / ATtiny44"},
  {0x1E, 0x93, 0x0C, "ATtiny84A / ATtiny84"},
  {0x1E, 0x93, 0x0A, "ATmega8A / ATmega8 / ATmega8L"},
  {0x1E, 0x94, 0x03, "ATmega16A / ATmega16 / ATmega16L"},
  {0x1E, 0x95, 0x02, "ATmega32A / ATmega32 / ATmega32L"},
  {0x1E, 0x94, 0x06, "ATmega168A / ATmega168PA / ATmega168 / ATmega168P"},
  {0x1E, 0x95, 0x0F, "ATmega328P / ATmega328PB"},
  {0x1E, 0x95, 0x14, "ATmega328"}, // (Без "P")
  {0x1E, 0x97, 0x03, "ATmega1280"},
  {0x1E, 0x98, 0x01, "ATmega2560"}
  // Додайте інші пристрої тут у форматі:
  // {SIG_BYTE_0, SIG_BYTE_1, SIG_BYTE_2, "Ім'я пристрою"},
};
// Автоматичне визначення кількості відомих пристроїв
const int numKnownDevices = sizeof(knownDevices) / sizeof(knownDevices[0]);

void setup() {
  Serial.begin(9600);
  while (!Serial) {
    ; // Чекаємо на підключення Serial монітора
  }
  Serial.println("Universal AVR Signature Reader");
  Serial.println("--------------------------------");

  // Налаштування пінів
  pinMode(RESET_PIN, OUTPUT);
  pinMode(MOSI_PIN, OUTPUT);
  pinMode(MISO_PIN, INPUT);
  pinMode(SCK_PIN, OUTPUT);

  // Ініціалізація SPI
  SPI.begin();
  // Швидкість SPI (250 кГц) є безпечною для більшості AVR на заводських налаштуваннях
  SPI.beginTransaction(SPISettings(250000, MSBFIRST, SPI_MODE0));

  if (enterProgrammingMode()) {
    Serial.println("Entered programming mode successfully.");
    byte signatureBytes[3];
    if (readSignatureBytes(signatureBytes)) {
      identifyDevice(signatureBytes[0], signatureBytes[1], signatureBytes[2]);
    } else {
      Serial.println("Failed to read signature bytes.");
    }
  } else {
    Serial.println("Failed to enter programming mode.");
    Serial.println("Check connections, target power, and ensure target is not locked.");
  }

  // Вихід з режиму програмування
  digitalWrite(RESET_PIN, HIGH);
  SPI.endTransaction();
  SPI.end();
  Serial.println("--------------------------------");
  Serial.println("Exited programming mode. Done.");
}

void loop() {
  // Нічого не робимо в циклі
}

// Функція для надсилання ISP команди та отримання відповіді (4 байти)
uint32_t isp_transaction(byte cmd1, byte cmd2, byte cmd3, byte cmd4) {
  byte b1 = SPI.transfer(cmd1);
  byte b2 = SPI.transfer(cmd2);
  byte b3 = SPI.transfer(cmd3);
  byte b4 = SPI.transfer(cmd4);
  return ((uint32_t)b1 << 24) | ((uint32_t)b2 << 16) | ((uint32_t)b3 << 8) | (uint32_t)b4;
}

// Функція для входу в режим програмування
bool enterProgrammingMode() {
  Serial.println("Attempting to enter programming mode...");
  digitalWrite(RESET_PIN, HIGH);
  delay(50);

  digitalWrite(RESET_PIN, LOW);
  delayMicroseconds(20); // t_RST > 2 CPU cycles of target

  byte b1 = SPI.transfer(PROG_ENABLE_CMD[0]);
  byte b2 = SPI.transfer(PROG_ENABLE_CMD[1]);
  byte b3 = SPI.transfer(PROG_ENABLE_CMD[2]); // Очікуємо 0x53 тут
  byte b4 = SPI.transfer(PROG_ENABLE_CMD[3]);

  if (Serial) { // Виводимо відповідь, якщо Serial доступний
    Serial.print("Prog Enable Response: 0x");
    Serial.print(b1, HEX); Serial.print(" 0x");
    Serial.print(b2, HEX); Serial.print(" 0x");
    Serial.print(b3, HEX); Serial.print(" 0x");
    Serial.println(b4, HEX);
  }

  if (b3 == PROG_ENABLE_CMD[1]) { // Перевірка, чи повернувся 0x53
    delay(25); // t_WD_FLASH > 20ms
    return true;
  }
  return false;
}

// Функція для читання сигнатурних байтів у наданий масив
bool readSignatureBytes(byte* sig_array) {
  Serial.println("Reading signature bytes...");
  // Команда: 0x30 0x00 <address_low> <read_value>
  // Адреси сигнатур: 0x0000, 0x0001, 0x0002
  for (byte i = 0; i < 3; i++) {
    uint32_t result = isp_transaction(READ_SIGNATURE_CMD, 0x00, i, 0x00);
    sig_array[i] = (byte)(result & 0xFF); // Четвертий байт відповіді містить сигнатуру
    if (Serial) {
        Serial.print("  Raw Signature Byte ");
        Serial.print(i);
        Serial.print(" (Addr 0x000");
        Serial.print(i);
        Serial.print("): 0x");
        Serial.println(sig_array[i], HEX);
    }
  }
  return true; // Можна додати перевірку, якщо потрібно (наприклад, чи всі байти не 0xFF)
}

// Функція для ідентифікації пристрою за сигнатурою
void identifyDevice(byte s0, byte s1, byte s2) {
  Serial.println("--------------------------------");
  Serial.print("Full Signature Read: 0x");
  if (s0 < 0x10) Serial.print("0"); Serial.print(s0, HEX);
  if (s1 < 0x10) Serial.print("0"); Serial.print(s1, HEX);
  if (s2 < 0x10) Serial.print("0"); Serial.println(s2, HEX);

  bool found = false;
  for (int i = 0; i < numKnownDevices; i++) {
    if (knownDevices[i].sig0 == s0 &&
        knownDevices[i].sig1 == s1 &&
        knownDevices[i].sig2 == s2) {
      Serial.print("Device Identified As: ");
      Serial.println(knownDevices[i].name);
      found = true;
      break; // Знайшли, виходимо з циклу
    }
  }

  if (!found) {
    Serial.println("Device signature not found in the known list.");
    Serial.println("You can add it to the 'knownDevices' array in the code.");
  }
}