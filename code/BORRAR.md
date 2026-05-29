Viewed address_space.cc:1-151
Viewed address_space.hh:1-62
Viewed executable.hh:1-72

---

## ¿Qué es esto en el contexto de NachOS?

NachOS es un sistema operativo educativo simulado. Este fragmento implementa el manejo de **espacios de direcciones** (`AddressSpace`), que es el mecanismo que permite cargar y ejecutar programas de usuario en la memoria simulada de NachOS.

---

## `executable.hh` — La clase `Executable`

Esta clase es una **abstracción del archivo ejecutable** en formato NOFF (Nachos Object File Format), que es el formato binario que entiende NachOS (una versión simplificada de los ejecutables de UNIX).

**¿Qué hace?** Sirve como un "lector inteligente" del archivo ejecutable. Permite preguntarle cosas como:
- ¿Cuánto pesa el código? (`GetCodeSize`)
- ¿Dónde empieza el segmento de datos? (`GetInitDataAddr`)
- Dame los primeros N bytes del código (`ReadCodeBlock`)

Un ejecutable tiene tres segmentos clásicos:
| Segmento | Qué contiene |
|---|---|
| **Code** | Las instrucciones del programa |
| **InitData** | Variables globales con valor inicial |
| **UninitData** | Variables globales sin valor inicial (todo ceros) |

---

## `address_space.hh` — La clase `AddressSpace`

Define la estructura que representa el **espacio de memoria virtual de un proceso**. Tiene dos atributos privados clave:

- **`pageTable`**: una tabla de páginas, que es el mapa de traducción entre direcciones virtuales y físicas.
- **`numPages`**: cuántas páginas ocupa el proceso en memoria.

Y cuatro métodos públicos:
- **Constructor**: carga el ejecutable en memoria.
- **Destructor**: libera la memoria al terminar.
- **`InitRegisters`**: prepara los registros de la CPU simulada para arrancar el programa.
- **`SaveState` / `RestoreState`**: guardan y restauran el estado en un cambio de contexto.

---

## `address_space.cc` — La implementación

### Constructor `AddressSpace(OpenFile *executable_file)`

Este es el corazón del código. Hace lo siguiente paso a paso:

1. **Abre e inspecciona el ejecutable**: crea un objeto `Executable` y verifica que el archivo sea válido (chequea el número mágico del formato NOFF).

2. **Calcula cuánta memoria necesita el proceso**: suma el tamaño del código+datos más el espacio para el **stack** del usuario (`USER_STACK_SIZE = 1024 bytes`). Redondea hacia arriba al múltiplo de `PAGE_SIZE` más cercano.

3. **Verifica que haya páginas físicas disponibles**: consulta el bitmap global `usedPages` para asegurarse de que hay suficientes marcos libres en la RAM simulada.

4. **Construye la tabla de páginas** (`pageTable`): para cada página virtual del proceso, busca un marco físico libre en `usedPages` (usando `Find()`, que también lo marca como ocupado), lo asigna, y limpia esa región de memoria física con ceros.
   > Esta es la evolución clave mencionada en los comentarios: antes había un mapeo 1:1 (página virtual N → marco físico N), lo cual solo funcionaba con un proceso a la vez. Ahora se asignan marcos de forma dinámica, permitiendo **multiprogramación**.

5. **Copia el código al lugar correcto en memoria física**: recorre byte a byte el segmento de código, traduce cada dirección virtual a física usando la tabla de páginas que acaba de construir, y escribe el byte en la memoria simulada.

6. **Copia los datos inicializados** de la misma manera.

### Destructor `~AddressSpace()`

Recorre la tabla de páginas y **marca como libres** en el bitmap `usedPages` todos los marcos físicos que tenía asignados. Luego libera la tabla de páginas. Esto es fundamental para que otros procesos puedan reusar esa memoria.

### `InitRegisters()`

Inicializa todos los registros de la CPU simulada (MIPS) a cero, y luego configura tres registros especiales:
- **PC** (`PC_REG`): el contador de programa arranca en 0, que es donde empieza el código.
- **Next PC** (`NEXT_PC_REG`): se pone en 4 (la siguiente instrucción), necesario por el *branch delay* de MIPS.
- **Stack Pointer** (`STACK_REG`): apunta al final del espacio de direcciones menos 16 bytes (el tope del stack).

### `SaveState()` / `RestoreState()`

- `SaveState` está **vacío por ahora** — en un sistema más completo debería guardar el estado específico del espacio de direcciones (ej: el TLB).
- `RestoreState` le indica a la MMU (unidad de manejo de memoria) cuál es la tabla de páginas del proceso actual y cuántas páginas tiene. Esto es lo que permite que al retomar un proceso, la CPU traduzca correctamente sus direcciones virtuales.

---

## Resumen del flujo completo

```
Archivo NOFF en disco
        ↓
  Executable (lee el binario)
        ↓
  AddressSpace (asigna marcos físicos libres del bitmap global)
        ↓
  Copia código y datos (traduciendo virtual → físico)
        ↓
  InitRegisters (prepara la CPU para arrancar)
        ↓
  RestoreState (le dice a la MMU qué tabla de páginas usar)
        ↓
  El proceso corre en la CPU simulada
```

---

## ¿Qué debería hacer (y qué le falta)?

El código **ya implementa la parte más importante**: la multiprogramación con asignación dinámica de páginas. Lo que típicamente falta en etapas posteriores de NachOS es:

- **`SaveState`**: guardar el TLB u otro estado al suspender un proceso.
- **Manejo de TLB**: en lugar de cargar toda la tabla de páginas, manejarla bajo demanda con fallos de TLB.
- **Swapping**: mover páginas a disco cuando la RAM está llena.
- **Memoria compartida** entre procesos.