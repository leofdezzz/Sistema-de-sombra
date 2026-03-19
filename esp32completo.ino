#include <math.h>
#include <ESP32Servo.h>

// =============================================
// PINES
// =============================================
#define PIN_FOTOTRANSISTOR  34
#define PIN_SERVO           18

#define PIN_STEPPER_IN1     19
#define PIN_STEPPER_IN2     21
#define PIN_STEPPER_IN3     22
#define PIN_STEPPER_IN4     23

#define PIN_BOTON_BUSCAR    14
#define PIN_BOTON_MOVER     27

#define PIN_LED             2
#define PIN_LED_DIR_1       26
#define PIN_LED_DIR_2       25
#define PIN_LED_DIR_3       33
#define PIN_LED_NUEVO_MAX   15

// =============================================
// PARÁMETROS DEL SISTEMA
// =============================================
const float DISTANCIA_CM        = 2;
const float ANCHO_PARED_CM      = 4;
const float LONGITUD_CARRIL_CM  = 30;

const float CM_POR_PASO         = 0.00250;

const int   ANGULO_SERVO_MIN    = 0;
const int   ANGULO_SERVO_MAX    = 180;
const int   PASO_BARRIDO        = 2;
const int   ESPERA_BARRIDO_MS   = 20;
const int   UMBRAL_LUZ          = 0;
const int   HISTERESIS_LUZ      = 8;
const float TOLERANCIA_CM       = 0.05;
const unsigned long PULSO_NUEVO_MAX_MS = 120;
const unsigned long ANIMACION_DIR_MS   = 90;

const int   VELOCIDAD_STEPPER   = 10;

// Secuencia de pasos original para el 28BYJ-48 con ULN2003 (half-step)
const int SECUENCIA[8][4] = {
  {1, 0, 0, 0},
  {1, 1, 0, 0},
  {0, 1, 0, 0},
  {0, 1, 1, 0},
  {0, 0, 1, 0},
  {0, 0, 1, 1},
  {0, 0, 0, 1},
  {1, 0, 0, 1}
};

// =============================================
// VARIABLES GLOBALES
// =============================================
Servo servoSensor;

float angulSolActual        = 90.0;
float posicionParedActual   = 0.0;
float posicionParedObjetivo = 0.0;

int   pasoActual            = 0;
int   anguloServoActual     = 90;
int   direccionBarrido      = +1;
int   mejorLecturaBarrido   = 0;
int   mejorAnguloBarrido    = 90;

bool  hayObjetivoValido      = false;
bool  stepperActivo          = false;
bool  barridoCompleto        = false;

unsigned long ultimoPasoServoMs   = 0;
unsigned long ultimoLogMs         = 0;
unsigned long finPulsoNuevoMaxMs  = 0;
unsigned long ultimoCambioAnimMs  = 0;

int ultimoDirStepper              = 0;
int indiceAnimacionDir            = 0;
bool animacionDirEncendida        = false;

// =============================================
// ESTRUCTURA DE RESULTADO
// =============================================
struct ResultadoPared {
  float posicion_ideal;
  float posicion_real;
  bool  puede_bloquear;
};

void pasarStepper(int dir);
void apagarStepper();
ResultadoPared calcularPosicionPared(float angulo_deg);
void registrarNuevaLectura(int angulo, int lectura);
void fijarObjetivoDesdeBarrido();
void actualizarBarridoServo();
void moverPared(float nuevaPosicion_cm);
void imprimirEstado();
bool leerBotonReset();
void reiniciarSistema();
void apagarLedsDireccion();
void actualizarAnimacionDireccion(int dir);
void iniciarPulsoNuevoMax();
void actualizarPulsoNuevoMax();

// =============================================
// FUNCIÓN: Mover un paso el stepper
// dir: +1 = derecha, -1 = izquierda
// =============================================
void pasarStepper(int dir) {
  pasoActual = (pasoActual + dir + 8) % 8;
  digitalWrite(PIN_STEPPER_IN1, SECUENCIA[pasoActual][0]);
  digitalWrite(PIN_STEPPER_IN2, SECUENCIA[pasoActual][1]);
  digitalWrite(PIN_STEPPER_IN3, SECUENCIA[pasoActual][2]);
  digitalWrite(PIN_STEPPER_IN4, SECUENCIA[pasoActual][3]);
}

// =============================================
// FUNCIÓN: Apagar bobinas del stepper
// =============================================
void apagarStepper() {
  digitalWrite(PIN_STEPPER_IN1, LOW);
  digitalWrite(PIN_STEPPER_IN2, LOW);
  digitalWrite(PIN_STEPPER_IN3, LOW);
  digitalWrite(PIN_STEPPER_IN4, LOW);
  stepperActivo = false;
  apagarLedsDireccion();
  ultimoDirStepper = 0;
  animacionDirEncendida = false;
}

// =============================================
// FUNCIÓN: Apagar LEDs de direccion
// =============================================
void apagarLedsDireccion() {
  digitalWrite(PIN_LED_DIR_1, LOW);
  digitalWrite(PIN_LED_DIR_2, LOW);
  digitalWrite(PIN_LED_DIR_3, LOW);
}

// =============================================
// FUNCIÓN: Animacion LEDs de direccion del stepper
// =============================================
void actualizarAnimacionDireccion(int dir) {
  unsigned long ahora = millis();
  if (dir != ultimoDirStepper) {
    ultimoDirStepper = dir;
    indiceAnimacionDir = 0;
    animacionDirEncendida = false;
    ultimoCambioAnimMs = 0;
    apagarLedsDireccion();
  }

  if (ahora - ultimoCambioAnimMs < ANIMACION_DIR_MS) {
    return;
  }

  ultimoCambioAnimMs = ahora;

  if (animacionDirEncendida) {
    apagarLedsDireccion();
    animacionDirEncendida = false;
    indiceAnimacionDir = (indiceAnimacionDir + 1) % 3;
    return;
  }

  int pinLed = PIN_LED_DIR_1;
  if (dir < 0) {
    if (indiceAnimacionDir == 0) pinLed = PIN_LED_DIR_1;
    if (indiceAnimacionDir == 1) pinLed = PIN_LED_DIR_2;
    if (indiceAnimacionDir == 2) pinLed = PIN_LED_DIR_3;
  } else {
    if (indiceAnimacionDir == 0) pinLed = PIN_LED_DIR_3;
    if (indiceAnimacionDir == 1) pinLed = PIN_LED_DIR_2;
    if (indiceAnimacionDir == 2) pinLed = PIN_LED_DIR_1;
  }

  apagarLedsDireccion();
  digitalWrite(pinLed, HIGH);
  animacionDirEncendida = true;
}

// =============================================
// FUNCIÓN: Pulso LED al detectar nuevo maximo
// =============================================
void iniciarPulsoNuevoMax() {
  digitalWrite(PIN_LED_NUEVO_MAX, HIGH);
  finPulsoNuevoMaxMs = millis() + PULSO_NUEVO_MAX_MS;
}

// =============================================
// FUNCIÓN: Actualizar fin del pulso LED
// =============================================
void actualizarPulsoNuevoMax() {
  if (finPulsoNuevoMaxMs == 0) {
    return;
  }

  if ((long)(millis() - finPulsoNuevoMaxMs) >= 0) {
    digitalWrite(PIN_LED_NUEVO_MAX, LOW);
    finPulsoNuevoMaxMs = 0;
  }
}

// =============================================
// FUNCIÓN: Leer botón de reset con debounce
// =============================================
bool leerBotonReset() {
  static unsigned long ultimoRebote = 0;

  if (digitalRead(PIN_BOTON_BUSCAR) == LOW) {
    if (millis() - ultimoRebote > 250) {
      ultimoRebote = millis();
      return true;
    }
  }

  return false;
}

// =============================================
// FUNCIÓN: Cálculo de posición de la pared
// =============================================
ResultadoPared calcularPosicionPared(float angulo_deg) {
  ResultadoPared res;

  angulo_deg = constrain(angulo_deg, 1.0, 179.0);

  float angulo_rad  = angulo_deg * PI / 180.0;
  float offset      = DISTANCIA_CM * (cos(angulo_rad) / sin(angulo_rad));
  float mitad_rango = (LONGITUD_CARRIL_CM - ANCHO_PARED_CM) / 2.0;

  res.posicion_ideal = offset;
  res.puede_bloquear = (abs(offset) <= mitad_rango);
  res.posicion_real  = constrain(offset, -mitad_rango, mitad_rango);

  return res;
}

// =============================================
// FUNCIÓN: Actualizar mejor objetivo detectado
// =============================================
void registrarNuevaLectura(int angulo, int lectura) {
  if (lectura <= UMBRAL_LUZ) {
    return;
  }

  if (lectura < (mejorLecturaBarrido + HISTERESIS_LUZ)) {
    return;
  }

  mejorLecturaBarrido = lectura;
  mejorAnguloBarrido  = angulo;
  iniciarPulsoNuevoMax();
}

// =============================================
// FUNCIÓN: Aplicar el mejor angulo al cerrar un barrido
// =============================================
void fijarObjetivoDesdeBarrido() {
  angulSolActual = mejorAnguloBarrido;

  ResultadoPared res = calcularPosicionPared(angulSolActual);
  posicionParedObjetivo = res.posicion_real;
  hayObjetivoValido = true;
  barridoCompleto = true;

  Serial.print("[TRACK] Mejor luz: ADC=");
  Serial.print(mejorLecturaBarrido);
  Serial.print(" | angulo=");
  Serial.print(angulSolActual);
  Serial.print(" | pared=");
  Serial.print(posicionParedObjetivo, 2);
  Serial.print(" cm | bloqueo total=");
  Serial.println(res.puede_bloquear ? "SI" : "NO");
}

// =============================================
// FUNCIÓN: Barrido continuo del servo
// =============================================
void actualizarBarridoServo() {
  unsigned long ahora = millis();
  if (ahora - ultimoPasoServoMs < ESPERA_BARRIDO_MS) {
    return;
  }

  ultimoPasoServoMs = ahora;

  int lectura = analogRead(PIN_FOTOTRANSISTOR);
  registrarNuevaLectura(anguloServoActual, lectura);

  int siguienteAngulo = anguloServoActual + (direccionBarrido * PASO_BARRIDO);

  if (siguienteAngulo >= ANGULO_SERVO_MAX || siguienteAngulo <= ANGULO_SERVO_MIN) {
    siguienteAngulo = constrain(siguienteAngulo, ANGULO_SERVO_MIN, ANGULO_SERVO_MAX);
    direccionBarrido *= -1;

    if (mejorLecturaBarrido > UMBRAL_LUZ) {
      fijarObjetivoDesdeBarrido();
      Serial.print("[BARRIDO] Fin de pasada. Mejor angulo=");
      Serial.print(mejorAnguloBarrido);
      Serial.print(" | ADC=");
      Serial.println(mejorLecturaBarrido);
    } else {
      Serial.println("[BARRIDO] Fin de pasada sin luz suficiente.");
      hayObjetivoValido = false;
    }

    mejorLecturaBarrido = 0;
    mejorAnguloBarrido  = siguienteAngulo;
  }

  anguloServoActual = siguienteAngulo;
  servoSensor.write(anguloServoActual);
}

// =============================================
// FUNCIÓN: Mover la pared con la logica original
// =============================================
void moverPared(float nuevaPosicion_cm) {
  float delta_cm = nuevaPosicion_cm - posicionParedActual;

  if (abs(delta_cm) <= TOLERANCIA_CM) {
    apagarStepper();
    return;
  }

  int pasos = (int)(delta_cm / CM_POR_PASO);
  int dir = (pasos > 0) ? +1 : -1;
  int total = abs(pasos);
  int delayPaso = (int)(60000.0 / (VELOCIDAD_STEPPER * 4096.0));

  stepperActivo = true;

  for (int i = 0; i < total; i++) {
    actualizarAnimacionDireccion(dir);
    pasarStepper(dir);
    delay(delayPaso);
    actualizarPulsoNuevoMax();
  }

  apagarStepper();
  posicionParedActual = nuevaPosicion_cm;
}

// =============================================
// FUNCIÓN: Reiniciar estado interno del sistema
// =============================================
void reiniciarSistema() {
  apagarStepper();

  angulSolActual = 90.0;
  posicionParedActual = 0.0;
  posicionParedObjetivo = 0.0;

  pasoActual = 0;
  anguloServoActual = 90;
  direccionBarrido = +1;
  mejorLecturaBarrido = 0;
  mejorAnguloBarrido = 90;

  hayObjetivoValido = false;
  stepperActivo = false;
  barridoCompleto = false;

  ultimoPasoServoMs = 0;
  ultimoLogMs = 0;
  finPulsoNuevoMaxMs = 0;
  ultimoCambioAnimMs = 0;
  ultimoDirStepper = 0;
  indiceAnimacionDir = 0;
  animacionDirEncendida = false;

  servoSensor.write(anguloServoActual);
  digitalWrite(PIN_LED, HIGH);
  digitalWrite(PIN_LED_NUEVO_MAX, LOW);
  apagarLedsDireccion();

  Serial.println("[RESET] Sistema reiniciado desde el boton 14.");
}

// =============================================
// FUNCIÓN: Log periódico del estado
// =============================================
void imprimirEstado() {
  unsigned long ahora = millis();
  if (ahora - ultimoLogMs < 1000) {
    return;
  }

  ultimoLogMs = ahora;

  Serial.print("[ESTADO] Servo=");
  Serial.print(anguloServoActual);
  Serial.print(" | Mejor angulo=");
  Serial.print(angulSolActual);
  Serial.print(" | Pared actual=");
  Serial.print(posicionParedActual, 2);
  Serial.print(" cm | Objetivo=");
  Serial.print(posicionParedObjetivo, 2);
  Serial.print(" cm | Seguimiento=");
  Serial.println(hayObjetivoValido ? "ACTIVO" : "SIN LUZ");
}

// =============================================
// SETUP
// =============================================
void setup() {
  Serial.begin(115200);
  Serial.println("=== Sistema de Sombra - ESP32 ===");

  // Se dejan configurados aunque ya no se usen en automatico.
  pinMode(PIN_BOTON_BUSCAR, INPUT_PULLUP);
  pinMode(PIN_BOTON_MOVER, INPUT_PULLUP);

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);
  pinMode(PIN_LED_DIR_1, OUTPUT);
  pinMode(PIN_LED_DIR_2, OUTPUT);
  pinMode(PIN_LED_DIR_3, OUTPUT);
  pinMode(PIN_LED_NUEVO_MAX, OUTPUT);
  apagarLedsDireccion();
  digitalWrite(PIN_LED_NUEVO_MAX, LOW);

  pinMode(PIN_STEPPER_IN1, OUTPUT);
  pinMode(PIN_STEPPER_IN2, OUTPUT);
  pinMode(PIN_STEPPER_IN3, OUTPUT);
  pinMode(PIN_STEPPER_IN4, OUTPUT);
  apagarStepper();

  servoSensor.attach(PIN_SERVO);
  servoSensor.write(anguloServoActual);
  delay(500);

  mejorAnguloBarrido = anguloServoActual;

  Serial.println("[AUTO] Seguimiento automatico activado.");
  Serial.println("[AUTO] El servo barre continuamente y la pared se corrige en tiempo real.");
}

// =============================================
// LOOP PRINCIPAL
// =============================================
void loop() {
  if (leerBotonReset()) {
    reiniciarSistema();
    delay(200);
    return;
  }

  actualizarBarridoServo();
  actualizarPulsoNuevoMax();

  if (hayObjetivoValido && barridoCompleto) {
    barridoCompleto = false;
    moverPared(posicionParedObjetivo);
  }

  imprimirEstado();
}
