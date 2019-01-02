//liste des bibliothèques utilisés
#include <SD.h>
#include <RTClib.h>
#include <DigiPotX9Cxxx.h>

//déclaration des constantes
const int HYSTERESIS = 1; //°C - définition du seuil d'hysteresis (+/- consigne)
const int PROGRAMME_SIZE = 336;   //taille du ableau de programmation. 336 = programmation par 1/2 heure
// les valeures définies ci-dessous sont valables pour une consigne de 19°C sur le poêle
const int POELE_STOP = 12;  //Ohm - <> ~21.5 °C
const int POELE_MOD = 27;   //Ohm - <> ~18,5 °C
const int POELE_START = 43; //Ohm - <> ~15.5 °C
const int SHUNT = 1490;   //Ohm
const int analogPin = A3; //pin utilisée pour la mesure Ushunt


// déclaration des prototypes de fonction
void chargerTemperature();
int getTemperatureConsigne();
float getTemperatureTest();
void setEteindre();
void setModuler();
void setTravailler();
float getTemperatureSonde();

//déclaration des variables globales
char planningTemperature[PROGRAMME_SIZE]; //le planning est sauvegarder sous forme de char, à convertir en int lors de l'utilisation.
bool modeModule; //option qui déclenche le mode modulé dès la température (consigne-hysteresis) atteinte
RTC_DS1307 rtc; //horloge
String PROGRAMME = "file.txt"; //nom du fichier dans le lequel est sauvegardé la programmation
int varCompteur; //utilisée pour la mesure du temps dans les interruptions
byte depassementSeuilHaut =0;
byte depassementSeuilBas =0;
byte poeleState=1; //état du poele; 0 = ON; 1 = MODULE; 2 = OFF

DigiPot pot(6,7,5); //déclaration du potentiomètre
float tempTest=19.0; // température mesure pour test
float pas=0.25; //pour test

void setup() {
  Serial.begin(9600);
  
  //Démarrage du module RTC
  if (! rtc.begin()) {
    Serial.println("Couldn't find RTC");
    while (1);
  }
  if (! rtc.isrunning()) {
    Serial.println("RTC is NOT running!");
    // following line sets the RTC to the date & time this sketch was compiled
    //rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  pot.reset(); //mise à 0 du potentiomètre
  
  chargerTemperature(); //charger le planning sur la carte SD
  setModuler(); //commencer en mode éteint
  modeModule = false;

  //Utilisation du timer : http://www.locoduino.org/spip.php?article84
  bitClear (TCCR2A, WGM20); // WGM20 = 0
  bitClear (TCCR2A, WGM21); // WGM21 = 0 
  TCCR2B = 0b00000110;      // Clock / 256 soit 16 micro-s et WGM22 = 0
  TIFR2 = 0b00000001;       // TOV2
  TCNT2 = 256 - 250;        // Chargement du timer à 6
  
  //TESTS
  //Serial.begin(9600);
  if(0){  //test - désactivé
    int consigne;
    consigne = getTemperatureConsigne();
    Serial.print("Consigne:");
    Serial.println(consigne);
  }
  if(0){ //test - désactivé
    //Affichage du tableau de température
    for(int i=0; i<50; i++){
      int aff = planningTemperature[i];
      Serial.println(aff);
      }
  }
}

void loop() {
  //Utilisation des timers
  if (bitRead (TIFR2, 0) == 1) {       // Flag TOV2 mis à 1 ?
    TCNT2 = 256 - 250;         // Rechargement du timer à 6
    bitSet (TIFR2, TOV2);      // Remise à zéro du flag TOV2 (voir texte)
    if (varCompteur++ > 2500) { // Incrémentation et a atteint 1500 => 10sec
      varCompteur = 0;         // On recommence un nouveau cycle
  
      float Temp_mesure=0;
      float Temp_consigne;
      
      // BOUCLE DE REGULATION - à répéter toutes les X secondes
      
      Temp_mesure = getTemperatureSonde(); //récupère la température ambiante (+1 car la calibration semble déclalée. Mapping à revoir.
      Temp_consigne = getTemperatureConsigne(); //récupère la consigne de température pour l'heure actuelle.
    
      //HYSTERISIS - machine à état
      switch(poeleState){
        case(0): //POELE en mode TRAVAIL
          if(Temp_mesure > Temp_consigne){
            setModuler(); //met le poele en mode travail modulé
          }
          else if(modeModule and (Temp_mesure > (Temp_consigne - HYSTERESIS))){
            setModuler();  //met le poele en mode travail modulé
            modeModule=false; //réinitialisation de l'option
          }
        break;
        case(1): //POELE en mode MODULE
          if(Temp_mesure>(Temp_consigne + HYSTERESIS)){
            if(depassementSeuilHaut++ > 5){ //condition remplie 5fois d'affilé
            setEteindre(); //met le poele en mode extinction
            modeModule = true; // active une option qui déclenche le mode modulé dès la température (consigne-hysteresis) atteinte
                              // car si la temp max est dépassée <> mode modulé suffit à chauffer
            //+temporiser 30minutes
            depassementSeuilHaut = 0 ;
            }
          }
          else if(Temp_mesure < (Temp_consigne - HYSTERESIS)){
            setTravailler(); //met le poele en mode travail
          }
          else{ //température proche de la consigne, on reste en mode modulé
            depassementSeuilHaut = 0; //réinitialisation du seuil
          }
        break;
        case(2): //POELE en mode ETEIND
          if(Temp_mesure < (Temp_consigne - HYSTERESIS)){
            if(depassementSeuilBas++ > 5){ //condition remplie 5fois d'affilé
            setTravailler(); //met le poele en mode travail
            depassementSeuilBas = 0;
            //+ temporiser 15minutes
            }
          else{ //La température est toujours dans la limite acceptable
            depassementSeuilBas =0; //réinitialisation du seuil
            }
          }
        break;
      }
             
      //AFFICHAGE  JOURNAL
      Serial.print("Mesure: ");
      Serial.print(Temp_mesure);
      Serial.print(" | Consigne: ");
      Serial.print(Temp_consigne);
      Serial.print(" | Etat: ");
      Serial.print(poeleState,OCT);
      Serial.print(" | pot valeur: ");
      Serial.println(pot.get());
    }
  }
}

//DEFINITION DES FONCTIONS -----------------------------------------------

void chargerTemperature() {
//chargerTemperature vient remplir le tableau PlanningTemperature[] avec les paramètres lus sur la carte SD.

  int chipSelect = 4; //chip select pin for the MicroSD Card Adapter
  File file; // file object that is used to read and write data
  pinMode(chipSelect, OUTPUT);

  if (!SD.begin(chipSelect)) {
    // Si erreur a l'inisialisation de lecteure SD
    return;
  }
  file = SD.open(PROGRAMME, FILE_READ); // open "file.txt" to read data
  if (file) {
    char character;
    String value = "";
    int index = 0;
    while ((character = file.read()) != -1 && index <= PROGRAMME_SIZE) { //parcours tous les caractères du fichier et concatène les valeurs en float en utilisant les ";" comme séparateur 
      if (character == ';') {
        character = value.toInt();
        planningTemperature[index] = character;
        value = "";
        index++;
      }
      else {
        value.concat(character);
      }
    }
    file.close();
  }
}

int getTemperatureConsigne(){
  //getTemperatureConsigne prend en entrée le tableau des températures, sa taille et une date (jour/heure/minute) et renvoie la température de consigne sauvegardée
  //si le tableau fait 336 cases <> semaine découpée en parcelles de 30minutes
  int index;
  DateTime now = rtc.now();
  
  if(PROGRAMME_SIZE == 336){
    index = now.dayOfTheWeek()*48 + now.hour()*2 + (now.minute()/30);
  }
  else{
    index=0;
  }
    index = planningTemperature[index];
    return 20; //test
    //return index;
}
float getTemperatureTest(){
   //pour test
    tempTest+=pas;
    if(tempTest>21.5 or tempTest<19.00) pas*=-1;
    return tempTest;
  }

 float getTemperatureSonde(){ //en utilisant la sonde ext.
    long R_moy=0;
    int val=0;
    long R=0; // pour éviter l'overflow
    
    for(int i = 0; i<20 ;i++){ //mesure moyenne de la résistance
      val = analogRead(analogPin); // val = [0;1023]
      R = ((1023*long(SHUNT))/val)-SHUNT; //valeur de la résistance de la sonde
      R_moy += R;
    }
    R_moy = R_moy/20;
    R = R_moy; //on utilise la mesure moyenne pour le mapping
    //Serial.print("Résistance mesurée : ");
    //Serial.print(R_moy);

    /* CORRECTION linéaire. Les coefficient sont calculés grâce à la fonction DROITEREG() de excel.
     *  déactivé car les coeff ddoivent être recalculés en comparant des mesures de résistances proche du domaine de fonctionnement [10K;18K]
    long R_cor=0;
    R_cor = float(0.989068*R_moy)-10.29;
    Serial.print(" | Résistance corrigée : ");
    Serial.println(R_cor);
    */
    
    //MAPPING, sonde température à recalibrer
    if(R>18000){ //res max, hors périmètre
      return 9.5;
    }
    else if(R>13000){ //R = [18K; 13K] => T = [9.5;17]
      int A = -666; //définition des coéfficients d'interpolation linéaire
      int B = 24333;
      return float((R-B))/A;
    }
    else if(R>11500){ //R = [13K; 11.5K] => T = [17;20.5]
      int A = -428; //définition des coéfficients d'interpolation
      int B = 20286;
      return float((R-B))/A;
    }
    else if(R>10750){ //R = [11.5K; 10.75K] => T = [20.5;22]
      int A = -500; //définition des coéfficients d'interpolation
      int B = 21750;
      return float((R-B))/A;
    }
    else if(R>10000){ //R = [10.75K; 10] => T = [22;24.5]
      int A = -300; //définition des coéfficients d'interpolation
      int B = 17350;
      return float((R-B))/A;
    }
    else if(R<=10000){ //Res min, hors périmètre
      return 25;
    }
}

void setTravailler(){
  pot.set(POELE_START);
  poeleState = 0;
}
void setEteindre(){
  pot.set(POELE_STOP);
  poeleState = 2;
}
void setModuler(){
  pot.set(POELE_STOP);
  delay(2000);
  pot.set(POELE_MOD);
  poeleState = 1;
}
