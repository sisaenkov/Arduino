/*
    Скетч получает показания счетчиков воды и записывает их на SD-карту и в базу данных.
    При включении происходит чтение последних показаний счетчиков из файлов на карте памяти.
    Затем производится получение последних значений из базы данных сервера, которые сравниваются с данными из файлов.
    Если показания на сервере меньше показаний в файлах, то на сервер отправляются актуальные данные.
    Если показания на сервере больше показаний в файлах, то на карту также записываются актуальные данные.
    Во время считывания показаний с сервера происходит поиск числа с помощью библиотеки TextFinder и функции finder().
*/

#include <SD.h>
#include <SPI.h>
#include <Bounce2.h>  // Эту библиотеку необходимо скачать тут: https://github.com/thomasfredericks/Bounce-Arduino-Wiring
#include <Ethernet.h>
#include <TextFinder.h>

File myFile;

byte mac[] = {0xB4, 0x43, 0x0D, 0x38, 0xDD, 0x66};  // MAC-адрес устройства (можно задать любой)
IPAddress ip(172, 16, 1, 33);    // IP адрес, если вдруг не получится получить его через DHCP
//IPAddress server(172,16,1,8);  // ip-адрес удалённого сервера (использовался, пока не было имени)
char server[] = "ivideon";       // Имя удалённого сервера
char select[50];                 // Переменная для формирования ссылок
char request[80];                // Переменная для формирования ссылок

int url;                         // Переменная с URL для получения, либо отправки показаний на сервер
long hot;                        // Показания счетчика ГВС
long cold;                       // Показания счетчика ХВС
long match;                      // Переменная показаний с сервера для сравнения
String CounterValue;             // Строка выбора параметра для GET-запроса

int CounterPin[2] = {5, 6};                   // Пин счетчика
char *CounterType[2] = {"hot", "cold"};       // Тип счетчика
char *CounterName[2] = {"695281", "751575"};  // Серийный номер счетчика
Bounce CounterBouncer[2] = {};                // Формируем для счетчиков Bounce объекты

EthernetClient rclient;                       // Объект для соединения с сервером
TextFinder finder(rclient);                   // Фукнция поиска показаний с сервера

void setup() {

//  Serial.begin(9600);
//  Serial.print("[ SD-card ");

  pinMode(10, OUTPUT);
  digitalWrite(10, HIGH);

  if (!SD.begin(4)) {
//    Serial.println("failed! ]");
    return;
  }
//  Serial.println("Done ]");

  for (int i = 0; i < 2; i++) {
    pinMode(CounterPin[i], INPUT);            // Инициализируем пин
    digitalWrite(CounterPin[i], HIGH);        // Включаем подтягивающий резистор
    CounterBouncer[i].attach(CounterPin[i]);  // Настраиваем Bouncer
    CounterBouncer[i].interval(10);           // и прописываем ему интервал дребезга
  }

  if (Ethernet.begin(mac) == 0) {
    Ethernet.begin(mac, ip);  // Если не получилось подключиться по DHCP, пробуем еще раз с явно указанным IP адресом
  }
//  Serial.println("[ Network Done ]");
  delay(1000);  // даем время для инициализации Ethernet shield

  // получаем текущие показания счетчиков из файла с SD-карты и сравниваем их с базой данных при включении контроллера
  getV("hot");
  getV("cold");

}

void loop() {

  delay(1000);  // Задержка в 1 сек. Два раза в секунду счетчик не может сработать ни при каких обстоятельствах, т.к. одно срабатывание - 10 литров.
  // Проверяем состояние всех счетчиков
  for (int i = 0; i < 2; i++) {
    boolean changed = CounterBouncer[i].update();
    if ( changed ) {
      int value = CounterBouncer[i].read();
      // Если значение датчика стало ЗАМКНУТО
      if ( value == LOW ) {
        if ( CounterPin[i] == 5 ) {
          getV("hot");  // получаем текущие показания счетчиков из файла с SD-карты и сравниваем их с базой данных при смене показаний
          putV("hot");  // записываем текущие показания счетчиков в файл на SD-карту и в базу данных
        } else if ( CounterPin[i] == 6 ) {
          getV("cold");  // получаем текущие показания счетчиков из файла с SD-карты и сравниваем их с базой данных при смене показаний
          putV("cold");  // записываем текущие показания счетчиков в файл на SD-карту и в базу данных
        }
        // Формируем ссылку запроса, куда вставляем номер счетчика, его тип, и текущие показания
        sprintf(request, "GET /cgi-bin/count.pl?device=%s&type=%s&counter=%s HTTP/1.0", CounterName[i], CounterType[i], CounterValue.c_str());
        url = 2;
        sendHTTPRequest();  // Отправляем HTTP запрос
      }
    }
  }

}

// функция записи новых показаний в файл после очередных 10 литров
void putV(String varType) {
  long varInt = 0;
  String varFile = varType + ".txt";
  if ( varType == "hot" ) {
    varInt = hot;
    SD.remove("hot.txt");
  } else if ( varType == "cold" ) {
    varInt = cold;
    SD.remove("cold.txt");
  }
  if (varInt >= 0 && varInt < 9999999) {
    varInt++;
  } else if (varInt = 9999999) {
    varInt = 0;
  }
  String varString = String(varInt, DEC);
  myFile = SD.open(varFile.c_str(), FILE_WRITE);
  if (myFile) {
    myFile.print(varString);
    myFile.close();
  }
  CounterValue = varString;
  if ( varType == "hot" ) {
    hot = varInt;
  } else if ( varType == "cold" ) {
    cold = varInt;
  }
//  Serial.println(varInt);
//  Serial.println(hot);
//  Serial.println(cold);
}

// функция получения текущих показаний счетчика из файла и сравнение их с базой данных
void getV (String varType) {
  long varInt = 0;
  String varFile = varType + ".txt";
  myFile = SD.open(varFile.c_str());
  if (myFile) {
    while (myFile.available()) {
      int ascii = myFile.read();
      if (isDigit(ascii)) {
        varInt = (varInt * 10) + (ascii - 48);
      } else if (ascii = 10) {
        varInt = 10;
      }
    }
  }
  myFile.close();
  sprintf(select, "GET /cgi-bin/count.pl?type=%s HTTP/1.0", varType.c_str());
  url = 1;
  sendHTTPRequest();
  if ( varInt < match ) {    // если значение в базе больше, чем на карте
    varInt = match;          // сделать значение на карте равным значению из базы
  }
  if ( varType == "hot" ) {
    hot = varInt;
  } else if ( varType == "cold" ) {
    cold = varInt;
  }
}

// Функция поиска показаний на странице ответа сервера
void findValue() {
  if (rclient.connected()) {
    match = 0;
    finder.find("<value>");
    match = finder.getValue();
  }
}

// Функция отправки HTTP-запроса на сервер
void sendHTTPRequest() {
  if (rclient.connect(server, 80)) {
    if ( url == 1 ) {
      rclient.println(select);
    } else if ( url == 2 ) {
      rclient.println(request);
    }
    //rclient.print("Host: ");
    //rclient.println(server);
    rclient.println("Authorization: Basic YXJkdWlubzphcmR1aW5v"); // Base64 "user:password"
    rclient.println("User-Agent: Arduino/1.0");
    rclient.println();
    if ( url == 1 ) {
      findValue();
    }
    rclient.stop();
  }
}
