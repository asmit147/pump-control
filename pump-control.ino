/* vi:set tabstop=2: shiftwidth=2: softtabstop=2: expandtab: */
/* :e ++ff=dos then remove ^M */
/*
 * Pump control system
 *
 * Each pin can provide or receive a maximum of 40 mA and has an internal
 * pull-up resistor (disconnected by default) of 20-50 kOhms.
 *
 * water pressure switch (pressurised by pump)
 * normally open = no pressure = pullup to HIGH
 * closed = pressure = LOW
 *
 * Tank float switches are normally open (pullup HIGH) when there is nothing to
 * do. Float switch will close (LOW) when it begins to fill and during filling.
 * When tank is full the float switch will open, nothing to do.
 *
 * Tank limit switches are open (pullup HIGH) when the tank is full at the
 * limit. Tank limit switch rests in the closed LOW state when the tank is not
 * at or above its limit. This way, if no limit switches are fitted, we always
 * read them as if the tank is full, so only the float switches will drive
 * actions.
 *
 */

/* Inputs pullup ACTIVE LOW */
#define IN_START                        2
#define IN_STOP                         3
#define IN_FUEL                         9
#define IN_WATER_PRESSURE               10
#define IN_TANK_1_FLOAT                 4
#define IN_TANK_2_FLOAT                 6
#define IN_TIME_CLOCK                   8
#define IN_TANK_1_LIM                   5
#define IN_TANK_2_LIM                   7

/* Outputs ACTIVE HIGH */
#define OUT_IGN                         A0
#define OUT_START                       A1
#define OUT_PRESSURE_OVERRIDE           A2
#define OUT_TANK_1_VALVE                A3
#define OUT_TANK_2_VALVE                A4
#define OUT_ALERT                       A5

/* max time to wait for pressure switch to open after ignition off (seconds) */
#define IGN_OFF_WAIT_MAX                10
#define CRANKING_DELAY                  5
#define CRANKING_TIME                   7
#define WATER_PRESSURE_WAIT_MAX         10
#define MAX_STARTUP_ATTEMPTS            2

/* uncomment to output to USB serial port */
#define debug_serial_USB
/* uncomment to output to TX RX header serial port */
//#define debug_serial_header
#define BAUD                            115200

/* Global variables */
int mode_auto = 1;
int quiet_time = 0;
int ignition_on = 0;
int no_water_pressure = 1;
int startup_attempt = 0;
int tank_1_valve_open = 0;
int tank_2_valve_open = 0;

void setup() {
  /* initalise all inputs and outputs */
  pinMode(IN_START, INPUT_PULLUP);
  pinMode(IN_STOP, INPUT_PULLUP);
  pinMode(IN_TANK_1_FLOAT, INPUT_PULLUP);
  pinMode(IN_TANK_1_LIM, INPUT_PULLUP);
  pinMode(IN_TANK_2_FLOAT, INPUT_PULLUP);
  pinMode(IN_TANK_2_LIM, INPUT_PULLUP);
  pinMode(IN_TIME_CLOCK, INPUT_PULLUP);
  pinMode(IN_FUEL, INPUT_PULLUP);
  pinMode(IN_WATER_PRESSURE, INPUT_PULLUP);
  
  pinMode(OUT_IGN, OUTPUT);
  pinMode(OUT_START, OUTPUT);
  pinMode(OUT_PRESSURE_OVERRIDE, OUTPUT);
  pinMode(OUT_TANK_1_VALVE, OUTPUT);
  pinMode(OUT_TANK_2_VALVE, OUTPUT);
  pinMode(OUT_ALERT, OUTPUT);

  #ifdef debug_serial_USB
  /* USB port ttyACM */
  Serial.begin(BAUD);
  #elif defined debug_serial_header
  /* TX RX pin header */
  Serial1.begin(BAUD);
  #endif
}

/* log function makes it easy to control which serial port is being used */
void log(const char *buf) {
  #ifdef debug_serial_USB
  Serial.println(buf);
  #elif defined debug_serial_header
  Serial1.println(buf);
  #else
  /* no serial output */
  #endif
}

void alert(const char *msg) {
  /* alert operator and do nothing forever until start button pressed */
  char buf[64];
  sprintf(buf, "Alert: %s", msg);
  log(buf);

  do_shutdown();
  digitalWrite(OUT_ALERT, HIGH);
  while (1) {
    delay(10);
    if (!digitalRead(IN_START)) {
      log("start button pressed, alert finished");
      digitalWrite(OUT_ALERT, LOW);
      /* wait for button release so this button press does not
       * also cause a manual startup. */
      while (!digitalRead(IN_START))
        delay(10);
      break;
    }
  }
}

/* when the pump is started by the button, we only care about the limit
 * switches. when the pump has started automatically we only care about the
 * float swicthes.
 */
int tanks_full() {
  int f = 0;
  if (mode_auto) {
    if (digitalRead(IN_TANK_1_FLOAT) == HIGH &&
        digitalRead(IN_TANK_2_FLOAT) == HIGH)
      f = 1;
  } else {
    if (digitalRead(IN_TANK_1_LIM) == HIGH &&
        digitalRead(IN_TANK_2_LIM) == HIGH) {
      f = 1;
      log("both tanks filled to limit switch. Now enable auto mode.");
      mode_auto = 1;
    }
  }
  return f;
}

int pump_is_running() {
  if (ignition_on && !no_water_pressure) {
    /* clear startup attempts so auto mode always tries up to 2 times */
    startup_attempt = 0;
    return 1;
  } else
    return 0;
}

void do_tank_1_valve_open() {
  log(__func__);
  digitalWrite(OUT_TANK_1_VALVE, HIGH);
  tank_1_valve_open = 1;
}

void do_tank_2_valve_open() {
  log(__func__);
  digitalWrite(OUT_TANK_2_VALVE, HIGH);
  tank_2_valve_open = 1;
}

void do_tank_1_valve_close() {
  log(__func__);
  digitalWrite(OUT_TANK_1_VALVE, LOW);
  tank_1_valve_open = 0;
}

void do_tank_2_valve_close() {
  log(__func__);
  digitalWrite(OUT_TANK_2_VALVE, LOW);
  tank_2_valve_open = 0;
}

void close_valves() {
  do_tank_1_valve_close();
  do_tank_2_valve_close();
}

/* blocking, which means buttons are not read during this time. */
void do_shutdown() {
  log(__func__);
  int cnt;
  if (!ignition_on)
    return;
  digitalWrite(OUT_IGN, LOW);
  ignition_on = 0;
  for (cnt = 0; cnt < IGN_OFF_WAIT_MAX; cnt++) {
    if (digitalRead(IN_WATER_PRESSURE) == HIGH)
      break;
    delay(1000);
  }
  close_valves();
}

/* blocking */
void do_startup() {
  log(__func__);
  int cnt;
  char buf[24];

  if (pump_is_running()) {
    log("pump is already running!");
    return;
  }
  if (!tank_1_valve_open && !tank_2_valve_open) {
    log("no valves open, not starting pump!");
    return;
  }

  /* manual mode we assume the operator knows best and will not limit
   * startup attempts */
  if (mode_auto) {
    if (startup_attempt >= MAX_STARTUP_ATTEMPTS) {
      alert("could not start!");
      startup_attempt = 0;
      return;
    }
    startup_attempt++;
    sprintf(buf, "start attempt %d", startup_attempt);
    log(buf);
  }

  log("ignition on...");
  digitalWrite(OUT_IGN, HIGH);
  ignition_on = 1;
  delay(CRANKING_DELAY * 1000);

  log("cranking...");
  digitalWrite(OUT_START, HIGH);
  delay(CRANKING_TIME * 1000);
  digitalWrite(OUT_START, LOW);

  for (cnt = 0; cnt < WATER_PRESSURE_WAIT_MAX; cnt++) {
    /* check pressue valve every 1 second */
    delay(1000);
    log("wait for water pressure...");
    if (digitalRead(IN_WATER_PRESSURE) == LOW)
      break;
  }
  /* if we never achieve water pressure, next loop will shutdown */
}

/*
 * main loop
 * the order of the funtions is critical!
 */
void loop() {
  delay(100);

  char buf[128] = "";
  int start_button = digitalRead(IN_START);
  int stop_button = digitalRead(IN_STOP);
  int fuel = digitalRead(IN_FUEL);

  /* time clock input high = contacts open = do not run */
  quiet_time = digitalRead(IN_TIME_CLOCK);
  no_water_pressure = digitalRead(IN_WATER_PRESSURE);

  sprintf(buf, "auto mode %s", mode_auto ? "on" : "off");
  log(buf);
  sprintf(buf, "ignition %s", ignition_on ? "on" : "off");
  log(buf);
  sprintf(buf, "pump is running %s", pump_is_running() ? "yes" : "no");
  log(buf);
  sprintf(buf, "water pressure %s", no_water_pressure ? "no" : "yes");
  log(buf);
  sprintf(buf, "tank 1: lim %d float %d valve %s",
    digitalRead(IN_TANK_1_LIM),
    digitalRead(IN_TANK_1_FLOAT),
    tank_1_valve_open ? "open" : "closed");
  log(buf);
  sprintf(buf, "tank 2: lim %d float %d valve %s",
    digitalRead(IN_TANK_2_LIM),
    digitalRead(IN_TANK_2_FLOAT),
    tank_2_valve_open ? "open" : "closed");
  log(buf);

  if (no_water_pressure || tanks_full() || mode_auto && quiet_time)
    do_shutdown();

  if (!fuel) {
    do_shutdown();
    alert("fuel low");
  }

  if (!start_button) {
    log("start button pressed");
    log("turn auto mode off");
    mode_auto = 0;
    /* manual start, we only care about the limit switches. */
    if (digitalRead(IN_TANK_1_LIM) == LOW)
      do_tank_1_valve_open();
    if (digitalRead(IN_TANK_2_LIM) == LOW)
      do_tank_2_valve_open();
    do_startup();
  }

  /*
   * stop button causes alert which means we do nothing forever until the
   * start button is pressed.
   * change back to auto mode.
   */
  if (!stop_button) {
    log("stop button pressed");
    log("turn auto mode on");
    mode_auto = 1;
    alert("stopped");
  }

  /* auto pump mode only reads the tank floats */
  if (!quiet_time && mode_auto) {
    if (digitalRead(IN_TANK_1_FLOAT) == LOW) {
      do_tank_1_valve_open();
      do_startup();
    }
    if (digitalRead(IN_TANK_2_FLOAT) == LOW) {
      do_tank_2_valve_open();
      do_startup();
    }
  }
}
