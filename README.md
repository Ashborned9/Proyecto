# Proyecto: Gestor de insumos Hospitalarios

Este proyecto corresponde a una simulación de gestión hospitalaria en lenguaje C, donde se administra el flujo de los insumos medicos entre distintas salas de un hspital. Este proyecto responde a la necesidad del Hospital Gustavo Fricke de Viña del Mar de una necesidad de optimizar el flujo de información entre salas de trabajo e insumos medicos.

## Estructura del Proyecto:

- **gestion_hospital.c**: Archivo principal con la logica de negocio del programa.
- **pacientes.csv**: Archivo con los datos inciales de los pacientes.
- **insumos.csv**: Archivo con los datos iniciales de los insumos.
- **tdas/**: Carpeta con implementaciones de estructuras de datos auxiliares utilizadas (listas, mapas, colas, pilas, heap, etc.).

## Compilacion:

Para compilar el proyecto, usa un compilador de C como gcc. Por ejemplo:

```sh
gcc gestion_hospital.c tdas/list.c tdas/map.c tdas/extra.c tdas/heap.c -o gestor_hospital
```

## Ejecucion

Primero asegurarse de tener los archivos "pacientes.csv" e "insumos.csv" en el mismo directorio que el ejecutable. Luego ejecuta:

```sh
./gestor_hospital
```

## Funcionalidades principales:

- **Carga de datos**: Lee paciente e insumos desde archivos CSV.
- **Gestion de salas**:Permite transferir insumos entre salas y gestionar la capacidad de cada una.
- **Atencion de pacientes**: Simula la atencion y curacion de pacientes, consumiendo insumos.
- **Gestion de bodega**: Permite pedir insumos al proveedor y distribuirlos a las salas, respetando un limite diario.
- **Estadistica**: Muestra estadisticas de pacientes, fallecidos y reputacion del hospital.
- **Turnos y eventos diarios**:Simula el paso de los dias, con llegada de nuevos pacientes y eventos automaticos.

## Estructuras de datos utilizadas:
- **Listas**: Para mejorar colecciones de pacientes, insumos y salas.
- **Mapas y Sets**: Para busquedas rapidas y gestion de elementos unicos.
- **Heap, Queue, Stack**: Estructuras auxiliares para futuras extensiones.

## Equipo de desarrollo:

Desarrollado como proyecto academico para la asignatura de Estructura de Dato 2025 S1.

Nicolás Fuentes
Felipe Aguilera
Pablo Saldivia
Lucas Manriquez
---