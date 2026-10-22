// The thermometer page, served by the example's endpoint: a copy of teta-wot's
// example's `index.html`.

#pragma once

inline constexpr const char* index_html = R"html(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Thermometer</title>
<style>
  :root { --bg: #fafaf8; --fg: #1d1d1b; --muted: #6b6b66; --line: #deded8; --accent: #0369a1; }
  @media (prefers-color-scheme: dark) {
    :root { --bg: #161615; --fg: #ececea; --muted: #9a9a94; --line: #34342f; --accent: #38bdf8; }
  }
  body { margin: 0; background: var(--bg); color: var(--fg); font: 15px/1.5 system-ui, sans-serif; }
  main { max-width: 40rem; margin: 0 auto; padding: 1.5rem 1rem; }
  h1 { font-size: 1.25rem; margin: 0 0 1rem; }
  .reading { font-size: 3rem; font-variant-numeric: tabular-nums; color: var(--accent); }
  .muted { color: var(--muted); }
  svg { width: 100%; height: 10rem; border: 1px solid var(--line); border-radius: .3rem; margin-top: 1rem; }
  polyline { fill: none; stroke: var(--accent); stroke-width: 2; vector-effect: non-scaling-stroke; }
  code { overflow-wrap: anywhere; }
</style>
</head>
<body>
<main>
  <h1>Thermometer</h1>
  <div><span class="reading" id="reading">–</span> <span class="muted">°C</span></div>
  <div class="muted" id="state">connecting…</div>
  <svg viewBox="0 0 120 100" preserveAspectRatio="none"><polyline id="plot" points=""></polyline></svg>
  <p class="muted">The last 30 seconds, from <code id="url"></code>.</p>
</main>
<script>
  const readings = [];

  function plot() {
    const low = Math.min(...readings) - 0.1;
    const high = Math.max(...readings) + 0.1;
    const points = readings.map((value, i) => `${i},${100 - ((value - low) / (high - low)) * 100}`);
    document.getElementById("plot").setAttribute("points", points.join(" "));
  }

  async function main() {
    // Find the stream as a TD consumer would: the temperature property's
    // form with "subprotocol": "sse", resolved against the TD's base.
    const td = await (await fetch("./")).json();
    const form = td.properties.temperature.forms.find((f) => f.subprotocol === "sse");
    const url = new URL(form.href, td.base);
    document.getElementById("url").textContent = url;

    // EventSource sends "Accept: text/event-stream", and reconnects by itself.
    const source = new EventSource(url);
    source.onopen = () => (document.getElementById("state").textContent = "observing");
    source.onerror = () => (document.getElementById("state").textContent = "reconnecting…");
    source.onmessage = (event) => {
      const value = JSON.parse(event.data);
      document.getElementById("reading").textContent = value.toFixed(2);
      readings.push(value);
      if (readings.length > 120) readings.shift();
      plot();
    };
  }

  main().catch((error) => (document.getElementById("state").textContent = String(error)));
</script>
</body>
</html>
)html";
