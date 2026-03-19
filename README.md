# Sistema de Sombra

Proyecto de control automático de sombra con **ESP32**, un **servo** y un **motor paso a paso (stepper)**.

La idea del sistema es:

1. Un **fototransistor** montado sobre un **servo** barre distintos ángulos para encontrar la dirección donde llega más luz.
2. A partir de ese ángulo, el programa calcula dónde debe colocarse una **pantalla/cartón** que se mueve por un carril.
3. Un **stepper 28BYJ-48 con ULN2003** desplaza el sistema de polea para colocar el cartón y generar sombra sobre un objetivo.

El archivo principal del proyecto es:

- `esp32completo.ino`

---

## Vista rápida

### Objetivo del proyecto
Construir un sistema que proyecte sombra automáticamente sobre un objetivo, desplazando un cartón por un carril según la dirección desde la que incide la luz.

### Resumen del algoritmo
1. El servo orienta el fototransistor.
2. El sensor barre todo el rango angular.
3. Se detecta el ángulo con mayor intensidad lumínica.
4. Ese ángulo se transforma en una posición lateral mediante trigonometría.
5. El stepper mueve el carro/polea hasta esa posición.
6. El proceso se repite continuamente para seguir la luz en tiempo real.

### Archivo del proyecto
- `esp32completo.ino`: sketch principal con toda la lógica de sensado, cálculo y movimiento.

---

## Cómo está organizado el código

Aunque el proyecto está en un único archivo, internamente se puede dividir en bloques muy claros:

- **Definición de pines**: conexión del hardware.
- **Parámetros del sistema**: geometría, tiempos, umbrales y conversión mecánica.
- **Estado global**: posición actual, mejor ángulo, flags y temporizadores.
- **Funciones de actuadores**: control de servo, stepper y LEDs.
- **Funciones de cálculo**: conversión de ángulo a posición lineal.
- **Funciones de control**: barrido, detección del máximo y corrección de la pared.
- **`setup()`**: inicialización del hardware.
- **`loop()`**: ejecución continua del sistema automático.

---

## 1. Qué hace el sistema

El sistema implementa un seguimiento automático de la luz:

- El servo gira el sensor entre `0°` y `180°`.
- En cada posición lee el valor analógico del fototransistor.
- Guarda el ángulo con mayor intensidad de luz detectada.
- Convierte ese ángulo en una posición lineal sobre el carril.
- Mueve la pared/pantalla mediante el stepper hasta la posición calculada.

En resumen:

**ángulo de la luz → cálculo geométrico → posición objetivo → movimiento del cartón**

---

## 2. Hardware implicado

Según el código, el sistema usa:

- **ESP32**
- **Fototransistor** conectado a una entrada analógica
- **Servo** para orientar el sensor
- **Stepper 28BYJ-48** con driver **ULN2003**
- **Botón de reset/búsqueda**
- **LEDs de estado**

---

## 3. Asignación de pines

### Tabla rápida de conexiones

- GPIO 34 → fototransistor (lectura analógica)
- GPIO 18 → señal del servo
- GPIO 19/21/22/23 → entradas IN1/IN2/IN3/IN4 del ULN2003
- GPIO 14 → botón de reset / reinicio de seguimiento
- GPIO 27 → botón auxiliar reservado
- GPIO 2 → LED principal de sistema
- GPIO 26/25/33 → LEDs de dirección
- GPIO 15 → LED de nuevo máximo detectado


Estas definiciones están al inicio del código:

```cpp
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
```

### Explicación

- `PIN_FOTOTRANSISTOR`: lectura analógica del sensor de luz.
- `PIN_SERVO`: salida PWM para mover el servo que orienta el sensor.
- `PIN_STEPPER_IN1..IN4`: bobinas del stepper controladas por el ULN2003.
- `PIN_BOTON_BUSCAR`: botón para reiniciar/rearmar el sistema.
- `PIN_BOTON_MOVER`: se configura, pero en esta versión no participa realmente en la lógica automática.
- `PIN_LED`: LED principal de encendido/estado.
- `PIN_LED_DIR_1..3`: LEDs para animar visualmente el sentido de movimiento del stepper.
- `PIN_LED_NUEVO_MAX`: LED que da un pulso al detectar un nuevo máximo de luz durante el barrido.

---

## 4. Parámetros físicos y de control

```cpp
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
```

### Significado

#### Geometría del sistema
- `DISTANCIA_CM`: distancia entre el eje de referencia y el plano donde se proyecta la sombra.
- `ANCHO_PARED_CM`: ancho del cartón o pantalla que hace sombra.
- `LONGITUD_CARRIL_CM`: recorrido total disponible del sistema lineal.

#### Conversión mecánica
- `CM_POR_PASO`: cuántos centímetros se desplaza la pared por cada paso del stepper.

#### Barrido del servo
- `ANGULO_SERVO_MIN` / `ANGULO_SERVO_MAX`: límites del barrido.
- `PASO_BARRIDO`: resolución angular del escaneo; el servo avanza de 2 en 2 grados.
- `ESPERA_BARRIDO_MS`: pausa entre pasos del barrido.

#### Detección de luz
- `UMBRAL_LUZ`: valor mínimo de ADC para considerar que hay luz útil.
- `HISTERESIS_LUZ`: evita que pequeñas variaciones sustituyan continuamente el máximo detectado.

#### Movimiento lineal
- `TOLERANCIA_CM`: si el objetivo está suficientemente cerca de la posición actual, no mueve el stepper.
- `VELOCIDAD_STEPPER`: velocidad usada para calcular el retardo entre pasos.

#### LEDs auxiliares
- `PULSO_NUEVO_MAX_MS`: duración del pulso del LED de nuevo máximo.
- `ANIMACION_DIR_MS`: tiempo entre cambios de animación de los LEDs de dirección.

---

## 5. Secuencia del motor paso a paso

```cpp
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
```

Esta es la **secuencia half-step** típica del **28BYJ-48**. Se usa para mover el motor con más suavidad y mejor resolución.

Cada fila indica qué bobinas deben estar activadas en cada instante.

---

## 6. Variables globales

### Servo y posición
```cpp
float angulSolActual        = 90.0;
float posicionParedActual   = 0.0;
float posicionParedObjetivo = 0.0;
```

- `angulSolActual`: mejor ángulo de luz detectado.
- `posicionParedActual`: posición lineal actual del cartón.
- `posicionParedObjetivo`: posición calculada a la que se quiere mover la pared.

### Estado del barrido
```cpp
int   pasoActual            = 0;
int   anguloServoActual     = 90;
int   direccionBarrido      = +1;
int   mejorLecturaBarrido   = 0;
int   mejorAnguloBarrido    = 90;
```

- `pasoActual`: índice dentro de la secuencia del stepper.
- `anguloServoActual`: posición instantánea del servo.
- `direccionBarrido`: sentido del barrido (`+1` o `-1`).
- `mejorLecturaBarrido`: mayor valor de luz encontrado en la pasada actual.
- `mejorAnguloBarrido`: ángulo asociado a esa mejor lectura.

### Flags de estado
```cpp
bool  hayObjetivoValido      = false;
bool  stepperActivo          = false;
bool  barridoCompleto        = false;
```

- `hayObjetivoValido`: indica si se detectó una luz válida.
- `stepperActivo`: indica si el motor está energizado/moviéndose.
- `barridoCompleto`: se activa al terminar una pasada útil del servo y sirve para disparar el movimiento de la pared.

### Temporización
```cpp
unsigned long ultimoPasoServoMs   = 0;
unsigned long ultimoLogMs         = 0;
unsigned long finPulsoNuevoMaxMs  = 0;
unsigned long ultimoCambioAnimMs  = 0;
```

Se usan para programar acciones con `millis()` sin depender completamente de `delay()`.

### Animación de LEDs
```cpp
int ultimoDirStepper              = 0;
int indiceAnimacionDir            = 0;
bool animacionDirEncendida        = false;
```

Gestionan el sentido y estado visual de los LEDs de dirección.

---

## 7. Estructura `ResultadoPared`

```cpp
struct ResultadoPared {
  float posicion_ideal;
  float posicion_real;
  bool  puede_bloquear;
};
```

Esta estructura devuelve el resultado del cálculo geométrico:

- `posicion_ideal`: posición teórica exacta para tapar la luz.
- `posicion_real`: posición limitada a lo que físicamente permite el carril.
- `puede_bloquear`: indica si el sistema puede bloquear completamente la luz sin salirse del rango mecánico.

---

## 8. Explicación de cada función

## `pasarStepper(int dir)`

```cpp
void pasarStepper(int dir)
```

### Qué hace
Avanza el motor paso a paso en una dirección:
- `+1` = derecha
- `-1` = izquierda

### Cómo funciona
- Actualiza `pasoActual` recorriendo circularmente los 8 estados de la secuencia.
- Escribe en `IN1..IN4` la combinación correspondiente.

### Papel en el sistema
Es la función base que realmente hace girar el stepper.

---

## `apagarStepper()`

```cpp
void apagarStepper()
```

### Qué hace
Desactiva todas las bobinas del stepper.

### Por qué es importante
- evita calentamiento innecesario
- reduce consumo
- deja el motor en reposo al terminar el movimiento
- apaga también la animación de LEDs asociada al movimiento

Además pone `stepperActivo = false`.

---

## `apagarLedsDireccion()`

```cpp
void apagarLedsDireccion()
```

### Qué hace
Apaga los tres LEDs que indican el sentido de desplazamiento.

### Papel en el sistema
Se usa como función auxiliar para limpiar el estado visual antes de encender un LED concreto o al parar el motor.

---

## `actualizarAnimacionDireccion(int dir)`

```cpp
void actualizarAnimacionDireccion(int dir)
```

### Qué hace
Genera una animación secuencial con tres LEDs para indicar visualmente hacia qué lado se mueve el carro.

### Lógica
- Si cambia la dirección del stepper, reinicia la animación.
- Cada `ANIMACION_DIR_MS` alterna entre:
  - apagar LEDs
  - encender el LED correspondiente
- Si `dir < 0`, la secuencia va de `LED1 → LED2 → LED3`.
- Si `dir > 0`, la secuencia va al revés.

### Papel en el sistema
No afecta al control mecánico, pero ayuda a depurar y visualizar el sentido del movimiento.

---

## `iniciarPulsoNuevoMax()`

```cpp
void iniciarPulsoNuevoMax()
```

### Qué hace
Enciende un LED temporalmente cuando el sistema detecta un nuevo valor máximo de luz en el barrido.

### Papel en el sistema
Da feedback inmediato de que el barrido acaba de encontrar un ángulo mejor que el anterior.

---

## `actualizarPulsoNuevoMax()`

```cpp
void actualizarPulsoNuevoMax()
```

### Qué hace
Comprueba si ya terminó el tiempo del pulso del LED de nuevo máximo y, si corresponde, lo apaga.

### Lógica
- Si `finPulsoNuevoMaxMs == 0`, no hay pulso activo.
- Si el tiempo actual supera el final programado, apaga el LED.

---

## `leerBotonReset()`

```cpp
bool leerBotonReset()
```

### Qué hace
Lee el botón de reset con una lógica simple de debounce.

### Lógica
- Si el botón está en `LOW`, se considera pulsado.
- Solo acepta una nueva pulsación si han pasado más de `250 ms` desde la última.

### Nota
Aunque el pin se llama `PIN_BOTON_BUSCAR`, en esta versión su función real es reiniciar el sistema.

---

## `calcularPosicionPared(float angulo_deg)`

```cpp
ResultadoPared calcularPosicionPared(float angulo_deg)
```

### Qué hace
Convierte el ángulo de incidencia de la luz en una posición lineal para la pared/cartón.

### Fórmula usada
El cálculo central es:

```cpp
float offset = DISTANCIA_CM * (cos(angulo_rad) / sin(angulo_rad));
```

Eso equivale a:

**offset = DISTANCIA * cot(ángulo)**

### Interpretación física
- Si el ángulo cambia, cambia el punto donde debe colocarse la sombra.
- El `offset` representa el desplazamiento lateral necesario para interceptar la luz.

### Protección de límites
- restringe el ángulo a `1°..179°` para evitar divisiones peligrosas cuando `sin(ángulo)` se acerca a 0.
- calcula `mitad_rango = (LONGITUD_CARRIL_CM - ANCHO_PARED_CM) / 2.0`
- limita la posición real con `constrain(...)`

### Devuelve
- la posición ideal
- la posición real posible
- si se puede bloquear totalmente o no

Es una de las funciones más importantes del proyecto porque convierte la detección angular en movimiento lineal útil.

---

## `registrarNuevaLectura(int angulo, int lectura)`

```cpp
void registrarNuevaLectura(int angulo, int lectura)
```

### Qué hace
Decide si una nueva lectura del fototransistor merece convertirse en el nuevo máximo del barrido actual.

### Reglas
1. Ignora lecturas menores o iguales que `UMBRAL_LUZ`.
2. Solo actualiza el máximo si supera al actual al menos en `HISTERESIS_LUZ`.

### Por qué hay histéresis
Sin histéresis, pequeñas fluctuaciones del ADC podrían cambiar continuamente el mejor ángulo aunque la diferencia no sea real o relevante.

### Si encuentra un nuevo máximo
- actualiza `mejorLecturaBarrido`
- actualiza `mejorAnguloBarrido`
- dispara el LED de nuevo máximo

---

## `fijarObjetivoDesdeBarrido()`

```cpp
void fijarObjetivoDesdeBarrido()
```

### Qué hace
Cuando termina una pasada del servo, toma el mejor ángulo encontrado y lo transforma en una nueva posición objetivo para la pared.

### Pasos
1. Copia `mejorAnguloBarrido` a `angulSolActual`.
2. Llama a `calcularPosicionPared(...)`.
3. Guarda la posición en `posicionParedObjetivo`.
4. Marca:
   - `hayObjetivoValido = true`
   - `barridoCompleto = true`
5. Imprime por serial información de depuración.

### Papel en el sistema
Es el puente entre el barrido del servo y el movimiento del stepper.

---

## `actualizarBarridoServo()`

```cpp
void actualizarBarridoServo()
```

### Qué hace
Gestiona el barrido continuo del servo.

### Flujo interno
1. Espera a que haya pasado `ESPERA_BARRIDO_MS` desde el último movimiento.
2. Lee el fototransistor.
3. Registra la lectura para ver si es el nuevo máximo.
4. Calcula el siguiente ángulo del servo.
5. Si llega a un extremo:
   - invierte la dirección del barrido
   - evalúa si hubo una luz válida en la pasada
   - si la hubo, fija un nuevo objetivo
   - si no, marca `hayObjetivoValido = false`
6. Mueve el servo al nuevo ángulo.

### Detalle importante
El servo no se mueve directamente al “mejor ángulo”; lo que hace es **barrer constantemente**. El mejor ángulo se usa para calcular la posición del cartón.

---

## `moverPared(float nuevaPosicion_cm)`

```cpp
void moverPared(float nuevaPosicion_cm)
```

### Qué hace
Desplaza el cartón a la nueva posición deseada usando el motor paso a paso.

### Lógica
1. Calcula el error:
   ```cpp
   delta_cm = nuevaPosicion_cm - posicionParedActual;
   ```
2. Si el error es menor que `TOLERANCIA_CM`, no mueve nada.
3. Convierte centímetros a pasos:
   ```cpp
   pasos = (int)(delta_cm / CM_POR_PASO);
   ```
4. Determina dirección y número total de pasos.
5. Calcula el retardo entre pasos a partir de `VELOCIDAD_STEPPER`.
6. Ejecuta un bucle moviendo el motor paso a paso.
7. Actualiza visualmente la dirección con LEDs.
8. Al terminar, apaga el motor y actualiza `posicionParedActual`.

### Nota importante
Esta función usa `delay(delayPaso)` dentro de un bucle. Eso simplifica el movimiento, pero mientras se mueve el stepper el microcontrolador queda menos disponible para otras tareas en tiempo real.

---

## `reiniciarSistema()`

```cpp
void reiniciarSistema()
```

### Qué hace
Restablece todas las variables internas del sistema a un estado inicial.

### Qué reinicia
- posición actual y objetivo
- ángulo del servo
- dirección del barrido
- máximos de luz
- flags de seguimiento
- temporizadores
- animaciones de LEDs

### Estado final
- coloca el servo en `90°`
- deja el LED principal encendido
- apaga LEDs auxiliares
- escribe un mensaje por serial

### Papel en el sistema
Permite volver a un estado conocido cuando el usuario pulsa el botón de reset.

---

## `imprimirEstado()`

```cpp
void imprimirEstado()
```

### Qué hace
Imprime una vez por segundo el estado actual por el puerto serie.

### Información mostrada
- ángulo actual del servo
- mejor ángulo de luz detectado
- posición actual de la pared
- posición objetivo
- si el seguimiento está activo o no

### Papel en el sistema
Es una función de diagnóstico muy útil para depuración y pruebas.

---

## 9. `setup()` explicado

```cpp
void setup()
```

### Qué hace al arrancar
1. Inicializa el puerto serie a `115200`.
2. Configura botones con `INPUT_PULLUP`.
3. Configura LEDs de estado.
4. Configura los pines del stepper como salida.
5. Apaga el stepper inicialmente.
6. Conecta el servo al pin indicado.
7. Coloca el servo en el ángulo inicial (`90°`).
8. Define `mejorAnguloBarrido = anguloServoActual`.
9. Muestra mensajes indicando que el seguimiento automático está activo.

### Comentario importante del código
El propio sketch aclara que algunos botones quedan configurados aunque ya no se usen en modo automático.

---

## 10. `loop()` explicado

```cpp
void loop()
```

### Orden de ejecución
1. Comprueba si se pulsó el botón de reset.
2. Si se pulsó, reinicia el sistema y sale de la iteración actual.
3. Actualiza el barrido del servo.
4. Actualiza el pulso del LED de nuevo máximo.
5. Si hay un objetivo válido y el barrido ha terminado:
   - limpia `barridoCompleto`
   - mueve la pared a `posicionParedObjetivo`
6. Imprime el estado por serial.

### En lenguaje simple
El bucle principal hace constantemente esto:

- buscar de dónde viene la luz
- decidir dónde debe ponerse la pantalla
- mover la pantalla
- informar del estado

---

## 11. Flujo completo del programa

### Diagrama mental del sistema

```text
Fototransistor + Servo
        ↓
Barrido angular continuo
        ↓
Detección del máximo de luz
        ↓
Cálculo trigonométrico de posición
        ↓
Stepper + polea
        ↓
Desplazamiento del cartón
        ↓
Sombra sobre el objetivo
```


El comportamiento completo del sistema puede resumirse así:

1. **Inicio**
   - el ESP32 arranca
   - el servo se pone en `90°`
   - el sistema entra en modo automático

2. **Barrido del sensor**
   - el servo recorre el rango angular
   - el fototransistor mide la intensidad de luz en cada punto

3. **Detección del mejor ángulo**
   - se guarda la lectura más intensa del barrido
   - un LED avisa cuando aparece un nuevo máximo

4. **Cálculo geométrico**
   - al terminar la pasada, el mejor ángulo se transforma en una posición lineal del cartón

5. **Movimiento del sistema de sombra**
   - el stepper mueve la polea/carro hasta la posición objetivo
   - los LEDs de dirección muestran hacia dónde se desplaza

6. **Seguimiento continuo**
   - el servo sigue barriendo
   - si cambia la dirección de la luz, se recalcula todo
   - el cartón se recoloca para seguir generando sombra sobre el objetivo

---

## 12. Comportamiento visual de los LEDs

### `PIN_LED`
LED principal de encendido/estado general.

### `PIN_LED_NUEVO_MAX`
Se enciende brevemente cuando el sistema encuentra un nuevo máximo de luz durante el barrido.

### `PIN_LED_DIR_1`, `PIN_LED_DIR_2`, `PIN_LED_DIR_3`
Muestran una animación de dirección mientras el stepper se mueve:
- en un sentido: `1 → 2 → 3`
- en el otro: `3 → 2 → 1`

Esto ayuda a interpretar el movimiento del carro sin necesidad de mirar la salida serie.

---

## 13. Salida serie

El programa imprime mensajes como:

- inicio del sistema
- fin de barrido
- mejor ángulo detectado
- posición objetivo de la pared
- si puede bloquear totalmente la luz o no
- estado periódico del sistema
- reset por botón

Ejemplo conceptual:

```text
[AUTO] Seguimiento automatico activado.
[BARRIDO] Fin de pasada. Mejor angulo=...
[TRACK] Mejor luz: ADC=... | angulo=... | pared=... cm | bloqueo total=SI
[ESTADO] Servo=... | Mejor angulo=... | Pared actual=... | Objetivo=... cm | Seguimiento=ACTIVO
```

---

## 14. Ideas clave del diseño

Este proyecto mezcla tres bloques principales:

### 1. Sensado
El fototransistor detecta por dónde entra más luz.

### 2. Cálculo
La función `calcularPosicionPared()` convierte un ángulo en una posición física.

### 3. Actuación
El stepper mueve la pantalla para generar la sombra deseada.

Es un sistema de control simple, pero muy didáctico, porque combina:
- electrónica
- sensado analógico
- control de servo
- control de stepper
- geometría trigonométrica
- depuración por LEDs y puerto serie

---

## 15. Limitaciones actuales del código

Antes de pensar en mejoras, conviene entender las limitaciones de esta versión:

- Todo el proyecto está en un único archivo `.ino`, lo que simplifica el prototipo pero dificulta escalarlo.
- `moverPared()` usa `delay()`, así que durante el movimiento del stepper el sistema no es completamente no bloqueante.
- No hay finales de carrera ni procedimiento de homing real, por lo que `posicionParedActual` depende de que no se pierdan pasos.
- `CM_POR_PASO` está fijado manualmente y puede requerir calibración experimental.
- El umbral de luz está configurado en `0`, así que prácticamente cualquier lectura positiva puede considerarse válida.
- El fototransistor usa una única medida por ángulo; no hay filtrado avanzado ni promedio estadístico.

## 16. Posibles mejoras futuras

Algunas mejoras que podrían añadirse en versiones siguientes:

- filtrar la lectura del fototransistor con promedios o media móvil
- evitar bloqueos por `delay()` usando una máquina de estados no bloqueante
- recalibrar automáticamente `CM_POR_PASO`
- añadir finales de carrera para homing real del carro
- usar dos sensores para estimar mejor la dirección de la luz
- guardar parámetros en memoria no volátil
- añadir modo manual con botones reales para mover el sistema

---

## 17. Montaje físico del sistema

Aunque el código no describe toda la estructura mecánica al detalle, por su lógica se deduce un montaje como este:

### Elementos mecánicos
- Un **sensor de luz** montado sobre el brazo de un **servo**.
- Un **cartón o pantalla opaca** que hace de barrera de sombra.
- Un **carril lineal** por el que se desplaza esa pantalla.
- Un sistema de **polea, cuerda o arrastre** unido al eje del stepper para convertir giro en desplazamiento lineal.
- Un **objetivo fijo** al que se quiere proteger de la luz directa.

### Disposición recomendada
1. Colocar el **objetivo** en una posición fija frente al sistema.
2. Situar el **cartón/pantalla** entre la fuente de luz y el objetivo.
3. Montar el cartón sobre un **carro deslizante** o soporte móvil.
4. Unir ese carro a una **polea/correa/hilo** accionado por el stepper.
5. Instalar el **servo con el fototransistor** en una posición desde la que pueda barrer el ángulo de incidencia de la luz.
6. Alinear la referencia geométrica del sensor con el sistema lineal para que el cálculo trigonométrico tenga sentido físico.

### Interpretación geométrica del montaje
El programa asume que:
- el servo mide la **dirección angular** desde la que incide la luz,
- existe una distancia fija (`DISTANCIA_CM`) entre la referencia angular y el plano donde actúa la sombra,
- la pantalla se mueve lateralmente a lo largo de un carril,
- y el ancho útil del cartón es `ANCHO_PARED_CM`.

En otras palabras: el sensor detecta "de dónde viene" la luz y el carro mueve la pantalla lateralmente para interponerse en esa trayectoria.

### Recomendaciones prácticas de montaje
- Fijar bien el servo para que no vibre durante el barrido.
- Evitar holguras grandes en la polea o cuerda, porque afectarían a la precisión de `CM_POR_PASO`.
- Si el carro tiene rozamiento, calibrar bien el sistema para que el stepper no pierda pasos.
- Usar topes mecánicos o, idealmente, finales de carrera si se quiere una versión más robusta.
- Procurar que el fototransistor tenga un campo de visión razonable y no reciba sombras parásitas del propio soporte.

### Esquema conceptual

```text
Fuente de luz
      ↓
 [ Servo + fototransistor ]  → detecta el ángulo
      ↓
 Cálculo en el ESP32
      ↓
 [ Stepper + polea ] → mueve → [ Cartón / pantalla ]
                                      ↓
                             genera sombra sobre el objetivo
```

## 18. Cómo cargarlo en el ESP32

Pasos típicos para usar el proyecto:

1. Abrir `esp32completo.ino` en Arduino IDE o PlatformIO.
2. Instalar la librería `ESP32Servo`.
3. Seleccionar la placa ESP32 correcta.
4. Revisar pines y alimentación del servo/stepper.
5. Subir el sketch.
6. Abrir el monitor serie a `115200 baudios`.
7. Observar el barrido del servo y el movimiento corrector de la pared.

### Recomendación eléctrica
No alimentar servo y stepper directamente desde una salida frágil del microcontrolador sin revisar consumos. Lo normal es usar alimentación adecuada y masa común con el ESP32.

## 19. Resumen final

Este código implementa un **sistema automático de sombra** donde:

- un **servo** busca la dirección de la luz,
- un **fototransistor** detecta el ángulo con máxima intensidad,
- una fórmula geométrica calcula dónde debe colocarse la pantalla,
- y un **stepper** mueve un sistema de polea con un cartón para proyectar sombra sobre un objetivo.

Todo el control está concentrado en `esp32completo.ino`, y la lógica está organizada en funciones bastante claras para:

- barrer,
- detectar,
- calcular,
- mover,
- reiniciar,
- y mostrar el estado.
