let socket;
let currentFormatName = "24x36";

// Stockage exhaustif des séries de 3 tirs par vitesse cible
const seriesData = {};

window.addEventListener('DOMContentLoaded', () => {
    initWebSocket();
    // Dessine la grille initiale pour 1/125s (8ms * 2.5 = 20ms)
    drawOscilloscopeGrid(20); 
});

/* ==========================================================================
   COMMUNICATION WEBSOCKET
   ========================================================================== */
function initWebSocket() {
    const gateway = `ws://${window.location.hostname}/ws`;
    socket = new WebSocket(gateway);

    socket.onopen = () => {
        const statusBadge = document.getElementById('status-badge');
        if (statusBadge) {
            statusBadge.innerText = "CONNECTÉ";
            statusBadge.classList.add('ready');
        }
    };

    socket.onclose = () => {
        const statusBadge = document.getElementById('status-badge');
        if (statusBadge) {
            statusBadge.innerText = "DECONNECTÉ";
            statusBadge.classList.remove('ready');
        }
        // Tentative de reconnexion automatique toutes les 2 secondes
        setTimeout(initWebSocket, 2000);
    };

    socket.onmessage = (event) => {
        const data = JSON.parse(event.data);
        if (data.type === "status") {
            currentFormatName = data.format;
            const formatBadge = document.getElementById('format-badge');
            if (formatBadge) formatBadge.innerText = data.format.toUpperCase();
        } else if (data.type === "measurement") {
            processMeasurement(data);
        }
    };
}

/* ==========================================================================
   TRAITEMENT DES MESURES & CALCULS
   ========================================================================== */
function getSelectedTargetMs() {
    const denominator = parseFloat(document.getElementById('targetSpeed').value);
    return (1.0 / denominator) * 1000.0; // Conversion en ms
}

function processMeasurement(data) {
    const targetMs = getSelectedTargetMs();

    // 1. Mise à jour de l'affichage en direct
    document.getElementById('shutter-type-tag').innerText = data.shutterType || "Focal (H)";
    document.getElementById('lastMeasurement').innerText = `${data.durationCenterMs.toFixed(2)} ms`;
    
    const calculatedSpeedText = data.calculatedSpeedS < 1.0 
        ? `(1/${Math.round(1.0 / data.calculatedSpeedS)} s)` 
        : `(${data.calculatedSpeedS.toFixed(2)} s)`;
    document.getElementById('lastSpeed').innerText = calculatedSpeedText;

    // Écart en EV par rapport à la consigne
    const deltaEV = Math.log2(data.durationCenterMs / targetMs);
    const evElem = document.getElementById('lastEV');
    evElem.innerText = (deltaEV >= 0 ? "+" : "") + deltaEV.toFixed(2) + " EV";
    evElem.className = "live-ev " + (Math.abs(deltaEV) < 0.33 ? "ev-good" : (Math.abs(deltaEV) < 0.66 ? "ev-warn" : "ev-bad"));

    // 2. Gestion de la série des 3 tirs pour la vitesse sélectionnée
    if (!seriesData[targetMs]) {
        seriesData[targetMs] = {
            shots: [],
            shutterType: data.shutterType || "Focal (H)",
            skew: data.curtain1SkewMs || 0,
            speedR1: data.speedR1Mps || 0,
            speedR2: data.speedR2Mps || 0,
            divergence: data.gapDivergencePct || 0
        };
    }

    // Réinitialise la série au bout de 3 tirs, sinon ajoute le résultat
    if (seriesData[targetMs].shots.length >= 3) {
        seriesData[targetMs].shots = [data.durationCenterMs];
    } else {
        seriesData[targetMs].shots.push(data.durationCenterMs);
    }
    
    // Conserve les dernières métriques géométriques physiques
    seriesData[targetMs].skew = data.curtain1SkewMs || 0;
    seriesData[targetMs].speedR1 = data.speedR1Mps || 0;
    seriesData[targetMs].speedR2 = data.speedR2Mps || 0;
    seriesData[targetMs].divergence = data.gapDivergencePct || 0;

    // 3. Rendu du tracé Oscilloscope
    renderMultiSensorScope(data, targetMs);

    // 4. Actualisation des tableaux et métadonnées
    updateSummaryTable();
}

/* ==========================================================================
   OSCILLOSCOPE CANVAS (TRACÉ DES 5 CAPTEURS OPTIQUES)
   ========================================================================== */
function renderMultiSensorScope(data, targetMs) {
    const canvas = document.getElementById('oscilloscope');
    if (!canvas) return;
    const ctx = canvas.getContext('2d');

    ctx.fillStyle = '#090a0d';
    ctx.fillRect(0, 0, canvas.width, canvas.height);

    // Fenêtre temporelle d'affichage : 2.5 fois la consigne cible
    const totalTimeMs = targetMs * 2.5;
    drawOscilloscopeGrid(totalTimeMs);

    if (!data.sensors) return;

    // Recherche de l'instant zéro (premier déclenchement)
    let minRise = Infinity;
    data.sensors.forEach(s => { if (s.valid && s.rise < minRise) minRise = s.rise; });

    const timeToPx = (ms) => (ms / totalTimeMs) * canvas.width;
    const startPxOffset = canvas.width * 0.08; // Offset visuel de 8% à gauche

    const sensorColors = ['#00d2ff', '#3a86ff', '#00e676', '#ff007f', '#ffbe0b'];
    const rowHeight = canvas.height / 6;

    data.sensors.forEach((s, idx) => {
        if (!s.valid) return;

        const yBase = (idx + 1) * rowHeight;
        const riseMs = (s.rise - minRise) / 1000.0;
        const fallMs = (s.fall - minRise) / 1000.0;

        const xStart = startPxOffset + timeToPx(riseMs);
        const xEnd   = startPxOffset + timeToPx(fallMs);

        // Tracé du créneau
        ctx.strokeStyle = sensorColors[idx];
        ctx.lineWidth = 2;
        ctx.beginPath();
        
        ctx.moveTo(0, yBase);
        ctx.lineTo(xStart, yBase);
        ctx.lineTo(xStart, yBase - (rowHeight * 0.55));
        ctx.lineTo(xEnd, yBase - (rowHeight * 0.55));
        ctx.lineTo(xEnd, yBase);
        ctx.lineTo(canvas.width, yBase);
        
        ctx.stroke();
    });
}

function drawOscilloscopeGrid(totalTimeMs) {
    const canvas = document.getElementById('oscilloscope');
    if (!canvas) return;
    const ctx = canvas.getContext('2d');

    ctx.strokeStyle = '#1e222b';
    ctx.lineWidth = 1;
    ctx.fillStyle = '#8a96a8';
    ctx.font = '10px monospace';

    const numDivs = 10;
    const stepPx = canvas.width / numDivs;
    const stepMs = totalTimeMs / numDivs;

    for (let i = 0; i <= numDivs; i++) {
        const x = i * stepPx;
        ctx.beginPath();
        ctx.moveTo(x, 0);
        ctx.lineTo(x, canvas.height);
        ctx.stroke();

        if (i > 0) {
            ctx.fillText((i * stepMs).toFixed(1) + "ms", x + 2, canvas.height - 6);
        }
    }
}

function onTargetSpeedChange() {
    const targetMs = getSelectedTargetMs();
    drawOscilloscopeGrid(targetMs * 2.5);
}

/* ==========================================================================
   MISE À JOUR DES TABLEAUX WEB ET RAPPORT PDF
   ========================================================================== */
function updateSummaryTable() {
    const tbody = document.querySelector('#resultsTable tbody');
    const printTbody = document.getElementById('print-table-body');
    const ticketGrid = document.getElementById('ticket-grid');

    if (tbody) tbody.innerHTML = '';
    if (printTbody) printTbody.innerHTML = '';
    if (ticketGrid) ticketGrid.innerHTML = '';

    const sortedTargets = Object.keys(seriesData).map(Number).sort((a, b) => b - a);
    let lastShutterType = "--";

    sortedTargets.forEach(targetMs => {
        const item = seriesData[targetMs];
        const shots = item.shots;
        lastShutterType = item.shutterType;

        const m1 = shots[0] ? shots[0].toFixed(2) : "--";
        const m2 = shots[1] ? shots[1].toFixed(2) : "--";
        const m3 = shots[2] ? shots[2].toFixed(2) : "--";

        let avgMsText = "--";
        let evText = "--";
        let dispText = "--";
        let evClass = "";
        let avgSpeedLabel = "";

        if (shots.length > 0) {
            const sum = shots.reduce((a, b) => a + b, 0);
            const avg = sum / shots.length;
            avgMsText = avg.toFixed(2) + " ms";

            const deltaEV = Math.log2(avg / targetMs);
            evText = (deltaEV >= 0 ? "+" : "") + deltaEV.toFixed(2) + " EV";
            evClass = Math.abs(deltaEV) < 0.33 ? "ev-good" : (Math.abs(deltaEV) < 0.66 ? "ev-warn" : "ev-bad");

            const min = Math.min(...shots);
            const max = Math.max(...shots);
            dispText = (max - min).toFixed(2) + " ms";

            const avgSec = avg / 1000.0;
            avgSpeedLabel = avgSec < 1.0 ? `1/${Math.round(1/avgSec)}` : `${avgSec.toFixed(1)}s`;
        }

        const targetSec = targetMs / 1000.0;
        const targetLabel = targetSec >= 1.0 ? `${targetSec}s` : `1/${Math.round(1/targetSec)}s`;

        // 1. Tableau Web
        if (tbody) {
            const tr = document.createElement('tr');
            tr.innerHTML = `
                <td><strong>${targetLabel}</strong> <br><small style="color:var(--text-muted)">(${targetMs.toFixed(1)}ms)</small></td>
                <td>${m1}</td>
                <td>${m2}</td>
                <td>${m3}</td>
                <td><strong>${avgMsText}</strong></td>
                <td><span class="ev-tag ${evClass}">${evText}</span></td>
                <td>${dispText}</td>
                <td><small>Angle: ${item.skew.toFixed(2)}ms<br>Div: ${item.divergence.toFixed(1)}%</small></td>
            `;
            tbody.appendChild(tr);
        }

        // 2. Tableau Impression PDF (Exhaustif)
        if (printTbody) {
            const printTr = document.createElement('tr');
            printTr.innerHTML = `
                <td><strong>${targetLabel}</strong></td>
                <td>${m1}</td>
                <td>${m2}</td>
                <td>${m3}</td>
                <td><strong>${avgMsText}</strong></td>
                <td>${evText}</td>
                <td>${dispText}</td>
                <td>${item.skew.toFixed(2)} ms</td>
                <td>${item.speedR1 > 0 ? item.speedR1.toFixed(2) + " m/s" : "--"}</td>
                <td>${item.speedR2 > 0 ? item.speedR2.toFixed(2) + " m/s" : "--"}</td>
                <td>${item.divergence >= 0 ? "+" : ""}${item.divergence.toFixed(1)} %</td>
            `;
            printTbody.appendChild(printTr);
        }

        // 3. Ticket Sac Photo
        if (ticketGrid && shots.length > 0) {
            const ticketItem = document.createElement('div');
            ticketItem.className = 'ticket-item';
            ticketItem.innerHTML = `<span>${targetLabel}:</span> <strong>${avgSpeedLabel} (${evText})</strong>`;
            ticketGrid.appendChild(ticketItem);
        }
    });

    // Mise à jour des dates et en-têtes
    const now = new Date().toLocaleDateString('fr-FR');
    const printDate = document.getElementById('print-date');
    const ticketDate = document.getElementById('ticket-date');
    const printFormat = document.getElementById('print-format');
    const ticketFormat = document.getElementById('ticket-format');
    const printShutterType = document.getElementById('print-shutter-type');

    if (printDate) printDate.innerText = now;
    if (ticketDate) ticketDate.innerText = now;
    if (printFormat) printFormat.innerText = currentFormatName;
    if (ticketFormat) ticketFormat.innerText = `Format: ${currentFormatName}`;
    if (printShutterType) printShutterType.innerText = lastShutterType;
}

/* ==========================================================================
   EXPORTATION ET GENERATION PDF
   ========================================================================== */
function printReport() {
    // Capture de l'oscilloscope Canvas en image PNG pour le PDF
    const canvas = document.getElementById('oscilloscope');
    const printImg = document.getElementById('print-oscilloscope-img');
    if (canvas && printImg) {
        printImg.src = canvas.toDataURL('image/png');
    }

    // Déclenchement de l'impression
    window.print();
}

function clearHistory() {
    for (let key in seriesData) delete seriesData[key];
    updateSummaryTable();
}

function armEngine() {
    if (socket && socket.readyState === WebSocket.OPEN) {
        socket.send(JSON.stringify({ cmd: "arm" }));
    }
}

/* ==========================================================================
   MODE DEMO / SIMULATION DE TEST
   ========================================================================== */
function triggerSimulation() {
    const targetMs = getSelectedTargetMs();
    // Génère un écart réaliste aléatoire entre -12% et +12%
    const randomError = (Math.random() - 0.4) * 0.20; 
    const simulatedDuration = targetMs * (1 + randomError);
    const travelTimeMs = 2.8; 
    
    const mockSensors = [];
    const baseTimestampUs = 1000000;
    
    for (let i = 0; i < 5; i++) {
        const positionOffsetMs = (i / 4.0) * travelTimeMs;
        const riseUs = baseTimestampUs + (positionOffsetMs * 1000);
        const fallUs = riseUs + (simulatedDuration * 1000);

        mockSensors.push({
            valid: true,
            rise: riseUs,
            fall: fallUs
        });
    }

    const mockData = {
        type: "measurement",
        shutterType: "Rideaux Horizontaux",
        durationCenterMs: simulatedDuration,
        calculatedSpeedS: simulatedDuration / 1000.0,
        curtain1SkewMs: 0.14,
        speedR1Mps: 2.45,
        speedR2Mps: 2.38,
        gapDivergencePct: -2.8,
        sensors: mockSensors
    };

    processMeasurement(mockData);
}