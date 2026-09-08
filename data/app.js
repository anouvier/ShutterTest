let socket;
let currentFormatName = "24x36";
const seriesData = {};

window.addEventListener('DOMContentLoaded', () => {
    initWebSocket();
    drawOscilloscopeGrid(20); 
    loadBrandModelLists();

    document.getElementById('meta-brand').addEventListener('change', (e) => {
        rememberValue('/api/lists/brand', 'brand-list', e.target.value);
    });
    document.getElementById('meta-model').addEventListener('change', (e) => {
        rememberValue('/api/lists/model', 'model-list', e.target.value);
    });
});

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

async function loadBrandModelLists() {
    try {
        const res = await fetch('/api/lists');
        const data = await res.json();
        fillDatalist('brand-list', data.brands || []);
        fillDatalist('model-list', data.models || []);
    } catch (e) {
        console.error('Impossible de charger les listes marque/modèle', e);
    }
}

function fillDatalist(datalistId, items) {
    const datalist = document.getElementById(datalistId);
    datalist.innerHTML = '';
    [...items].sort((a, b) => a.localeCompare(b)).forEach(v => {
        const opt = document.createElement('option');
        opt.value = v;
        datalist.appendChild(opt);
    });
}

async function rememberValue(endpoint, datalistId, value) {
    if (!value || !value.trim()) return;
    try {
        const res = await fetch(endpoint, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ value: value.trim() })
        });
        const data = await res.json();
        fillDatalist(datalistId, data);
    } catch (e) {
        console.error("Impossible d'enregistrer la valeur", e);
    }
}

/* ==========================================================================
   COMMANDES IHM & WEBSOCKET
   ========================================================================== */

function getSelectedTargetMs() {
    const elem = document.getElementById('targetSpeed');
    const denominator = elem ? parseFloat(elem.value) : 125;
    return (1.0 / denominator) * 1000.0;
}

// 1. Commande de réarmement envoyée au ESP32 (avec vitesse cible synchronisée)
function armEngine() {
    if (socket && socket.readyState === WebSocket.OPEN) {
        const targetMs = getSelectedTargetMs();
        socket.send(JSON.stringify({
            cmd: "arm",
            targetMs: targetMs,
            targetSec: targetMs / 1000.0
        }));
    } else {
        console.warn("WebSocket non connecté, impossible de réarmer.");
    }
}

// 2. Callback lors du changement de vitesse dans la liste déroulante
function onTargetSpeedChange() {
    // Si une mesure précédente existe pour cette vitesse, rafraîchir la grille
    const targetMs = getSelectedTargetMs();
    drawOscilloscopeGrid(targetMs * 1.5);
    updateSummaryTable();
}

// 3. Réinitialisation de l'historique des tirs
function clearHistory() {
    for (const key in seriesData) {
        delete seriesData[key];
    }
    updateSummaryTable();
}

/* ==========================================================================
   TRAITEMENT DES MESURES
   ========================================================================== */

function processMeasurement(data) {
    const targetMs = getSelectedTargetMs();

    document.getElementById('shutter-type-tag').innerText = data.shutterType || "Focal (H)";
    document.getElementById('lastMeasurement').innerText = `${data.durationCenterMs.toFixed(2)} ms`;
    
    const calculatedSpeedText = data.calculatedSpeedS < 1.0 
        ? `(1/${Math.round(1.0 / data.calculatedSpeedS)} s)` 
        : `(${data.calculatedSpeedS.toFixed(2)} s)`;
    document.getElementById('lastSpeed').innerText = calculatedSpeedText;

    const deltaEV = Math.log2(data.durationCenterMs / targetMs);
    const evElem = document.getElementById('lastEV');
    evElem.innerText = (deltaEV >= 0 ? "+" : "") + deltaEV.toFixed(2) + " EV";
    evElem.className = "live-ev " + (Math.abs(deltaEV) < 0.33 ? "ev-good" : (Math.abs(deltaEV) < 0.66 ? "ev-warn" : "ev-bad"));

    if (!seriesData[targetMs]) {
        seriesData[targetMs] = {
            shots: [],
            shutterType: data.shutterType || "Focal (H)",
            skew1: data.curtain1SkewMs || 0,
            skew2: data.curtain2SkewMs || 0,
            speedR1: data.speedR1Mps || 0,
            speedR2: data.speedR2Mps || 0,
            divergence: data.gapDivergencePct || 0,
            isPartial: data.partial || false
        };
    }

    if (seriesData[targetMs].shots.length >= 3) {
        seriesData[targetMs].shots = [data.durationCenterMs];
    } else {
        seriesData[targetMs].shots.push(data.durationCenterMs);
    }
    
    seriesData[targetMs].skew1 = data.curtain1SkewMs || 0;
    seriesData[targetMs].skew2 = data.curtain2SkewMs || 0;
    seriesData[targetMs].speedR1 = data.speedR1Mps || 0;
    seriesData[targetMs].speedR2 = data.speedR2Mps || 0;
    seriesData[targetMs].divergence = data.gapDivergencePct || 0;
    seriesData[targetMs].isPartial = data.partial || false;

    // Rendu Canvas Oscilloscope Dynamique
    renderMultiSensorScope(data, targetMs);
    
    // Rendu Schéma Géométrique des rideaux
    renderGeometryDiagram(data);

    updateSummaryTable();
}

/* ==========================================================================
   OSCILLOSCOPE CANVAS AVEC ÉCHELLE TEMPORELLE DYNAMIQUE
   ========================================================================== */
function renderMultiSensorScope(data, targetMs) {
    const canvas = document.getElementById('oscilloscope');
    if (!canvas) return;
    const ctx = canvas.getContext('2d');

    ctx.fillStyle = '#090a0d';
    ctx.fillRect(0, 0, canvas.width, canvas.height);

    if (!data.sensors) return;

    let minRise = Infinity;
    let maxFall = -Infinity;
    data.sensors.forEach(s => { 
        if (s.valid) {
            if (s.rise < minRise) minRise = s.rise;
            if (s.fall > maxFall) maxFall = s.fall;
        }
    });

    if (minRise === Infinity || maxFall === -Infinity) {
        // Aucun capteur valide : afficher un message d'erreur dans le canvas
        ctx.fillStyle = '#ff5252';
        ctx.font = '14px sans-serif';
        ctx.fillText("ERREUR : Aucun capteur n'a détecté de signal", 20, canvas.height / 2);
        return;
    }

    const totalDurationMs = (maxFall - minRise) / 1000.0;
    const totalTimeWindowMs = Math.max(totalDurationMs * 1.3, targetMs * 1.5);

    drawOscilloscopeGrid(totalTimeWindowMs);

    const timeToPx = (ms) => (ms / totalTimeWindowMs) * (canvas.width * 0.85);
    const startPxOffset = canvas.width * 0.08;

    const sensorColors = ['#00d2ff', '#3a86ff', '#00e676', '#ff007f', '#ffbe0b'];
    const SENSOR_LABELS = ["Haut Gauche", "Bas Gauche", "Centre", "Haut Droite", "Bas Droite"];
    const rowHeight = canvas.height / 6;

    data.sensors.forEach((s, idx) => {
        const yBase = (idx + 1) * rowHeight;

        ctx.fillStyle = "rgba(255, 255, 255, 0.25)";
        ctx.font = "11px sans-serif";
        ctx.textAlign = "left";
        ctx.fillText(SENSOR_LABELS[idx], 10, yBase - 6);

        if (!s.valid) {
            // Dessiner une ligne pointillée rouge pour un capteur n'ayant pas déclenché
            ctx.strokeStyle = '#ff5252';
            ctx.setLineDash([4, 4]);
            ctx.beginPath();
            ctx.moveTo(0, yBase);
            ctx.lineTo(canvas.width, yBase);
            ctx.stroke();
            ctx.setLineDash([]);
            return;
        }

        const riseMs = (s.rise - minRise) / 1000.0;
        const fallMs = (s.fall - minRise) / 1000.0;

        const xStart = startPxOffset + timeToPx(riseMs);
        const xEnd   = startPxOffset + timeToPx(fallMs);

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

/* ==========================================================================
   SCHÉMA VECTORIEL DE LA GÉOMÉTRIE DES RIDEAUX
   ========================================================================== */
function renderGeometryDiagram(data) {
    const canvas = document.getElementById('geometryCanvas');
    if (!canvas) return;
    const ctx = canvas.getContext('2d');

    ctx.fillStyle = '#ffffff';
    ctx.fillRect(0, 0, canvas.width, canvas.height);

    const margin = 35;
    const w = canvas.width - (margin * 2);
    const h = canvas.height - (margin * 2) - 20; // 20px de marge basse en plus pour le texte

    ctx.strokeStyle = '#888888';
    ctx.setLineDash([4, 4]);
    ctx.strokeRect(margin, margin, w, h);
    ctx.setLineDash([]);

    // --- Rideau 1 (rouge) ---
    const skew1Ms = data.curtain1SkewMs || 0;
    const px1 = (skew1Ms / 0.5) * 20;
    const top1X = margin + px1, top1Y = margin;
    const bot1X = margin,       bot1Y = margin + h;

    ctx.strokeStyle = '#d90429';
    ctx.lineWidth = 2.5;
    ctx.beginPath();
    ctx.moveTo(top1X, top1Y);
    ctx.lineTo(bot1X, bot1Y);
    ctx.stroke();

    const lead1X = skew1Ms >= 0 ? top1X : bot1X;
    const lead1Y = skew1Ms >= 0 ? top1Y : bot1Y;
    ctx.fillStyle = '#d90429';
    ctx.beginPath();
    ctx.arc(lead1X, lead1Y, 4, 0, Math.PI * 2);
    ctx.fill();

    // --- Rideau 2 (bleu) — utilise maintenant SON PROPRE skew, pas celui de R1 ---
    const skew2Ms = data.curtain2SkewMs || 0;
    const px2 = (skew2Ms / 0.5) * 20;
    const offsetX = 40; // décalage horizontal pour distinguer visuellement R1 et R2
    const top2X = margin + offsetX + px2, top2Y = margin;
    const bot2X = margin + offsetX,       bot2Y = margin + h;

    ctx.strokeStyle = '#023e8a';
    ctx.lineWidth = 2.5;
    ctx.beginPath();
    ctx.moveTo(top2X, top2Y);
    ctx.lineTo(bot2X, bot2Y);
    ctx.stroke();

    const lead2X = skew2Ms >= 0 ? top2X : bot2X;
    const lead2Y = skew2Ms >= 0 ? top2Y : bot2Y;
    ctx.fillStyle = '#023e8a';
    ctx.beginPath();
    ctx.arc(lead2X, lead2Y, 4, 0, Math.PI * 2);
    ctx.fill();

    // --- Légendes texte, une ligne chacune ---
    const SKEW_ALIGNED_TOLERANCE_MS = 0.05;

    function skewLabel(skewMs) {
        if (Math.abs(skewMs) < SKEW_ALIGNED_TOLERANCE_MS) return "Aligné";
        return skewMs > 0 ? "Haut en avance" : "Bas en avance";
    }

    ctx.fillStyle = '#000000';
    ctx.font = '10px sans-serif';
    ctx.fillText(`Perpendicularité R1 : ${skewLabel(skew1Ms)} (${Math.abs(skew1Ms).toFixed(2)} ms)`, margin, canvas.height - 30);
    ctx.fillText(`Perpendicularité R2 : ${skewLabel(skew2Ms)} (${Math.abs(skew2Ms).toFixed(2)} ms)`, margin, canvas.height - 18);
    ctx.fillText(`Divergence fente (Accél. R2) : ${data.gapDivergencePct >= 0 ? '+' : ''}${(data.gapDivergencePct || 0).toFixed(1)} %`, margin, canvas.height - 6);
}

/* ==========================================================================
   MISE À JOUR DES TABLEAUX
   ========================================================================== */
function updateSummaryTable() {
    const tbody = document.querySelector('#resultsTable tbody');
    const printTbody = document.getElementById('print-table-body');
    const ticketGrid = document.getElementById('ticket-grid');

    if (tbody) tbody.innerHTML = '';
    if (printTbody) printTbody.innerHTML = '';
    if (ticketGrid) ticketGrid.innerHTML = '';

    const sortedTargets = Object.keys(seriesData).map(Number).sort((a, b) => b - a);
    const toleranceLimit = parseFloat(document.getElementById('meta-tolerance')?.value || 0.50);

    sortedTargets.forEach(targetMs => {
        const item = seriesData[targetMs];
        const shots = item.shots;

        const m1 = shots[0] ? shots[0].toFixed(2) : "--";
        const m2 = shots[1] ? shots[1].toFixed(2) : "--";
        const m3 = shots[2] ? shots[2].toFixed(2) : "--";

        let avgMsText = "--", evText = "--", stdDevText = "--", repeatText = "--", statusHtml = "--";

        if (shots.length > 0) {
            const sum = shots.reduce((a, b) => a + b, 0);
            const avg = sum / shots.length;
            avgMsText = avg.toFixed(2) + " ms";

            const deltaEV = Math.log2(avg / targetMs);
            evText = (deltaEV >= 0 ? "+" : "") + deltaEV.toFixed(2) + " EV";

            if (shots.length > 1) {
                const variance = shots.reduce((acc, val) => acc + Math.pow(val - avg, 2), 0) / shots.length;
                const stdDev = Math.sqrt(variance);
                stdDevText = stdDev.toFixed(2) + " ms";
                
                const cv = (stdDev / avg) * 100;
                repeatText = (100 - cv).toFixed(1) + " %";
            } else {
                stdDevText = "0.00 ms";
                repeatText = "100 %";
            }

            const isOk = Math.abs(deltaEV) <= toleranceLimit && !item.isPartial;
            if (item.isPartial) {
                statusHtml = `<span class="status-fail" title="Capteur(s) manquant(s)">INCOMPLET</span>`;
            } else {
                statusHtml = isOk ? `<span class="status-ok">CONFORME</span>` : `<span class="status-fail">HORS TOL.</span>`;
            }
        }

        const targetSec = targetMs / 1000.0;
        const targetLabel = targetSec >= 1.0 ? `${targetSec}s` : `1/${Math.round(1/targetSec)}s`;

        if (tbody) {
            const tr = document.createElement('tr');
            tr.innerHTML = `
                <td><strong>${targetLabel}</strong></td>
                <td>${m1}</td>
                <td>${m2}</td>
                <td>${m3}</td>
                <td><strong>${avgMsText}</strong></td>
                <td><span class="ev-tag ${Math.abs(Math.log2((shots.reduce((a,b)=>a+b,0)/shots.length||targetMs)/targetMs)) < 0.33 ? 'ev-good' : 'ev-warn'}">${evText}</span></td>
                <td>${stdDevText}</td>
                <td>${item.skew1.toFixed(2)}ms</td>
                <td>${item.skew2.toFixed(2)}ms</td>
                <td>${item.divergence >= 0 ? "+" : ""}${item.divergence.toFixed(1)}% ${item.isPartial ? '⚠️' : ''}</td>
            `;
            tbody.appendChild(tr);
        }

        if (printTbody) {
            const printTr = document.createElement('tr');
            printTr.innerHTML = `
                <td><strong>${targetLabel}</strong></td>
                <td>${m1}</td>
                <td>${m2}</td>
                <td>${m3}</td>
                <td><strong>${avgMsText}</strong></td>
                <td>${evText}</td>
                <td>${stdDevText}</td>
                <td>${repeatText}</td>
                <td>${item.skew1.toFixed(2)} ms</td>
                <td>${item.skew2.toFixed(2)} ms</td>
                <td>${item.divergence >= 0 ? "+" : ""}${item.divergence.toFixed(1)} %</td>
                <td>${statusHtml}</td>
            `;
            printTbody.appendChild(printTr);
        }

        if (ticketGrid && shots.length > 0) {
            const ticketItem = document.createElement('div');
            ticketItem.className = 'ticket-item';
            ticketItem.innerHTML = `<span>${targetLabel}:</span> <strong>${evText}</strong>`;
            ticketGrid.appendChild(ticketItem);
        }
    });

    const now = new Date().toLocaleDateString('fr-FR');
    const cameraModel = document.getElementById('meta-model')?.value || "Boîtier Inconnu";
    const cameraBrand = document.getElementById('meta-brand')?.value || "";
    const cameraType = document.getElementById('meta-type')?.value || "";
    const cameraSerial = document.getElementById('meta-serial')?.value;
    const techName = document.getElementById('meta-tech')?.value || "Technicien";
    const notes = document.getElementById('meta-notes')?.value;
    const reportType = document.getElementById('report-type')?.value || "initial";

    const fullCamStr = [cameraBrand, cameraModel].filter(Boolean).join(' ')
        + (cameraType ? ` (${cameraType})` : "")
        + (cameraSerial ? ` — #${cameraSerial}` : "");

    document.getElementById('print-report-title').innerText =
        reportType === "calibration" ? "RAPPORT DE CALIBRATION" : "RAPPORT D'ESSAI INITIAL";

    document.getElementById('print-date').innerText = now;
    document.getElementById('ticket-date').innerText = now;
    document.getElementById('print-camera-info').innerText = fullCamStr;
    document.getElementById('ticket-camera').innerText = fullCamStr;
    document.getElementById('print-tech-info').innerText = techName;

    const notesSec = document.getElementById('print-notes-section');
    if (notes && notesSec) {
        notesSec.style.display = 'block';
        document.getElementById('print-notes-text').innerText = notes;
    } else if (notesSec) {
        notesSec.style.display = 'none';
    }
}

/* ==========================================================================
   EXPORTATION PDF ET SIMULATION
   ========================================================================== */
function printReport() {
    const canvasGeo = document.getElementById('geometryCanvas');
    const printImgGeo = document.getElementById('print-geometry-img');
    if (canvasGeo && printImgGeo) {
        printImgGeo.src = canvasGeo.toDataURL('image/png');
    }

    window.print();
}

function triggerSimulation() {
    const targetMs = getSelectedTargetMs();
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
        curtain1SkewMs: 0.18,
        curtain2SkewMs: -0.03,
        speedR1Mps: 2.45,
        speedR2Mps: 2.38,
        gapDivergencePct: -2.8,
        partial: false,
        sensors: mockSensors
    };

    processMeasurement(mockData);
}