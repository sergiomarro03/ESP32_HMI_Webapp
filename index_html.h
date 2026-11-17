// Página web (HTML + CSS + JavaScript) que sirve el ESP32.
// El JavaScript consulta la API JSON del firmware con fetch():
//   GET /api/status, /api/history, /api/log, /log.csv
//   POST /api/led?state=1|0, /api/intervalo?s=N
#pragma once

const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>HMI ESP32 - DS18B20</title>
<style>
  :root {
    --fondo: #f3f5f8; --tarjeta: #ffffff; --texto: #18212b; --suave: #5a6978;
    --linea: #dce2ea; --acento: #0b6bcb; --acento-texto: #ffffff;
    --ok: #1a8f4e; --mal: #d33a2c; --aviso: #c98a00;
  }
  @media (prefers-color-scheme: dark) {
    :root {
      --fondo: #0e1318; --tarjeta: #171f27; --texto: #e7ecf1; --suave: #93a2b1;
      --linea: #2a3541; --acento: #4da3ff; --acento-texto: #06121f;
      --ok: #3ccb76; --mal: #ff6e61; --aviso: #f2b63c;
    }
  }
  * { box-sizing: border-box; }
  html { -webkit-text-size-adjust: 100%; }
  body {
    margin: 0; background: var(--fondo); color: var(--texto);
    font: 16px/1.45 system-ui, -apple-system, "Segoe UI", Roboto, sans-serif;
  }
  header {
    display: flex; flex-wrap: wrap; align-items: center; justify-content: space-between;
    gap: 8px 16px; padding: 14px 16px; background: var(--tarjeta);
    border-bottom: 1px solid var(--linea);
  }
  h1 { margin: 0; font-size: 1.25rem; }
  h1 small { font-weight: 400; color: var(--suave); font-size: .85rem; margin-left: 6px; }
  h2 { margin: 0 0 10px; font-size: .8rem; letter-spacing: .06em; text-transform: uppercase; color: var(--suave); }

  /* Semáforo de conexión */
  .semaforo {
    display: inline-flex; align-items: center; gap: 8px; padding: 6px 12px;
    border: 1px solid var(--linea); border-radius: 999px; font-weight: 600; font-size: .9rem;
  }
  .semaforo .luz { width: 14px; height: 14px; border-radius: 50%; background: var(--suave); }
  .semaforo.ok  .luz { background: var(--ok);  box-shadow: 0 0 8px var(--ok); }
  .semaforo.mal .luz { background: var(--mal); box-shadow: 0 0 8px var(--mal); }
  .semaforo.ok  { color: var(--ok); }
  .semaforo.mal { color: var(--mal); }

  main {
    display: grid; gap: 14px; padding: 14px 16px 8px; max-width: 1100px; margin: 0 auto;
    grid-template-columns: repeat(auto-fit, minmax(290px, 1fr));
  }
  main.obsoleto .valor-vivo { opacity: .45; }
  .tarjeta { background: var(--tarjeta); border: 1px solid var(--linea); border-radius: 14px; padding: 16px; }
  .ancha { grid-column: 1 / -1; }

  .temp { display: flex; align-items: baseline; gap: 6px; }
  .temp .num { font-size: 3.4rem; font-weight: 700; line-height: 1; font-variant-numeric: tabular-nums; }
  .temp .unidad { font-size: 1.3rem; color: var(--suave); }
  .nota { color: var(--suave); font-size: .85rem; margin-top: 6px; }

  .alarma { display: inline-block; margin-top: 10px; padding: 4px 10px; border-radius: 8px;
            background: var(--mal); color: #fff; font-weight: 600; font-size: .85rem; }
  .alarma[hidden] { display: none; }

  .estado-led { display: flex; align-items: center; gap: 10px; margin-bottom: 12px; font-weight: 700; }
  .led { width: 22px; height: 22px; border-radius: 50%; background: var(--linea); border: 2px solid var(--suave); }
  .led.on { background: var(--aviso); border-color: var(--aviso); box-shadow: 0 0 12px var(--aviso); }

  .botones { display: flex; flex-wrap: wrap; gap: 10px; }
  .btn {
    appearance: none; display: inline-flex; align-items: center; justify-content: center;
    min-height: 44px; padding: 0 18px; border-radius: 10px; cursor: pointer; text-decoration: none;
    font: inherit; font-weight: 600; color: var(--texto); background: transparent;
    border: 1.5px solid var(--linea);
  }
  .btn:hover { border-color: var(--acento); }
  .btn:focus-visible, input[type=range]:focus-visible { outline: 3px solid var(--acento); outline-offset: 2px; }
  .btn.primario { background: var(--acento); color: var(--acento-texto); border-color: var(--acento); }
  .btn[aria-pressed="true"] { box-shadow: inset 0 0 0 2px var(--texto); }
  .btn:disabled { opacity: .5; cursor: not-allowed; }

  input[type=range] { width: 100%; accent-color: var(--acento); height: 32px; }
  .escala { display: flex; justify-content: space-between; color: var(--suave); font-size: .8rem; }
  .intervalo-val { font-size: 1.6rem; font-weight: 700; margin-bottom: 4px; }

  .caja-grafico { position: relative; height: 260px; }
  #graficoMsg { color: var(--mal); }

  .fila-log { display: flex; flex-wrap: wrap; align-items: center; justify-content: space-between; gap: 10px; margin-bottom: 10px; }
  .fila-log h2 { margin: 0; }
  .tabla-wrap { overflow-x: auto; }
  table { width: 100%; border-collapse: collapse; font-size: .85rem; font-variant-numeric: tabular-nums; }
  th, td { text-align: left; padding: 6px 8px; border-bottom: 1px solid var(--linea); white-space: nowrap; }
  th { color: var(--suave); font-weight: 600; }
  td.ok { color: var(--ok); } td.mal { color: var(--mal); font-weight: 600; } td.aviso { color: var(--aviso); font-weight: 600; }

  #mensaje { min-height: 1.4em; margin: 6px 0 0; color: var(--aviso); font-size: .9rem; }
  footer { max-width: 1100px; margin: 0 auto; padding: 8px 16px 24px; color: var(--suave); font-size: .8rem; }
  .sr { position: absolute; width: 1px; height: 1px; overflow: hidden; clip: rect(0 0 0 0); }
</style>
</head>
<body>

<header>
  <h1>HMI ESP32 <small>Termómetro DS18B20</small></h1>
  <div id="semaforo" class="semaforo" role="status" aria-live="polite">
    <span class="luz" aria-hidden="true"></span><span id="semTxt">Conectando…</span>
  </div>
</header>

<main id="principal">

  <section class="tarjeta" aria-labelledby="h-temp">
    <h2 id="h-temp">Temperatura</h2>
    <div class="temp valor-vivo"><span class="num" id="tempVal">--.-</span><span class="unidad">°C</span></div>
    <div class="nota">Última lectura: segundo <span id="tempHora">--</span> desde el arranque</div>
    <div id="alarma" class="alarma" hidden></div>
  </section>

  <section class="tarjeta" aria-labelledby="h-led">
    <h2 id="h-led">LED embebido</h2>
    <div class="estado-led valor-vivo">
      <span id="ledDot" class="led" aria-hidden="true"></span><span id="ledTxt">--</span>
    </div>
    <div class="botones">
      <button id="btnOn"  class="btn primario" type="button" aria-pressed="false">Encender</button>
      <button id="btnOff" class="btn"          type="button" aria-pressed="false">Apagar</button>
    </div>
    <p id="mensaje" role="status" aria-live="polite"></p>
  </section>

  <section class="tarjeta" aria-labelledby="h-int">
    <h2 id="h-int">Intervalo de muestreo</h2>
    <div class="intervalo-val"><span id="intervaloVal">2 s</span></div>
    <input id="intervalo" type="range" min="1" max="5" step="1" value="2" aria-label="Intervalo de muestreo en segundos">
    <div class="escala" aria-hidden="true"><span>1 s</span><span>5 s</span></div>
    <div class="nota">El ESP32 mide el sensor con esta frecuencia.</div>
  </section>

  <section class="tarjeta ancha" aria-labelledby="h-graf">
    <h2 id="h-graf">Últimas 20 lecturas</h2>
    <div class="caja-grafico">
      <canvas id="grafico" role="img" aria-label="Gráfico de temperatura en tiempo real"></canvas>
    </div>
    <div id="graficoMsg" class="nota" hidden>No se pudo cargar Chart.js; los demás controles siguen funcionando.</div>
  </section>

  <section class="tarjeta ancha" aria-labelledby="h-log">
    <div class="fila-log">
      <h2 id="h-log">Registro (últimas 10 entradas)</h2>
      <a class="btn primario" href="/log.csv" download="log.csv">Descargar log</a>
    </div>
    <div class="tabla-wrap">
      <table>
        <caption class="sr">Últimas entradas del registro del ESP32</caption>
        <thead><tr><th>Timestamp (s desde el arranque)</th><th>Evento</th><th>Valor</th><th>Estado</th></tr></thead>
        <tbody id="logCuerpo"><tr><td colspan="4">Sin datos todavía…</td></tr></tbody>
      </table>
    </div>
  </section>

</main>
<footer id="pie">ESP32: --</footer>

<script src="/chart.js"></script>
<script>
(() => {
  'use strict';

  const POLL_MS = 1000;
  const TIMEOUT_MS = 2500;
  const FALLOS_PARA_ROJO = 2;
  const MAX_PUNTOS = 20;

  const $ = (id) => document.getElementById(id);

  let enLinea = null;
  let fallos = 0;
  let ultimoSeq = -1;
  let ultimoLogCount = -1;
  let ultimaInteraccionIntervalo = 0;

  // Llama a la API del ESP32. Lanza error si falla la red o si el ESP32 responde con error
  async function api(ruta, metodo = 'GET') {
    const ctl = new AbortController();
    const temporizador = setTimeout(() => ctl.abort(), TIMEOUT_MS);
    try {
      const r = await fetch(ruta, { method: metodo, cache: 'no-store', signal: ctl.signal });
      if (!r.ok) { const e = new Error('HTTP ' + r.status); e.http = true; throw e; }
      return r;
    } finally {
      clearTimeout(temporizador);
    }
  }

  // Gráfico de temperatura (Chart.js)
  let grafico = null;
  if (typeof Chart !== 'undefined') {
    const css = getComputedStyle(document.documentElement);
    Chart.defaults.color = css.getPropertyValue('--suave').trim();
    Chart.defaults.borderColor = css.getPropertyValue('--linea').trim();
    grafico = new Chart($('grafico'), {
      type: 'line',
      data: { labels: [], datasets: [{
        label: 'DS18B20 (°C)', data: [],
        borderColor: css.getPropertyValue('--acento').trim(),
        backgroundColor: 'rgba(77,163,255,0.15)',
        borderWidth: 2, tension: 0.3, pointRadius: 3, fill: true, spanGaps: false
      }]},
      options: {
        responsive: true, maintainAspectRatio: false, animation: false,
        plugins: { legend: { display: false } },
        scales: {
          x: { title: { display: true, text: 'segundos desde el arranque' },
               ticks: { maxRotation: 0, autoSkip: true, maxTicksLimit: 6 } },
          y: { title: { display: true, text: '°C' } }
        }
      }
    });
  } else {
    $('graficoMsg').hidden = false;
  }

  // Agrega una lectura al gráfico y descarta las más antiguas (máximo 20)
  function agregarPunto(etiqueta, valor) {
    if (!grafico) return;
    const d = grafico.data;
    d.labels.push(etiqueta);
    d.datasets[0].data.push(valor);
    while (d.labels.length > MAX_PUNTOS) { d.labels.shift(); d.datasets[0].data.shift(); }
    grafico.update('none');
  }

  // Carga las últimas lecturas guardadas en el ESP32
  async function cargarHistorial() {
    const h = await (await api('/api/history')).json();
    if (grafico) {
      grafico.data.labels = h.labels;
      grafico.data.datasets[0].data = h.values;
      grafico.update('none');
    }
    ultimoSeq = h.seq;
  }

  // Muestra las últimas 10 entradas del log, la más nueva arriba
  async function cargarLog() {
    const texto = await (await api('/api/log')).text();
    const filas = texto.split('\n').filter(Boolean).reverse();
    const cuerpo = $('logCuerpo');
    cuerpo.replaceChildren();
    if (filas.length === 0) {
      const tr = document.createElement('tr'), td = document.createElement('td');
      td.colSpan = 4; td.textContent = 'Sin entradas todavía.';
      tr.appendChild(td); cuerpo.appendChild(tr); return;
    }
    for (const linea of filas) {
      const tr = document.createElement('tr');
      linea.split(',').forEach((campo, i) => {
        const td = document.createElement('td');
        td.textContent = campo;
        if (i === 3) {
          if (/ERROR|ACTIVA|PERDIDO/.test(campo)) td.className = 'mal';
          else if (/RECHAZADO/.test(campo)) td.className = 'aviso';
          else td.className = 'ok';
        }
        tr.appendChild(td);
      });
      cuerpo.appendChild(tr);
    }
  }

  // Semáforo: verde si hay comunicación, rojo si no
  function ponerEnLinea(valor) {
    enLinea = valor;
    $('semaforo').className = 'semaforo ' + (valor ? 'ok' : 'mal');
    $('semTxt').textContent = valor ? 'Conectado' : 'Sin conexión';
    $('principal').classList.toggle('obsoleto', !valor);
    $('btnOn').disabled = $('btnOff').disabled = !valor;
  }

  // Muestra en pantalla el estado recibido. forzar = true cuando es la respuesta a un comando del usuario
  function pintar(s, forzar = false) {
    $('tempVal').textContent = (s.temp === null) ? '--.-' : s.temp.toFixed(1);
    $('tempHora').textContent = s.etiqueta || '--';

    const alarma = $('alarma');
    if (s.alarmaSensor) { alarma.textContent = 'ALARMA: sensor DS18B20 desconectado'; alarma.hidden = false; }
    else if (s.alarma)  { alarma.textContent = 'ALARMA: temperatura ≥ ' + s.umbral.toFixed(0) + ' °C'; alarma.hidden = false; }
    else                { alarma.hidden = true; }

    $('ledDot').classList.toggle('on', s.led);
    $('ledTxt').textContent = s.led ? 'ENCENDIDO' : 'APAGADO';
    $('btnOn').setAttribute('aria-pressed', String(s.led));
    $('btnOff').setAttribute('aria-pressed', String(!s.led));

    // La barra sigue al valor del ESP32, salvo mientras el usuario la mueve
    if (forzar || Date.now() - ultimaInteraccionIntervalo > 2000) {
      $('intervalo').value = s.intervalo;
      $('intervaloVal').textContent = s.intervalo + ' s';
    }

    $('pie').textContent = 'ESP32 encendido hace ' + s.uptime + ' s';
  }

  function mostrarMensaje(texto) {
    $('mensaje').textContent = texto;
    if (texto) setTimeout(() => { if ($('mensaje').textContent === texto) $('mensaje').textContent = ''; }, 4000);
  }

  // Ciclo principal: pide el estado cada segundo y actualiza gráfico, log y semáforo
  async function consultar() {
    try {
      const s = await (await api('/api/status')).json();
      fallos = 0;

      if (enLinea !== true) {              // primera conexión o reconexión
        await cargarHistorial();
        ultimoLogCount = -1;
        ponerEnLinea(true);
      } else if (s.seq < ultimoSeq) {      // el ESP32 se reinició
        await cargarHistorial();
      } else if (s.seq !== ultimoSeq) {    // lectura nueva
        agregarPunto(s.etiqueta, s.temp);
        ultimoSeq = s.seq;
      }

      pintar(s);

      if (s.logOk && s.logCount !== ultimoLogCount) {
        ultimoLogCount = s.logCount;
        cargarLog().catch(() => { ultimoLogCount = -1; });
      }
    } catch (e) {
      fallos++;
      if (fallos >= FALLOS_PARA_ROJO && enLinea !== false) ponerEnLinea(false);
    } finally {
      setTimeout(consultar, POLL_MS);
    }
  }

  // Envía un comando (LED o intervalo) y muestra la respuesta
  async function enviarComando(ruta) {
    try {
      pintar(await (await api(ruta, 'POST')).json(), true);
      mostrarMensaje('');
    } catch (e) {
      mostrarMensaje(e.http ? 'El ESP32 rechazó el comando.' : 'No hay comunicación con el ESP32.');
    }
  }

  $('btnOn').addEventListener('click',  () => enviarComando('/api/led?state=1'));
  $('btnOff').addEventListener('click', () => enviarComando('/api/led?state=0'));

  $('intervalo').addEventListener('input', (ev) => {
    ultimaInteraccionIntervalo = Date.now();
    $('intervaloVal').textContent = ev.target.value + ' s';
  });
  $('intervalo').addEventListener('change', (ev) => {
    enviarComando('/api/intervalo?s=' + encodeURIComponent(ev.target.value));
  });

  $('btnOn').disabled = $('btnOff').disabled = true;
  $('principal').classList.add('obsoleto');
  consultar();
})();
</script>
</body>
</html>
)rawliteral";
