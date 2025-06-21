#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "tdas/extra.h"
#include "tdas/list.h"

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
    char tipo[50];
    int cantidad;
    char unidad[30];
    char fecha_vencimiento[20]; // puede estar vacío
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
void asignar_insumos_a_salas(List* insumos, List* salas);

// Mostrar estado
void mostrar_salas(List* salas);

// Transferencia manual
void transferir_pacientes_menu(List* salas);
void transferir_paciente_unico(List* salas, Paciente* p);

// Sistema de turnos y muertes
void ejecutar_turno(List* salas);
void ejecutar_procesos_fin_dia();

// Generar pacientes aleatorios
void generar_pacientes_nuevos();

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
            perror("Error malloc");
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
    List* lista_insumos = list_create();
    char linea[300];

    fgets(linea, sizeof(linea), archivo); // Saltar encabezado

    while (fgets(linea, sizeof(linea), archivo)) {
        Insumo* ins = (Insumo*) malloc(sizeof(Insumo));
        if (ins == NULL) continue;

        sscanf(linea, "%d,%99[^,],%49[^,],%d,%29[^,],%19[^,],%49[^\n]",
               &ins->id, ins->nombre, ins->tipo, &ins->cantidad,
               ins->unidad, ins->fecha_vencimiento, ins->ubicacion);

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
void asignar_pacientes_a_espera(List* pacientes, List* salas) {
    Sala* espera = buscar_sala(salas, "Sala de Espera");
    if (!espera) return;

    Paciente* p = list_first(pacientes);
    while (p != NULL) {
        list_pushBack(espera->pacientes, p);
        p = list_next(pacientes);
    }
}

// ----------------------------------------------------
// Asignar insumos iniciales a las salas (incluida Bodega)
// ----------------------------------------------------
void asignar_insumos_a_salas(List* insumos, List* salas) {
    Insumo* i = list_first(insumos);
    while (i != NULL) {
        Sala* sala = buscar_sala(salas, i->ubicacion);
        if (sala == NULL) {
            printf("Sala '%s' no existe. No se asigno insumo ID %d (%s).\n", i->ubicacion, i->id, i->nombre);
        } else {
            // Si es Bodega, además creamos StockDiario
            if (strcmp(sala->nombre, "Bodega Central") == 0) {
                // Insertar Insumo en Bodega
                list_pushBack(sala->insumos, i);
                // Crear StockDiario
                StockDiario* sd = malloc(sizeof(StockDiario));
                sd->id_insumo = i->id;
                sd->cantidad_total = i->cantidad;
                sd->retirado_hoy = 0;
                list_pushBack(sala->stock_diario, sd);
            } else {
                // Insertar Insumo en sala clínica
                list_pushBack(sala->insumos, i);
            }
        }
        i = list_next(insumos);
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
            printf("  Insumo: %s (ID %d, %d %s) Vence: %s\n",
                   i->nombre, i->id, i->cantidad, i->unidad,
                   i->fecha_vencimiento[0] ? i->fecha_vencimiento : "N/A");
            i = list_next(s->insumos);
        }

        s = list_next(salas);
    }
}

// ----------------------------------------------------
// Transferencia manual de varios pacientes (max 5 por turno)
// ----------------------------------------------------
void transferir_pacientes_menu(List* salas) {
    Sala* espera = buscar_sala(salas, "Sala de Espera");
    if (!espera || list_size(espera->pacientes) == 0) {
        printf("No hay pacientes en Sala de Espera.\n");
        return;
    }

    int acciones_restantes = 5;
    int pagina = 1;
    int total = list_size(espera->pacientes);
    int por_pagina = 10;
    int max_paginas = (total + por_pagina - 1) / por_pagina;
    int opcion;
    while (acciones_restantes > 0 && total > 0) {
        printf("\nPacientes en Sala de Espera (pag %d/%d). Acciones restantes: %d\n",
               pagina, max_paginas, acciones_restantes);

        // Mostrar la página actual
        int inicio = (pagina - 1) * por_pagina;
        for (int i = 0; i < inicio; i++) {
            list_first(espera->pacientes);
            for (int j = 0; j < i; j++) list_next(espera->pacientes);
        }
        Paciente* p = NULL;
        for (int i = 0; i < por_pagina && inicio + i < total; i++) {
            if (i == 0) p = list_first(espera->pacientes);
            else p = list_next(espera->pacientes);
            printf("%2d) ID %d - %s %s - Gravedad %d - Turnos %d\n",
                   i + 1, p->id, p->nombre, p->apellido, p->gravedad, p->turnos_espera);
        }

        printf("\n[1-%d] Transferir paciente  [P]ag sig  [A]nterior  [0] Salir: ", por_pagina);
        char input[10];
        fgets(input, sizeof(input), stdin);

        if (input[0] == 'P' || input[0] == 'p') {
            if (pagina < max_paginas) pagina++;
            else printf("Ya estas en la ultima pagina.\n");
            continue;
        }
        if (input[0] == 'A' || input[0] == 'a') {
            if (pagina > 1) pagina--;
            else printf("Ya estas en la primera pagina.\n");
            continue;
        }

        opcion = atoi(input);
        if (opcion == 0) break;
        if (opcion < 1 || opcion > por_pagina || inicio + opcion > total) {
            printf("Opcion invalida.\n");
            continue;
        }

        // Identificar al paciente
        p = list_first(espera->pacientes);
        for (int k = 0; k < inicio + (opcion - 1); k++) {
            p = list_next(espera->pacientes);
        }

        // Transferir paciente individual
        transferir_paciente_unico(salas, p);
        acciones_restantes--;
        total--;
        max_paginas = (total + por_pagina - 1) / por_pagina;
        if (pagina > max_paginas) pagina = max_paginas;
    }

    if (acciones_restantes == 0) {
        printf("Has agotado tus acciones de transferencia para este turno.\n");
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

void limpiarPantalla() { system("clear"); }
void presioneTeclaParaContinuar() {
  puts("Presione una tecla para continuar...");
  getchar(); // Consume el '\n' del buffer de entrada
  getchar(); // Espera a que el usuario presione una tecla
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
// Generar pacientes nuevos cada día (aleatorio 3–8)
// ----------------------------------------------------
void generar_pacientes_nuevos() {
    int n = rand() % 6 + 3;  // entre 3 y 8
    printf("Llegan %d pacientes nuevos al hospital.\n", n);

    Sala* espera = buscar_sala(salas_global, "Sala de Espera");
    if (!espera) return;

    const char* areas[] = {
        "UCI", "Urgencias", "Ginecologia",
        "Traumatologia", "Medicina Interna", "Pediatria"
    };

    for (int i = 0; i < n; i++) {
        Paciente* p = malloc(sizeof(Paciente));
        if (!p) continue;

        p->id = siguiente_id_paciente++;
        int prob = rand() % 100;
        if (prob < 20) p->gravedad = 3;
        else if (prob < 50) p->gravedad = 2;
        else p->gravedad = 1;

        strcpy(p->area, areas[rand() % 6]);
        strcpy(p->diagnostico, "Condicion aleatoria");
        p->edad = rand() % 90 + 1;
        p->turnos_espera = 0;

        if (p->gravedad == 3) {
            p->insumo_req_id = 1005;
            p->cantidad_req = 3;
        } else if (p->gravedad == 2) {
            p->insumo_req_id = 1001;
            p->cantidad_req = 2;
        } else {
            p->insumo_req_id = 1003;
            p->cantidad_req = 1;
        }

        list_pushBack(espera->pacientes, p);
    }
}

// ----------------------------------------------------
// Mostrar estadísticas y alertas antes de cada acción
// ----------------------------------------------------
void mostrar_encabezado() {
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

    printf("\n======= ESTADISTICAS (Dia %d) =======\n", dia_actual);
    printf("Curados:   %d    Fallecidos: %d    Reputacion: %d    Pacientes Graves: %d\n",
           pacientes_curados, pacientes_fallecidos, reputacion, pacientes_graves);

    if (en_peligro > 0) {
        printf("ALERTA! %d paciente(s) de gravedad 3 en peligro (1 turno)\n", en_peligro);
    }
    printf("=====================================\n");
}

// ----------------------------------------------------
// Atender (curar) un paciente en una sala
// ----------------------------------------------------
void atender_paciente(List* salas) {
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
    if (!sala_elegida) {
        printf("Error al encontrar sala seleccionada.\n");
        return;
    }

    printf("\nPacientes en %s:\n", sala_elegida->nombre);
    int contador_pac = 0;
    Paciente* p = list_first(sala_elegida->pacientes);
    while (p != NULL) {
        contador_pac++;
        printf("%d) ID %d - %s %s - Gravedad %d - Turnos espera %d - Requiere insumo %d x%d\n",
               contador_pac,
               p->id, p->nombre, p->apellido, p->gravedad, p->turnos_espera,
               p->insumo_req_id, p->cantidad_req);
        p = list_next(sala_elegida->pacientes);
    }

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
    if (!p) {
        printf("Error al encontrar paciente.\n");
        return;
    }

    Insumo* ins_req = NULL;
    Insumo* cand = list_first(sala_elegida->insumos);
    while (cand != NULL) {
        if (cand->id == p->insumo_req_id) {
            ins_req = cand;
            break;
        }
        cand = list_next(sala_elegida->insumos);
    }
    if (!ins_req) {
        printf("No hay el insumo requerido (ID %d) en esta sala. No se puede atender.\n", p->insumo_req_id);
        return;
    }

    if (ins_req->cantidad < p->cantidad_req) {
        printf("Insuficiente stock de '%s' (requiere %d, disponible %d). No se puede atender.\n",
               ins_req->nombre, p->cantidad_req, ins_req->cantidad);
        return;
    }

    ins_req->cantidad -= p->cantidad_req;
    printf("\nSe han consumido %d unidades de '%s' para atender al paciente.\n",
           p->cantidad_req, ins_req->nombre);

    if (ins_req->cantidad == 0) {
        list_popCurrent(sala_elegida->insumos);
        printf("El insumo '%s' se agotó y fue eliminado de la sala.\n", ins_req->nombre);
    }

    printf("Paciente ID %d (%s %s) ha sido curado y sale de la sala.\n",
           p->id, p->nombre, p->apellido);
    list_popCurrent(sala_elegida->pacientes);
    pacientes_curados++;
    reputacion++;
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
    Sala* bodega = buscar_sala(salas, "Bodega Central");
    if (!bodega) {
        printf("No se encontro la Bodega Central.\n");
        return;
    }

    printf("\nInsumos actuales en Bodega Central:\n");
    int idx = 1;
    StockDiario* sd = list_first(bodega->stock_diario);
    while (sd != NULL) {
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
            printf("%d) ID %d – %s – Stock actual: %d unidades\n",
                   idx, ins->id, ins->nombre, sd->cantidad_total);
        }
        sd = list_next(bodega->stock_diario);
        idx++;
    }
    if (idx == 1) {
        printf("  (No hay insumos registrados en Bodega hasta ahora)\n");
    }

    printf("\nSeleccione el numero de insumo a reabastecer (0 para crear uno nuevo): ");
    int opcion_ins;
    scanf("%d", &opcion_ins);
    getchar();

    if (opcion_ins > 0 && opcion_ins < idx) {
        sd = list_first(bodega->stock_diario);
        for (int i = 1; i < opcion_ins; i++) {
            sd = list_next(bodega->stock_diario);
        }
        if (!sd) {
            printf("Error interno al seleccionar insumo.\n");
            return;
        }
        Insumo* ins_sel = NULL;
        Insumo* cand2 = list_first(bodega->insumos);
        while (cand2 != NULL) {
            if (cand2->id == sd->id_insumo) {
                ins_sel = cand2;
                break;
            }
            cand2 = list_next(bodega->insumos);
        }
        if (!ins_sel) {
            printf("Error: insumo no encontrado en lista.\n");
            return;
        }
        printf("Ingrese unidades a reabastecer de '%s': ", ins_sel->nombre);
        int cant_add;
        scanf("%d", &cant_add);
        getchar();
        if (cant_add <= 0) {
            printf("Cantidad invalida. Operacion cancelada.\n");
            return;
        }
        sd->cantidad_total += cant_add;
        ins_sel->cantidad = sd->cantidad_total;
        printf("Se agregaron %d unidades a '%s'. Nuevo stock en Bodega: %d\n",
               cant_add, ins_sel->nombre, sd->cantidad_total);
    }
    else if (opcion_ins == 0) {
        Insumo* nuevoIns = malloc(sizeof(Insumo));
        if (!nuevoIns) {
            printf("Error de memoria.\n");
            return;
        }
        printf("Ingrese ID numerico para el nuevo insumo: ");
        scanf("%d", &nuevoIns->id);
        getchar();
        printf("Ingrese nombre del insumo: ");
        fgets(nuevoIns->nombre, sizeof(nuevoIns->nombre), stdin);
        nuevoIns->nombre[strcspn(nuevoIns->nombre, "\n")] = '\0';
        printf("Ingrese tipo de insumo (e.g. Medicamento, Instrumental): ");
        fgets(nuevoIns->tipo, sizeof(nuevoIns->tipo), stdin);
        nuevoIns->tipo[strcspn(nuevoIns->tipo, "\n")] = '\0';
        printf("Ingrese unidades a stockear: ");
        scanf("%d", &nuevoIns->cantidad);
        getchar();
        printf("Ingrese unidad (e.g. unidades, cajas): ");
        fgets(nuevoIns->unidad, sizeof(nuevoIns->unidad), stdin);
        nuevoIns->unidad[strcspn(nuevoIns->unidad, "\n")] = '\0';
        printf("Ingrese fecha de vencimiento (YYYY-MM-DD) o '-' si no aplica: ");
        fgets(nuevoIns->fecha_vencimiento, sizeof(nuevoIns->fecha_vencimiento), stdin);
        nuevoIns->fecha_vencimiento[strcspn(nuevoIns->fecha_vencimiento, "\n")] = '\0';
        strcpy(nuevoIns->ubicacion, "Bodega Central");

        StockDiario* sd_n = malloc(sizeof(StockDiario));
        sd_n->id_insumo = nuevoIns->id;
        sd_n->cantidad_total = nuevoIns->cantidad;
        sd_n->retirado_hoy = 0;
        list_pushBack(bodega->stock_diario, sd_n);

        list_pushBack(bodega->insumos, nuevoIns);

        printf("Nuevo insumo '%s' (ID %d) agregado con %d unidades en Bodega Central.\n",
               nuevoIns->nombre, nuevoIns->id, nuevoIns->cantidad);
    }
    else {
        printf("Opcion invalida. Operacion cancelada.\n");
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
    if (ins_dest) {
        ins_dest->cantidad += cantidad_retirar;
    } else {
        Insumo* nuevoIns = malloc(sizeof(Insumo));
        Insumo* orig = list_first(bodega->insumos);
        while (orig != NULL) {
            if (orig->id == sd->id_insumo) {
                nuevoIns->id = orig->id;
                strcpy(nuevoIns->nombre, orig->nombre);
                strcpy(nuevoIns->tipo, orig->tipo);
                nuevoIns->cantidad = cantidad_retirar;
                strcpy(nuevoIns->unidad, orig->unidad);
                strcpy(nuevoIns->fecha_vencimiento, orig->fecha_vencimiento);
                strcpy(nuevoIns->ubicacion, sala_destino->nombre);
                break;
            }
            orig = list_next(bodega->insumos);
        }
        list_pushBack(sala_destino->insumos, nuevoIns);
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
        printf("  a) 1. Pedir insumos a proveedor\n");
        printf("  b) 2. Distribuir insumos de Bodega a salas (quota diaria: %d)\n", *limite_diario);
        printf("  c) 0. Volver al menu anterior\n");
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
            case 0:
                printf("Volviendo al menu de Acciones.\n");
                break;
            default:
                printf("Opcion invalida.\n");
        }
    } while (opcion_bodega != 0);
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

    Sala* bodega = buscar_sala(salas_global, "Bodega Central");
    if (bodega) {
        StockDiario* sd = list_first(bodega->stock_diario);
        while (sd != NULL) {
            sd->retirado_hoy = 0;
            sd = list_next(bodega->stock_diario);
        }
        printf("Fin del dia: cuota diaria de retiro de insumos restablecida.\n");
    }

    if (bodega) {
        StockDiario* sd2 = list_first(bodega->stock_diario);
        while (sd2 != NULL) {
            int tope_max = 200;
            int reposicion = 10;
            if (sd2->cantidad_total + reposicion > tope_max) {
                reposicion = tope_max - sd2->cantidad_total;
            }
            if (reposicion > 0) {
                sd2->cantidad_total += reposicion;
                Insumo* ins_ori = list_first(bodega->insumos);
                while (ins_ori != NULL) {
                    if (ins_ori->id == sd2->id_insumo) {
                        ins_ori->cantidad = sd2->cantidad_total;
                        break;
                    }
                    ins_ori = list_next(bodega->insumos);
                }
            }
            sd2 = list_next(bodega->stock_diario);
        }
        printf("Reabastecimiento parcial: cada insumo en bodega +10 unidades (hasta tope).\n");
    }

    printf("\n--- Resumen Dia %d ---\n", dia_actual);
    printf("Curados hoy: %d  |  Fallecidos hoy: %d  |  Reputacion actual: %d\n\n",
           pacientes_curados, pacientes_fallecidos, reputacion);
}

// ----------------------------------------------------
// Función principal del ciclo diario
// ----------------------------------------------------
void ciclo_diario() {
    dia_actual++;
    printf("\n----- Comenzando Dia %d -----\n", dia_actual);

    generar_pacientes_nuevos();

    int limite_retirar_diario = BASE_RETIRAR + reputacion * MULTIPLICADOR_REPUTACION;
    if (limite_retirar_diario < 0) limite_retirar_diario = 0;

    int opcion_dia;
    do {
        mostrar_encabezado();

        printf("\nMenu de Acciones - Dia %d (Limite diario de retiro: %d unidades)\n",
               dia_actual, limite_retirar_diario);
        printf("1. Mostrar estado de las salas\n");
        printf("2. Transferir paciente desde Sala de Espera (max 5 acciones)\n");
        printf("3. Atender (curar) un paciente\n");
        printf("4. Gestionar Bodega (Pedir o Distribuir insumos)\n");
        printf("5. Mostrar estadisticas (curados, fallecidos, reputacion)\n");
        printf("6. Finalizar Turno\n");
        printf("Seleccione una opcion: ");
        scanf("%d", &opcion_dia);
        getchar();

        switch (opcion_dia) {
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
                gestionar_bodega(salas_global, &limite_retirar_diario);
                presioneTeclaParaContinuar();
                break;
            case 5:
                limpiarPantalla();
                mostrar_estadisticas();
                presioneTeclaParaContinuar();
                break;
            case 6:
                break;
            default:
                printf("Opcion invalida.\n");
        }
    } while (opcion_dia != 6);

    ejecutar_procesos_fin_dia();
}

// ----------------------------------------------------
// Función main
// ----------------------------------------------------
int main() {
    srand(time(NULL));

    // Inicializar salas
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
    List* insumos = leer_insumos(archivo_ins);
    fclose(archivo_pac);
    fclose(archivo_ins);

    asignar_pacientes_a_espera(pacientes, salas_global);
    asignar_insumos_a_salas(insumos, salas_global);

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