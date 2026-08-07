let socket;
let shotCounter = 0;

window.addEventListener('DOMContentLoaded', () => {
    initWebSocket();
});

function initWebSocket() {
    const gateway = `ws://${window.location.hostname}/ws`;
    socket = new WebSocket(gateway);

    socket.onopen = () => {
        document.getElementById('status-badge').innerText = "CONNECTÉ";
        document.getElementById('status-badge').classList.add('ready');
    };

    socket.onclose = () => {
        document.getElementById('status-badge').innerText = "DECONNECTÉ";
        document.getElementById('status-badge').classList.remove('ready');
        setTimeout(initWebSocket, 2000); // Reconnexion auto
    };

    socket.onmessage = (event) => {
        const data = JSON.parse(event.data);
        if (data.type === "status") {
            updateStatus(data);
        } else if (data.type === "measurement") {
            updateMeasurement(data);
        }
    };
}

function updateStatus(data) {
    document.getElementById('format-badge').innerText = data.format.toUpperCase();
}

function updateMeasurement(data) {
    shotCounter++;

    // 1. Vitesse & Écart EV
    let speedText = "--";
    if (data.calculatedSpeedS > 0) {
        if (data.calculatedSpeedS < 1.0) {
            speedText = `1/${Math.round(1.0 / data.calculatedSpeedS)}`;
        } else {
            speedText = data.calculatedSpeedS.toFixed(2);
        }
    }
    document.getElementById('speed-value').innerText = speedText;

    const evElem = document.getElementById('ev-value');
    evElem.innerText = (data.deltaEV >= 0 ? "+" : "") + data.deltaEV.toFixed(2);
    evElem.style.color = Math.abs(data.deltaEV) < 0.33 ? "var(--accent-green)" : "var(--accent-red)";

    document.getElementById('shutter-type').innerText = data.shutterType;

    // 2. Détails rideaux
    document.getElementById('c1-travel').innerText = `${data.curtain1TravelMs.toFixed(2)} ms`;
    document.getElementById('c2-travel').innerText = `${data.curtain2TravelMs.toFixed(2)} ms`;
    
    const skew1Elem = document.getElementById('c1-skew');
    skew1Elem.innerText = `${data.curtain1SkewMs.toFixed(3)} ms`;
    skew1Elem.style.color = Math.abs(data.curtain1SkewMs) > 0.2 ? "var(--accent-orange)" : "var(--text-color)";

    const skew2Elem = document.getElementById('c2-skew');
    skew2Elem.innerText = `${data.curtain2SkewMs.toFixed(3)} ms`;

    const divElem = document.getElementById('gap-div');
    divElem.innerText = `${data.gapDivergencePct.toFixed(1)} %`;
    divElem.style.color = Math.abs(data.gapDivergencePct) > 10 ? "var(--accent-red)" : "var(--accent-blue)";

    // 3. Rendu du Chronogramme
    renderTimeline(data.sensors);

    // 4. Ajout à l'historique
    addHistoryRow(shotCounter, data.shutterType, speedText, data.deltaEV, data.curtain1SkewMs, data.gapDivergencePct);
}

function renderTimeline(sensors) {
    const container = document.getElementById('sensor-timeline');
    container.innerHTML = "";

    const sensorNames = ["Top-Left", "Bot-Left", "Center", "Top-Right", "Bot-Right"];

    // Trouver le min rise et max fall pour déterminer l'échelle 100%
    let minRise = Infinity;
    let maxFall = 0;

    sensors.forEach(s => {
        if (s.valid) {
            if (s.rise < minRise) minRise = s.rise;
            if (s.fall > maxFall) maxFall = s.fall;
        }
    });

    const totalDuration = maxFall - minRise;

    sensors.forEach((s, idx) => {
        const row = document.createElement('div');
        row.className = "sensor-row";

        let barStyle = "left: 0; width: 0;";
        if (s.valid && totalDuration > 0) {
            const leftPct = ((s.rise - minRise) / totalDuration) * 100;
            const widthPct = ((s.fall - s.rise) / totalDuration) * 100;
            barStyle = `left: ${leftPct}%; width: ${widthPct}%;`;
        }

        row.innerHTML = `
            <span class="sensor-label">${sensorNames[idx]}</span>
            <div class="bar-wrapper">
                <div class="sensor-bar" style="${barStyle}"></div>
            </div>
        `;
        container.appendChild(row);
    });
}

function addHistoryRow(num, type, speed, ev, skew, div) {
    const tbody = document.querySelector('#history-table tbody');
    const row = document.createElement('tr');
    row.innerHTML = `
        <td>#${num}</td>
        <td>${type}</td>
        <td><strong>${speed} s</strong></td>
        <td style="color:${Math.abs(ev) < 0.33 ? 'var(--accent-green)' : 'var(--accent-red)'}">${ev >= 0 ? '+' : ''}${ev.toFixed(2)} EV</td>
        <td>${skew.toFixed(3)} ms</td>
        <td>${div.toFixed(1)} %</td>
    `;
    tbody.insertBefore(row, tbody.firstChild); // Ajoute au début
}

function armEngine() {
    if (socket && socket.readyState === WebSocket.OPEN) {
        socket.send(JSON.stringify({ cmd: "arm" }));
    }
}

function triggerSimulation() {
    if (socket && socket.readyState === WebSocket.OPEN) {
        socket.send(JSON.stringify({ cmd: "simulate", speed: 0.002, travel: 2.5 }));
    }
}