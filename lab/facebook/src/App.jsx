/* lab/facebook/src/App.jsx — React minimo (createElement, sin build).
 * Flujo: Connect / Create / Status / Update / GoLive / End / Delete.
 * secure_stream_url: boton mostrar-una-vez, jamas persistida.
 * Vista Progreso T-077: actores Tu / Lab / Meta Graph API / OBS-Aitum(manual);
 * cada paso con estado pendiente/ok/fallo + code/subcode Meta + hora + causa.
 * Sin automatizar Aitum; sin polling (Check solo a peticion del operador).
 */
"use strict";

const e = React.createElement;

/* Pasos T-077: id, etiqueta, actor responsable, causa Meta esperable. */
const STEPS = [
  { id: "connect", label: "1. Connect (dialog/oauth + PKCE + exchange)", actor: "Tu / Lab / Meta", hint: "causa 190=reconnect" },
  { id: "create", label: "2. Create UNPUBLISHED (EVERYONE, titulo 1-254)", actor: "Lab / Meta", hint: "causa 100/33=no gestionable" },
  { id: "readback", label: "3. Read-back (Status a peticion)", actor: "Lab / Meta", hint: "code/subcode Meta + hora" },
  { id: "update", label: "4. Update titulo/desc/EVERYONE", actor: "Lab / Meta", hint: "causa 100/33, 190" },
  { id: "persist", label: "5. Persistencia: 2 creates + comparar URLs", actor: "Lab / Meta + Tu (comparas)", hint: "URLs distintas por create; el lab no persiste ingesta" },
  { id: "ingest", label: "6. Ingesta manual en Aitum (operador)", actor: "Tu / OBS-Aitum(manual)", hint: "el lab JAMAS automatiza Aitum (§§23/51)" },
  { id: "livenow", label: "7. LIVE_NOW (GoLive a peticion)", actor: "Tu / Lab / Meta", hint: "causa 1363120/44=elegibilidad" },
  { id: "end", label: "8. End/Delete (cierre y limpieza)", actor: "Tu / Lab / Meta", hint: "fin del ciclo throwaway" },
];

function initialProgress() {
  const o = {};
  for (const s of STEPS) o[s.id] = { status: "pendiente", code: "", time: "" };
  return o;
}

function nowTime() {
  const d = new Date();
  return d.toTimeString().slice(0, 8);
}

/* Extrae "code/subcode" legible de una respuesta del lab sin loguear secretos. */
function codeOf(r) {
  if (!r || typeof r !== "object") return "";
  if (r.ok) return "ok";
  const err = typeof r.error === "string" ? r.error : JSON.stringify(r.error || "error");
  return err.slice(0, 60);
}

function Field({ label, value, onChange, textarea }) {
  return e(
    "label",
    { style: { display: "block", margin: "8px 0" } },
    e("div", null, label),
    textarea
      ? e("textarea", { rows: 3, cols: 60, value, onChange: (ev) => onChange(ev.target.value) })
      : e("input", { size: 60, value, onChange: (ev) => onChange(ev.target.value) })
  );
}

function App() {
  const [st, setSt] = React.useState({ connected: false, hasCode: false, liveId: null });
  const [title, setTitle] = React.useState("");
  const [description, setDescription] = React.useState("");
  const [liveId, setLiveId] = React.useState("");
  const [info, setInfo] = React.useState(null); // {id,status,privacy,...}
  const [ingest, setIngest] = React.useState(null); // {server,key} solo memoria UI
  const [ingestVisible, setIngestVisible] = React.useState(false);
  const [msg, setMsg] = React.useState("");
  const [progress, setProgress] = React.useState(initialProgress);
  const [creates, setCreates] = React.useState([]); // [{id,time,hadUrl}] solo memoria

  const mark = (id, status, code) => {
    setProgress((p) => ({ ...p, [id]: { status, code: code || "", time: nowTime() } }));
  };

  const refresh = async () => {
    try {
      const s = await api.state();
      setSt(s);
      if (s.liveId && !liveId) setLiveId(s.liveId);
    } catch (err) {
      setMsg("state: " + err.message);
    }
  };
  React.useEffect(() => { refresh(); }, []);

  const say = (x) => setMsg(typeof x === "string" ? x : JSON.stringify(x));

  const doConnect = async () => {
    const r = await api.authUrl();
    if (!r.ok) {
      mark("connect", "fallo", codeOf(r));
      return say(r);
    }
    window.open(r.url, "_blank", "noopener");
    mark("connect", "pendiente", "dialog abierto");
    say("Autoriza en facebook.com y vuelve: el callback capturara el code.");
  };
  const doExchange = async () => {
    const r = await api.exchange();
    mark("connect", r.ok ? "ok" : "fallo", codeOf(r));
    say(r);
    refresh();
  };
  const doCreate = async () => {
    if (!validTitle(title)) {
      mark("create", "fallo", "invalid-title local");
      return say("titulo invalido (1-254)");
    }
    const r = await api.createLive(title, description);
    if (r.ok && r.data && r.data.id) {
      setLiveId(r.data.id);
      const hadUrl = !!(r.data.secure_stream_url);
      if (hadUrl) setIngest(splitIngest(r.data.secure_stream_url));
      else setIngest(null);
      setIngestVisible(false);
      setCreates((c) => {
        const next = [...c, { id: r.data.id, time: nowTime(), hadUrl }].slice(-4);
        if (next.length >= 2) {
          const a = next[next.length - 2];
          const b = next[next.length - 1];
          const sameId = a.id === b.id ? "mismo id (revisar)" : "ids distintos ok";
          mark("persist", "ok", `${next.length} creates; ${sameId}; URLs solo en memoria`);
        } else {
          mark("persist", "pendiente", `${next.length}/2 creates (falta uno)`);
        }
        return next;
      });
      mark("create", "ok", codeOf(r));
    } else {
      mark("create", "fallo", codeOf(r));
    }
    say(r);
    refresh();
  };
  const doStatus = async () => {
    if (!liveId.trim()) return say("live_video_id requerido");
    const r = await api.liveStatus(liveId.trim());
    if (r.ok) {
      setInfo(r.data);
      mark("readback", "ok", `status=${(r.data && r.data.status) || "?"}`);
      say("status ok");
    } else {
      mark("readback", "fallo", codeOf(r));
      say(r);
    }
  };
  const doUpdate = async () => {
    if (!liveId.trim()) return say("live_video_id requerido");
    if (!validTitle(title)) {
      mark("update", "fallo", "invalid-title local");
      return say("titulo invalido (1-254)");
    }
    const r = await api.updateLive(liveId.trim(), title, description);
    mark("update", r.ok ? "ok" : "fallo", codeOf(r));
    say(r);
  };
  const doGoLive = async () => {
    if (!liveId.trim()) return say("live_video_id requerido");
    const r = await api.goLive(liveId.trim());
    mark("livenow", r.ok ? "ok" : "fallo", codeOf(r));
    say(r);
  };
  const doEnd = async () => {
    if (!liveId.trim()) return say("live_video_id requerido");
    const r = await api.endLive(liveId.trim());
    mark("end", r.ok ? "ok" : "fallo", codeOf(r));
    say(r);
  };
  const doDelete = async () => {
    if (!liveId.trim()) return say("live_video_id requerido");
    const r = await api.deleteLive(liveId.trim());
    mark("end", r.ok ? "ok" : "fallo", codeOf(r));
    say(r);
  };

  const statusColor = (s) => (s === "ok" ? "green" : s === "fallo" ? "red" : "#888");

  return e(
    "div",
    { style: { fontFamily: "sans-serif", maxWidth: 760, margin: "0 auto", padding: 16 } },
    e("h1", null, "Lab Facebook (throwaway, T-077)"),
    e("p", null, "Lab local puerto 3002. Aitum se configura MANUALMENTE por el operador."),
    e("p", null, `Estado: ${st.connected ? "conectado" : "no conectado"}${st.hasCode ? " (code listo: pulsa Exchange)" : ""}`),
    e(
      "div",
      null,
      e("button", { onClick: doConnect }, "Connect"),
      " ",
      e("button", { onClick: doExchange }, "Exchange"),
      " ",
      e("button", { onClick: async () => { say(await api.reset()); setIngest(null); setInfo(null); refresh(); } }, "Desconectar/limpiar")
    ),
    e(Field, { label: "Titulo (1-254)", value: title, onChange: setTitle }),
    e(Field, { label: "Descripcion", value: description, onChange: setDescription, textarea: true }),
    e(Field, { label: "live_video_id", value: liveId, onChange: setLiveId }),
    e(
      "div",
      null,
      e("button", { onClick: doCreate }, "Create"),
      " ",
      e("button", { onClick: doStatus }, "Status"),
      " ",
      e("button", { onClick: doUpdate }, "Update"),
      " ",
      e("button", { onClick: doGoLive }, "GoLive"),
      " ",
      e("button", { onClick: doEnd }, "End"),
      " ",
      e("button", { onClick: doDelete }, "Delete")
    ),
    info &&
      e(
        "pre",
        { style: { background: "#f4f4f4", padding: 8 } },
        JSON.stringify(
          { id: info.id, status: info.status, privacy: info.privacy, title: info.title },
          null,
          2
        )
      ),
    ingest &&
      e(
        "div",
        { style: { border: "1px solid #c00", padding: 8, marginTop: 8 } },
        e("p", null, "Ingesta (transito en memoria, mostrar-una-vez; copia MANUAL a Aitum):"),
        ingestVisible
          ? e(
              "div",
              null,
              e("p", null, e("b", null, "Servidor: "), e("code", null, ingest.server)),
              e("p", null, e("b", null, "Clave: "), e("code", null, ingest.key)),
              e(
                "p",
                null,
                e(
                  "button",
                  { onClick: () => { setIngestVisible(false); mark("ingest", "ok", "copiado manual confirmado"); } },
                  "Ocultar y confirmar copiado manual"
                )
              )
            )
          : e("button", { onClick: () => setIngestVisible(true) }, "Mostrar ingesta una vez")
      ),
    e(
      "div",
      { style: { border: "1px solid #333", padding: 8, marginTop: 12 } },
      e("h2", null, "Progreso T-077 (sin polling: cada Check es a peticion)"),
      e(
        "p",
        { style: { fontSize: 13 } },
        "Actores: ",
        e("b", null, "Tu"),
        " (operador) · ",
        e("b", null, "Lab"),
        " (localhost:3002) · ",
        e("b", null, "Meta Graph API"),
        " · ",
        e("b", null, "OBS-Aitum(manual)"),
        " (el lab jamas lo automatiza)."
      ),
      e(
        "p",
        { style: { fontSize: 12, color: "#555" } },
        "Causas Meta: 100/33=no gestionable por API · 190=reconectar · 1363120/44=elegibilidad. Cada paso guarda estado + code/subcode + hora."
      ),
      e(
        "table",
        { style: { borderCollapse: "collapse", width: "100%", fontSize: 13 } },
        e(
          "tbody",
          null,
          STEPS.map((s) =>
            e(
              "tr",
              { key: s.id, style: { borderTop: "1px solid #ccc" } },
              e("td", { style: { padding: 4 } }, e("span", { style: { color: statusColor(progress[s.id].status) } }, "●"), " ", s.label),
              e("td", { style: { padding: 4, color: "#555" } }, s.actor),
              e("td", { style: { padding: 4 } }, progress[s.id].status),
              e("td", { style: { padding: 4 } }, progress[s.id].code || s.hint),
              e("td", { style: { padding: 4 } }, progress[s.id].time || "—")
            )
          )
        )
      ),
      e(
        "p",
        { style: { fontSize: 12 } },
        creates.length
          ? `Creates en memoria (esta sesion): ${creates.map((c) => `${c.id}@${c.time}${c.hadUrl ? "+url" : "-url"}`).join(" | ")}. Compara Servidor+Clave visualmente; el lab no persiste ingesta.`
          : "Aun sin creates en esta sesion: pulsa Create dos veces y compara URLs (persistencia)."
      ),
      e(
        "p",
        { style: { fontSize: 12 } },
        e("button", { onClick: () => mark("ingest", "ok", "copiado manual confirmado") }, "Confirmo ingesta manual en Aitum"),
        " ",
        e("button", { onClick: () => { setProgress(initialProgress()); setCreates([]); } }, "Reiniciar progreso")
      )
    ),
    e("pre", { style: { background: "#eee", padding: 8, marginTop: 8 } }, msg)
  );
}

ReactDOM.createRoot(document.getElementById("root")).render(e(App));
