#include <Arduino.h>
#include <stdio.h>
#include <stdlib.h>
#include <DHT.h>
#include "HX711.h"
#include "GPS.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SoftwareSerial.h> // Required for GPS on Arduino Uno

// --- PIN DEFINITIONS ---
#define BROCHE_DHT 4        
#define TYPE_DHT DHT11
#define BROCHE_FUMEE A0    
#define BROCHE_FLAMME A1   
#define BROCHE_LED 2
#define BROCHE_LED_AVERTISSEMENT 12   

// Shifted scale and actuator pins to fit within Uno's 13 digital pins
#define DOUT 3        
#define CLK 5         
#define BROCHE_BUZZER 13
#define BROCHE_POMPE 6

#define FACTEUR_CALIBRATION -7050.0

// --- THRESHOLDS ---
const float SEUIL_NIVEAU_NORMAL = 15.0; 
const float SEUIL_NIVEAU_CRITIQUE = 40.0; 
const int SEUIL_FUMEE_ACCIDENT = 750; 
const float SEUIL_TEMP_ALERT = 45.0;

// --- INSTANCES ---
DHT dht(BROCHE_DHT, TYPE_DHT);
HX711 balance;

// Set up Software Serial on pins 10 (RX) and 11 (TX)
SoftwareSerial liaisonGps(10, 11); 
GPS gps(&liaisonGps); 

LiquidCrystal_I2C lcd(0x27, 16, 2);

// --- TIMING VARIABLES ---
unsigned long dernierClignotement = 0;
bool etatLed = false;
unsigned long dernierTraitement = 0;
unsigned long dernierTicHorloge = 0;
unsigned long tempsFinInitialisation = 0;

int anneeSysteme = 2026;
int moisSysteme = 6;
int jourSysteme = 23;
int heureSysteme = 21;
int minuteSysteme = 52;
int secondeSysteme = 0;

typedef struct 
{
  String dateHeure;
  String latitude;
  String longitude;
  String carteLien;
  float vitesseActuelle;
  float distanceTotale;
} DonneesGps;

typedef struct 
{
  String flamme;
  float fumee;
  float temperature;
  float humidite;
  float pression;
  float niveau;
  DonneesGps gpsData;
} Donnee;

Donnee capteur_flamme_fumee(Donnee info);
Donnee capteur_temperature_humidite(Donnee info);
Donnee pression_niveau(Donnee info);
Donnee fonction_GPS(Donnee info);
void miseAJourHorlogeSysteme();
String obtenirDateHeureSysteme();
void afficherLCD(Donnee info);
void afficherSerial(Donnee info);
void Alarme(int zone, float fumee, Donnee info);
void clignotementNonBloquant();
void extraireCoordonnees(String rawGps, DonneesGps &data);

void setup() 
{
  Serial.begin(115200); // For computer serial monitor connection
  
  pinMode(BROCHE_LED, OUTPUT); 
  pinMode(BROCHE_LED_AVERTISSEMENT, OUTPUT);
  pinMode(BROCHE_BUZZER, OUTPUT);
  pinMode(BROCHE_POMPE, OUTPUT);

  digitalWrite(BROCHE_BUZZER, LOW);
  digitalWrite(BROCHE_POMPE, LOW);
  digitalWrite(BROCHE_LED_AVERTISSEMENT, LOW);

  // Initialize Software Serial for GPS at 9600 baud
  liaisonGps.begin(9600); 
  if (!gps.begin()) 
  {
    Serial.println(F("Erreur de liaison GPS"));
  }
  
  dht.begin();
  balance.begin(DOUT, CLK);
  balance.set_scale(FACTEUR_CALIBRATION); 
  
  lcd.begin();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print(F("Stabilisation..."));
  
  delay(4000); 
  tempsFinInitialisation = millis();
  
  lcd.clear();
  lcd.print(F("Systeme Prete"));
  dernierTicHorloge = millis();
}

void loop() 
{
  static Donnee infosCourantes;
  
  // Software Serial needs active monitoring to capture bytes correctly
  if (liaisonGps.available() > 0) {
     // Allows library processing inside underlying buffer layers
  }
  
  miseAJourHorlogeSysteme();
  infosCourantes = capteur_flamme_fumee(infosCourantes);

  int lectureFlamme = analogRead(BROCHE_FLAMME);
  int zoneActuelle = map(lectureFlamme, 0, 1023, 0, 3); 
  
  if (millis() - tempsFinInitialisation > 5000)  
  {
    Alarme(zoneActuelle, infosCourantes.fumee, infosCourantes);
  }

  unsigned long millisecondesCourantes = millis();
  if (millisecondesCourantes - dernierTraitement >= 2000) 
  {
    dernierTraitement = millisecondesCourantes;
    
    infosCourantes = fonction_GPS(infosCourantes);
    infosCourantes = capteur_temperature_humidite(infosCourantes);
    infosCourantes = pression_niveau(infosCourantes);
    
    afficherLCD(infosCourantes);
    afficherSerial(infosCourantes);
  }
  
  clignotementNonBloquant();
}

void extraireCoordonnees(String rawGps, DonneesGps &data) 
{
  if (rawGps.indexOf("$PUBX,00") == -1) 
  {
    data.latitude = "0.000000";
    data.longitude = "0.000000";
    data.carteLien = "No Fix";
    return;
  }

  int indices[15]; // Downsized buffer array to preserve Uno RAM space
  int compteur = 0;
  for (unsigned int i = 0; i < rawGps.length(); i++) 
  {
    if (rawGps.charAt(i) == ',') 
    {
      indices[compteur++] = i;
      if (compteur >= 15) break;
    }
  }

  if (compteur < 6) 
  {
    data.latitude = "0.000000";
    data.longitude = "0.000000";
    data.carteLien = "No Fix";
    return;
  }

  String bruteLat = rawGps.substring(indices[2] + 1, indices[3]);
  String directionLat = rawGps.substring(indices[3] + 1, indices[4]);
  String bruteLon = rawGps.substring(indices[4] + 1, indices[5]);
  String directionLon = rawGps.substring(indices[5] + 1, indices[6]);

  if (bruteLat.length() < 4 || bruteLon.length() < 4) 
  {
    data.latitude = "0.000000";
    data.longitude = "0.000000";
    data.carteLien = "No Fix";
    return;
  }

  float degLat = bruteLat.substring(0, 2).toFloat();
  float minLat = bruteLat.substring(2).toFloat();
  float decLat = degLat + (minLat / 60.0);
  if (directionLat == "S") decLat = -decLat;

  float degLon = bruteLon.substring(0, 3).toFloat();
  float minLon = bruteLon.substring(3).toFloat();
  float decLon = degLon + (minLon / 60.0);
  if (directionLon == "W") decLon = -decLon;

  data.latitude = String(decLat, 4);  // Reduced float precision string allocation to save RAM
  data.longitude = String(decLon, 4);
  data.carteLien = "maps.google.com/?q=" + data.latitude + "," + data.longitude;
}

void miseAJourHorlogeSysteme() 
{
  if (millis() - dernierTicHorloge >= 1000) 
  {
    dernierTicHorloge += 1000;
    secondeSysteme++;
    if (secondeSysteme >= 60) 
    {
      secondeSysteme = 0;
      minuteSysteme++;
      if (minuteSysteme >= 60) 
      {
        minuteSysteme = 0;
        heureSysteme++;
        if (heureSysteme >= 24) 
        {
          heureSysteme = 0;
          jourSysteme++;
          if ((moisSysteme == 4 || moisSysteme == 6 || moisSysteme == 9 || moisSysteme == 11) && jourSysteme > 30) { jourSysteme = 1; moisSysteme++; }
          else if (moisSysteme == 2 && jourSysteme > 28) { jourSysteme = 1; moisSysteme++; } 
          else if (jourSysteme > 31) { jourSysteme = 1; moisSysteme++; }
          if (moisSysteme > 12) { moisSysteme = 1; anneeSysteme++; }
        }
      }
    }
  }

  String dateHeureBruteGps = gps.getDateTime();
  if (dateHeureBruteGps != "" && dateHeureBruteGps != "0" && dateHeureBruteGps.length() >= 14 && !dateHeureBruteGps.startsWith("0000")) 
  {
    int gpsAnnee   = dateHeureBruteGps.substring(0, 4).toInt();
    int gpsMois    = dateHeureBruteGps.substring(4, 6).toInt();
    int gpsJour    = dateHeureBruteGps.substring(6, 8).toInt();
    int gpsHeure   = dateHeureBruteGps.substring(8, 10).toInt();
    int gpsMinute  = dateHeureBruteGps.substring(10, 12).toInt();
    int gpsSeconde = dateHeureBruteGps.substring(12, 14).toInt();
    
    gpsHeure += 2; 
    if (gpsHeure >= 24) { gpsHeure -= 24; gpsJour++; }

    if (gpsAnnee >= 2026) 
    {
      anneeSysteme = gpsAnnee;
      moisSysteme = gpsMois;
      jourSysteme = gpsJour;
      heureSysteme = gpsHeure;
      minuteSysteme = gpsMinute;
      secondeSysteme = gpsSeconde;
    }
  }
}

String obtenirDateHeureSysteme() 
{
  char tampon[20];
  sprintf(tampon, "%02d-%02d %02d:%02d:%02d", moisSysteme, jourSysteme, heureSysteme, minuteSysteme, secondeSysteme);
  return String(tampon);
}

Donnee fonction_GPS(Donnee info) 
{
  String trameBrute = gps.getGeolocation();
  extraireCoordonnees(trameBrute, info.gpsData);
  info.gpsData.vitesseActuelle = gps.getSpeed();
  info.gpsData.distanceTotale = gps.getDistance();
  info.gpsData.dateHeure = obtenirDateHeureSysteme();
  return info;
}

void clignotementNonBloquant() 
{
  unsigned long millisecondesCourantes = millis();
  if (millisecondesCourantes - dernierClignotement >= 500) 
  {
    dernierClignotement = millisecondesCourantes;
    etatLed = !etatLed;
    digitalWrite(BROCHE_LED, etatLed); 
  }
}

Donnee capteur_flamme_fumee(Donnee info) 
{
  int lectureFumee = analogRead(BROCHE_FUMEE);
  int lectureFlamme = analogRead(BROCHE_FLAMME);
  int zone = map(lectureFlamme, 0, 1023, 0, 3);

  if (zone == 0) info.flamme = F("Proche");
  else if (zone == 1) info.flamme = F("Detectee");
  else info.flamme = F("Normal");

  info.fumee = lectureFumee; 
  return info;
}

void Alarme(int zone, float fumee, Donnee info) 
{
  bool incidentCritique = false;
  bool avertissementSysteme = false;
  
  if (zone < 2 || fumee > SEUIL_FUMEE_ACCIDENT) 
  {
    incidentCritique = true;
  }

  if (incidentCritique) 
  {
    digitalWrite(BROCHE_POMPE, HIGH); 
    digitalWrite(BROCHE_BUZZER, HIGH);  

    unsigned long minuteurBasculement = millis();
    bool etatBasculementCritique = true;
    
    while(true) 
    {
      digitalWrite(BROCHE_POMPE, HIGH); 
      if (millis() - minuteurBasculement >= 800) 
      { 
        minuteurBasculement = millis();
        etatBasculementCritique = !etatBasculementCritique;
        
        digitalWrite(BROCHE_BUZZER, etatBasculementCritique);
        digitalWrite(BROCHE_LED_AVERTISSEMENT, etatBasculementCritique); 
        
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(F("!! LOCKDOWN !!"));
      }
    }
  } 
  
  if (info.temperature > SEUIL_TEMP_ALERT || info.pression > SEUIL_NIVEAU_NORMAL || info.niveau > SEUIL_NIVEAU_CRITIQUE) 
  {
    avertissementSysteme = true;
  }

  if (avertissementSysteme) 
  {
    digitalWrite(BROCHE_POMPE, LOW); 
    digitalWrite(BROCHE_BUZZER, LOW); 
    digitalWrite(BROCHE_LED_AVERTISSEMENT, HIGH); 

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("SYSTEM WARNING!"));
  } 
  else 
  {
    digitalWrite(BROCHE_POMPE, LOW);
    digitalWrite(BROCHE_BUZZER, LOW);
    digitalWrite(BROCHE_LED_AVERTISSEMENT, LOW); 
  }
}

Donnee capteur_temperature_humidite(Donnee info) 
{
  info.temperature = dht.readTemperature();
  info.humidite = dht.readHumidity();
  if (isnan(info.temperature)) info.temperature = 0.0;
  if (isnan(info.humidite)) info.humidite = 0.0;
  return info;
}

Donnee pression_niveau(Donnee info) 
{
  if (balance.is_ready()) info.pression = balance.get_units(2); 
  else info.pression = 0;
  info.niveau = info.pression; 
  return info;
}

void afficherLCD(Donnee info) 
{
  static unsigned long dernierChangementPage = 0;
  static int page = 0;
  unsigned long millisecondesCourantes = millis();

  if (millisecondesCourantes - dernierChangementPage >= 3000) 
  {
    dernierChangementPage = millisecondesCourantes;
    page = (page + 1) % 3; 
    lcd.clear();
  }

  if (page == 0) 
  {
    lcd.setCursor(0, 0);
    lcd.print(info.gpsData.dateHeure);
  } 
  else if (page == 1) 
  {
    lcd.setCursor(0, 0);
    lcd.print(F("T:")); lcd.print(info.temperature, 1);
    lcd.print(F(" H:")); lcd.print(info.humidite, 0);
  } 
  else if (page == 2) 
  {
    lcd.setCursor(0, 0);
    lcd.print(F("Lat:")); lcd.print(info.gpsData.latitude);
  }
}

void afficherSerial(Donnee info) 
{
  Serial.println(F("\n--- DIAGNOSTIC ARDUINO UNO ---"));
  Serial.print(F("Heure : ")); Serial.println(info.gpsData.dateHeure);
  Serial.print(F("Lat :   ")); Serial.println(info.gpsData.latitude);
  Serial.print(F("Lon :   ")); Serial.println(info.gpsData.longitude); // Fixed line
  Serial.print(F("Temp :  ")); Serial.println(info.temperature, 1);
  Serial.print(F("Fumee : ")); Serial.println(info.fumee);
  Serial.println(F("------------------------------"));
}
