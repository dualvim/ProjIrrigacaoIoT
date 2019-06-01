/*
* Teste_11A_ModuloIrrigacao_Melhorado.ino     
*
* --> Versao melhorada do script que utiliza o Mosquito MQTT
* --> 
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




/***************************************************************************
** Declaracoes de constantes, variaveis, objetos e prototipos de funcoes  **
***************************************************************************/
// --> Constantes referentes aos pinos do NodeMCU
#define SOLENOIDE D7
#define SENSOR_UMIDADE A0

// --> Nome do arquivo json com os parametros:
#define ARQ_PARAMS  "/params.json"

// Constantes pertinentes ao display LCD
#define LINS_DISPLAY 2 //Numero de linhas no display LCD 
#define COLS_DISPLAY 16 //Numero de colunas no display LCD
const unsigned int ENDERECO_I2C_DISPLAY = 0x27; //Endereco do diplay OLEd no barramento I2C


// --> Declaracacoes das variaveis globais
char SSID_REDE[40];
char SENHA_REDE[20];
char SERV_MQTT[40];
unsigned long lastMillis = 0;
//String SSID_REDE, SENHA_REDE, SERV_MQTT, 
String stringTime, stringDate, stringDay;
uint16_t PORTA_MQTT, yr;
strDateTime dateTime; //Variavel que ira armazenar a data e a hora atual
byte hh, mm, ss, month, day, dayofweek; //Compenentes da data e da hora
double umidadeMaxima, umidadeMinima;
bool solenoideLigada = false, caixaCheia = false, nivel_atual_alto=false;


//--> Inicializar os objetos para controlar o Wi-Fi, o MQTT e o display LCD:
WiFiClient espClient; //-->Cliente Wi-Fi
PubSubClient cliente(espClient); //--> Configurar o servidor MQTT
LiquidCrystal_PCF8574 lcd(ENDERECO_I2C_DISPLAY); //Display LCD 16x2
// --> Objeto da classe 'NTPtime'
NTPtime NTP("br.pool.ntp.org"); // Brasil (ver https://www.ntppool.org/zone/south-america)


//--> Prototipos das funcoes
void ler_arq_params_spiffs();
void atualizar_dados_arq_json();
void conectar(); 
void escrever_mensagem_recebida(String nomeTopico, byte* payload, unsigned int length);
void messageReceived(char* topic, byte* payload, unsigned int length);
float retornar_percentual_umidade_solo(int valor_sensor);
void gerarStringTime();
void gerarStringDate();
void fGetTime();


void setup() {
      Serial.begin(115200);

      // --> Modo dos pinos conectados aos objetos
      pinMode(SOLENOIDE, OUTPUT);
      pinMode(SENSOR_UMIDADE, INPUT);

      // --> Inicializar o display LCD:
      lcd.begin(COLS_DISPLAY, LINS_DISPLAY); 
      lcd.setBacklight(255);

      // --> Ler o arquivo 'ARQ_PARAMS' com os parametros da conexao Wi-Fi, MQTT e umidade maxima e minima
      ler_arq_params_spiffs();

      // Conectar a rede Wi-Fi
      WiFi.begin(SSID_REDE, SENHA_REDE);
      // --> Inciar o servidor MQTT
      cliente.setServer(SERV_MQTT, PORTA_MQTT);
      // --> Definir a funcao 'messageReceived()' como a 'callback function' desse sketch
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

      // Publicar mensagens a cada 1s.
      if( (millis() - lastMillis) > 1000 ) {
            lastMillis = millis();

            // --> Ler o percentual de umidade do solo, detectado pelo sensor higrometro, e publicar a umidade:
            unsigned int adc = analogRead(SENSOR_UMIDADE);
            float umidade = retornar_percentual_umidade_solo(adc);
            cliente.publish("irrigacao/Umidade", String(umidade).c_str());
            
            /*Serial.print("ADC = ");
            Serial.print(adc);
            Serial.print("; umidade = ");
            Serial.print(umidade);
            Serial.print("; umidadeMinima = ");
            Serial.print(umidadeMinima);
            Serial.print("; umidadeMaxima = ");
            Serial.print(umidadeMaxima);
            Serial.print("; solenoideLigada = ");
            Serial.print(solenoideLigada);
            Serial.print("; caixaCheia = ");
            Serial.println(caixaCheia);*/

            
            // --> Publicar os parametros de umidade minima e maxima:
            //String dadosPublicar = String(umidadeMinima) + "," + String(umidadeMaxima);
            //cliente.publish("geral/paramUmidade", dadosPublicar.c_str());

            //Condicao executada se a valvula solenoide estiver ligada
            if (solenoideLigada){
                  // Se a solenoide estiver ligada e a umidade acima da maxima, desligar a solenoide e publicar o novo estado
                  if (umidade >= umidadeMaxima){
                        digitalWrite(SOLENOIDE, LOW);
                        //digitalWrite(SOLENOIDE, HIGH);
                        solenoideLigada = false;
                        cliente.publish("irrigacao/EstadoSolenoide", "0");
                        Serial.println("Desligando a valvula solenoide...");

                        //--> Publicar o desligamento da solenoide
                        fGetTime(); //Identificar a data e Hora atuais da internet:
                        String dadosPublicar = stringDate + "," + stringTime + "," + String(static_cast<uint8_t>(solenoideLigada)) + ",Desligar,Umidade_Maxima_Detectada";
                        dadosPublicar += "," + String(umidade) + "," + String(umidadeMinima) + "," + String(umidadeMaxima);
                        cliente.publish("irrigacao/EventosSolenoide", dadosPublicar.c_str());
                  }
            } 
            else {
                  /* Se a umidade estiver abaixo da minima estabelecida e a caixa d'agua estiver cheia, 
                        Ligar a solenoide e publicar o novo estado dessa valvula*/    
                  if (( umidade <= umidadeMinima ) && caixaCheia ){
                        digitalWrite(SOLENOIDE, HIGH);
                        //digitalWrite(SOLENOIDE, LOW);
                        solenoideLigada = true;
                        cliente.publish("irrigacao/EstadoSolenoide", "1");
                        Serial.println("Ligando a valvula solenoide...");

                        //--> Publicar o evento de ligar solenoide
                        fGetTime(); //Identificar a data e Hora atuais da internet:
                        String dadosPublicar = stringDate + "," + stringTime + "," + String(static_cast<uint8_t>(solenoideLigada)) + ",Ligar,Umidade_Minima_Detectada";
                        dadosPublicar += "," + String(umidade) + "," + String(umidadeMinima) + "," + String(umidadeMaxima);
                        cliente.publish("irrigacao/EventosSolenoide", dadosPublicar.c_str());
                  }

                  //--> Registrar o evento especial de atingir a umidade minima e nao ligar a solenoide
                  else if(( umidade <= umidadeMinima ) && !caixaCheia ){ 
                        //--> Publicar o evento especial de nao poder ligar solenoide
                        fGetTime(); //Identificar a data e Hora atuais da internet:
                        String dadosPublicar = stringDate + "," + stringTime + "," + String(static_cast<uint8_t>(solenoideLigada)) + ",Nao_Ligar,Umidade_Minima_Reserv_Baixo";
                        dadosPublicar += "," + String(umidade) + "," + String(umidadeMinima) + "," + String(umidadeMaxima);
                        cliente.publish("irrigacao/EventosSolenoide", dadosPublicar.c_str());
                  }
            }

            // --> Escrever os dados de umidade e estado da solenoide no display LCD:
            lcd.clear();
            // Escrever a umidade
            lcd.setCursor(0, 0);
            lcd.print("Umidade: ");
            lcd.print(umidade, 1);
            lcd.print("%");
            // Escrever o status da solenoide
            lcd.setCursor(0, 1);
            lcd.print("Solenoide: ");
            if( solenoideLigada ){ lcd.print("ON"); }
            else{ lcd.print("OFF"); }

            // --> Publicar os valores gerados nesse modulo: umidade do solo e estado da solenoide
            fGetTime(); //Identificar a data e Hora atuais da internet:
            String dadosPublicar = stringDate + "," + stringTime + ",";
            dadosPublicar += String(static_cast<uint8_t>(solenoideLigada)) + "," + String(umidade) + ","; 
            dadosPublicar += String(umidadeMinima) + "," + String(umidadeMaxima);
            cliente.publish("geral/SerieDadosIrrigacao", dadosPublicar.c_str());
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
      umidadeMinima = root["umidade_minima"];
      umidadeMaxima = root["umidade_maxima"];

      /*Serial.print("SSID_REDE = ");
      Serial.println(SSID_REDE);
      Serial.print("SENHA_REDE = ");
      Serial.println(SENHA_REDE);
      Serial.print("SERV_MQTT = ");
      Serial.println(SERV_MQTT);
      Serial.print("PORTA_MQTT = ");
      Serial.println(PORTA_MQTT);
      Serial.print("umidadeMinima = ");
      Serial.println(umidadeMinima);
      Serial.print("umidadeMaxima = ");
      Serial.println(umidadeMaxima);*/
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
      json["umidade_minima"] = umidadeMinima;
      json["umidade_maxima"] = umidadeMaxima;

      // Inserir os dados no arquivo
      File arqParams = SPIFFS.open(ARQ_PARAMS, "w");
      if( !arqParams ){Serial.println("Falha em criar o arquivo JSON com os dados");}
      serializeJson(json, arqParams);
      arqParams.close();
}



/************************
** Funcao conectar()   **
************************/
void conectar(){
      bool mostrar = true;
      Serial.print("Verificando wifi...");
      while( WiFi.status() != WL_CONNECTED ) {
            Serial.print(".");
            // Escrever o status no display LCD
            lcd.clear();
            if(mostrar){
                  lcd.setCursor(0, 0);
                  lcd.print("   Conectando   ");
                  mostrar = false;
            }
            else { mostrar = true; }
            delay(500);
      }

      
      while (!cliente.connected()) {
            Serial.println("\nTentando se conectar ao servidor MQTT...");
            if (cliente.connect("ModuloIrrigacao")){
                  Serial.print(F("Connectado ao servidor MQTT em "));
                  Serial.print(SERV_MQTT);
                  Serial.print(":");
                  Serial.println(PORTA_MQTT);

                  // Subscrever aos topicos de interesse
                  cliente.subscribe("param/UmidadeMaxima");
                  cliente.subscribe("param/UmidadeMinima");
                  cliente.subscribe("bomba/NivelAgua");
            }
            else{
                  Serial.print("Falha na conexao com o servidor MQTT [rc = ");
                  Serial.print(cliente.state());
                  Serial.println("]. Realizando uma nova tentativa em 5 segundos.");

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




/***********************************************
** Funcao retornar_percentual_umidade_solo()  **
***********************************************/
float retornar_percentual_umidade_solo(int valor_sensor){
      float percent = (1024.0 - static_cast<float>(valor_sensor)) / 1024.0;
      percent = percent * 100;
      return percent;
}




/********************************
** Funcao messageReceived()    **
********************************/
void escrever_mensagem_recebida(String nomeTopico, byte* payload, unsigned int length){
      // Escrever o topico da mensagem
      Serial.print("Mensagem recebida [");
      Serial.print(nomeTopico);
      Serial.print("] - ");

      // Escrever o conteudo de 'payload'
      for( unsigned int i = 0; i < length; i++ ){
            Serial.print(static_cast<char>(payload[i]));
      }
      Serial.println("");
      
}


void messageReceived(char* topic, byte* payload, unsigned int length) {
      String nomeTopico = String(topic);

      // --> Inserir o conteudo de 'payload' em um array de caracteres
      char conteudo[30]; 
      for( unsigned int i = 0; i < length; i++ ){
            conteudo[i] = static_cast<char>(payload[i]);
      }
      conteudo[length] = '\0';
            
      // --> Caso o topico seja 'bomba/NivelAgua', verificar se deve-se escrever a mensagem ou nao
      if (nomeTopico == "bomba/NivelAgua"){
            //Atualmente o nivel da agua esta alto?
            bool nivel_alto = (String(conteudo).toInt() == 2);
            //Serial.print("nivel_alto = ");
            //Serial.print(nivel_alto);
            //Serial.print("; nivel_atual_alto = ");
            //Serial.println(nivel_atual_alto);
            
            //Verificar se houveram mudancas no nivel:
            if( nivel_atual_alto != nivel_alto ){
                  //Escrever a mensagem de recebimento
                  escrever_mensagem_recebida(nomeTopico, payload, length);
                  
                  //Se houve mudanca, alterar 'nivel_atual_alto'
                  nivel_atual_alto = nivel_alto;

                  //Se o nivel atualmente estiver alto, alterar o estado de 'caixaCheia'
                  if(nivel_atual_alto){
                        caixaCheia = true;  
                        //Serial.println("Caixa cheia"); 
                  }
                  
                  //Caso contrario, forcar o desligamento da solenoide
                  else{ 
                        caixaCheia = false;
                        digitalWrite(SOLENOIDE, LOW); //Desligar a solenoide
                        //digitalWrite(SOLENOIDE, HIGH); //Desligar a solenoide
                        cliente.publish("irrigacao/EstadoSolenoide", "0");
                        solenoideLigada = false;
                        Serial.println("Desligando a valvula solenoide...");
            
                        //--> Publicar o desligamento da solenoide
                        fGetTime(); //Identificar a data e Hora atuais da internet:
                        String dadosPublicar = stringDate + "," + stringTime + "," + String(static_cast<uint8_t>(solenoideLigada)) + ",Desligar,Nivel_Baixo_Reservatorio";
                        cliente.publish("irrigacao/EventosSolenoide", dadosPublicar.c_str());
                  }
            }
      }


      // --> Caso o 'nomeTopico' seja 'param/UmidadeMaxima'
      else if (nomeTopico == "param/UmidadeMaxima"){
           escrever_mensagem_recebida(nomeTopico, payload, length);
           umidadeMaxima = String(conteudo).toFloat();
           // --> Atualizar o arquivo salvo na memoria flash:
           atualizar_dados_arq_json();
           //Mensagem indicando que o arquivo foi atualizado
           Serial.println("O valor da umidade maxima foi atualizado no arquivo com os parametros.");
      }


      // --> Caso o 'nomeTopico' seja 'param/UmidadeMinima'
      else  if (nomeTopico == "param/UmidadeMinima"){
            escrever_mensagem_recebida(nomeTopico, payload, length);
            umidadeMinima = String(conteudo).toFloat(); 
            // --> Atualizar o arquivo salvo na memoria flash:
            atualizar_dados_arq_json();
            //Mensagem indicando que o arquivo foi atualizado
            Serial.println("O valor da umidade minima foi atualizado no arquivo com os parametros.");
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
