# 007 — Portal de autenticación + Managed por suscripción

- Estado: idea
- Contexto: el backend productivo expone ingreso público intencional con la
  frontera en `backend_auth` (bootstrap anónimo rate-limitado + sesiones HMAC).
  Por diseño es "seguro" hoy, pero no es el deber ser: el modo Managed está
  destinado a planes de pago y hoy no hay control de acceso por suscripción
  ni identidad de usuario final (ver T-059 futuro y `docs/FASES-COMERCIAL.md`).
- Problema observado: cualquiera con el host puede auto-registrar una
  instalación y usar el servicio sin pagar ni identificarse; no hay puerta
  para cobrar, ni para reforzar la seguridad con identidad verificada.

## Propuesta

Abrir un ciclo de desarrollo de seguridad y autenticación del servidor, por
fases con gates (ninguna fase avanza sin pruebas y verificación de la
anterior), para que el modo Managed exija usuario registrado + suscripción
vigente:

1. **Identidad por OAuth de plataformas**: registro/login con cuentas que el
   usuario ya posee (Google, Twitch, Kick, Facebook, etc.). Dato mínimo al
   registrar: correo + nombre de usuario. Evaluar qué dato expone cada
   proveedor y normalizarlo.
2. **Refuerzos de seguridad (evaluar costo antes de adoptar)**: código de
   verificación por correo, 2FA (celular, correo o app de autenticación).
   Regla: si un método exige pagar servicios de correo/teléfono/autenticación
   y no hay alternativa gratuita viable, se descarta. Nada se asume gratis
   sin verificar precios y límites actuales.
3. **Doble vía de acceso**: autenticación desde la interfaz del plugin y
   desde una página web sencilla y minimalista (diseño sobrio tipo Next.js,
   fácil de usar). El registro/acceso al portal es gratuito; dentro, sin
   suscripción no hay beneficios del servicio.
4. **Pasarela de pago global y barata**: el servicio arranca en ~1$/mes, así
   que la comisión importa. Investigar pasarelas con cobertura amplia de
   países y costo cero o comisión mínima; la página debe presentar el
   servicio con todos los detalles antes del pago.
5. **Investigación previa obligatoria**: documentación actualizada de cada
   herramienta/tecnología candidata (OAuth por proveedor, envío de correo,
   2FA, pasarelas) antes de decidir; sin tutoriales viejos como autoridad.

## Relación

- Toca (futuro): `backend/` (identidad + gates por suscripción), nuevo
  portal web, `docs/FASES-COMERCIAL.md`, BACKLOG (T-059 y tareas derivadas).
- No toca: lógica OAuth/metadata actual del plugin hasta que una fase lo exija.
- Reglas vigentes al ejecutar: `docs/WORKFLOW.md` completo (TASK→NEXT sin
  saltar fases), skills según toque, MCP según `AGENTS.md` global,
  investigación con fuentes fechadas, `VALIDATION.md` con comando+salida,
  `FINDINGS.md` ante hallazgos, BACKLOG como única fuente de tareas, y
  autorización explícita para cada mutación git.

## Requerimiento textual del operador

- Vale, por diseño me parece que es "seguro", pero creo que no es el deber ser, me gustaria crear una nota en "docs/notes" con sus reglas,

crear un nuevo siclo de desarrollo de seguridad y autenticación para el servidor donde usuarios que deseen usar el modo managed que esta destinado a un uso por plan de pago, esto es recomendable ya que en un futuro necesitamos que haya control de pago y acceso a usuarios subscritos al servicio, y tambien añadimos una capa de segurida mas al servidor evitando inconvenientes, en estas notas se debe agregar que la autenticacion a este servicio puede ser por medio de la interfaz del plugin y por medio de una pagina web sencilla minimalista que ofrezca autenticación por oauth aprovechando los ouauth de proveedores o plataformas como google, twitch, facebook, kick etc, para darle al usuario la facilidad de registrarse con cuentas que ya posee, por ejemplo:

el usuario si tiene alguna plataforma, puede registrarse con alguna de ellas, abra información necesaria una vez se registre por oauth con estas plataformas, como:

- Correo
- Nombre de usuario

Ya luego una vez registrado de forma preliminar, habrá métodos para reforzar la seguridad como pedir un código de verificación enviado por correo, autenticación en dos pasos, sea por celular, correo electrónico o aplicación de autenticación, todos estos métodos deben ser evaluados y definidos con total precisión para saber si es necesario pagar servicios de correo, teléfono o autenticación, en caso de que no haya un método gratuito para implementarlo entonces se descarta.

La interfaz de autenticación y acceso al portal es gratuito, esto es para tener al usuario ya registrado sin embargo, una ves dentro no puede acceder aun a los beneficios del servicio hasta que pague la suscripción, por tanto una ves dentro debe ofrecerle el servicio con todos los detalles y si quiere pagar la suscripción debe haber una pasarela de pago que sea global, o por lo menos que cubra una gran cantidad de países para recibir pagos, hay que buscar una pasarela de pago que sea gratuito o que cobre comisiones mas bajas ya que el servicio empieza con un monto de 1$ por suscripción y quizá a futuro se hagan mejoras y se pueda subir un poco mas el monto, la pagina debe ser minimalista facil de usar, un diseño parecido al de NEXT.js, se deben hacer todas las investigaciones necesarias sobre documentación actualizada de las herramientas y tecnologías a implementar.

No se debe ejecutar todo el ciclo en una sola sesion, debe diferirlo por fases y si es necesario por sub-fases o tareas, con el fin de ejecutarlas una por una y hasta que la anterior no se hagan pruebas y se verifique que funciona perfectamente no se procede con la siguiente fase y tarea, se debe respetar las reglas de flujo de desarrollo del proyecto, uso de skills, uso de mcp, flujo de investigacion e implementacion, flujo de documentacion, hallazgos y backlogs, cada que se empiece una tarea o ejecucion no se puede precindir de ninguno de estas reglas de desarrollo.
