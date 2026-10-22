// The oven monitor page, served by the example's endpoint: a copy of teta-wot's
// example's `monitor.html`.

#pragma once

inline constexpr const char* monitor_html = R"html(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Oven monitor</title>
<style>
  :root { --bg: #fafaf8; --fg: #1d1d1b; --muted: #6b6b66; --line: #deded8; --accent: #c2410c; }
  @media (prefers-color-scheme: dark) {
    :root { --bg: #161615; --fg: #ececea; --muted: #9a9a94; --line: #34342f; --accent: #fb923c; }
  }
  body { margin: 0; background: var(--bg); color: var(--fg); font: 15px/1.5 system-ui, sans-serif; }
  main { max-width: 40rem; margin: 0 auto; padding: 1.5rem 1rem; }
  h1 { font-size: 1.25rem; margin: 0 0 1rem; }
  .reading { font-size: 3rem; font-variant-numeric: tabular-nums; color: var(--accent); }
  .muted { color: var(--muted); }
  form { display: flex; gap: .5rem; margin: 1rem 0; }
  input, button { font: inherit; padding: .35rem .6rem; border: 1px solid var(--line); border-radius: .3rem;
                  background: transparent; color: inherit; }
  button { cursor: pointer; }
  ol { list-style: none; padding: 0; margin: 0; border-top: 1px solid var(--line); }
  li { padding: .3rem 0; border-bottom: 1px solid var(--line); font: 13px/1.4 ui-monospace, monospace;
       overflow-wrap: anywhere; }
</style>
</head>
<body>
<main>
  <h1>Oven monitor</h1>
  <div><span class="reading" id="temperature">–</span> <span class="muted">°C</span></div>
  <div class="muted" id="state">connecting…</div>
  <form id="heat">
    <label for="target">Heat to</label>
    <input id="target" type="number" value="60" step="10" style="width: 6rem">
    <button>Start</button>
  </form>
  <p class="muted">Messages from <code id="url"></code>, newest first:</p>
  <ol id="log"></ol>
</main>
<script>
  const log = (text) => {
    const item = document.createElement("li");
    item.textContent = text;
    document.getElementById("log").prepend(item);
  };

  async function main() {
    // The Thing Description says where the WebSocket is: the temperature
    // property's form with a ws:// (or wss://) URL.
    const td = await (await fetch("./")).json();
    const url = td.properties.temperature.forms
      .map((form) => form.href)
      .find((href) => href.startsWith("ws:") || href.startsWith("wss:"));
    document.getElementById("url").textContent = url;

    const socket = new WebSocket(url);
    socket.onopen = () => {
      document.getElementById("state").textContent = "observing";
      socket.send(JSON.stringify({ messageType: "addPropertyObservation", data: { temperature: true } }));
      socket.send(JSON.stringify({ messageType: "addActionObservation", data: { heat_to: true } }));
    };
    socket.onclose = () => (document.getElementById("state").textContent = "disconnected");
    socket.onmessage = (event) => {
      const message = JSON.parse(event.data);
      if (message.messageType === "propertyStatus" && "temperature" in message.data) {
        document.getElementById("temperature").textContent = message.data.temperature.toFixed(1);
      }
      log(event.data);
    };

    const current = await (await fetch("temperature")).json();
    document.getElementById("temperature").textContent = current.toFixed(1);

    document.getElementById("heat").onsubmit = async (event) => {
      event.preventDefault();
      const target = Number(document.getElementById("target").value);
      await fetch("heat_to", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ target }),
      });
    };
  }

  main().catch((error) => (document.getElementById("state").textContent = String(error)));
</script>
</body>
</html>
)html";
