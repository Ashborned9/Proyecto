#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "tdas/extra.h"
#include "tdas/list.h"
#include "tdas/map.h"
#include "tdas/queue.h"
#include <stddef.h>    
#include <stdbool.h>   


#define MAX_LINEA 512

// ----------------------------------------------------
// Estructuras principales
// ----------------------------------------------------

typedef struct {
    int id;
    char nombre[50];
    char apellido[50];
    int edad;
    char area[50];
    char diagnostico[100];
    int gravedad;         // 1: leve, 2: moderada, 3: grave
    int turnos_espera;    // contador de turnos en Sala de Espera
    int insumo_req_id;    // ID del insumo que necesita
    int cantidad_req;     // cuántas unidades de ese insumo requiere
} Paciente;

typedef struct {
    int id;
    char nombre[100];
    char ubicacion[50];
} Insumo;

typedef struct {
    int id_insumo;
    int cantidad_total;    // stock real en Bodega
    int retirado_hoy;      // cuántas unidades de este ítem ya se retiraron hoy
} StockDiario;

typedef struct {
    char nombre[50];
    int capacidad_pacientes;
    int capacidad_insumos;
    List* pacientes;
    List* insumos;
    List* stock_diario;    // sólo se usa para Bodega Central
} Sala;

// ----------------------------------------------------
// Variables globales
// ----------------------------------------------------

List* salas_global;            // lista de todas las salas
int pacientes_curados = 0;
int pacientes_fallecidos = 0;
int reputacion = 0;
int dia_actual = 0;
int siguiente_id_paciente = 6; // asume que ya cargamos 5 pacientes del CSV inicial
int turnos_restantes;
Map* mapa_stock;
Queue* cola_pacientes;
#define BASE_RETIRAR             50   // unidades mínimas que puedes retirar sin reputación
#define MULTIPLICADOR_REPUTACION 5    // por cada punto de reputación, se añade esta cantidad

// ----------------------------------------------------
// Prototipos de funciones
// ----------------------------------------------------

// Lectura CSV
List* leer_pacientes(FILE* archivo);
List* leer_insumos(FILE* archivo);

// Salas y asignaciones iniciales
Sala* crear_sala(const char* nombre, int cap_pacientes, int cap_insumos);
List* inicializar_salas();
Sala* buscar_sala(List* salas, const char* nombre);
void asignar_pacientes_a_espera(List* pacientes, List* salas);
void asignar_insumos_a_salas(List* insumos, List* salas, Map* mapa_stock);

// Mostrar estado
void mostrar_salas(List* salas);

// Transferencia manual
void transferir_pacientes_menu(List* salas);
void transferir_paciente_unico(List* salas, Paciente* p);

// Sistema de turnos y muertes
void ejecutar_turno(List* salas);
void ejecutar_procesos_fin_dia();

// Gestión de pacientes
void admitir_pacientes_dia(int max_por_dia);
Insumo* buscar_insumo_por_id(int id);


// Menu de gestión diario
void mostrar_encabezado();
void ciclo_diario();

// Atender pacientes
void atender_paciente(List* salas);
void mostrar_estadisticas();

// Gestión de Bodega
void gestionar_bodega(List* salas, int* limite_diario);
void pedir_insumos_proveedor(List* salas);
void distribuir_insumos_a_salass(List* salas, int* limite_diario);

// ----------------------------------------------------
// Funciones de comparación y hash para Map
// 1) int_eq: devuelve true si *a == *b
int int_eq(void *a, void *b) {
    return *(int*)a == *(int*)b;
}

// 2) int_hash: devuelve un hash sencillo (por ejemplo, la propia clave)
//    Convierte el entero en size_t
size_t int_hash(void *key) {
    return (size_t)*(int*)key;
}

// ----------------------------------------------------
// Implementaciones
// ----------------------------------------------------

// ----------------------------------------------------
// Leer pacientes desde CSV
// ----------------------------------------------------
List* leer_pacientes(FILE* archivo) {
    List* lista_pacientes = list_create();
    if (!lista_pacientes) return NULL;
    char linea[300];

    fgets(linea, sizeof(linea), archivo); // Saltar encabezado

    while (fgets(linea, sizeof(linea), archivo)) {
        Paciente* p = (Paciente*) malloc(sizeof(Paciente));
        if (!p) {
            continue; // Si falla, saltar a la siguiente línea
        }

        sscanf(linea,
            "%d,%49[^,],%49[^,],%d,%49[^,],%99[^,],%d,%d,%d",
            &p->id,
            p->nombre,
            p->apellido,
            &p->edad,
            p->area,
            p->diagnostico,
            &p->gravedad,
            &p->insumo_req_id,
            &p->cantidad_req);
        
        if (p->gravedad < 1 || p->gravedad > 3 ||
        p->edad < 0 || p->edad > 120 ||
        p->cantidad_req < 0) {
            free(p);
            continue;
        }

        p->turnos_espera = 0;
        list_pushBack(lista_pacientes, p);
    }

    return lista_pacientes;
}

// ----------------------------------------------------
// Leer insumos desde CSV
// ----------------------------------------------------
List* leer_insumos(FILE* archivo) {
    static const int CAMPOS_ESPERADOS = 3;

    if (!archivo) return NULL;
    
    List* lista_insumos = list_create();
    if (!lista_insumos) return NULL;

    char linea[MAX_LINEA];

    if (fgets(linea, sizeof(linea), archivo) == NULL) { // Saltar encabezado
        return lista_insumos; // Retorna lista vacía si no hay datos
    }

    while (fgets(linea, sizeof(linea), archivo)) {

        size_t len = strlen(linea);
        if (len > 0 && linea[len - 1] == '\n') {
            int ch;
            while ((ch = fgetc(archivo)) != '\n' && ch != EOF); // Consumir el resto de la línea
        }

        linea[strcspn(linea, "\r\n")] = '\0'; // Eliminar salto de línea

        Insumo* ins = malloc(sizeof(Insumo));
        if (ins == NULL) continue;

        int leidos = sscanf(linea, "%d,%99[^,],%49[^\n]",
                &ins->id, 
                ins->nombre,
                ins->ubicacion);

        if (leidos != CAMPOS_ESPERADOS) {
            free(ins);
            continue; // Si no se leyeron todos los campos o cantidad es negativa, saltar
        }

        list_pushBack(lista_insumos, ins);
    }

    return lista_insumos;
}

// ----------------------------------------------------
// Crear y configurar salas
// ----------------------------------------------------
Sala* crear_sala(const char* nombre, int cap_pacientes, int cap_insumos) {
    Sala* s = malloc(sizeof(Sala));
    if (!s) return NULL;

    strncpy(s->nombre, nombre, 50);
    s->capacidad_pacientes = cap_pacientes;
    s->capacidad_insumos = cap_insumos;
    s->pacientes = list_create();
    s->insumos = list_create();
    s->stock_diario = NULL;  // sólo válido si es Bodega Central
    return s;
}

List* inicializar_salas() {
    List* salas = list_create();

    // Sala de Espera (capacidad muy grande, sin insumos)
    list_pushBack(salas, crear_sala("Sala de Espera", 999, 0));

    // Salas clínicas
    list_pushBack(salas, crear_sala("UCI", 10, 100));
    list_pushBack(salas, crear_sala("Urgencias", 20, 150));
    list_pushBack(salas, crear_sala("Ginecologia", 8, 80));
    list_pushBack(salas, crear_sala("Traumatologia", 12, 120));
    list_pushBack(salas, crear_sala("Medicina Interna", 15, 100));
    list_pushBack(salas, crear_sala("Pediatria", 10, 90));

    // Bodega Central (capacidad pacientes=0, insumos se cargan dinámicamente)
    Sala* bodega = crear_sala("Bodega Central", 0, 200);
    bodega->stock_diario = list_create();
    list_pushBack(salas, bodega);

    return salas;
}

Sala* buscar_sala(List* salas, const char* nombre) {
    Sala* s = list_first(salas);
    while (s != NULL) {
        if (strcmp(s->nombre, nombre) == 0) return s;
        s = list_next(salas);
    }
    return NULL;
}

// ----------------------------------------------------
// Asignar todos los pacientes iniciales a Sala de Espera
// ----------------------------------------------------
void admitir_pacientes_dia(int max_por_dia) {
    Sala* espera = buscar_sala(salas_global, "Sala de Espera");
    for (int i = 0; i < max_por_dia && !queue_is_empty(cola_pacientes); i++) {
        Paciente* p = queue_remove(cola_pacientes);
        list_pushBack(espera->pacientes, p);
    }
}
Insumo* buscar_insumo_por_id(int id) {
    Sala* b = buscar_sala(salas_global, "Bodega Central");
    Insumo* tmp = list_first(b->insumos);
    while (tmp) {
        if (tmp->id == id) return tmp;
        tmp = list_next(b->insumos);
    }
    return NULL;
}
// ----------------------------------------------------
// Asignar insumos iniciales a las salas (incluida Bodega)
// ----------------------------------------------------
#define STOCK_INICIAL 100

void asignar_insumos_a_salas(List* insumos, List* salas, Map* mapa_stock) {
    /* 1) buscamos la Bodega Central */
    Sala* bodega = buscar_sala(salas, "Bodega Central");
    if (!bodega) {
        fprintf(stderr, "Error: no existe la sala 'Bodega Central'.\n");
        return;
    }

    /* 2) recorremos todos los insumos */
    Insumo* ins = list_first(insumos);
    while (ins) {
        /* a) lo almacenamos en la Bodega (para nombre/ID) */
        list_pushBack(bodega->insumos, ins);

        /* b) si existe la sala destino, también lo asignamos ahí */
        Sala* destino = buscar_sala(salas, ins->ubicacion);
        if (destino) {
            list_pushBack(destino->insumos, ins);
        } else {
            printf("Sala '%s' no existe para insumo ID %d\n",
                   ins->ubicacion, ins->id);
        }

        /* c) creamos y registramos su StockDiario con stock inicial */
        StockDiario* sd = malloc(sizeof(StockDiario));
        sd->id_insumo      = ins->id;
        sd->cantidad_total = STOCK_INICIAL;   // ← stock inicial
        sd->retirado_hoy   = 0;
        list_pushBack(bodega->stock_diario, sd);

        /* d) lo ponemos también en el mapa para búsquedas rápidas */
        int* clave = malloc(sizeof(int));
        *clave = ins->id;
        map_insert(mapa_stock, clave, sd);

        /* siguiente insumo */
        ins = list_next(insumos);
    }
}




// ----------------------------------------------------
// Mostrar estado de todas las salas
// ----------------------------------------------------
void mostrar_salas(List* salas) {
    Sala* s = list_first(salas);
    while (s != NULL) {
        printf("\nSala: %s\n", s->nombre);
        printf("Capacidad: %d pacientes, %d insumos\n", s->capacidad_pacientes, s->capacidad_insumos);
        printf("Ocupado:   %d pacientes, %d insumos\n",
               list_size(s->pacientes), list_size(s->insumos));

        Paciente* p = list_first(s->pacientes);
        while (p != NULL) {
            printf("  Paciente #%d: %s %s (Gravedad: %d) Turnos espera: %d Requiere ID%d x%d\n",
                   p->id, p->nombre, p->apellido, p->gravedad, p->turnos_espera,
                   p->insumo_req_id, p->cantidad_req);
            p = list_next(s->pacientes);
        }

        Insumo* i = list_first(s->insumos);
        while (i != NULL) {
            printf("  Insumo %d - %s\n", i->id, i->nombre);
            i = list_next(s->insumos);
        }
        s = list_next(salas);
    }
}

// ----------------------------------------------------
// Transferencia manual de varios pacientes (max 5 por turno)
// ----------------------------------------------------
void transferir_pacientes_menu(List* salas) {
    // 1) Verificamos si aún quedan turnos para transferir
    if (turnos_restantes <= 0) {
        printf("No quedan turnos para transferir.\n");
        return;
    }

    Sala* espera = buscar_sala(salas, "Sala de Espera");
    if (!espera || list_size(espera->pacientes) == 0) {
        printf("No hay pacientes en Sala de Espera.\n");
        return;
    }

    int pagina = 1;
    int total = list_size(espera->pacientes);
    int por_pagina = 10;
    int max_paginas = (total + por_pagina - 1) / por_pagina;
    int opcion;

    // 2) Repetimos mientras queden turnos y pacientes
    while (turnos_restantes > 0 && total > 0) {
        printf("\nPacientes en Sala de Espera (pag %d/%d). Turnos restantes: %d\n",
               pagina, max_paginas, turnos_restantes);

        // Mostrar la página actual
        int inicio = (pagina - 1) * por_pagina;
        // mover el cursor al primer elemento de la página
        Paciente* aux = list_first(espera->pacientes);
        for (int i = 0; i < inicio; i++) {
            aux = list_next(espera->pacientes);
        }
        // imprimir hasta por_pagina o hasta el final
        for (int i = 0; i < por_pagina && inicio + i < total; i++) {
            if (i > 0) aux = list_next(espera->pacientes);
            printf("%2d) ID %d - %s %s - Gravedad %d - Turnos espera %d\n",
                   i + 1,
                   aux->id, aux->nombre, aux->apellido,
                   aux->gravedad, aux->turnos_espera);
        }

        printf("\n[1-%d] Transferir paciente  [P]ag sig  [A]nterior  [0] Salir: ",
               por_pagina);
        char input[10];
        fgets(input, sizeof(input), stdin);

        // Navegación de páginas
        if (input[0] == 'P' || input[0] == 'p') {
            if (pagina < max_paginas) pagina++;
            else printf("Ya estás en la última página.\n");
            continue;
        }
        if (input[0] == 'A' || input[0] == 'a') {
            if (pagina > 1) pagina--;
            else printf("Ya estás en la primera página.\n");
            continue;
        }

        opcion = atoi(input);
        if (opcion == 0) break;
        if (opcion < 1 || opcion > por_pagina || inicio + opcion > total) {
            printf("Opción inválida.\n");
            continue;
        }

        // 3) Identificamos al paciente seleccionado
        Paciente* p = list_first(espera->pacientes);
        for (int k = 0; k < inicio + (opcion - 1); k++) {
            p = list_next(espera->pacientes);
        }

        // 4) Transferimos y descontamos un turno
        transferir_paciente_unico(salas, p);
        turnos_restantes--;
        total--;
        max_paginas = (total + por_pagina - 1) / por_pagina;
        if (pagina > max_paginas) pagina = max_paginas;
    }

    // 5) Mensaje si se acabaron los turnos
    if (turnos_restantes == 0) {
        printf("Has agotado tus turnos para transferir.\n");
    }
}
// ----------------------------------------------------
// Transferir un solo paciente (subfunción de arriba)
// ----------------------------------------------------
void transferir_paciente_unico(List* salas, Paciente* p) {
    if (!p) return;
    Sala* espera = buscar_sala(salas, "Sala de Espera");
    if (!espera) return;

    printf("\nTransferir Paciente ID %d (%s %s, Gravedad %d):\n",
           p->id, p->nombre, p->apellido, p->gravedad);

    int idx = 1;
    Sala* destino_preferido = buscar_sala(salas, p->area);
    if (destino_preferido &&
        list_size(destino_preferido->pacientes) < destino_preferido->capacidad_pacientes) {
        printf("%d) %s (ocupado: %d/%d)\n", idx,
               destino_preferido->nombre,
               list_size(destino_preferido->pacientes),
               destino_preferido->capacidad_pacientes);
        idx++;
    } else {
        destino_preferido = NULL;
    }

    Sala* s = list_first(salas);
    while (s != NULL) {
        if (strcmp(s->nombre, "Sala de Espera") != 0 &&
            strcmp(s->nombre, "Bodega Central") != 0 &&
            (destino_preferido == NULL || strcmp(s->nombre, destino_preferido->nombre) != 0)) {
            if (list_size(s->pacientes) < s->capacidad_pacientes) {
                printf("%d) %s (ocupado: %d/%d)\n", idx,
                       s->nombre,
                       list_size(s->pacientes),
                       s->capacidad_pacientes);
                idx++;
            }
        }
        s = list_next(salas);
    }
    printf("0) Cancelar\n");

    int opcion_sala;
    scanf("%d", &opcion_sala);
    getchar();
    if (opcion_sala == 0) {
        printf("Transferencia cancelada.\n");
        return;
    }

    Sala* sala_destino = NULL;
    int contador = 1;
    if (destino_preferido) {
        if (opcion_sala == contador) {
            sala_destino = destino_preferido;
        }
        contador++;
    }

    if (!sala_destino) {
        s = list_first(salas);
        while (s != NULL) {
            if (strcmp(s->nombre, "Sala de Espera") != 0 &&
                strcmp(s->nombre, "Bodega Central") != 0 &&
                (destino_preferido == NULL || strcmp(s->nombre, destino_preferido->nombre) != 0)) {
                if (list_size(s->pacientes) < s->capacidad_pacientes) {
                    if (opcion_sala == contador) {
                        sala_destino = s;
                        break;
                    }
                    contador++;
                }
            }
            s = list_next(salas);
        }
    }

    if (!sala_destino) {
        printf("Opcion invalida.\n");
        return;
    }

    // Verificar espacio en destino
    if (list_size(sala_destino->pacientes) >= sala_destino->capacidad_pacientes) {
        printf("Sala %s esta llena. No se puede transferir.\n", sala_destino->nombre);
        return;
    }

    list_pushBack(sala_destino->pacientes, p);

    // Eliminar de Sala de Espera
    Paciente* buscado = list_first(espera->pacientes);
    while (buscado != NULL) {
        if (buscado == p) {
            list_popCurrent(espera->pacientes);
            break;
        }
        buscado = list_next(espera->pacientes);
    }

    printf("Paciente ID %d transferido a %s.\n", p->id, sala_destino->nombre);
}
// ----------------------------------------------------
// Ejecutar un turno: muertes y auto-transferencias
// ----------------------------------------------------
void ejecutar_turno(List* salas) {
    Sala* espera = buscar_sala(salas, "Sala de Espera");
    Sala* urgencias = buscar_sala(salas, "Urgencias");
    Sala* uci = buscar_sala(salas, "UCI");

    if (!espera) return;

    int n = list_size(espera->pacientes);
    for (int idx = 0; idx < n; idx++) {
        Paciente* p = list_first(espera->pacientes);
        if (!p) break;

        p->turnos_espera++;

        // Auto-transferir gravedad 3
        if (p->gravedad == 3) {
            if (urgencias && list_size(urgencias->pacientes) < urgencias->capacidad_pacientes) {
                list_pushBack(urgencias->pacientes, p);
                list_popCurrent(espera->pacientes);
                printf("Paciente #%d transferido a Urgencias (gravedad 3).\n", p->id);
                continue;
            } else if (uci && list_size(uci->pacientes) < uci->capacidad_pacientes) {
                list_pushBack(uci->pacientes, p);
                list_popCurrent(espera->pacientes);
                printf("Paciente #%d transferido a UCI (gravedad 3).\n", p->id);
                continue;
            }
        }

        // Muertes por exceso de espera
        if ((p->gravedad == 3 && p->turnos_espera > 2) ||
            (p->gravedad == 2 && p->turnos_espera > 3)) {
            pacientes_fallecidos++;
            reputacion -= 2;
            printf("Paciente #%d murio en Sala de Espera (gravedad %d, turnos %d).\n",
                   p->id, p->gravedad, p->turnos_espera);
            list_popCurrent(espera->pacientes);
        } else {
            list_pushBack(espera->pacientes, p);
            list_popCurrent(espera->pacientes);
        }
    }

    printf("Turno finalizado.\n");
}

// ----------------------------------------------------
// Mostrar estadísticas y alertas antes de cada acción
// ----------------------------------------------------
void mostrar_encabezado() {
    // 1) Calculamos cuántos pacientes de gravedad 3 están a punto de morir
    int en_peligro = 0;
    Sala* espera = buscar_sala(salas_global, "Sala de Espera");
    if (espera) {
        Paciente* p = list_first(espera->pacientes);
        while (p != NULL) {
            if (p->gravedad == 3 && p->turnos_espera >= 2) {
                en_peligro++;
            }
            p = list_next(espera->pacientes);
        }
    }

    // 2) Contamos todos los pacientes graves en el hospital
    int pacientes_graves = 0;
    Sala* s = list_first(salas_global);
    while (s != NULL) {
        Paciente* p = list_first(s->pacientes);
        while (p != NULL) {
            if (p->gravedad == 3) pacientes_graves++;
            p = list_next(s->pacientes);
        }
        s = list_next(salas_global);
    }

    // 3) Cabecera con turnos restantes y estadísticas clave
    printf("\n======= ESTADISTICAS (Dia %d) =======\n", dia_actual);
    printf("Turnos restantes: %d    Curados: %d    Fallecidos: %d    Reputacion: %d\n",
           turnos_restantes, pacientes_curados, pacientes_fallecidos, reputacion);

    // 4) Estado de pacientes graves
    printf("Pacientes Graves Totales: %d\n", pacientes_graves);

    // 5) Alerta si alguno está a punto de morir
    if (en_peligro > 0) {
        printf("ALERTA! %d paciente(s) de gravedad 3 en peligro (>=2 turnos de espera)\n", en_peligro);
    }

    printf("=====================================\n");
}

// ----------------------------------------------------
// Atender (curar) un paciente en una sala
// ----------------------------------------------------
void atender_paciente(List* salas) {
    // 1) Comprobar que queden turnos para atender
    if (turnos_restantes <= 0) {
        printf("No quedan turnos para atender.\n");
        return;
    }

    // 2) Mostrar salas con pacientes e insumos
    int contador_sal = 0;
    Sala* s = list_first(salas);
    printf("\nSalas con pacientes y al menos 1 insumo disponible:\n");
    while (s != NULL) {
        if (list_size(s->pacientes) > 0 && list_size(s->insumos) > 0) {
            contador_sal++;
            printf("%d) %s (Pacientes: %d, Insumos: %d)\n",
                   contador_sal,
                   s->nombre,
                   list_size(s->pacientes),
                   list_size(s->insumos));
        }
        s = list_next(salas);
    }
    if (contador_sal == 0) {
        printf("No hay ninguna sala con pacientes e insumos suficientes.\n");
        return;
    }

    // 3) Selección de sala
    printf("Seleccione el numero de sala para atender un paciente (0 para cancelar): ");
    int opcion_sala;
    scanf("%d", &opcion_sala);
    getchar();
    if (opcion_sala <= 0 || opcion_sala > contador_sal) {
        printf("Operacion cancelada o sala invalida.\n");
        return;
    }
    int indice_actual = 0;
    Sala* sala_elegida = NULL;
    s = list_first(salas);
    while (s != NULL) {
        if (list_size(s->pacientes) > 0 && list_size(s->insumos) > 0) {
            indice_actual++;
            if (indice_actual == opcion_sala) {
                sala_elegida = s;
                break;
            }
        }
        s = list_next(salas);
    }

    // 4) Listar pacientes en la sala elegida
    printf("\nPacientes en %s:\n", sala_elegida->nombre);
    int contador_pac = 0;
    Paciente* p = list_first(sala_elegida->pacientes);
    while (p != NULL) {
        contador_pac++;
        printf("%d) ID %d - %s %s - Gravedad %d - Turnos espera %d - Requiere insumo %d x%d\n",
               contador_pac,
               p->id, p->nombre, p->apellido,
               p->gravedad, p->turnos_espera,
               p->insumo_req_id, p->cantidad_req);
        p = list_next(sala_elegida->pacientes);
    }
    if (contador_pac == 0) {
        printf("No hay pacientes en esta sala.\n");
        return;
    }

    // 5) Seleccionar paciente
    printf("Seleccione el numero de paciente para curar (0 para cancelar): ");
    int opcion_pac;
    scanf("%d", &opcion_pac);
    getchar();
    if (opcion_pac <= 0 || opcion_pac > contador_pac) {
        printf("Operacion cancelada o paciente invalido.\n");
        return;
    }
    int indice2 = 0;
    p = list_first(sala_elegida->pacientes);
    while (p != NULL) {
        indice2++;
        if (indice2 == opcion_pac) break;
        p = list_next(sala_elegida->pacientes);
    }

    // 6) Verificar stock en bodega
    Sala* bodega = buscar_sala(salas, "Bodega Central");
    StockDiario* sd = NULL;
    if (bodega) {
        sd = list_first(bodega->stock_diario);
        while (sd && sd->id_insumo != p->insumo_req_id) {
            sd = list_next(bodega->stock_diario);
        }
    }
    if (!sd || sd->cantidad_total < p->cantidad_req) {
        printf("Stock insuficiente para el insumo ID %d. Operación cancelada.\n",
               p->insumo_req_id);
        return;
    }

    // 7) Consumir el stock
    sd->cantidad_total -= p->cantidad_req;

    // 8) Obtener nombre del insumo para el mensaje
    Insumo* ins_original = NULL;
    Insumo* tmp = list_first(bodega->insumos);
    while (tmp) {
        if (tmp->id == p->insumo_req_id) {
            ins_original = tmp;
            break;
        }
        tmp = list_next(bodega->insumos);
    }
    const char* nombre_ins = ins_original ? ins_original->nombre : "<insumo desconocido>";

    // 9) Mostrar resultado y remover paciente
    printf("\nSe han consumido %d unidades de '%s' para atender al paciente.\n",
           p->cantidad_req, nombre_ins);
    printf("Paciente ID %d (%s %s) ha sido curado y sale de la sala.\n",
           p->id, p->nombre, p->apellido);
    list_popCurrent(sala_elegida->pacientes);
    pacientes_curados++;
    reputacion++;

    // 10) Descontar un turno por la acción de curar
    turnos_restantes--;
}


// ----------------------------------------------------
// Mostrar estadísticas globales
// ----------------------------------------------------
void mostrar_estadisticas() {
    printf("\n=== ESTADISTICAS ===\n");
    printf("Pacientes curados:    %d\n", pacientes_curados);
    printf("Pacientes fallecidos: %d\n", pacientes_fallecidos);
    printf("Reputacion:           %d\n", reputacion);
}

// ----------------------------------------------------
// Función para pedir insumos a proveedor (sin límite diario)
// ----------------------------------------------------
void pedir_insumos_proveedor(List* salas) {
    // 1) Buscar Bodega Central
    Sala* bodega = buscar_sala(salas, "Bodega Central");
    if (!bodega) {
        printf("No se encontró la Bodega Central.\n");
        return;
    }

    // 2) Calcular cuota diaria según reputación
    int cuota = BASE_RETIRAR + reputacion * MULTIPLICADOR_REPUTACION;
    if (cuota < 0) cuota = 0;
    printf("\nMáximo a retirar hoy: %d dosis\n", cuota);

    // 3) Iterar sobre el mapa para mostrar los insumos disponibles
    printf("\nInsumos disponibles:\n");
    MapPair *par = map_first(mapa_stock);
    int idx = 1;
    while (par) {
        int id = *(int*)par->key;
        StockDiario *sd = par->value;
        Insumo *ins = buscar_insumo_por_id(id);
        printf("%2d) ID %d - %s (Stock: %d)\n",
               idx, id, ins->nombre, sd->cantidad_total);
        par = map_next(mapa_stock);
        idx++;
    }
    if (idx == 1) {
        printf("  (No hay insumos registrados en Bodega)\n");
        return;
    }

    // 4) Seleccionar insumo
    printf("Seleccione opción (0 para cancelar): ");
    int opcion;
    scanf("%d", &opcion);
    getchar();
    if (opcion <= 0 || opcion >= idx) {
        printf("Operación cancelada.\n");
        return;
    }

    // 5) Volver a iterar hasta la posición elegida
    par = map_first(mapa_stock);
    for (int i = 1; i < opcion; i++) {
        par = map_next(mapa_stock);
    }
    StockDiario *sel = par ? par->value : NULL;
    if (!sel) {
        printf("Error interno al seleccionar insumo.\n");
        return;
    }
    int id_sel = *(int*)par->key;

    // 6) Pedir cantidad a retirar
    printf("Unidades a retirar (máx %d): ", cuota);
    int cantidad;
    scanf("%d", &cantidad);
    getchar();

    // 7) Validar y descontar
    if (cantidad <= 0 || cantidad > cuota || cantidad > sel->cantidad_total) {
        printf("Cantidad inválida.\n");
    } else {
        sel->cantidad_total -= cantidad;
        sel->retirado_hoy   += cantidad;
        Insumo *ins = buscar_insumo_por_id(id_sel);
        printf("Se retiraron %d unidades de %s.\n", cantidad, ins->nombre);
    }
}


// ----------------------------------------------------
// Distribuir insumos de Bodega a salas (respetando cuota diaria)
// ----------------------------------------------------
void distribuir_insumos_a_salass(List* salas, int* limite_diario) {
    Sala* bodega = buscar_sala(salas, "Bodega Central");
    if (!bodega) {
        printf("No se encontro la Bodega Central.\n");
        return;
    }

    printf("\nInsumos en Bodega Central (Stock, RetiradoHoy), Quota restante hoy = %d\n", *limite_diario);
    int idx = 1;
    StockDiario* sd = list_first(bodega->stock_diario);
    while (sd != NULL) {
        if (sd->cantidad_total > 0) {
            Insumo* ins = NULL;
            Insumo* cand = list_first(bodega->insumos);
            while (cand != NULL) {
                if (cand->id == sd->id_insumo) {
                    ins = cand;
                    break;
                }
                cand = list_next(bodega->insumos);
            }
            if (ins) {
                printf("%d) %s (ID %d) – Stock=%d, RetiradoHoy=%d\n",
                       idx, ins->nombre, ins->id, sd->cantidad_total, sd->retirado_hoy);
            }
            idx++;
        }
        sd = list_next(bodega->stock_diario);
    }
    if (idx == 1) {
        printf(" No hay insumos en bodega.\n");
        return;
    }

    printf("Seleccione numero de insumo para distribuir (0 para cancelar): ");
    int opcion_ins;
    scanf("%d", &opcion_ins);
    getchar();
    if (opcion_ins <= 0 || opcion_ins >= idx) {
        printf("Solicitud cancelada.\n");
        return;
    }

    sd = list_first(bodega->stock_diario);
    for (int i = 1; i < opcion_ins; i++) {
        sd = list_next(bodega->stock_diario);
    }
    if (!sd || sd->cantidad_total <= 0) {
        printf("Error al seleccionar insumo o stock 0.\n");
        return;
    }

    printf("Ingrese cantidad a retirar (maximo %d, stock Bodega=%d): ",
           *limite_diario, sd->cantidad_total);
    int cantidad_retirar;
    scanf("%d", &cantidad_retirar);
    getchar();
    if (cantidad_retirar <= 0) {
        printf("Cantidad invalida.\n");
        return;
    }
    if (cantidad_retirar > *limite_diario) {
        printf("No puede retirar tanto hoy (excede cuota diaria = %d).\n", *limite_diario);
        return;
    }
    if (cantidad_retirar > sd->cantidad_total) {
        printf("No hay suficiente stock en bodega (solo quedan %d).\n", sd->cantidad_total);
        return;
    }

    printf("\nSeleccione sala destino para este insumo:\n");
    int contador_sal = 0;
    Sala* s = list_first(salas);
    while (s != NULL) {
        if (strcmp(s->nombre, "Sala de Espera") != 0 &&
            strcmp(s->nombre, "Bodega Central") != 0) {
            contador_sal++;
            printf("%d) %s (insumos: %d, cap: %d)\n",
                   contador_sal, s->nombre, list_size(s->insumos), s->capacidad_insumos);
        }
        s = list_next(salas);
    }
    if (contador_sal == 0) {
        printf("No hay salas destino disponibles.\n");
        return;
    }

    printf("Seleccione numero de sala destino (0 para cancelar): ");
    int opcion_sal_dest;
    scanf("%d", &opcion_sal_dest);
    getchar();
    if (opcion_sal_dest <= 0 || opcion_sal_dest > contador_sal) {
        printf("Operacion cancelada o sala invalida.\n");
        return;
    }

    int idx_sal = 0;
    Sala* sala_destino = NULL;
    s = list_first(salas);
    while (s != NULL) {
        if (strcmp(s->nombre, "Sala de Espera") != 0 &&
            strcmp(s->nombre, "Bodega Central") != 0) {
            idx_sal++;
            if (idx_sal == opcion_sal_dest) {
                sala_destino = s;
                break;
            }
        }
        s = list_next(salas);
    }
    if (!sala_destino) {
        printf("Error al encontrar sala destino.\n");
        return;
    }

    sd->cantidad_total  -= cantidad_retirar;
    sd->retirado_hoy    += cantidad_retirar;
    *limite_diario      -= cantidad_retirar;

    Insumo* ins_dest = NULL;
    Insumo* cand2 = list_first(sala_destino->insumos);
    while (cand2 != NULL) {
        if (cand2->id == sd->id_insumo) {
            ins_dest = cand2;
            break;
        }
        cand2 = list_next(sala_destino->insumos);
    }
    printf("Se han retirado %d unidades de '%s' para %s.\n",
           cantidad_retirar,
           ((cand2) ? cand2->nombre : "<insumo desconocido>"),
           sala_destino->nombre);
}

// ----------------------------------------------------
// Submenú para gestionar Bodega (Pedir o Distribuir)
// ----------------------------------------------------
void gestionar_bodega(List* salas, int* limite_diario) {
    int opcion_bodega;
    do {
        printf("\n--- Gestionar Bodega Central ---\n");
        printf("1. Pedir insumos a proveedor\n");
        printf("2. Distribuir insumos de Bodega a salas (quota diaria: %d)\n", *limite_diario);
        printf("3. Volver al menu anterior\n");
        printf("Seleccione una opcion: ");
        scanf("%d", &opcion_bodega);
        getchar();

        switch (opcion_bodega) {
            case 1:
                pedir_insumos_proveedor(salas);
                break;
            case 2:
                distribuir_insumos_a_salass(salas, limite_diario);
                break;
            case 3:
                printf("Volviendo al menu de Acciones.\n");
                break;
            default:
                printf("Opcion invalida.\n");
        }
    } while (opcion_bodega != 3);
}

// ----------------------------------------------------
// Procesos de fin de día: muertes, reinicio cuota, reposición
// ----------------------------------------------------
void ejecutar_procesos_fin_dia() {
    Sala* espera = buscar_sala(salas_global, "Sala de Espera");
    if (espera) {
        int n = list_size(espera->pacientes);
        for (int idx = 0; idx < n; idx++) {
            Paciente* p = list_first(espera->pacientes);
            if (!p) break;

            p->turnos_espera++;
            if ((p->gravedad == 3 && p->turnos_espera > 2) ||
                (p->gravedad == 2 && p->turnos_espera > 3)) {
                pacientes_fallecidos++;
                reputacion -= 2;
                printf("Paciente #%d murio en Sala de Espera (gravedad %d, turnos %d).\n",
                       p->id, p->gravedad, p->turnos_espera);
                list_popCurrent(espera->pacientes);
                continue;
            }

            if (p->gravedad == 3) {
                Sala* urgencias = buscar_sala(salas_global, "Urgencias");
                Sala* uci = buscar_sala(salas_global, "UCI");
                if (urgencias && list_size(urgencias->pacientes) < urgencias->capacidad_pacientes) {
                    list_pushBack(urgencias->pacientes, p);
                    list_popCurrent(espera->pacientes);
                    printf("Paciente #%d (gravedad 3) transferido automaticamente a Urgencias.\n", p->id);
                    continue;
                } else if (uci && list_size(uci->pacientes) < uci->capacidad_pacientes) {
                    list_pushBack(uci->pacientes, p);
                    list_popCurrent(espera->pacientes);
                    printf("Paciente #%d (gravedad 3) transferido automaticamente a UCI.\n", p->id);
                    continue;
                }
            }

            list_pushBack(espera->pacientes, p);
            list_popCurrent(espera->pacientes);
        }
    }

    /* --- Sólo reinicio de cuota diaria en Bodega --- */
    Sala* bodega = buscar_sala(salas_global, "Bodega Central");
    if (bodega) {
        StockDiario* sd = list_first(bodega->stock_diario);
        while (sd != NULL) {
            sd->retirado_hoy = 0;
            sd = list_next(bodega->stock_diario);
        }
        printf("Fin del dia: cuota diaria de retiro de insumos restablecida.\n");
    }

    printf("\n--- Resumen Dia %d ---\n", dia_actual);
    printf("Curados hoy: %d  |  Fallecidos hoy: %d  |  Reputacion actual: %d\n\n",
           pacientes_curados, pacientes_fallecidos, reputacion);
}


// ----------------------------------------------------
// Función principal del ciclo diario
// ----------------------------------------------------
// Función ciclo_diario modificada para usar turnos, admisión diaria y límite de insumos
void ciclo_diario() {
    dia_actual++;
    printf("\n----- Comenzando Dia %d -----\n", dia_actual);

    // 1) Admitir hasta 5 pacientes nuevos cada día
    admitir_pacientes_dia(5);

    // 2) Inicializar turnos disponibles según reputación
    turnos_restantes = reputacion + 3;

    // 3) Calcular límite diario de retiro de insumos
    int limite_ins = BASE_RETIRAR + reputacion * MULTIPLICADOR_REPUTACION;
    if (limite_ins < 0) limite_ins = 0;

    int opt;
    do {
        mostrar_encabezado();  // mostrará turnos_restantes en lugar de solo estadísticas

        printf("\nMenu de Acciones - Dia %d\n", dia_actual);
        printf("1. Mostrar salas\n");
        printf("2. Transferir paciente (turnos restantes: %d)\n", turnos_restantes);
        printf("3. Atender paciente   (turnos restantes: %d)\n", turnos_restantes);
        printf("4. Pedir insumos (máx hoy: %d unidades)\n", limite_ins);
        printf("5. Estadísticas\n");
        printf("6. Finalizar Turno\n");
        printf("Seleccione una opcion: ");
        scanf("%d", &opt);
        getchar();

        switch (opt) {
            case 1:
                limpiarPantalla();
                mostrar_salas(salas_global);
                presioneTeclaParaContinuar();
                break;
            case 2:
                limpiarPantalla();
                transferir_pacientes_menu(salas_global);
                presioneTeclaParaContinuar();
                break;
            case 3:
                limpiarPantalla();
                atender_paciente(salas_global);
                presioneTeclaParaContinuar();
                break;
            case 4:
                limpiarPantalla();
                gestionar_bodega(salas_global, &limite_ins);
                presioneTeclaParaContinuar();
                break;
            case 5:
                limpiarPantalla();
                mostrar_estadisticas();
                presioneTeclaParaContinuar();
                break;
            case 6:
                // salir del bucle
                break;
            default:
                printf("Opcion invalida.\n");
        }
    } while (opt != 6 && turnos_restantes > 0);

    // Al finalizar el turno, ejecutamos muertes automáticas y reinicio de cuotas
    ejecutar_procesos_fin_dia();
}
// ----------------------------------------------------
// Función main
// ----------------------------------------------------
int main() {
    // Inicializar salas
    mapa_stock = map_create(int_eq);
    cola_pacientes = queue_create(NULL);
    salas_global = inicializar_salas();

    // Cargar datos iniciales
    FILE* archivo_pac = fopen("pacientes.csv", "r");
    FILE* archivo_ins = fopen("insumos.csv", "r");
    if (!archivo_pac || !archivo_ins) {
        perror("Error al abrir pacientes o insumos");
        if (archivo_pac) fclose(archivo_pac);
        if (archivo_ins) fclose(archivo_ins);
        return 1;
    }
    List* pacientes = leer_pacientes(archivo_pac);
    Paciente* p = list_first(pacientes);
    while (p) {
        queue_insert(cola_pacientes, p);
        p = list_next(pacientes);
    }
    List* insumos = leer_insumos(archivo_ins);
    fclose(archivo_pac);
    fclose(archivo_ins);

    
    asignar_insumos_a_salas(insumos, salas_global, mapa_stock);

    printf("Datos cargados correctamente.\n");

    int opcion_principal;
    do {
        printf("\n=== GESTOR DE INSUMOS HOSPITALARIOS ===\n");
        printf("1. Comenzar Dia\n");
        printf("0. Salir\n");
        printf("Seleccione una opcion: ");
        scanf("%d", &opcion_principal);
        getchar();

        switch (opcion_principal) {
            case 1:
                ciclo_diario();
                break;
            case 0:
                printf("Saliendo del programa...\n");
                break;
            default:
                printf("Opcion invalida.\n");
        }
    } while (opcion_principal != 0);

    return 0;
}

// CAMBIAR EL SISTEMA DE INGRESO POR DIA, QUITAR LA CREACION DE PACIENTES RANDOM Y HACER EL CSV DE PACIENTES MAS GRANDE.