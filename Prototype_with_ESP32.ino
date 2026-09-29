#include <Arduino.h>
#include <stdio.h>
#include <stdlib.h>
#include <DHT.h>
#include "HX711.h"
#include "GPS.h"
#include "Adafruit_Sensor.h" 
#include <SPI.h>       
#include <SD.h>        
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Firebase_ESP_Client.h>
#include <time.h>

#ifndef WIFI_SSID
#define WIFI_SSID "PRIVATE CONNECTION"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

#ifndef TELEGRAM_BOT_TOKEN
#define TELEGRAM_BOT_TOKEN ""
#endif

#ifndef TELEGRAM_CHAT_ID
#define TELEGRAM_CHAT_ID ""
#endif

#define FIREBASE_HOST "" 
#define FIREBASE_AUTH ""

#define BROCHE_DHT 4        
#define TYPE_DHT DHT11
#define BROCHE_FUMEE 34    
#define BROCHE_FLAMME 35  
#define BROCHE_LED 2
#define BROCHE_LED_AVERTISSEMENT 12   
#define BROCHE_CS_SD 5

#define FACTEUR_CALIBRATION -7050.0
#define DOUT 32        
#define CLK 33         

#define BROCHE_BUZZER 13
#define BROCHE_POMPE 26

const float SEUIL_NIVEAU_NORMAL = 15.0; 
const float SEUIL_NIVEAU_CRITIQUE = 40.0; 
const int SEUIL_FUMEE_ACCIDENT = 750;
const float SEUIL_TEMP_ALERT = 45.0;

DHT dht(BROCHE_DHT, TYPE_DHT);
HX711 balance;
GPS gps(&Serial2);
LiquidCrystal_I2C lcd(0x27, 16, 2);

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

unsigned long dernierClignotement = 0;
bool etatLed = false;
unsigned long dernierTraitement = 0;
unsigned long dernierTicHorloge = 0;
unsigned long derniereAlerteTelegram = 0;
unsigned long tempsFinInitialisation = 0;

const unsigned long delaiAttenteAlerte = 300000; 

int anneeSysteme = 2026;
int moisSysteme = 6;
int jourSysteme = 8;
int heureSysteme = 10;
int minuteSysteme = 36;
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
void envoyerAlerteTelegram(String messageTelegram);
void enregistreurSD(Donnee info);
void clignotementNonBloquant();
void connexionWiFi();
void extraireCoordonnees(String rawGps, DonneesGps &data);
void televerserFirebase(Donnee info);

void setup() 
{
  Serial.begin(115200);
  pinMode(BROCHE_LED, OUTPUT); 
  pinMode(BROCHE_LED_AVERTISSEMENT, OUTPUT);
  pinMode(BROCHE_BUZZER, OUTPUT);
  pinMode(BROCHE_POMPE, OUTPUT);

  digitalWrite(BROCHE_BUZZER, LOW);
  digitalWrite(BROCHE_POMPE, LOW);
  digitalWrite(BROCHE_LED_AVERTISSEMENT, LOW);

  connexionWiFi();

  if (WiFi.status() == WL_CONNECTED) 
  {
    configTime(3600, 0, "pool.ntp.org", "time.nist.gov");
    delay(1500);
    struct tm infoTemps;
    if (getLocalTime(&infoTemps)) 
    {
      anneeSysteme = infoTemps.tm_year + 1900;
      moisSysteme = infoTemps.tm_mon + 1;
      jourSysteme = infoTemps.tm_mday;
      heureSysteme = infoTemps.tm_hour;
      minuteSysteme = infoTemps.tm_min;
      secondeSysteme = infoTemps.tm_sec;
    }

    Serial.println("Initialisation de la liaison Firebase...");
    config.database_url = FIREBASE_HOST;
    config.signer.tokens.legacy_token = FIREBASE_AUTH;
    
    Firebase.reconnectWiFi(true);
    Firebase.begin(&config, &auth);
    Serial.println("Liaison Firebase etablie avec succes.");
  }

  if (WiFi.status() == WL_CONNECTED) 
  {
    Serial.println("Test de la liaison avec l'API Telegram...");
    envoyerAlerteTelegram("🔄 Coeur de Securite ESP32 En Ligne : Liaison API reussie.");
  }

  Serial.print("Initialisation de la carte SD...");
  if (!SD.begin(BROCHE_CS_SD)) 
  {
    Serial.println("Échec du montage de la carte SD !");
  } 
  else 
  {
    Serial.println("Carte SD initialisée avec succès.");
  }

  Serial2.begin(9600, SERIAL_8N1, 16, 17);
  if (!gps.begin()) 
  {
    Serial.println("Erreur GPS");
  }
  
  dht.begin();
  balance.begin(DOUT, CLK);
  balance.set_scale(FACTEUR_CALIBRATION); 
  
  lcd.begin();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Stabilisation...");
  
  delay(10000); 
  tempsFinInitialisation = millis();
  
  lcd.clear();
  lcd.print("Systeme Prete");
  dernierTicHorloge = millis();
}

void loop() 
{
  static Donnee infosCourantes;
  
  miseAJourHorlogeSysteme();
  infosCourantes = capteur_flamme_fumee(infosCourantes);

  int lectureFlamme = analogRead(BROCHE_FLAMME);
  int zoneActuelle = map(lectureFlamme, 0, 4095, 0, 3);
  
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
    enregistreurSD(infosCourantes); 
    
    if (WiFi.status() == WL_CONNECTED) 
    {
      televerserFirebase(infosCourantes);
    }
  }
  
  clignotementNonBloquant();
}

void televerserFirebase(Donnee info) 
{
  String cheminRacine = "/Systeme_Surveillance/Telemetrie";
  FirebaseJson json;

  json.add("DateHeure", info.gpsData.dateHeure);
  json.add("Temperature", info.temperature);
  json.add("Humidite", info.humidite);
  json.add("Pression", info.pression);
  json.add("NiveauGaz", info.niveau);
  json.add("FumeeDensity", info.fumee);
  json.add("StatusFlamme", info.flamme);
  json.add("Latitude", info.gpsData.latitude);
  json.add("Longitude", info.gpsData.longitude);
  json.add("LienMaps", info.gpsData.carteLien);

  if (Firebase.RTDB.setJSON(&fbdo, cheminRacine, &json)) 
  {
    Serial.println("✅ Transmission Cloud Reussie (Firebase Sync OK)");
  } 
  else 
  {
    Serial.print("❌ Echec Synchro Cloud: ");
    Serial.println(fbdo.errorReason());
  }
}

void connexionWiFi() 
{
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connexion au Wi-Fi");
  unsigned long debutTentative = millis();
  
  while (WiFi.status() != WL_CONNECTED && millis() - debutTentative < 60000) 
  {
    delay(500);
    Serial.print(".");
  }
  
  if(WiFi.status() == WL_CONNECTED) 
  {
    Serial.println("\nWi-Fi connecté avec succès !");
  } 
  else 
  {
    Serial.println("\nTemps d'attente Wi-Fi dépassé. Enregistrement local via l'heure GPS.");
  }
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

  int indices[25];
  int compteur = 0;
  for (unsigned int i = 0; i < rawGps.length(); i++) 
  {
    if (rawGps.charAt(i) == ',') 
    {
      indices[compteur++] = i;
      if (compteur >= 25) 
      {
        break;
      }
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
  if (directionLat == "S") 
  {
    decLat = -decLat;
  }

  float degLon = bruteLon.substring(0, 3).toFloat();
  float minLon = bruteLon.substring(3).toFloat();
  float decLon = degLon + (minLon / 60.0);
  if (directionLon == "W") 
  {
    decLon = -decLon;
  }

  data.latitude = String(decLat, 6);
  data.longitude = String(decLon, 6);
  data.carteLien = "http://maps.google.com/?q=" + data.latitude + "," + data.longitude;
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
          if ((moisSysteme == 4 || moisSysteme == 6 || moisSysteme == 9 || moisSysteme == 11) && jourSysteme > 30) 
          { 
            jourSysteme = 1; 
            moisSysteme++; 
          }
          else if (moisSysteme == 2 && jourSysteme > 28) 
          { 
            jourSysteme = 1; 
            moisSysteme++; 
          } 
          else if (jourSysteme > 31) 
          { 
            jourSysteme = 1; 
            moisSysteme++; 
          }
          if (moisSysteme > 12) 
          { 
            moisSysteme = 1; 
            anneeSysteme++; 
          }
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
    if (gpsHeure >= 24) 
    {
      gpsHeure -= 24;
      gpsJour++;
    }

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
  char tampon[25];
  sprintf(tampon, "%04d-%02d-%02d %02d:%02d:%02d", anneeSysteme, moisSysteme, jourSysteme, heureSysteme, minuteSysteme, secondeSysteme);
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
  int zone = map(lectureFlamme, 0, 4095, 0, 3);

  if (zone == 0) 
  {
    info.flamme = "Flamme proche";
  }
  else if (zone == 1) 
  {
    info.flamme = "Flamme detectee";
  }
  else 
  {
    info.flamme = "Normal";
  }

  info.fumee = lectureFumee; 
  return info;
}

void Alarme(int zone, float fumee, Donnee info) 
{
  String motifIncident = "";
  String lcdLine2Error = ""; 
  bool incidentCritique = false;
  bool avertissementSysteme = false;
  
  if (zone < 2) 
  {
    motifIncident += "[FIRE] ";
    lcdLine2Error = "FIRE DETECTED   ";
    incidentCritique = true;
  }
  if (fumee > SEUIL_FUMEE_ACCIDENT) 
  {
    motifIncident += "[SMOKE EXCEEDED] ";
    if(lcdLine2Error != "") 
    {
      lcdLine2Error = "MULTI-HAZARD   ";
    }
    else 
    {
      lcdLine2Error = "SMOKE DANGER    ";
    }
    incidentCritique = true;
  }

  if (incidentCritique) 
  {
    digitalWrite(BROCHE_POMPE, HIGH); 
    digitalWrite(BROCHE_BUZZER, HIGH);  

    Serial.print("CRITICAL DIRECT HAZARD: ");
    Serial.println(motifIncident);
    
    WiFiClientSecure clientNettoyage;
    clientNettoyage.stop();
    delay(100);
    
    char tamponTelegram[512];
    snprintf(tamponTelegram, sizeof(tamponTelegram),
             "CRITICAL INDUSTRIAL SAFETY ALARM\n\n"
             "Status: %s\n"
             "Time: %s\n"
             "Maps Link: %s\n\n"
             "Telemetry Data:\n"
             "Temp: %.1f C\n"
             "Humidity: %.0f %%\n"
             "Pressure Level: %.2f\n"
             "Smoke Density: %.0f\n"
             "Flame Sensor: %s\n\n"
             "EXTINGUISHER ACTIVE: System loop halted.",
             motifIncident.c_str(),
             info.gpsData.dateHeure.c_str(),
             info.gpsData.carteLien.c_str(),
             info.temperature,
             info.humidite,
             info.niveau,
             fumee,
             info.flamme.c_str());

    if (WiFi.status() == WL_CONNECTED) 
    {
      envoyerAlerteTelegram(String(tamponTelegram));
      
      FirebaseJson panicJson;
      panicJson.add("AlerteStatut", "CRITIQUE_INCENDIE");
      panicJson.add("Details", motifIncident);
      panicJson.add("Horodatage", info.gpsData.dateHeure);
      Firebase.RTDB.setJSON(&fbdo, "/Systeme_Surveillance/StatutUrgence", &panicJson);
    }

    Firebase.RTDB.setString(&fbdo, "/Systeme_Surveillance/StatutUrgence/AlerteStatut", "CRITIQUE_INCENDIE");

    enregistreurSD(info);

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
        if (etatBasculementCritique) 
        {
          lcd.setCursor(0, 0);
          lcd.print("!! LOCKDOWN !!  ");
          lcd.setCursor(0, 1);
          lcd.print(lcdLine2Error); 
        } 
        else 
        {
          lcd.setCursor(0, 0);
          lcd.print("SYSTEM LOCKED   ");
          lcd.setCursor(0, 1);
          lcd.print("RESET REQUIRED  ");
        }
      }
      yield(); 
    }
  } 
  
  String motifAvertissement = "";
  if (info.temperature > SEUIL_TEMP_ALERT) 
  {
    motifAvertissement += "[HIGH TEMP] ";
    avertissementSysteme = true;
  }
  if (info.pression > SEUIL_NIVEAU_NORMAL) 
  {
    motifAvertissement += "[HIGH PRESSURE] ";
    avertissementSysteme = true;
  }
  if (info.niveau > SEUIL_NIVEAU_CRITIQUE) 
  {
    motifAvertissement += "[CRITICAL LEVEL] ";
    avertissementSysteme = true;
  }

  if (avertissementSysteme) 
  {
    digitalWrite(BROCHE_POMPE, LOW); 
    digitalWrite(BROCHE_BUZZER, LOW); 
    digitalWrite(BROCHE_LED_AVERTISSEMENT, HIGH); 

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("SYSTEM WARNING! ");
    lcd.setCursor(0, 1);
    lcd.print(motifAvertissement.substring(0, 16));

    if (WiFi.status() == WL_CONNECTED && (millis() - derniereAlerteTelegram >= delaiAttenteAlerte)) 
    {
      derniereAlerteTelegram = millis();
      
      char tamponAvertissement[384];
      snprintf(tamponAvertissement, sizeof(tamponAvertissement),
               "⚠️ SYSTEM MONITORING ALERT ⚠️\n\n"
               "Issue detected: %s\n"
               "Current Temp: %.1f C\n"
               "Current Pressure/Level: %.2f\n"
               "Time: %s\n"
               "Location Link: %s\n\n"
               "Status: Pump/Buzzer offline. High surveillance active.",
               motifAvertissement.c_str(),
               info.temperature,
               info.pression,
               info.gpsData.dateHeure.c_str(),
               info.gpsData.carteLien.c_str());

      envoyerAlerteTelegram(String(tamponAvertissement));
      
      Firebase.RTDB.setString(&fbdo, "/Systeme_Surveillance/StatutUrgence/Avertissement", motifAvertissement);
    }
  } 
  else 
  {
    digitalWrite(BROCHE_POMPE, LOW);
    digitalWrite(BROCHE_BUZZER, LOW);
    digitalWrite(BROCHE_LED_AVERTISSEMENT, LOW); 
  }
}

void envoyerAlerteTelegram(String messageTelegram) 
{
  WiFiClientSecure client;
  client.setInsecure(); 

  HTTPClient http;
  String url = "https://api.telegram.org/bot" + String(TELEGRAM_BOT_TOKEN) + "/sendMessage";
  
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  
  String chargeUtileJson = "{\"chat_id\":\"" + String(TELEGRAM_CHAT_ID) + "\", \"text\":\"" + messageTelegram + "\"}";
  
  int codeReponseHttp = http.POST(chargeUtileJson);
  if (codeReponseHttp > 0) 
  {
    Serial.printf("Paquet Telegram envoyé. Code Serveur : %d\n", codeReponseHttp);
  } 
  else 
  {
    Serial.printf("Échec Telegram : %d\n", codeReponseHttp);
  }
  http.end();
}

Donnee capteur_temperature_humidite(Donnee info) 
{
  info.temperature = dht.readTemperature();
  info.humidite = dht.readHumidity();
  if (isnan(info.temperature)) 
  {
    info.temperature = 0.0;
  }
  if (isnan(info.humidite)) 
  {
    info.humidite = 0.0;
  }
  return info;
}

Donnee pression_niveau(Donnee info) 
{
  if (balance.is_ready()) 
  {
    info.pression = balance.get_units(5); 
  } 
  else 
  {
    info.pression = 0;
  }
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
    lcd.print(info.gpsData.dateHeure.substring(2, 10));
    lcd.setCursor(0, 1);
    lcd.print(info.gpsData.dateHeure.substring(11, 19));
  } 
  else if (page == 1) 
  {
    lcd.setCursor(0, 0);
    lcd.print("T:" + String(info.temperature, 1) + "C H:" + String(info.humidite, 0) + "%");
    lcd.setCursor(0, 1);
    lcd.print("Niveau: " + String(info.niveau, 1));
  } 
  else if (page == 2) 
  {
    lcd.setCursor(0, 0);
    lcd.print("Lat:" + info.gpsData.latitude.substring(0, 7));
    lcd.setCursor(0, 1);
    lcd.print("Lon:" + info.gpsData.longitude.substring(0, 7));
  }
}

void afficherSerial(Donnee info) 
{
  Serial.println("\n--- RAPPORT DU CŒUR DE DIAGNOSTIC ESP32 ---");
  Serial.print("Heure Systeme : "); 
  Serial.println(info.gpsData.dateHeure);
  Serial.print("Latitude :      "); 
  Serial.println(info.gpsData.latitude);
  Serial.print("Longitude :     "); 
  Serial.println(info.gpsData.longitude);
  Serial.print("Lien Google :   "); 
  Serial.println(info.gpsData.carteLien);
  Serial.print("Vitesse Sol :   "); 
  Serial.print(info.gpsData.vitesseActuelle); 
  Serial.println(" km/h");
  Serial.print("Temperature :   "); 
  Serial.print(info.temperature); 
  Serial.println(" °C");
  Serial.print("Humidite :      "); 
  Serial.print(info.humidite); 
  Serial.println(" %");
  Serial.print("Poids Pression :"); 
  Serial.println(info.pression);
  Serial.print("Niveau de Gaz : "); 
  Serial.println(info.niveau);
  Serial.print("Densite Fumee : "); 
  Serial.println(info.fumee);
  Serial.print("Etat Incendie : "); 
  Serial.println(info.flamme);
  Serial.println("-------------------------------------------");
}

void enregistreurSD(Donnee info) 
{
  String nomFichier = "/" + info.gpsData.dateHeure.substring(0, 10) + ".csv";
  File f = SD.open(nomFichier, FILE_APPEND);
  
  if (f) 
  {
    f.printf("%s,%s,%s,%.2f,%.2f,%.2f,%s,%.2f,%.2f\n", 
             info.gpsData.dateHeure.c_str(), 
             info.gpsData.latitude.c_str(),
             info.gpsData.longitude.c_str(),
             info.gpsData.vitesseActuelle,
             info.temperature, 
             info.humidite, 
             info.flamme.c_str(), 
             info.fumee, 
             info.niveau); 
    f.close();
    Serial.println("Données sauvegardées proprement sur la carte SD : " + nomFichier);
  } 
  else 
  {
    Serial.println("Erreur : Impossible d'ouvrir ou d'écrire dans le fichier de la carte SD !");
  }
}
