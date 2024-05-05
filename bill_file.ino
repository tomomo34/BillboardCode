// ***********************************************************
// *                                                         *
// *     ESP32 T8 - Zobrazeni na displeji ze souboru BMP     *
// *     rozlišení displeje 240x135 bodů, data 565           *
// *     verze 20210327                                      *
// *                                                         *
// ***********************************************************

// Zakladni sada BMP obrazku pro demo datovych souboru:
// cola, diana, hervis, iveco, kaufland, kosmetika, mascara, mercedes, mogul,
// obi, obi1, obi2, pepsi, pizza, siko, srouby, tesla, utulek

#include <TFT_eSPI.h>                // grafická knihovna
#include <SPI.h>
#include <Wire.h>
#include "FS.h"
#include "SD.h"                      // knihovna práce s SD kartou
#include "rozbite_sklo.h"            // obrázek rozbitého skla do programové paměti

SPIClass SDSPI(HSPI);

#define MY_CS       13               // piny pro připojení modulu SD karty
#define MY_SCLK     14
#define MY_MISO     2
#define MY_MOSI     15
// zapojeni pinu displeje pro info - bere se z konfiguracniho souboru
// MOSI = SDA       19
// MISO = DC         4
// SCLK             18
// CS                5 volny, nahrazen tremi piny CS pro jednotlive panely
// RST              23
// chipselekty pro prepinani panelu
#define cs1         32               // CS pro první panel
#define cs2         33               // CS pro druhý panel 
#define cs3         25               // CS pro třetí panel
#define BL          21               // vývod zatmívání PWM pro všechny panely (je na něm i LED)
// svit             A0 = 36          snímání úrovně okolního osvětlení
#define volba_1     34               // volba souboru s programem nižší bit 
#define volba_2     35               // volba souboru s programem vyšší bit

// rozmery okna pro jezdici text pri poruse
#define IWIDTH  240
#define IHEIGHT 28

TFT_eSPI    tft = TFT_eSPI();        // "tft"
TFT_eSprite img = TFT_eSprite(&tft); // sprite pro jezdici napis

uint16_t prac[32400];                // pracovní proměnná pro uložení obrazu ve formátu 565
String prikaz = "";                  // uložení jednoho načteného příkazového řádku
String jmeno = "";                   // jméno bmp souboru připravovaného k přenosu na displej
int panel = 0;                       // číslo panelu kam se bude zobrazovat
int prechod = 0;                     // číslo přechodobého jevu mezi předchozím a tímto obrázkem
int doba = 0;                        // parametr pro přechodový jev
char znak = 0;                       // na čtení ze souboru byte po bytu
unsigned long svit_prum = 3900;      // průměrný okolní svit
int strida = 255;

File povelyFile;                     // soubor povelů "00_program.txt"
File obrazFile;                      // aktuálně nečítaný obrázek

void setup()
{
  delay(500);
  pinMode(cs1, OUTPUT);             // chipselekty jednotlivych panelu
  pinMode(cs2, OUTPUT);
  pinMode(cs3, OUTPUT);

  pinMode(volba_1, INPUT);          // vývody pro volbu souboru s programem
  pinMode(volba_2, INPUT);

  ledcSetup(0, 4000, 8);            // nastavení kanálu na řízení jasu (kanál 0, f=4000Hz?,rozlišení 8 bit)
  ledcAttachPin(BL, 0);             // přidělení kanálu k pinu

  tft.init();                        // inicializace grafiky
  tft.setRotation(1);
  tft.setSwapBytes(true);
  tft.fillScreen(TFT_BLUE);          // pocatecni vybarvení modrou
  for (int r = 0; r < 30; r++) {     // mnohokrát měří jas, takže od začátku bude nastaveno správně
    nastav_jas();
  };
  Serial.begin(115200);              // sériová komunikace pro hlášení přes USB
  Serial.println("******************************************************");
  Serial.println("Start programu pro obsluhu billboardu");
  setupSDCard();                     // inicializace SD karty
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);            // výpočet velikosti karty v MB
  Serial.printf("Kapacita SD karty: %lluMB\n", cardSize);
  uint64_t cardFull = SD.usedBytes() / (1024 * 1024);           // výpočet obsazené paměti v MB
  Serial.printf("Kapacita obsazená: %lluMB", cardFull);
  Serial.print("      ");
  Serial.print(100 * long(cardFull) / long(cardSize));
  Serial.println("%");
  listDir(SD, "/", 0);                   // vypis obsahu karty do urovne 0 - kompletní funkční funkcni
}

// *********************************************************************************
// separace příkazů ze souboru "/00_program.txt" a jejich vykonání
void loop() {
  if (digitalRead (volba_2) == LOW) {                                 // výběr a otevření souboru s programem
    if (digitalRead (volba_1) == LOW) {
      povelyFile = SD.open("/03_program.txt");
    } else {
      povelyFile = SD.open("/02_program.txt");
    }
  } else {
    if (digitalRead(volba_1) == LOW) {
      povelyFile = SD.open("/01_program.txt");
    } else {
      povelyFile = SD.open("/00_program.txt");
    }
  }
  if (povelyFile.available()) {
    Serial.println("*********************************************************");
    Serial.println("Prikazovy soubor otevren");
  } else {
    Serial.println("Prikazovy soubor nejde otevrit!");
    porucha();                                  // vypis poruchy na displeji

    // sem dodat výpis chyby na displej
    povelyFile.close();
  };

  znak = 0;                                    // zahodíme vše až do prvního výskytu znaku #
  do {
    if (povelyFile.available()) {
      znak = povelyFile.read();
      //     Serial.print(char(znak));        // kontrola zahozené hlavičky
    } else {
      Serial.println("Nenalezen zadny prikaz v prikazovem souboru");
      // sem dodat vypis chyby na displej
    }
  } while (znak != '#');                      // teď následuje první příkaz souboru

  do {                                        // budeme načítat a vykonávat příkazy - začátek smyčky
    prikaz = "";
    do {                                      // zahodit vše až do CR nebo LF
      znak = povelyFile.read();
    } while ((znak != 0x0d) && (znak != 0x0a));
    do {                                      // zahodit všechny znaky jako CR LF do prvního platného
      znak = povelyFile.read();
    } while ((znak == 0x0d) || (znak == 0x0a) || (znak == ' '));
    do {                                      // číst příkaz až do CR, LF nebo "*"
      prikaz += char(znak);
      znak = povelyFile.read();
    } while ((znak != 0x0d) && (znak != 0x0a)); // teď víme, že příkaz skončil na CR nebo LF,
    if (prikaz != "#") {
      vykonej();
    }
  } while (prikaz != "#");
  povelyFile.close();
};

void panel_1() {
  digitalWrite(cs1, LOW);
  digitalWrite(cs2, HIGH);
  digitalWrite(cs3, HIGH);
}

void panel_2() {
  digitalWrite(cs1, HIGH);
  digitalWrite(cs2, LOW);
  digitalWrite(cs3, HIGH);
}

void panel_3() {
  digitalWrite(cs1, HIGH);
  digitalWrite(cs2, HIGH);
  digitalWrite(cs3, LOW);
}

void panel_123() {
  digitalWrite(cs1, LOW);
  digitalWrite(cs2, LOW);
  digitalWrite(cs3, LOW);
}

void panel_0() {
  digitalWrite(cs1, HIGH);
  digitalWrite(cs2, HIGH);
  digitalWrite(cs3, HIGH);
}

void nastav_jas() {
  svit_prum = (svit_prum * 9 + analogRead(A0)) / 10;
  if (svit_prum < 500) {
    strida = 10;
  }
  if (svit_prum >= 500 && svit_prum <= 2500) {
    strida = (svit_prum - 500) / 8.2 + 10 ;
    if (strida > 255) {
      strida = 255;
    }
  }
  ledcWrite(0, 265 - strida);   // dodatečné invertování funkce
}

void vykonej () {  // rozebere příkaz a vykoná ho
  nastav_jas();
  jmeno = "/";                                             // načíst jméno z promenne
  int i = -1;
  prikaz += "  ";
  do {
    i++;
    jmeno += prikaz[i];
  } while (prikaz[i + 1] != ' ');
  jmeno += ".bmp";

  String panelstr = "";                                   // jméno máme, vyházet mezery a načíst panel
  do {
    i++;
  } while (prikaz[i + 1] == ' ');

  do {                                                    // načíst panel kam se bude posílat
    i++;
    panelstr += prikaz[i];
  } while (prikaz[i + 1] != ' ');
  panel = panelstr.toInt();

  String prechodstr = "";                                 // panel máme, vyházet mezery
  do {
    i++;
  } while (prikaz[i + 1] == ' ');

  do {                                                    // načíst kód přechodového efektu
    i++;
    prechodstr += prikaz[i];
  } while (prikaz[i + 1] != ' ');
  prechod = prechodstr.toInt();

  do {                                                  // vyházet mezery mezi přechodem a rychlostí
    i++;
  } while (prikaz[i + 1] == ' ');

  String dobastr = "";

  do {                                                  // načíst rychlost v sekundách
    i++;
    dobastr += char(prikaz[i]);
  } while ((prikaz[i + 1] != ' ') && (prikaz[i + 1] != 0x0d) && (prikaz[i + 1] != 0x0a)); // ukončeno mezerou, CR nebo LF

  doba = dobastr.toInt();
  // dál nás tento příkazový řádek nezajímá, může na něm být komentář nebo něco jiného

  obrazFile = SD.open(jmeno);
  if (!obrazFile.available()) {
    Serial.print("Soubor ");
    Serial.print(jmeno);
    Serial.println(" nejde otevrit!");
    // sem dodat výpis chyby na displej
    obrazFile.close();
  };
  nactiFile(SD, jmeno.c_str());                     // preneseni souboru s obrázkem do pole
  // rozhodování který přechodový efekt se použije

  switch (panel) {
    case 1:
      panel_1(); break;
    case 2:
      panel_2(); break;
    case 3:
      panel_3(); break;
    case 4:
      panel_123(); break;
  }

  switch (prechod) {
    case 0:
      prechod00(); break;
    case 1:
      prechod01(); break;
    case 2:
      prechod02(); break;
    case 3:
      prechod03(); break;
    case 4:
      prechod04(); break;
    case 5:
      prechod05(); break;
    case 6:
      prechod06(); break;
    case 7:
      prechod07(); break;
    case 8:
      prechod08(); break;
    case 9:
      prechod09(); break;
    case 10:
      prechod10(); break;
    case 11:
      prechod11(); break;
    case 12:
      prechod12(); break;
    case 13:
      prechod13(); break;
    case 14:
      prechod14(); break;
    case 15:
      prechod15(); break;
    case 16:
      prechod16(); break;
    case 17:
      prechod17(); break;
    case 18:
      prechod18(); break;
    case 19:
      prechod19(); break;
    case 20:
      prechod20(); break;
    default:
      prechod00(); break;
  }
  for (int i = doba; i > 0; i--) {
    delay(1000);
  }
  panel_0();
};

void setupSDCard()
{
  SDSPI.begin(MY_SCLK, MY_MISO, MY_MOSI, MY_CS);
  //Assuming use of SPI SD card
  if (!SD.begin(MY_CS, SDSPI)) {
    Serial.println("SD karta nenalezena!");
    porucha();                          // signalizace poruchy na displeji
  } else {
    Serial.println("SD karta OK");
  }
}

void listDir(fs::FS & fs, const char * dirname, uint8_t levels) {
  Serial.printf("Listing directory: %s\n", dirname);

  File root = fs.open(dirname);
  if (!root) {
    Serial.println("Failed to open directory");
    porucha();                          // signalizace poruchy na displeji
    return;
  }
  if (!root.isDirectory()) {
    Serial.println("Not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if (levels) {
        listDir(fs, file.name(), levels - 1);
      }
    } else {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("  SIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}

void nactiFile(fs::FS & fs, const char * path)
{
  byte r, g, b;                                           // barevné složky
  long pozice;                                            // pořadí byte, kde začínají data
  Serial.printf("Nacitam soubor: %s\n", path);
  File file = fs.open(path);
  if (!file) {
    Serial.print("Nepodarilo se nacist graficky soubor ");
    Serial.print(fs.open(path));
    Serial.println(" !");
    return;
  }
  // zapomen prvních 10 byte hlavičky, rozmer je pevny 240x135
  for (int i = 0; i < 10; i++) {
    file.read();
  }
  // načti z dalších 4 bytů pozici, kde začínají data
  pozice = file.read();
  pozice = pozice + (256 * file.read());
  pozice = pozice + (256 * file.read());
  pozice = pozice + (256 * file.read()) - 14;             // odečteme pozici kde teď jsme
  // přesuň se PŘED první pozici dat - můžeš zapomínat
  for (int i = 0; i < pozice; i++) {
    file.read();
  }
  // nacti cely soubor do pameti a soucasne konvertuj na 565
  for (uint16_t i = 0; i < 32400; i++) {
    b = file.read() >> 3;
    g = file.read() >> 2;
    r = file.read() >> 3;
    prac[(239 - (i % 240)) + (240 * (i / 240))] = b + (g * 32) + (r * 2048 ) ;
  }
  file.close();
}

void porucha() {
  String inf ;
  tft.setRotation(1);
  tft.pushImage(0, 0,  240, 135, rozbite_sklo);
  inf = "      Billboard mimo provoz!   Volejte servis 606 680 550.";
  img.createSprite(IWIDTH, IHEIGHT);
  while (true) {
    for (int pos = 0; pos > -800; pos--) {  // tyto meze se musi upravit podle delky textu
      build_banner(inf, pos);
      img.pushSprite(0, 106);
      delay(6);                          // zpomaleni behu textu
    }
  }
  img.deleteSprite();
}

void build_banner(String msg, int xpos)
{
  int h = IHEIGHT;
  img.fillSprite(TFT_WHITE); // Optional here as we fill the sprite later anyway
  img.setTextSize(1);           // Font size scaling is x1
  img.setTextFont(4);           // Font 4 selected
  img.setTextColor(TFT_RED);    // blue text, no background colour
  img.setTextWrap(false);       // Turn of wrap so we can print past end of sprite
  img.setCursor(xpos + 200, 2); // Print text at xpos - sprite width
  img.print(msg);
}

// #################################################################################################################
// Přechodové efekty

// střídání nejrychlejší výměnou
void prechod00 () {
  tft.pushImage(0, 0, 240, 135, prac);
}

// překreslení vodorovně zprava doleva rychle
void prechod01() {
  for (int x = 0; x < 240; x++) {
    for (int y = 0; y < 135; y++) {
      tft.drawPixel(x, y, prac[y * 240 + x]);
    }
  }
}

// překreslení vodorovně zprava doleva pomalu
void prechod02() {
  for (int x = 0; x < 240; x++) {
    for (int y = 0; y < 135; y++) {
      tft.drawPixel(x, y, prac[y * 240 + x]);
    }
    delay(8);
  }
}

// překreslení vodorovně zleva doprava rychle
void prechod03() {
  for (int x = 239; x >= 0; x--) {
    for (int y = 0; y < 135; y++) {
      tft.drawPixel(x, y, prac[y * 240 + x]);
    }
  }
}

// překreslení vodorovně zleva doprava pomalu
void prechod04() {
  for (int x = 239; x >= 0; x--) {
    for (int y = 0; y < 135; y++) {
      tft.drawPixel(x, y, prac[y * 240 + x]);
    }
    delay(8);
  }
}

// překreslení svisle zdola nahoru rychle
void prechod05() {
  for (int y = 0; y < 135; y++) {
    for (int x = 0; x < 240; x++) {
      tft.drawPixel(x, y, prac[y * 240 + x]);
    }
  }
}

// překreslení svisle zdola nahoru pomalu
void prechod06() {
  for (int y = 0; y < 135; y++) {
    for (int x = 0; x < 240; x++) {
      tft.drawPixel(x, y, prac[y * 240 + x]);
    }
    delay(12);
  }
}

// překreslení svisle shora dolů rychle
void prechod07() {
  for (int y = 134; y >= 0; y--) {
    for (int x = 0; x < 240; x++) {
      tft.drawPixel(x, y, prac[y * 240 + x]);
    }
  }
}

// překreslení svisle shora dolů pomalu
void prechod08() {
  for (int y = 134; y >= 0; y--) {
    for (int x = 0; x < 240; x++) {
      tft.drawPixel(x, y, prac[y * 240 + x]);
    }
    delay(12);
  }
}

// zhasne respektive rychle přepíše displej černou barvou, na zadaném obrázku nezáleží
void prechod09 () {
  digitalWrite(5, HIGH);                     // aktivace společného selectu
  tft.fillScreen(TFT_BLACK);
  digitalWrite(5, LOW);
}

// rozsvítí respektive rychle přepíše displej bílou barvou, na zadaném obrázku nezáleží
void prechod10 () {
  digitalWrite(5, HIGH);                     // aktivace společného selectu
  tft.fillScreen(TFT_WHITE);
  digitalWrite(5, LOW);
}

//  rozsvítí respektive rychle přepíše displej modrou barvou, na zadaném obrázku nezáleží
void prechod11 () {
  digitalWrite(5, HIGH);                     // aktivace společného selectu
  tft.fillScreen(TFT_BLUE);
  digitalWrite(5, LOW);
}

// prolínání obrazů rychle
void prechod12 () {
  for (int i = 0; i < 300; i++) {
    for (int j = 0; j < 500; j++) {
      int  x = random(0, 240);
      int  y = random(0, 135);
      tft.drawPixel(x, y , prac[y * 240 + x]);
    }
  }
  tft.pushImage(0, 0,  240, 135, prac);
}

// prolínání obrazů pomalu
void prechod13 () {
  for (int i = 1; i < 300; i++) {
    for (int j = 1; j < 400; j++) {
      int  x = random(0, 240);
      int  y = random(0, 135);
      tft.drawPixel(x, y , prac[y * 240 + x]);
    }
    delay(300 / i);
  }
  tft.pushImage(0, 0,  240, 135, prac);
}

// překreslení ze středu rychle
void prechod14 () {
  int xx;
  int yy;
  for (int i = 67; i >= 0; i--) {
    yy = i;
    for (int x = i; x < 240 - i; x++) {
      tft.drawPixel(x, yy, prac[yy * 240 + x]);
    }
    xx = 240 - i;
    for (int y = i; y < 135 - i; y++) {
      tft.drawPixel(xx, y, prac[y * 240 + xx]);
    }
    yy = 135 - i;
    for (int x = 240 - i; x >= i; x--) {
      tft.drawPixel(x, yy, prac[yy * 240 + x]);
    }
    xx = i;
    for (int y = 135 - i; y >= i; y--) {
      tft.drawPixel(xx, y, prac[y * 240 + xx]);
    }
  }
}

// překreslení ze středu pomalu
void prechod15 () {
  int xx;
  int yy;
  for (int i = 67; i >= 0; i--) {
    yy = i;
    for (int x = i; x < 240 - i; x++) {
      tft.drawPixel(x, yy, prac[yy * 240 + x]);
    }
    xx = 240 - i;
    for (int y = i; y < 135 - i; y++) {
      tft.drawPixel(xx, y, prac[y * 240 + xx]);
    }
    yy = 135 - i;
    for (int x = 240 - i; x >= i; x--) {
      tft.drawPixel(x, yy, prac[yy * 240 + x]);
    }
    xx = i;
    for (int y = 135 - i; y >= i; y--) {
      tft.drawPixel(xx, y, prac[y * 240 + xx]);
    }
    delay(25);
  }
}

// překlreslit do středu rychle
void prechod16 () {
  int xx;
  int yy;
  for (int i = 0; i <= 67; i++) {
    yy = i;
    for (int x = i; x < 240 - i; x++) {
      tft.drawPixel(x, yy, prac[yy * 240 + x]);
    }
    xx = 240 - i;
    for (int y = i; y < 135 - i; y++) {
      tft.drawPixel(xx, y, prac[y * 240 + xx]);
    }
    yy = 135 - i;
    for (int x = 240 - i; x >= i; x--) {
      tft.drawPixel(x, yy, prac[yy * 240 + x]);
    }
    xx = i;
    for (int y = 134 - i; y >= i; y--) {
      tft.drawPixel(xx, y, prac[y * 240 + xx]);
    }
  }
}

// překlreslit do středu pomalu
void prechod17 () {
  int xx;
  int yy;
  for (int i = 0; i <= 67; i++) {
    yy = i;
    for (int x = i; x < 240 - i; x++) {
      tft.drawPixel(x, yy, prac[yy * 240 + x]);
    }
    xx = 240 - i;
    for (int y = i; y < 135 - i; y++) {
      tft.drawPixel(xx, y, prac[y * 240 + xx]);
    }
    yy = 135 - i;
    for (int x = 240 - i; x >= i; x--) {
      tft.drawPixel(x, yy, prac[yy * 240 + x]);
    }
    xx = i;
    for (int y = 134 - i; y >= i; y--) {
      tft.drawPixel(xx, y, prac[y * 240 + xx]);
    }
    delay(25);
  }
}

// ???
void prechod18 () {
  tft.pushImage(0, 0, 240, 135, prac);
}

// ???
void prechod19 () {
  tft.pushImage(0, 0, 240, 135, prac);
}

// ???
void prechod20 () {
  tft.pushImage(0, 0, 240, 135, prac);
}
