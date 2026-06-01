/**
 * @file dashboard.h
 * @brief Interfaz web (HTML/CSS/JS) para el receptor LoRa.
 */

#ifndef DASHBOARD_H
#define DASHBOARD_H

const char index_html[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="es">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Smart Hive | Panel de Control</title>
    <style>
        body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background-color: #121212; color: #ffffff; text-align: center; margin: 0; padding: 20px; }
        h1 { color: #f39c12; letter-spacing: 2px; margin-bottom: 5px; }
        h2.section-title { color: #aaaaaa; border-bottom: 1px solid #333; padding-bottom: 10px; margin-top: 40px; font-weight: 300; text-transform: uppercase; font-size: 1.2em; }
        
        .wrapper { max-width: 1000px; margin: 0 auto; }
        .grid-container { display: grid; grid-template-columns: repeat(auto-fit, minmax(220px, 1fr)); gap: 20px; margin-top: 20px; }
        
        .card { background-color: #1e1e1e; border-radius: 12px; padding: 25px; box-shadow: 0 4px 10px rgba(0,0,0,0.5); position: relative; }
        .number { font-size: 3em; font-weight: bold; margin: 15px 0; }
        .unit { font-size: 0.4em; color: #888; }
        .label { font-size: 0.9em; color: #bbb; }

        .border-red { border-top: 4px solid #e74c3c; }
        .border-blue { border-top: 4px solid #3498db; }
        .border-green { border-top: 4px solid #2ecc71; }
        .border-purple { border-top: 4px solid #9b59b6; }
        .border-orange { border-top: 4px solid #e67e22; }

        .text-red { color: #e74c3c; }
        .text-blue { color: #3498db; }
        .text-green { color: #2ecc71; }
        .text-purple { color: #9b59b6; }
        .text-orange { color: #e67e22; }

        /* Botón Reset */
        .btn-reset { margin-top: 15px; background-color: #2c3e50; color: #fff; border: 1px solid #34495e; padding: 8px 15px; border-radius: 6px; cursor: pointer; transition: 0.2s; font-size: 0.8em; text-transform: uppercase; letter-spacing: 1px; }
        .btn-reset:hover { background-color: #e74c3c; border-color: #c0392b; }

        /* Barra de Estado Inferior */
        .status-bar { margin-top: 50px; padding: 10px; border-radius: 8px; font-size: 0.9em; font-weight: bold; display: inline-block; }
        .status-connected { color: #2ecc71; background-color: rgba(46, 204, 113, 0.1); }
        .status-disconnected { color: #e74c3c; background-color: rgba(231, 76, 60, 0.1); }
        .status-waiting { color: #f39c12; background-color: rgba(243, 156, 18, 0.1); }
    </style>
</head>
<body>
    <div class="wrapper">
        <h1>SMART HIVE 🐝</h1>
        <p style="color: #666; margin-top: 0;">Centro de Monitorización IoT Edge</p>

        <h2 class="section-title">Amenazas Detectadas (IA)</h2>
        <div class="grid-container">
            <div class="card border-red">
                <div class="label">Avispas en Vivo (Cámara)</div>
                <div class="number text-red" id="live_count">0</div>
            </div>
            <div class="card border-blue">
                <div class="label">Total Acumulado Hoy</div>
                <div class="number text-blue" id="total_count">0</div>
                <button class="btn-reset" onclick="resetTotal()">Resetear Contador</button>
            </div>
        </div>

        <h2 class="section-title">Ambiente de la Colmena</h2>
        <div class="grid-container">
            <div class="card border-green">
                <div class="label">Temperatura Interior</div>
                <div class="number text-green"><span id="hive_temp">--</span><span class="unit">°C</span></div>
            </div>
            <div class="card border-green">
                <div class="label">Humedad Relativa</div>
                <div class="number text-green"><span id="hive_hum">--</span><span class="unit">%</span></div>
            </div>
        </div>

        <h2 class="section-title">Salud del Sistema (Edge Node)</h2>
        <div class="grid-container">
            <div class="card border-purple">
                <div class="label">Temperatura CPU (Pi 5)</div>
                <div class="number text-purple"><span id="cpu_temp">--</span><span class="unit">°C</span></div>
            </div>
            <div class="card border-purple">
                <div class="label">Carga CPU</div>
                <div class="number text-purple"><span id="cpu_load">--</span><span class="unit">x</span></div>
            </div>
            <div class="card border-orange">
                <div class="label">Señal LoRa (RSSI / SNR)</div>
                <div class="number text-orange" style="font-size: 2em; margin-top: 25px;">
                    <span id="lora_rssi">--</span><span class="unit" style="font-size: 0.5em;"> dBm</span><br>
                    <span id="lora_snr">--</span><span class="unit" style="font-size: 0.5em;"> dB</span>
                </div>
            </div>
        </div>

        <div id="connection_status" class="status-bar status-waiting">
            🟠 Esperando primer paquete LoRa...
        </div>
    </div>

    <script>
        // Función para enviar la petición de RESET al ESP32
        function resetTotal() {
            if (confirm("¿Estás seguro de que quieres poner el contador de avispas a CERO?")) {
                fetch('/api/reset', { method: 'POST' })
                    .then(response => {
                        if(response.ok) {
                            document.getElementById('total_count').innerText = "0";
                        }
                    })
                    .catch(err => alert("Error al reiniciar el contador."));
            }
        }

        // Bucle asíncrono de actualización de datos
        setInterval(() => {
            fetch('/api/data')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('live_count').innerText = data.live;
                    document.getElementById('total_count').innerText = data.total;
                    
                    document.getElementById('hive_temp').innerText = data.hive_temp !== 0 ? data.hive_temp : '--';
                    document.getElementById('hive_hum').innerText = data.hive_hum !== 0 ? data.hive_hum : '--';
                    document.getElementById('cpu_temp').innerText = data.cpu_temp !== 0 ? data.cpu_temp : '--';
                    document.getElementById('cpu_load').innerText = data.cpu_load !== 0 ? data.cpu_load : '--';
                    document.getElementById('lora_rssi').innerText = data.lora_rssi !== 0 ? data.lora_rssi : '--';
                    document.getElementById('lora_snr').innerText = data.lora_rssi !== 0 ? data.lora_snr : '--';

                    // LÓGICA DEL WATCHDOG (Perro Guardián)
                    const statusEl = document.getElementById('connection_status');
                    if (data.connected === true) {
                        statusEl.innerText = "🟢 Conexión LoRa Estable";
                        statusEl.className = "status-bar status-connected";
                    } else if (data.lora_rssi !== 0) {
                        // Si ya hemos recibido datos antes pero 'connected' es false, es un corte.
                        statusEl.innerText = "🔴 ERROR: Señal LoRa perdida (>10 min)";
                        statusEl.className = "status-bar status-disconnected";
                    }
                })
                .catch(error => console.error("Error API:", error));
        }, 1000);
    </script>
</body>
</html>
)=====";

#endif