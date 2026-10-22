#include <WiFi.h>
#include <WebServer.h>
#include <HX711.h>

const int LOADCELL_DOUT_PIN = 21;
const int LOADCELL_SCK_PIN = 22;

// Configuración de la red propia que crea el ESP32 (modo Access Point)
const char* AP_SSID = "RCP_Entrenador";
const char* AP_PASSWORD = "rcp12345"; // mínimo 8 caracteres

HX711 scale;
WebServer server(80);

float valorActual = 0.0;
bool midiendo = false;

// Variables para el almacenamiento del historial completo
float historialMedicion[500]; 
int indiceHistorial = 0;

// Variables para la detección de picos por Máx/Mín
float picosHistorial[100]; 
int cantidadPicos = 0;

// Máquina de estados para buscar mínimo y luego máximo
enum EstadoPico { ESPERANDO_SUBIDA, BUSCANDO_MAXIMO };
EstadoPico estadoPico = ESPERANDO_SUBIDA;
float maximoActual = 0.0;

const char paginaHTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Entrenador en RCP - Autónomo</title>
    <style>
        body { font-family: Arial, sans-serif; text-align: center; background: #f4f7f6; padding: 15px; margin: 0; }
        .contenedor { max-width: 850px; margin: auto; background: white; padding: 20px; border-radius: 15px; box-shadow: 0 4px 15px rgba(0,0,0,0.15); }
        #valor { font-size: 45px; font-weight: bold; margin: 5px 0; color: #333; }
        #estado { font-size: 22px; font-weight: bold; margin: 10px 0; padding: 8px; border-radius: 8px; background: #eee; }
        .btn-control { background-color: #00aa33; color: white; border: none; padding: 12px 25px; font-size: 18px; border-radius: 8px; cursor: pointer; width: 100%; font-weight: bold; margin-bottom: 15px;}
        .btn-control.pausado { background-color: #cc0000; }
        
        .panel-principal { display: flex; justify-content: space-around; align-items: center; flex-wrap: wrap; gap: 20px; margin-top: 10px; }
        
        .contenedor-barra { display: flex; align-items: center; gap: 15px; }
        .zona-grafico { position: relative; width: 50px; height: 240px; background: #e0e0e0; border: 3px solid #333; border-radius: 8px; overflow: hidden; }
        #barra { position: absolute; bottom: 0; width: 100%; height: 0%; background: #00aa33; transition: height 0.05s; }
        
        .marcas { display: flex; flex-direction: column; justify-content: space-between; height: 240px; text-align: left; font-size: 12px; font-weight: bold; }
        .marca-item { display: flex; align-items: center; gap: 5px; }
        .punto { width: 10px; height: 10px; border-radius: 50%; display: inline-block; }

        .contenedor-cartesiano { background: #fafafa; border: 2px solid #ccc; border-radius: 8px; padding: 5px; position: relative; max-width: 100%; box-sizing: border-box; }
        canvas { background: #fff; border-radius: 4px; display: block; cursor: grab; touch-action: none; max-width: 100%; height: auto; }
        canvas.arrastrando { cursor: grabbing; }
        .ayuda { font-size: 11px; color: #888; margin-top: 4px; }

        #promedio-resultado { margin-top: 15px; font-size: 18px; font-weight: bold; color: #0056b3; background: #e6f7ff; border: 2px solid #91d5ff; padding: 10px; border-radius: 8px; display: none; }
        
        #tooltip {
            position: absolute;
            display: none;
            background: rgba(0, 0, 0, 0.85);
            color: #fff;
            padding: 6px 10px;
            font-size: 14px;
            font-weight: bold;
            border-radius: 6px;
            pointer-events: none;
            transform: translate(-50%, -130%);
            white-space: nowrap;
            z-index: 10;
        }
    </style>
</head>
<body>
    <div class="contenedor">
        <h1>Entrenador en RCP</h1>
        <button id="btnToggle" class="btn-control pausado" onclick="toggleMedicion()">INICIAR MEDICIÓN</button>
        
        <div id="valor">0.0 kg</div>
        <div id="estado">EN ESPERA</div>
        
        <div class="panel-principal">
            <div class="contenedor-barra">
                <div class="zona-grafico">
                    <div id="barra"></div>
                </div>
                <div class="marcas">
                    <div class="marca-item"><span class="punto" style="background:#cc0000;"></span> &gt;60kg</div>
                    <div class="marca-item"><span class="punto" style="background:#00aa33;"></span> 50-60kg</div>
                    <div class="marca-item"><span class="punto" style="background:#0066ff;"></span> &lt;50kg</div>
                </div>
            </div>

            <div class="contenedor-cartesiano" id="contenedorCanvas">
                <div id="tooltip"></div>
                <canvas id="graficoRcp" width="450" height="240"></canvas>
                <div class="ayuda">Tocá el gráfico para ver el valor · Arrastrá para moverte · Doble toque: volver al presente</div>
            </div>
        </div>
        
        <div id="promedio-resultado">Fuerza promedio <b>de los picos</b>: <span id="valPromedio">0.0</span> kg</div>
    </div>

    <script>
        const canvas = document.getElementById('graficoRcp');
        const ctx = canvas.getContext('2d');
        const tooltip = document.getElementById('tooltip');
        let historialDatos = [];
        let estaMidiendo = false;

        // Punto marcado en el gráfico: {indice} = posición dentro de historialDatos.
        // hoverFijo = true cuando lo marcó el usuario con un toque/clic (se queda hasta tocar otro lado).
        let puntoHover = null;
        let hoverFijo = false;

        // Estado del arrastre horizontal (paneo del eje X)
        let offsetArrastre = 0;       // 0 = viendo lo más reciente ("en vivo")
        let arrastrando = false;
        let movioDedo = false;        // false = fue un toque corto, true = fue un arrastre
        let inicioArrastreX = 0;
        let offsetInicialArrastre = 0;
        let ultimoTapTiempo = 0;
        let ultimoTapX = 0;

        const margenIzq = 40;
        const margenAbajo = 30;
        const VENTANA = 50;           // cantidad de muestras visibles a la vez

        function calcularInicioVentana() {
            let maxOffset = Math.max(0, historialDatos.length - VENTANA);
            if (offsetArrastre > maxOffset) offsetArrastre = maxOffset;
            return Math.max(0, historialDatos.length - VENTANA - offsetArrastre);
        }

        function yDeValor(valor) {
            const altoG = canvas.height - margenAbajo - 10;
            let y = margenAbajo + altoG - (valor / 100.0) * altoG;
            if (y < margenAbajo) y = margenAbajo;
            if (y > margenAbajo + altoG) y = margenAbajo + altoG;
            return y;
        }

        // Devuelve el índice de la muestra más cercana al toque/clic, o null si cae fuera de la línea
        function indiceDesdeEvento(e) {
            const rect = canvas.getBoundingClientRect();
            const escala = canvas.width / rect.width;
            const x = (e.clientX - rect.left) * escala;
            const pasoX = (canvas.width - margenIzq - 10) / VENTANA;
            const inicio = calcularInicioVentana();
            const ventana = Math.min(VENTANA, historialDatos.length - inicio);
            const iRel = Math.round((x - margenIzq) / pasoX);
            if (iRel < 0 || iRel >= ventana) return null;
            return inicio + iRel;
        }

        function dibujarGrafico() {
            ctx.clearRect(0, 0, canvas.width, canvas.height);

            const anchoG = canvas.width - margenIzq - 10;
            const altoG = canvas.height - margenAbajo - 10;

            ctx.font = "11px Arial";
            ctx.fillStyle = "#666";
            ctx.strokeStyle = "#e0e0e0";
            ctx.lineWidth = 1;

            for (let kg = 0; kg <= 100; kg += 25) {
                let y = margenAbajo + altoG - (kg / 100.0) * altoG;
                ctx.beginPath();
                ctx.moveTo(margenIzq, y);
                ctx.lineTo(margenIzq + anchoG, y);
                ctx.stroke();
                ctx.fillText(kg + "kg", 5, y + 4);
            }

            if (historialDatos.length < 2) {
                tooltip.style.display = "none";
                return;
            }

            const pasoX = anchoG / VENTANA;
            const inicio = calcularInicioVentana();
            const ventana = Math.min(VENTANA, historialDatos.length - inicio);

            ctx.strokeStyle = "#00aa33";
            ctx.lineWidth = 2;
            ctx.beginPath();
            for (let i = 0; i < ventana; i++) {
                let x = margenIzq + (i * pasoX);
                let y = yDeValor(historialDatos[inicio + i]);
                if (i === 0) ctx.moveTo(x, y);
                else ctx.lineTo(x, y);
            }
            ctx.stroke();

            // Punto marcado: guía vertical + círculo rojo + cartelito con el valor exacto
            let mostroTooltip = false;
            if (puntoHover !== null) {
                const iReal = puntoHover.indice - inicio;
                if (iReal >= 0 && iReal < ventana) {
                    const valorH = historialDatos[puntoHover.indice];
                    const xH = margenIzq + (iReal * pasoX);
                    const yH = yDeValor(valorH);

                    ctx.strokeStyle = "rgba(204, 0, 0, 0.35)";
                    ctx.lineWidth = 1;
                    ctx.beginPath();
                    ctx.moveTo(xH, margenAbajo);
                    ctx.lineTo(xH, margenAbajo + altoG);
                    ctx.stroke();

                    ctx.fillStyle = "#cc0000";
                    ctx.beginPath();
                    ctx.arc(xH, yH, 6, 0, 2 * Math.PI);
                    ctx.fill();

                    // Posición del cartelito (corrige por si el canvas se achicó en pantallas chicas)
                    const k = canvas.getBoundingClientRect().width / canvas.width;
                    tooltip.style.left = (canvas.offsetLeft + xH * k) + 'px';
                    tooltip.style.top = (canvas.offsetTop + yH * k) + 'px';
                    // Si el punto está muy arriba, el cartelito va abajo para que no se corte
                    tooltip.style.transform = (yH < 50) ? 'translate(-50%, 60%)' : 'translate(-50%, -130%)';
                    tooltip.innerHTML = valorH.toFixed(1) + " kg";
                    tooltip.style.display = "block";
                    mostroTooltip = true;
                }
            }
            if (!mostroTooltip) tooltip.style.display = "none";

            ctx.fillStyle = "#333";
            ctx.fillText("Tiempo ->", margenIzq + (anchoG / 2) - 20, canvas.height - 5);
        }

        canvas.addEventListener('pointerdown', function(e) {
            if (e.pointerType === 'mouse' && e.button !== 0) return;
            arrastrando = true;
            movioDedo = false;
            canvas.classList.add('arrastrando');
            inicioArrastreX = e.clientX;
            offsetInicialArrastre = offsetArrastre;
            canvas.setPointerCapture(e.pointerId);
        });

        canvas.addEventListener('pointermove', function(e) {
            if (arrastrando) {
                const rect = canvas.getBoundingClientRect();
                const escala = canvas.width / rect.width;
                const pasoX = (canvas.width - margenIzq - 10) / VENTANA;
                const deltaX = e.clientX - inicioArrastreX;

                // Hasta moverse unos píxeles se considera un toque, no un arrastre
                if (!movioDedo && Math.abs(deltaX) > 8) movioDedo = true;
                if (!movioDedo) return;

                // Arrastrar hacia la derecha = ver datos más viejos
                const deltaPuntos = Math.round((deltaX * escala) / pasoX);
                const maxOffset = Math.max(0, historialDatos.length - VENTANA);
                let nuevoOffset = offsetInicialArrastre + deltaPuntos;
                if (nuevoOffset < 0) nuevoOffset = 0;
                if (nuevoOffset > maxOffset) nuevoOffset = maxOffset;
                offsetArrastre = nuevoOffset;
                if (!hoverFijo) puntoHover = null;
                dibujarGrafico();
                return;
            }

            // Sin arrastre: en computadora, mostrar el valor al pasar el mouse
            if (e.pointerType === 'mouse' && !hoverFijo) {
                const idx = indiceDesdeEvento(e);
                puntoHover = (idx === null) ? null : { indice: idx };
                dibujarGrafico();
            }
        });

        function terminarArrastre() {
            arrastrando = false;
            canvas.classList.remove('arrastrando');
        }

        canvas.addEventListener('pointerup', function(e) {
            const eraToque = arrastrando && !movioDedo;
            terminarArrastre();
            if (!eraToque) return;

            // Doble toque = volver a la vista "en vivo"
            const ahora = Date.now();
            const esDobleToque = (ahora - ultimoTapTiempo < 350) && Math.abs(e.clientX - ultimoTapX) < 25;
            ultimoTapTiempo = ahora;
            ultimoTapX = e.clientX;
            if (esDobleToque) {
                offsetArrastre = 0;
                puntoHover = null;
                hoverFijo = false;
                ultimoTapTiempo = 0;
                dibujarGrafico();
                return;
            }

            // Toque simple = marcar el punto más cercano (o borrar la marca si tocó afuera)
            const idx = indiceDesdeEvento(e);
            if (idx === null) {
                puntoHover = null;
                hoverFijo = false;
            } else {
                puntoHover = { indice: idx };
                hoverFijo = true;
            }
            dibujarGrafico();
        });

        canvas.addEventListener('pointercancel', terminarArrastre);

        canvas.addEventListener('pointerleave', function(e) {
            // En el celular el "leave" llega apenas se levanta el dedo: no hay que borrar la marca
            if (arrastrando || hoverFijo || e.pointerType !== 'mouse') return;
            puntoHover = null;
            dibujarGrafico();
        });

        function toggleMedicion() {
            let endpoint = estaMidiendo ? "/detener" : "/iniciar";
            
            fetch(window.location.origin + endpoint)
                .then(res => res.json())
                .then(data => {
                    estaMidiendo = data.midiendo;
                    let btn = document.getElementById("btnToggle");
                    let divPromedio = document.getElementById("promedio-resultado");
                    
                    if (estaMidiendo) {
                        btn.innerHTML = "DETENER MEDICIÓN";
                        btn.className = "btn-control";
                        divPromedio.style.display = "none";
                        historialDatos = []; 
                        puntoHover = null;
                        hoverFijo = false;
                        offsetArrastre = 0;
                    } else {
                        btn.innerHTML = "INICIAR MEDICIÓN";
                        btn.className = "btn-control pausado";
                        
                        fetch(window.location.origin + "/promedio")
                            .then(res => res.json())
                            .then(resData => {
                                if (resData.promedio !== null) {
                                    document.getElementById("valPromedio").innerText = resData.promedio.toFixed(1);
                                    divPromedio.style.display = "block";
                                }
                            });
                    }
                })
                .catch(err => console.log("Error:", err));
        }

        function actualizar() {
            fetch(window.location.origin + "/datos")
                .then(res => res.json())
                .then(data => {
                    let valor = data.valor;

                    // Sincroniza el botón con el estado real del ESP32
                    // (por si se recargó la página en medio de una medición)
                    if (data.midiendo !== estaMidiendo) {
                        estaMidiendo = data.midiendo;
                        let btn = document.getElementById("btnToggle");
                        let divPromedio = document.getElementById("promedio-resultado");
                        if (estaMidiendo) {
                            btn.innerHTML = "DETENER MEDICIÓN";
                            btn.className = "btn-control";
                            divPromedio.style.display = "none";
                        } else {
                            btn.innerHTML = "INICIAR MEDICIÓN";
                            btn.className = "btn-control pausado";
                        }
                    }

                    document.getElementById("valor").innerHTML = valor.toFixed(1) + " kg";
                    
                    let porcentaje = (valor / 100.0) * 100; 
                    if (porcentaje > 100) porcentaje = 100;
                    document.getElementById("barra").style.height = porcentaje + "%";

                    let estado = document.getElementById("estado");
                    let barra = document.getElementById("barra");

                    if (!estaMidiendo) {
                        estado.innerHTML = "EN ESPERA";
                        estado.style.background = "#eee";
                        estado.style.color = "#333";
                        barra.style.background = "#00aa33";
                        dibujarGrafico();
                        return; 
                    }

                    if (valor < 50) {
                        estado.innerHTML = "MUY SUAVE";
                        estado.style.background = "#e6f0ff";
                        estado.style.color = "#0066ff";
                        barra.style.background = "#0066ff";
                    } else if (valor > 60) {
                        estado.innerHTML = "MUY FUERTE";
                        estado.style.background = "#ffe6e6";
                        estado.style.color = "#cc0000";
                        barra.style.background = "#cc0000";
                    } else {
                        estado.innerHTML = "¡COMPRESIÓN CORRECTA!";
                        estado.style.background = "#e6ffed";
                        estado.style.color = "#00aa33";
                        barra.style.background = "#00aa33";
                    }

                    historialDatos.push(valor);
                    // Buffer de 600 puntos (~3 minutos) para poder arrastrar hacia atrás
                    if (historialDatos.length > 600) {
                        historialDatos.shift();
                        // Al descartar la muestra más vieja, el punto marcado se corre un lugar
                        if (puntoHover !== null) {
                            puntoHover.indice--;
                            if (puntoHover.indice < 0) {
                                puntoHover = null;
                                hoverFijo = false;
                            }
                        }
                    }
                    dibujarGrafico();
                })
                .catch(err => {})
                .finally(() => {
                    // El próximo pedido se programa recién cuando terminó el anterior,
                    // así no se apilan pedidos si la señal WiFi se pone lenta.
                    setTimeout(actualizar, 300);
                });
        }

        actualizar(); // arranca el ciclo de actualización
    </script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send_P(200, "text/html", paginaHTML);
}

void handleGetDatos() {
  if (midiendo && scale.is_ready()) {
    float lecturaCruda = scale.get_units(1);
    if (lecturaCruda < 0) lecturaCruda = 0.0;

    valorActual = lecturaCruda;
    
    if (indiceHistorial < 500) {
      historialMedicion[indiceHistorial] = valorActual;
      indiceHistorial++;
    }

    // --- DETECTOR DE MÁXIMOS Y MÍNIMOS ---
    static float valorAnterior = 0.0;

    switch (estadoPico) {
      case ESPERANDO_SUBIDA:
        if (valorActual > 20.0 && valorActual > valorAnterior) {
          maximoActual = valorActual;
          estadoPico = BUSCANDO_MAXIMO;
        }
        break;

      case BUSCANDO_MAXIMO:
        if (valorActual > maximoActual) {
          maximoActual = valorActual;
        } 
        else if (valorActual < (maximoActual - 1.5)) {
          if (cantidadPicos < 100) {
            picosHistorial[cantidadPicos] = maximoActual;
            cantidadPicos++;
          }
          estadoPico = ESPERANDO_SUBIDA;
        }
        break;
    }

    valorAnterior = valorActual;

  } else if (!midiendo) {
    valorActual = 0.0;
  }

  // Buffer fijo con snprintf en vez de concatenar Strings (evita fragmentar el heap)
  char json[80];
  snprintf(json, sizeof(json), "{\"valor\": %.1f, \"midiendo\": %s}",
           valorActual, midiendo ? "true" : "false");
  server.send(200, "application/json", json);
}

void handleIniciar() {
  midiendo = true;
  indiceHistorial = 0; 
  cantidadPicos = 0;   
  maximoActual = 0.0;
  estadoPico = ESPERANDO_SUBIDA;
  scale.tare();        

  char json[32];
  snprintf(json, sizeof(json), "{\"midiendo\": true}");
  server.send(200, "application/json", json);
}

void handleDetener() {
  midiendo = false;
  valorActual = 0.0;

  char json[32];
  snprintf(json, sizeof(json), "{\"midiendo\": false}");
  server.send(200, "application/json", json);
}

void handlePromedio() {
  float suma = 0;
  
  if (cantidadPicos <= 0) {
    server.send(200, "application/json", "{\"promedio\": 0.0}");
    return;
  }

  for (int i = 0; i < cantidadPicos; i++) {
    suma += picosHistorial[i];
  }
  
  float promedioPicos = suma / cantidadPicos;

  char json[48];
  snprintf(json, sizeof(json), "{\"promedio\": %.1f}", promedioPicos);
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(43200);
  scale.tare();

  // El ESP32 crea su propia red WiFi (Access Point)
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  // Desactiva el ahorro de energía del WiFi (reduce la latencia)
  WiFi.setSleep(false);

  IPAddress ip = WiFi.softAPIP();
  Serial.println("\n¡Red WiFi propia creada!");
  Serial.print("Conectate a la red: ");
  Serial.println(AP_SSID);
  Serial.print("Contraseña: ");
  Serial.println(AP_PASSWORD);
  Serial.print("Entrá a: http://");
  Serial.println(ip); // Normalmente 192.168.4.1

  server.on("/", handleRoot);
  server.on("/datos", handleGetDatos);
  server.on("/iniciar", handleIniciar);
  server.on("/detener", handleDetener);
  server.on("/promedio", handlePromedio);
  
  server.begin();
  Serial.println("Servidor HTTP autónomo iniciado");
}

void loop() {
  server.handleClient();
  delay(2);
}
