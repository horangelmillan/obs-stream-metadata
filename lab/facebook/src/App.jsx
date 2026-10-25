/* lab/facebook/src/App.jsx — React minimo (createElement, sin build).
 * Flujo: Connect / Create / Status / Update / GoLive / End / Delete.
 * secure_stream_url: boton mostrar-una-vez, jamas persistida.
 */
"use strict";

const e = React.createElement;

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
    if (!r.ok) return say(r);
    window.open(r.url, "_blank", "noopener");
    say("Autoriza en facebook.com y vuelve: el callback capturara el code.");
  };
  const doExchange = async () => say(await api.exchange());
  const doCreate = async () => {
    if (!validTitle(title)) return say("titulo invalido (1-254)");
    const r = await api.createLive(title, description);
    if (r.ok && r.data && r.data.id) {
      setLiveId(r.data.id);
      if (r.data.secure_stream_url) setIngest(splitIngest(r.data.secure_stream_url));
      else setIngest(null);
      setIngestVisible(false);
    }
    say(r);
    refresh();
  };
  const doStatus = async () => {
    if (!liveId.trim()) return say("live_video_id requerido");
    const r = await api.liveStatus(liveId.trim());
    if (r.ok) setInfo(r.data);
    say(r.ok ? "status ok" : r);
  };
  const doUpdate = async () => {
    if (!liveId.trim()) return say("live_video_id requerido");
    if (!validTitle(title)) return say("titulo invalido (1-254)");
    say(await api.updateLive(liveId.trim(), title, description));
  };

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
      e("button", { onClick: async () => say(await api.goLive(liveId.trim())) }, "GoLive"),
      " ",
      e("button", { onClick: async () => say(await api.endLive(liveId.trim())) }, "End"),
      " ",
      e("button", { onClick: async () => say(await api.deleteLive(liveId.trim())) }, "Delete")
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
              e("p", null, e("b", null, "Clave: "), e("code", null, ingest.key))
            )
          : e("button", { onClick: () => setIngestVisible(true) }, "Mostrar ingesta una vez")
      ),
    e("pre", { style: { background: "#eee", padding: 8, marginTop: 8 } }, msg)
  );
}

ReactDOM.createRoot(document.getElementById("root")).render(e(App));
