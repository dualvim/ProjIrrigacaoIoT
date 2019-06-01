
/*
* Teste_11B_Bomba_Melhorado.ino     
*
* --> Versao modificada do script incluso nos codigos dos exemplos do livro de Oliveira
* --> Utiliza o Mosquitto (no Raspberry Pi) ao inves do site 'Shiftr.io'
*/


#include <ESP8266WiFi.h>
#include <WiFiclient.h>
#include <WiFiUdp.h>
#include <LiquidCrystal_PCF8574.h>
#include <PubSubClient.h> 
// Biblioteca para acessar os arquivos na memoria Flash e para ler arquivos JSON
#include <ArduinoJson.h>
#include <FS.h>
#include <NTPtimeESP.h> //Biblioteca para usar o Network Time Protocol




// --> Constantes referentes aos pinos do NodeMCU
#define BOIA A0
#define BOMBA D6

// --> Nome do arquivo json com os parametros:
#define ARQ_PARAMS  "/params.json"

// Constantes pertinentes ao display LCD
#define LINS_DISPLAY 2 //Numero de linhas no display LCD 
#define COLS_DISPLAY 16 //Numero de colunas no display LCD
#define ENDERECO_I2C_DISPLAY  0x27 //Endereco do diplay OLEd no barramento I2C

// Constantes: Limites dos niveis da bomba
#define RESERV_BAIXO 256
#define RESERV_ALTO 512

// Declaracacoes das variaveis globais
unsigned long lastMillis = 0;
bool solenoideLigada = false, bombaLigada = false;
char SSID_REDE[40];
char SENHA_REDE[20];
char SERV_MQTT[40];
uint16_t PORTA_MQTT, yr, adc_reserv; 
uint8_t nivel_agua, hh, mm, ss, month, day, dayofweek;
strDateTime dateTime; //Variavel que ira armazenar a data e a hora atual
String stringTime, stringDate, stringDay;



//--> Inicializar os objetos para controlar o Wi-Fi, o MQTT e o display LCD:
WiFiClient espClient; //-->Cliente Wi-Fi
PubSubClient cliente(espClient); //--> Configurar o servidor MQTT
LiquidCrystal_PCF8574 lcd(ENDERECO_I2C_DISPLAY); //Display LCD 16x2
// --> Objeto da classe 'NTPtime'
NTPtime NTP("br.pool.ntp.org"); // Brasil (ver https://www.ntppool.org/zone/south-america)




//--> Prototipos das funcoes
void messageReceived(char* topic, byte* payload, unsigned int length);
void ler_arq_params_spiffs();
void ler_dados_arq_json();
void atualizar_dados_arq_json();
void conectar();
void fGetTime();
void gerarStringTime();
void gerarStringDate();




/******************
** setup()       ** 
******************/
void setup() {
      Serial.begin(115200);

      // --> Modos dos pinos da bomba e da boia
      pinMode(BOMBA, OUTPUT);
      pinMode(BOIA, INPUT);
      
      // --> Inicializar o display LCD:
      lcd.begin(COLS_DISPLAY, LINS_DISPLAY); 
      lcd.setBacklight(255);

      // --> Ler o arquivo 'ARQ_PARAMS' com os parametros da conexao Wi-Fi, MQTT e umidade maxima e minima
      ler_arq_params_spiffs();
      
      // Conectar a rede Wi-Fi
      WiFi.begin(SSID_REDE, SENHA_REDE);

      // --> Inciar o servidor MQTT
      cliente.setServer(SERV_MQTT, PORTA_MQTT);
      
      // --> Definir a funcao de 'callback'
      cliente.setCallback(messageReceived);
      conectar();

}




/****************
** loop()      **  
****************/
void loop() {
      cliente.loop();
      delay(10); // <- fixes some issues with WiFi stability
      
      if(!cliente.connected()) {
            conectar();
      }

      // Publica uma mensagem periodicamente a cada segundo.
      if(millis() - lastMillis > 1000) {
            lastMillis = millis();

            /* --> Ler o nivel de agua no reservatorio 
                  - Como o valor lido no pino 'BOIA' eh um valor binario, o valor eh convertido para 'bool'
                  - Como os valores do pino estao ao contrario (1 == vazio e 0 == cheio), usar o operador NOT (!) 
            */
            adc_reserv = static_cast<uint16_t>( analogRead(BOIA) );
            Serial.print("Valor ADC Nivel Agua = ");
            Serial.println(adc_reserv);
            
            // Limpar o display LCD e escrever os dados
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Nivel: ");

            String nome_nivel;
            // Publicar o nivel da agua e escrever o conteudo no display LCD
            if( adc_reserv >= RESERV_ALTO ){ //Reservatorio cheio (ou com agua)
                  nivel_agua = 2;
                  nome_nivel = "Alto";
                  cliente.publish("bomba/NivelAgua", "2");
            }
            else if ( adc_reserv < RESERV_BAIXO ){ //Reservatorio vazio
                  nivel_agua = 0;
                  nome_nivel = "Vazio";
                  cliente.publish("bomba/NivelAgua", "0");
                  
            }
            else {
                  nivel_agua = 1;
                  nome_nivel = "Baixo";
                  cliente.publish("bomba/NivelAgua", "1");
            }
            
            // Publicar o nivel da agua
            Serial.print("solenoideLigada: ");
            Serial.print(solenoideLigada);
            Serial.print("; Nivel da agua: ");
            Serial.print(nivel_agua);
            Serial.print("; Nome Nivel: ");
            Serial.println(nome_nivel);
            lcd.print(nome_nivel);
                  

            //--> Se o nivel estiver baixo, desligar a bomba imediatamente
            if ( bombaLigada && (nivel_agua != 2)){ 
                  digitalWrite(BOMBA, LOW);
                  //digitalWrite(BOMBA, HIGH);
                  cliente.publish("bomba/EstadoBomba", "0");
                  bombaLigada = false;
                  Serial.println("Desligando a bomba...");
                  
                  //--> Publicar o desligamento da bomba
                  fGetTime(); //Identificar a data e Hora atuais da internet:
                  String dadosPublicar = stringDate + "," + stringTime + "," + String(static_cast<uint8_t>(bombaLigada)) + ",Desligar,Nivel_Baixo_Reservatorio";
                  cliente.publish("bomba/EventosBomba", dadosPublicar.c_str());
                  
            }  

            // --> Se a solenoide for desligada, desligar a bomba:
            else if ( bombaLigada && !solenoideLigada){
                  digitalWrite(BOMBA, LOW);
                  //digitalWrite(BOMBA, HIGH);
                  cliente.publish("bomba/EstadoBomba", "0");
                  bombaLigada = false;
                  Serial.println("Desligando a bomba...");
                  
                  //--> Publicar o desligamento da bomba
                  fGetTime(); //Identificar a data e Hora atuais da internet:
                  String dadosPublicar = stringDate + "," + stringTime + "," + String(static_cast<uint8_t>(bombaLigada)) + ",Desligar,Solenoide_Desligada";
                  cliente.publish("bomba/EventosBomba", dadosPublicar.c_str());
            }

            // -->
            else if ( (solenoideLigada && (nivel_agua == 2)) && !bombaLigada ){ // Se o nivel do reservatorio estiver alto e a solenoide ligada e a bomba desligada
                  digitalWrite(BOMBA, HIGH);
                  //digitalWrite(BOMBA, LOW);
                  cliente.publish("bomba/EstadoBomba", "1");
                  bombaLigada = true;
                  Serial.println("Ligando a bomba...");

                  //--> Publicar o evento de ligar a bomba
                  fGetTime(); //Identificar a data e Hora atuais da internet:
                  String dadosPublicar = stringDate + "," + stringTime + "," + String(static_cast<uint8_t>(bombaLigada)) + ",Ligar, Solenoide_Ligada";
                  cliente.publish("bomba/EventosBomba", dadosPublicar.c_str());
            }

            // Escrever no display LCD o status da bomba
            lcd.setCursor(0, 1);
            lcd.print("Bomba: ");
            if( bombaLigada ){ lcd.print("ON"); }
            else { lcd.print("OFF"); }

            
            // --> Publicar os valores gerados nesse modulo: nivel do reservatorio e estado da bomba
            fGetTime(); //Identificar a data e Hora atuais da internet:
            String dadosPublicar = stringDate + "," + stringTime + ",";
            dadosPublicar += String(adc_reserv) + ", " + nome_nivel + ", ";
            dadosPublicar += String(nivel_agua) + "," + String(static_cast<uint8_t>(bombaLigada));
            cliente.publish("geral/SerieDadosBomba", dadosPublicar.c_str());
      }
}





/*******************************
** Funcao messageReceived()   **
*******************************/
// Função requerida e chamada pela biblioteca MQTT, exatamente nesse formato
void messageReceived(char* topic, byte* payload, unsigned int length) {
      String nomeTopico = String(topic);
      
      Serial.print("Mensagem recebida [");
      Serial.print(nomeTopico);
      Serial.print("] - ");
      // Escrever o conteudo de 'payload'
      for( unsigned int i = 0; i < length; i++ ){
            Serial.print(static_cast<char>(payload[i]));
      }
      Serial.println("");

      // --> Inserir o conteudo de 'payload' em um array de caracteres
      char conteudo[30]; 
      for( unsigned int i = 0; i < length; i++ ){
            conteudo[i] = static_cast<char>(payload[i]);
      }
      conteudo[length] = '\0';

      
      //--> Mensagens com o estado atual da solenoide
      if(nomeTopico == "irrigacao/EstadoSolenoide"){
            if (String(conteudo).toInt() == 1){
                  solenoideLigada = true;
                  Serial.println("Ligando a Solenoide...");
            }      
            else if (String(conteudo).toInt() == 0){
                  solenoideLigada = false;
                  Serial.println("Desligando a Solenoide...");
            }
      }
}





/*************************************
** Funcao ler_arq_params_spiffs()   **
*************************************/
void ler_arq_params_spiffs(){
      if(SPIFFS.begin()){
            Serial.println(F("SPIFFS montado com sucesso!"));

            // Verificar se o arquivo 'ARQ_PARAMS' existe:
            if(SPIFFS.exists(ARQ_PARAMS) ){
                  Serial.print("Foi encontrado no SPIFFS o arquivo ");
                  Serial.println(ARQ_PARAMS);
                  //Chamar 'ler_dados_arq_json()' para inicializar os valores
                  ler_dados_arq_json();
                  Serial.println("Os dados do arquivo JSON foram lidos com sucesso");
            } 
      }
}




/**************************************
** Funcao ler_dados_arq_json()       **
**************************************/
void ler_dados_arq_json() {
      //--> Abrir o arquivo JSON com os parametros e gerar uma string com o conteudo
      File f = SPIFFS.open(ARQ_PARAMS, "r");
      String conteudo = f.readStringUntil('\n');
      f.close(); //fechar o arquivo
      
      // --> Abrir o arquivo JSON 'ARQ_PARAMS' com os dados e deserializar os dados:
      DynamicJsonDocument root(1024);
      auto erro_json = deserializeJson(root, conteudo);
      if(erro_json){Serial.println("Erro ao tentar ler o arquivo JSON"); return;} //Sair em caso de erro

      // --> Inicializar os valores das variaveis cujos parametros estao no arquivo JSON
      strcpy(SSID_REDE, root["nome_rede"].as<char*>());
      strcpy(SENHA_REDE, root["senha_rede"].as<char*>());
      strcpy(SERV_MQTT, root["servidor_mqtt"].as<char*>());
      PORTA_MQTT = root["porta_mqtt"];

      /*Serial.print("SSID_REDE = ");
      Serial.println(SSID_REDE);
      Serial.print("SENHA_REDE = ");
      Serial.println(SENHA_REDE);
      Serial.print("SERV_MQTT = ");
      Serial.println(SERV_MQTT);
      Serial.print("PORTA_MQTT = ");
      Serial.println(PORTA_MQTT);
      Serial.println("\n\n");*/
}




/**************************************
** Funcao atualizar_dados_arq_json() **
**************************************/
void atualizar_dados_arq_json(){
      // Criar o objeto com os dados a serem salvos no JSON
      DynamicJsonDocument json(1024);
      // Atualizar os dados
      json["nome_rede"] = SSID_REDE;
      json["senha_rede"] = SENHA_REDE;
      json["servidor_mqtt"] = SERV_MQTT;
      json["porta_mqtt"] = PORTA_MQTT;

      // Inserir os dados no arquivo
      File arqParams = SPIFFS.open(ARQ_PARAMS, "w");
      if( !arqParams ){Serial.println("Falha em criar o arquivo JSON com os dados");}
      serializeJson(json, arqParams);
      arqParams.close();
}




/************************
** Funcao conectar()   **
************************/
void conectar() {
      bool mostrar = true;
      Serial.print("Verificando wifi...");
      while (WiFi.status() != WL_CONNECTED) {
            Serial.print(".");
            lcd.clear();
            // Conteudo no display LCD:
            if(mostrar){
                  lcd.setCursor(0, 0);
                  lcd.print("   Conectando   ");
                  mostrar = false;
            }
            else { mostrar = true; }
            delay(500);
      }

      // Conectar ao servidor MQTT:
      while (!cliente.connected()) {
            Serial.print("\nTentando se conectar ao servidor MQTT...");
            if (cliente.connect("ModuloBomba")){
                  Serial.print(F("Connectado ao servidor MQTT em "));
                  Serial.print(SERV_MQTT);
                  Serial.print(":");
                  Serial.println(PORTA_MQTT);

                  // Subscrever ao topico de interesse
                  cliente.subscribe("irrigacao/EstadoSolenoide");
            }
            else{
                  Serial.print("Falha na conexao com o servidor MQTT [rc = ");
                  Serial.print(cliente.state());
                  Serial.println("]. Sera realizada uma nova tentativa em 5 segundos.");

                  // Avisar no display LCD
                  lcd.clear();
                  lcd.setCursor(0, 0);
                  lcd.print("Falha na conexao");
                  lcd.setCursor(0, 1);
                  lcd.print("servidor MQTT");
                  // Wait 5 seconds before retrying
                  delay(5000);
            }
      }
}


/***************************
** Funcao 'fGetTime()'    **
***************************/
void fGetTime(){
      // --> Pegar a data e hora atuais no servidor
      dateTime = NTP.getNTPtime(-3, 0);
      if(dateTime.valid){ //--> Se a data e hora recebida for valida
            // --> Extrair o conteudo da data e hora em 'dateTime'
            hh = dateTime.hour;
            mm = dateTime.minute;
            ss = dateTime.second;
            yr = dateTime.year;
            month = dateTime.month;
            day = dateTime.day;
            dayofweek = dateTime.dayofWeek;

            // Gerar a string 'stringTime':
            gerarStringTime();

            // Gerar a string 'stringDate'
            gerarStringDate();
      }
}


/**************************************
** Funcoes auxiliares de 'getTime()' **
**************************************/
// --> Funcao 'gerarStringTime()': Realiza a atividade gerar a string da hora no formato hh:mm:ss
void gerarStringTime(){
      stringTime = "";
      // Inserir as horas
      if(hh < 10){
            stringTime += "0";
      }
      stringTime += String(hh)+ ":";

      // Inserir os minutos
      if(mm < 10){
            stringTime += "0";
      }
      stringTime += String(mm)+ ":";   

      // Inserir os segundos
      if(ss < 10){
            stringTime += "0";
      }
      stringTime += String(ss); 
}


// --> Funcao 'gerarStringDate()': Gera a data por extenso
void gerarStringDate(){
      // Gerar 'stringDate'
      stringDate = String(yr) + "-";
      if(month < 10){
            stringDate += "0";
      }
      stringDate += String(month) + "-";
      if(day < 10){
            stringDate += "0";
      }
      stringDate += String(day);
}
