# SMART HIVE: Sistema Edge AI para la Monitorización y Detección de *Vespa velutina*

🎓 **Trabajo de Fin de Máster (TFM)** <br>
*Máster Universitario en Nuevas Tecnologías Electrónicas y Fotónicas* <br>
**Universidad Complutense de Madrid (UCM)**

<br>

![C++](https://img.shields.io/badge/C%2B%2B-00599C?style=for-the-badge&logo=c%2B%2B&logoColor=white)
![Python](https://img.shields.io/badge/Python-3776AB?style=for-the-badge&logo=python&logoColor=white)
![Raspberry Pi](https://img.shields.io/badge/Raspberry%20Pi-A22846?style=for-the-badge&logo=Raspberry%20Pi&logoColor=white)
![ESP32](https://img.shields.io/badge/ESP32-E7352C?style=for-the-badge&logo=espressif&logoColor=white)
![PyTorch](https://img.shields.io/badge/PyTorch-EE4C2C?style=for-the-badge&logo=pytorch&logoColor=white)

<br>
</div>

---

Repositorio oficial del Trabajo de Fin de Máster (TFM) centrado en el desarrollo de un nodo IoT de misión crítica para la detección en tiempo real de la avispa asiática (*Vespa velutina*) en entornos apícolas. 

El sistema integra visión artificial acelerada por \textit{hardware} (NPU Hailo-8), procesamiento multihilo asíncrono en C++, y telemetría de largo alcance mediante radiofrecuencia (LoRa), operando de forma autónoma bajo severas restricciones energéticas y computacionales.

---

## Estructura del Repositorio

La arquitectura del \textit{software} está dividida en cuatro bloques principales que abarcan todo el ciclo de vida del proyecto: desde el entrenamiento del modelo de IA hasta su despliegue físico en la colmena y la recepción de telemetría.


SMART_HIVE/
├── hailo_optimizer/    # Pipeline de cuantización, optimización y profiling (HDC)
├── LoRa_Receiver/      # Gateway ESP32: Recepción de telemetría y Dashboard web
├── smart_hive/         # Core de Producción: Aplicación C++ para la Raspberry Pi 5
└── yolo_training/      # Entorno de Machine Learning: Preparación y entrenamiento YOLO


## Descripción Detallada de los Módulos

### 1. `smart_hive/` (Nodo Edge IoT)
Este directorio contiene la aplicación empotrada final, diseñada para ejecutarse en la Raspberry Pi 5. Desarrollada íntegramente en C++, implementa una arquitectura multihilo (Productor-Consumidor) de altísima eficiencia.

* **`src/` & `include/`**: Código fuente y cabeceras. Gestiona la captura asíncrona de la cámara, la inferencia en la NPU Hailo, el enrutamiento selectivo mediante Filtro de Kalman y la transmisión de *payloads* LoRa.
* **`models/`**: Binarios ejecutables (`.hef`) listos para inferencia.
* **`CMakeLists.txt`**: Archivo de configuración para la compilación del proyecto vinculando librerías nativas (HailoRT, OpenCV, libcamera).

### 2. `LoRa_Receiver/` (Gateway y Dashboard)
Código fuente para el microcontrolador ESP32 que actúa como puente de enlace entre la colmena y el apicultor.

* **`LoRa_Receiver.ino`**: Bucle principal (*Non-Blocking Polling*) que intercepta las tramas de radiofrecuencia enviadas por el nodo perimetral.
* **`dashboard.h`**: Implementación de un servidor web asíncrono embebido. Contiene el HTML, CSS y JS necesarios para renderizar un *Dashboard* interactivo que expone las alertas de avispas y las métricas ambientales en tiempo real.

### 3. `hailo_optimizer/` (Compilación de Hardware y Benchmarking)
Entorno dedicado a la traducción de los modelos matemáticos al silicio del coprocesador Hailo-8 utilizando el *Hailo Dataflow Compiler* (HDC) en una máquina Linux.

* **`notebooks_HDC/`**: Cuadernos Jupyter para el *pipeline* oficial de Hailo:
  * `parsing.ipynb`: Traducción del formato ONNX al formato intermedio HAR.
  * `optimization.ipynb`: Calibración y cuantización a INT8 (*Post-Training Quantization*).
  * `compilation.ipynb`: Compilación final y asignación de recursos físicos.
* **`notebooks_general/`**: Automatización de pruebas e instrumentación:
  * `experiments.ipynb`: Orquestación y automatización de *benchmarks* de validación física (FPS, Latencia, Consumo).
  * `profiler.ipynb`: Automatización para la generación de reportes topográficos y cuellos de botella.
* **`models/`**: Repositorio de modelos divididos por su estado de desarrollo (`original`, `intermediate` y `binaries`).
* **`experiments/` & `data_out/`**: Archivos CSV con los resultados de las métricas (`master_results.csv`, `master_yolo_results.csv`) y volcados del *profiler*.

### 4. `yolo_training/` (Deep Learning y Entrenamiento)
Entorno de *Machine Learning* en Python utilizado para el entrenamiento teórico de las redes neuronales en servidores GPU.

* **`notebooks/`**:
  * `data_processing.ipynb`: Limpieza, aumento (*Data Augmentation*) y formateo del conjunto de datos.
  * `training.ipynb`: Rutinas de entrenamiento de la familia YOLO.
  * `testing.ipynb`: Evaluación de métricas de rendimiento (mAP, Precisión, Recall) tras el entrenamiento.
  * `compare.ipynb`: Análisis comparativo entre las distintas arquitecturas (YOLOv8n/s vs YOLOv11n/s) para seleccionar la topología óptima.

  ## Tecnologías y Hardware Utilizado

* **Hardware Edge:** Raspberry Pi 5 (8GB), Hailo-8 AI HAT+ (26 TOPS), Cámara RPi Module 3.
* **Comunicaciones:** Módulos LoRa SX1262/SX1261 (868 MHz), ESP32 Dev Module.
* **Sensores:** BME280 (Temp/Hum), PZEM-004T (Auditoría de Potencia).
* **Software Core:** C++17, Python 3, PyTorch, Ultralytics YOLO, HailoRT, OpenCV.

---

## Autor y Créditos

Este repositorio contiene el código fuente, la investigación y los diseños de \textit{hardware} desarrollados para el **Trabajo de Fin de Máster (TFM)** presentado en el curso académico correspondiente.

* 🏛️ **Institución:** [Universidad Complutense de Madrid (UCM)](https://www.ucm.es/)
* 🎓 **Titulación:** Máster Universitario en Nuevas Tecnologías Electrónicas y Fotónicas
* 👤 **Autor:** Jaime Nogueira Fuentes
* 🧑‍🏫 **Tutores:** Guillermo Botella y Sandra Catalán

<div align="center">
  <b>© 2026 Smart Hive Project</b>
</div>