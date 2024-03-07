//------------------------------------------------------------------------------------
//  Управление приводом с концевиками подключенным по 2-x релейной схеме. 
//  Контролирует время движения до концевика и перегрузку по току 
//------------------------------------------------------------------------------------
#pragma once
#include <Arduino.h>

//Направление движения
enum driveDir {   
  DRIVE_DIR_BACKWARD = 0,
  DRIVE_DIR_FORWARD
};
//Состояние концевиков 
enum driveEndSw {   
  DRIVE_SW_PUSHED = 0,
  DRIVE_SW_REALIZED
};
//Текущее состояние привода
enum driveMoveStatus {  
  DRIVE_STATUS_OK = 0,
  DRIVE_STATUS_OVERTIME,
  DRIVE_STATUS_OVERLOAD,
  DRIVE_STATUS_STOPPED,
  DRIVE_STATUS_IN_WORK = 10
};

class AdvDrive
{
public:
  //  Конструктор
  AdvDrive  (uint32_t pinFrw,        uint32_t pinBkw = NC,       //Пины назад и вперед
            uint32_t pinSwFrw = NC,  uint32_t pinSwBkw = NC,     //Пин переднего и заднего концевика 
            bool swLevelFrw = HIGH,  bool swLevelBkw = HIGH)     //Уровень концевиков в НЕ сработаном состоянии (0 или 1)
  {
    //Сохраняем пины
    _pinFrw = pinFrw;           
    _pinBkw = pinBkw;
    _pinSwFrw = pinSwFrw;
    _pinSwBkw = pinSwBkw;
    //Сохраняем нормальный уровень концевиков
    _swLevelFrw = swLevelFrw;
    _swLevelBkw = swLevelBkw;
    //Настройка портов
    pinMode(_pinFrw, OUTPUT);   //Пин переднего направления
    if (_pinBkw != NC)    pinMode(_pinBkw, OUTPUT); 
    if (_pinSwFrw != NC)  pinMode(_pinSwFrw, INPUT_PULLUP);
    if (_pinSwBkw != NC)  pinMode(_pinSwBkw, INPUT_PULLUP);
  }

  //  Включить защиту от перегрузки по току.
  //  Принимает: указатель на внешнюю float переменную, в которую постоянно считывается значение тока привода сторонней функцией
  //             макс. ток и макс. время перегрузки до отключения
  void setOverload ( float * curr, float maxCurr, uint32_t time_ms = 1) {
    _curr = curr;
    _maxCurr = maxCurr;
    _overload_ms = time_ms;
  }

  //Отключить защиту от перегрузки по току.
  void offOverload () {
    _curr = 0;    //Признак отключения защиты
  }

  //  Прочитать статус движения привода. 
  //  Возвращает: 0 - успешное завершение, 
  //              1 - превышено время,
  //              2 - перегрузка по току,
  //              10 - в движении 
  int getStatus() {
    return moveStatus;
  }

  //  Стартуем, далее необходимо выполнять функцию run() 
  void start () {
    motorOn(_dir); //Включаем  мотор
    moveStatus = DRIVE_STATUS_IN_WORK; //Статус - в процессе выполнения
    tmrStart = millis(); // Фиксируем время запуска
  }

    //  Остановить движение в любой момент 
  void stop () {
    motorOff();
  }
  
  //  Работа привода. Вызывается в цикле, результат выполнения отслеживается через getStatus()
  //  Автоматически останавливает при достижении концевика, превышении времени, перегрузке по току
  void run()  {
    while(1) {
      //Проверка пришла ли дверь в положение
      if (readEndSw(_dir)) {
        moveStatus = DRIVE_STATUS_OK;		      //ОК
        break; 
      }    
      //проверка времени работы, если задано (0 - время не ограничено)
      if ( _maxTimeSec  &&  millis() - tmrStart > _maxTimeSec * 1000 ) {
        moveStatus = DRIVE_STATUS_OVERTIME;     //Превышено время
        break; 					
      }
      //Проверка перегрузки
      if ( overloadControl() ) {
        moveStatus = DRIVE_STATUS_OVERLOAD;		      //Перегрузка по току
        break; 
      }
      return;
    }
    //Отключение
    stop();
  }

  //  Запуск движения, блокирующая функция (можно использовать при многозадачности)
  //  Принимает направление: 1 - впред, 0 - назад; время работы (0 - не ограничено)
  //  Возвращает: см. getStatus()
  int move(bool dir, int maxTimeSec) {
    setDir(dir);
    if (maxTimeSec) //Если задано время работы,
      setTime(maxTimeSec);   //установить его (0 - время не ограничено)
    moveStatus = DRIVE_STATUS_IN_WORK;    //Статус выставить в процессе выполнения
    start();
    while (getStatus() == DRIVE_STATUS_IN_WORK) //Пока не завершилась работа
      run();      //Запускаем в цикле 
    return getStatus();   //Статус сменился, значит завершение
  }

  //  Запуск движения, блокирующая функция, без времени (задается предварительно)
  int move(bool dir) {
    return move(_dir, _maxTimeSec);
  }
    //  Запуск движения, блокирующая функция, без аргументов (задаются предварительно)
  int move() {
    return move(_dir, _maxTimeSec);
  }

  //  Установить направление: 0 - назад, 1 - вперед
  void setDir ( bool dir ) {
    _dir = dir;
  }

  //  Установить макс. время работы мотора до достижения концевика
  void setTime ( uint32_t maxTimeSec ) {
    _maxTimeSec = maxTimeSec;
  }

  //  Отключить мотор
  void motorOff (void)
  {
    digitalWrite(_pinFrw, LOW);
    if (_pinBkw != NC) digitalWrite(_pinBkw, LOW);
  }

  //  Включаем мотор
  void motorOn (bool dir = 0)
  {
    motorOff(); //Предванительно отключить все
    if (dir)                  digitalWrite(_pinFrw, HIGH);    // Вперед
    else if (_pinBkw != NC)   digitalWrite(_pinBkw, HIGH);
  }

  //  Считать состояние концевика. 
  //  Принимает концевик: 1 - передний, 0 - задний
  //  Возвращает: 1 - концевик сработан, 0 - не сработан
  bool readEndSw( bool dir = 1 )
  {
    bool sw;
    switch (dir) {
      case DRIVE_DIR_FORWARD:
        if (_pinSwFrw != NC) {
          sw = digitalRead(_pinSwFrw); 
          if (_swLevelFrw == HIGH)   sw = !sw;   //Если разомкнутый концевик дает 1, то инверсия, иначе прямое считывание
          return sw;
        }
        return 0;
        
      case DRIVE_DIR_BACKWARD: 
        if (_pinSwBkw != NC) {
          sw = digitalRead(_pinSwBkw); 
          if (_swLevelBkw == HIGH)   sw = !sw;   
          return sw;
        }
        return 0;
    }
  }
  
  //  Контроль пререгрузки по току, вызывается в цикле
  //  Принимает: текущее значение тока
  //  Возвращает: 0 - нет перегрузки, 1 - перегрузка более указанного времени 
  int overloadControl() {
    if (_curr == 0) return 0;  //Если не установлено

    //Контроль перегрузки
    switch ( f_overCurr ) 
    {
      case 0:			//Режим без перегрузки 
        if ( *_curr > _maxCurr ) {     //Проверка  есть ли перегрузка
          tmrOverload = millis(); //Запуск таймера
          f_overCurr = 1;	//Зафиксирована перегрузка
        }
        return 0;
      
      case 1:			//Режим перегрузки 
        if (*_curr <= _maxCurr) {
          f_overCurr = 0;  //Убрать перегрузку если ток упал 
          return 0;
        }
        if ( millis() - tmrOverload > _overload_ms * 1000 ) { //Перегрузка более заданного времени
          f_overCurr = 0;	//Убрать перегрузку
          return 1;						
        }
        return 0;
    }
  }

private:
  uint32_t _pinFrw, _pinBkw;        //Пины включения 
  uint32_t _pinSwFrw, _pinSwBkw;    //Пины концевиков 
  bool _swLevelFrw, _swLevelBkw;    //Уровень с концевиков в не сработаном состоянии (0 или 1)
  float _maxCurr = 0;               //Максимальный порог тока
  int _overload_ms = 0;             //Максималное время перегрузки
  float *_curr = 0;                 //Указатель на переменную с текущим значением тока

  int _maxTimeSec = 0;
  bool _dir = DRIVE_DIR_FORWARD;
  int moveStatus = DRIVE_STATUS_OK;
  bool f_overCurr = 0;
  uint32_t tmrStart;
  uint32_t tmrOverload;
};
