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